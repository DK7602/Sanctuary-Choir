// Builds the Sanctuary voice pack from the VocalSet singing dataset (CC BY 4.0).
//
// usage: sanctuary_voicepack <VocalSet FULL folder> <output folder>
//
// Cuts every clean, steady held note out of each singer's straight-tone long tones, scales and
// arpeggios, sorts it into a choir section by voice and pitch (women: sopranos/altos, men:
// tenors/basses), keeps the best few takes per semitone with as many different singers as
// possible, and writes one WAV per note named <singer>_<n>_m<pitch*100>.wav under
// <Section>/<vowel>/ (the pitch in the name means the plugin need not analyse it again).
#include "TestSignals.h"

#include <sanctuary/Learn.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

using namespace sc;
namespace fs = std::filesystem;

namespace
{
struct Cut
{
    std::string singer;
    bool female = false;
    int vowel = 0;
    float root = 0.0f, seconds = 0.0f, wobble = 0.0f, level = 0.0f;
    bool longTone = false;
    std::vector<float> audio;
    double sampleRate = 44100.0;
};

const char* kSectionDirs[] = { "Sopranos", "Altos", "Tenors", "Basses" };
const char* kVowelDirs[] = { "ah", "oh", "oo", "ee", "eh" };

int vowelIndex (char v)
{
    switch (v) { case 'a': return 0; case 'o': return 1; case 'u': return 2; case 'i': return 3; case 'e': return 4; default: return -1; }
}

// Which sections a note can serve, by voice and pitch (with some overlap between sections).
std::vector<int> sectionsFor (const Cut& c)
{
    std::vector<int> s;
    if (c.female)
    {
        if (c.root >= 62.5f) s.push_back (Soprano);
        if (c.root >= 52.5f && c.root <= 74.5f) s.push_back (Alto);
    }
    else
    {
        if (c.root >= 49.5f && c.root <= 69.5f) s.push_back (Tenor);
        if (c.root <= 62.5f) s.push_back (Bass);
    }
    return s;
}

void cutFile (const fs::path& file, const std::string& singer, bool female, int vowel, bool longTone, std::vector<Cut>& out)
{
    test::Audio in;
    std::string err;
    if (! test::readWav (file.string(), in, err))
        return;
    const auto& x = in.channels[0];
    const double sr = in.sampleRate;
    const auto take = analyseTake (x.data(), (int64_t) x.size(), sr);
    const double hop = take.frames.size() > 1 ? (double) (take.frames[1].centre - take.frames[0].centre) : sr * 0.005;

    float loudest = 0.0f;
    std::vector<float> rmsOf (take.notes.size(), 0.0f);
    for (size_t k = 0; k < take.notes.size(); ++k)
    {
        const auto& n = take.notes[k];
        double e = 0;
        for (int64_t i = n.start; i < n.end; ++i) e += (double) x[(size_t) i] * x[(size_t) i];
        rmsOf[k] = (float) std::sqrt (e / (double) std::max<int64_t> (1, n.end - n.start));
        loudest = std::max (loudest, rmsOf[k]);
    }

    for (size_t k = 0; k < take.notes.size(); ++k)
    {
        const auto& n = take.notes[k];
        // Legato scale notes begin with the glide from the previous note: skip it.
        const int64_t skip = longTone ? -(int64_t) (0.03 * sr) : (int64_t) (0.05 * sr);
        const int64_t start = std::max<int64_t> (0, n.start + skip);
        const int64_t end = std::min<int64_t> ((int64_t) x.size(), std::min (n.end, start + (int64_t) (2.6 * sr)));
        const float secs = (float) ((double) (end - start) / sr);
        if (secs < (longTone ? 1.2f : 0.42f) || rmsOf[k] < loudest * 0.25f)
            continue;

        // Steadiness: pitch spread over the middle of the note (a straight tone barely moves).
        std::vector<float> m;
        const int64_t a = start + (end - start) / 5, b = end - (end - start) / 5;
        for (const auto& f : take.frames)
            if (f.voiced && f.centre >= a && f.centre <= b)
                m.push_back (hzToMidi (f.hz));
        if (m.size() < 6)
            continue;
        std::sort (m.begin(), m.end());
        const float spread = m[m.size() * 9 / 10] - m[m.size() / 10];
        if (spread > 0.6f)
            continue;

        Cut c;
        c.singer = singer;
        c.female = female;
        c.vowel = vowel;
        c.root = m[m.size() / 2];
        c.seconds = secs;
        c.wobble = spread;
        c.level = rmsOf[k];
        c.longTone = longTone;
        c.sampleRate = sr;
        c.audio.assign (x.begin() + start, x.begin() + end);
        out.push_back (std::move (c));
        (void) hop;
    }
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: %s <VocalSet FULL folder> <output folder>\n", argv[0]);
        return 2;
    }
    const fs::path src = argv[1], dst = argv[2];
    std::vector<Cut> cuts;
    for (const auto& singerDir : fs::directory_iterator (src))
    {
        if (! singerDir.is_directory())
            continue;
        const std::string singer = singerDir.path().filename().string();   // female3, male7...
        if (singer == "male9")
            continue;   // a countertenor: his voice sits in neither the tenor nor the bass section
        const bool female = singer.rfind ("female", 0) == 0;
        const std::string tag = (female ? "f" : "m") + singer.substr (female ? 6 : 4);
        for (const char* group : { "long_tones/straight", "scales/straight", "arpeggios/straight" })
        {
            const fs::path dir = singerDir.path() / group;
            if (! fs::exists (dir))
                continue;
            for (const auto& f : fs::directory_iterator (dir))
            {
                const std::string name = f.path().stem().string();
                if (f.path().extension() != ".wav" || name.size() < 2 || name[name.size() - 2] != '_')
                    continue;
                const int vowel = vowelIndex (name.back());
                if (vowel >= 0)
                    cutFile (f.path(), tag, female, vowel, std::string (group).rfind ("long", 0) == 0, cuts);
            }
        }
    }
    std::printf ("%zu clean notes cut\n", cuts.size());

    // Per section, vowel and semitone keep a few takes: long tones first, then different
    // singers, then the steadiest.
    constexpr int kPerSemitone = 3;
    int written = 0;
    double seconds = 0.0;
    for (int section = 0; section < NumSections; ++section)
    {
        for (int vowel = 0; vowel < 5; ++vowel)
        {
            std::map<int, std::vector<const Cut*>> bins;
            for (const auto& c : cuts)
                if (c.vowel == vowel)
                    for (int s : sectionsFor (c))
                        if (s == section)
                            bins[(int) std::lround (c.root)].push_back (&c);

            const fs::path dir = dst / kSectionDirs[section] / kVowelDirs[vowel];
            fs::create_directories (dir);
            for (auto& [note, list] : bins)
            {
                std::sort (list.begin(), list.end(), [] (const Cut* a, const Cut* b)
                {
                    if (a->longTone != b->longTone) return a->longTone;
                    return a->wobble < b->wobble;
                });
                std::vector<const Cut*> keep;
                for (int pass = 0; pass < 2 && (int) keep.size() < kPerSemitone; ++pass)
                    for (const Cut* c : list)
                    {
                        if ((int) keep.size() >= kPerSemitone) break;
                        if (std::find (keep.begin(), keep.end(), c) != keep.end()) continue;
                        const bool singerUsed = std::any_of (keep.begin(), keep.end(), [c] (const Cut* k) { return k->singer == c->singer; });
                        if (pass == 0 && singerUsed) continue;   // first pass: one take per singer
                        keep.push_back (c);
                    }
                for (const Cut* c : keep)
                {
                    char name[96];
                    std::snprintf (name, sizeof (name), "%s_%s_%03d_m%04d.wav", c->singer.c_str(), kVowelDirs[vowel], ++written % 1000,
                                   (int) std::lround (c->root * 100.0f));
                    test::Audio a;
                    a.sampleRate = c->sampleRate;
                    a.channels = { c->audio };
                    test::writeWav ((dir / name).string(), a, 16);
                    seconds += c->seconds;
                }
            }
        }
    }
    std::printf ("wrote %d notes, %.0f s of audio\n", written, seconds);
    return 0;
}
