// Builds the Sanctuary voice pack from the VocalSet singing dataset (CC BY 4.0).
//
// usage: sanctuary_voicepack <VocalSet FULL folder> <output folder>
//
// Cuts every clean, steady held note out of each singer's long tones, scales and arpeggios,
// sorts it into a choir section by voice and pitch (women: sopranos/altos, men: tenors/basses),
// keeps the best few takes per semitone with as many different singers as possible, and writes
// one WAV per note named <singer>_<vowel>_<n>_m<pitch*100>.wav under <Section>/<vowel>/ (the
// pitch in the name means the plugin need not analyse it again). Next to those held notes:
//   short/    notes sung from silence, cut with their onset: how a short or detached note
//             begins (the held notes above start mid-note);
//   legato/   one note moving to the next, out of the scales and arpeggios, named
//             <singer>_<vowel>_<n>_m<from*100>_to<to*100>_x<ms>.wav (x: when the pitch is
//             halfway there);
//   breathy/, belt/   the same three, sung breathy or belted.
#include "TestSignals.h"

#include <sanctuary/Learn.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <tuple>
#include <vector>

using namespace sc;
namespace fs = std::filesystem;

namespace
{
enum Technique { Straight = 0, Breathy, Belt, NumTechniques };
enum Kind { Sustain = 0, Onset, Legato, NumKinds };
const char* kTechniqueDirs[] = { "", "breathy", "belt" };
const char* kKindDirs[] = { "", "short", "legato" };
const char* kSectionDirs[] = { "Sopranos", "Altos", "Tenors", "Basses" };
const char* kVowelDirs[] = { "ah", "oh", "oo", "ee", "eh" };

struct Cut
{
    std::string singer;
    bool female = false;
    int vowel = 0, technique = Straight, kind = Sustain;
    float root = 0.0f, seconds = 0.0f, wobble = 0.0f;
    float to = 0.0f, crossing = 0.0f;   // legato: the next note, and when the pitch is halfway there (s)
    float attack = 0.0f;                // onsets: seconds to reach 70% of the note's level
    bool longTone = false;
    std::vector<float> audio;
    double sampleRate = 44100.0;
};

/** What to cut from one file. */
struct Wanted
{
    bool longTone = false, sustains = true, onsets = true, legato = false;
};

int vowelIndex (char v)
{
    switch (v) { case 'a': return 0; case 'o': return 1; case 'u': return 2; case 'i': return 3; case 'e': return 4; default: return -1; }
}

// Which sections a note (or a move from lo to hi) can serve, by voice and pitch, with some
// overlap between sections.
std::vector<int> sectionsFor (bool female, float lo, float hi)
{
    std::vector<int> s;
    if (female)
    {
        if (lo >= 62.5f) s.push_back (Soprano);
        if (lo >= 52.5f && hi <= 74.5f) s.push_back (Alto);
    }
    else
    {
        if (lo >= 49.5f && hi <= 69.5f) s.push_back (Tenor);
        if (hi <= 62.5f) s.push_back (Bass);
    }
    return s;
}

/** Pitches (MIDI) of the voiced frames between a and b, sorted. */
std::vector<float> pitchesBetween (const TakeAnalysis& take, int64_t a, int64_t b)
{
    std::vector<float> m;
    for (const auto& f : take.frames)
        if (f.voiced && f.centre >= a && f.centre <= b)
            m.push_back (hzToMidi (f.hz));
    std::sort (m.begin(), m.end());
    return m;
}

float rmsBetween (const std::vector<float>& x, int64_t a, int64_t b)
{
    a = std::max<int64_t> (0, a);
    b = std::min<int64_t> ((int64_t) x.size(), b);
    double e = 0.0;
    for (int64_t i = a; i < b; ++i) e += (double) x[(size_t) i] * x[(size_t) i];
    return b > a ? (float) std::sqrt (e / (double) (b - a)) : 0.0f;
}

Cut makeCut (const std::string& singer, bool female, int vowel, int technique, int kind, double sr)
{
    Cut c;
    c.singer = singer;
    c.female = female;
    c.vowel = vowel;
    c.technique = technique;
    c.kind = kind;
    c.sampleRate = sr;
    return c;
}

/** Legato moves between neighbouring scale / arpeggio notes: the end of one steady note, the
    move, and the start of the next. */
void cutMoves (const TakeAnalysis& take, const std::vector<float>& x, double sr, const Cut& proto, std::vector<Cut>& out)
{
    for (size_t k = 0; k + 1 < take.notes.size(); ++k)
    {
        const auto& a = take.notes[k];
        const auto& b = take.notes[k + 1];
        if ((double) (b.start - a.end) > 0.06 * sr || (double) (a.end - a.start) < 0.2 * sr || (double) (b.end - b.start) < 0.25 * sr)
            continue;
        const auto pa = pitchesBetween (take, a.end - (int64_t) (0.15 * sr), a.end - (int64_t) (0.03 * sr));
        const auto pb = pitchesBetween (take, b.start + (int64_t) (0.06 * sr), b.start + (int64_t) (0.2 * sr));
        if (pa.size() < 5 || pb.size() < 5)
            continue;
        const float wobble = (pa[pa.size() * 9 / 10] - pa[pa.size() / 10]) + (pb[pb.size() * 9 / 10] - pb[pb.size() / 10]);
        if (wobble > 0.9f)
            continue;
        const float from = pa[pa.size() / 2], to = pb[pb.size() / 2];
        const float interval = to - from;
        if (std::abs (interval) < 0.6f || std::abs (interval) > 7.4f)
            continue;
        const int64_t start = std::max<int64_t> (0, a.end - (int64_t) (0.15 * sr));
        const int64_t end = std::min<int64_t> ((int64_t) x.size(), b.start + (int64_t) (0.24 * sr));

        // When the pitch is halfway between the two notes.
        const float mid = 0.5f * (from + to);
        float crossing = -1.0f;
        for (const auto& f : take.frames)
        {
            if (! f.voiced || f.centre < start || f.centre > end)
                continue;
            const float m = hzToMidi (f.hz);
            if ((interval > 0.0f && m >= mid) || (interval < 0.0f && m <= mid))
            {
                crossing = (float) ((double) (f.centre - start) / sr);
                break;
            }
        }
        if (crossing < 0.08f || crossing > (float) ((double) (end - start) / sr) - 0.14f)
            continue;
        // The move itself must not dip much in level (a breath or a re-attack between the notes).
        const float steady = std::min (rmsBetween (x, start, start + (int64_t) (0.08 * sr)), rmsBetween (x, end - (int64_t) (0.08 * sr), end));
        const int64_t c = start + (int64_t) (crossing * sr);
        if (rmsBetween (x, c - (int64_t) (0.02 * sr), c + (int64_t) (0.02 * sr)) < 0.35f * steady)
            continue;
        // Keep what the plug-in plays: from just before the slide until the new note has settled.
        const int64_t cutStart = std::max (start, c - (int64_t) (0.1 * sr));
        const int64_t cutEnd = std::min (end, c + (int64_t) (0.24 * sr));
        crossing = (float) ((double) (c - cutStart) / sr);
        const float seconds = (float) ((double) (cutEnd - cutStart) / sr);

        Cut cut = proto;
        cut.kind = Legato;
        cut.root = from;
        cut.to = to;
        cut.crossing = crossing;
        cut.wobble = wobble;
        cut.seconds = seconds;
        cut.audio.assign (x.begin() + cutStart, x.begin() + cutEnd);
        out.push_back (std::move (cut));
    }
}

void cutFile (const fs::path& file, const std::string& singer, bool female, int vowel, int technique, Wanted want, std::vector<Cut>& out)
{
    test::Audio in;
    std::string err;
    if (! test::readWav (file.string(), in, err))
        return;
    const auto& x = in.channels[0];
    const double sr = in.sampleRate;
    const auto take = analyseTake (x.data(), (int64_t) x.size(), sr);
    const Cut proto = makeCut (singer, female, vowel, technique, Sustain, sr);
    if (want.legato)
        cutMoves (take, x, sr, proto, out);

    float loudest = 0.0f;
    std::vector<float> rmsOf (take.notes.size(), 0.0f);
    for (size_t k = 0; k < take.notes.size(); ++k)
    {
        rmsOf[k] = rmsBetween (x, take.notes[k].start, take.notes[k].end);
        loudest = std::max (loudest, rmsOf[k]);
    }
    const float maxWobble = technique == Straight ? 0.6f : 0.75f;

    for (size_t k = 0; k < take.notes.size(); ++k)
    {
        const auto& n = take.notes[k];
        // Held notes must be near the take's full voice; an onset (often a scale's first, lowest
        // note) only clearly audible.
        if (want.sustains && rmsOf[k] >= loudest * 0.25f)
        {
            // Legato scale notes begin with the glide from the previous note: skip it.
            const int64_t skip = want.longTone ? -(int64_t) (0.03 * sr) : (int64_t) (0.05 * sr);
            const int64_t start = std::max<int64_t> (0, n.start + skip);
            const int64_t end = std::min<int64_t> ((int64_t) x.size(), std::min (n.end, start + (int64_t) (2.6 * sr)));
            const float secs = (float) ((double) (end - start) / sr);
            // Steadiness: pitch spread over the middle of the note (a straight tone barely moves).
            const auto m = pitchesBetween (take, start + (end - start) / 5, end - (end - start) / 5);
            if (secs >= (want.longTone ? 1.2f : 0.42f) && m.size() >= 6)
            {
                const float spread = m[m.size() * 9 / 10] - m[m.size() / 10];
                if (spread <= maxWobble)
                {
                    Cut c = proto;
                    c.root = m[m.size() / 2];
                    c.seconds = secs;
                    c.wobble = spread;
                    c.longTone = want.longTone;
                    c.audio.assign (x.begin() + start, x.begin() + end);
                    out.push_back (std::move (c));
                }
            }
        }

        if (want.onsets && rmsOf[k] >= loudest * 0.05f)
        {
            // A note sung from silence: keep its onset and its first moments.
            const float before = rmsBetween (x, n.start - (int64_t) (0.12 * sr), n.start - (int64_t) (0.03 * sr));
            if (before > 0.12f * rmsOf[k])
                continue;
            const int64_t start = std::max<int64_t> (0, n.start - (int64_t) (0.02 * sr));
            const int64_t end = std::min<int64_t> ((int64_t) x.size(), std::min (n.end, start + (int64_t) (0.45 * sr)));
            const float secs = (float) ((double) (end - start) / sr);
            const auto m = pitchesBetween (take, start + (int64_t) (0.05 * sr), end - (int64_t) (0.02 * sr));
            if (secs < 0.12f || m.size() < 4)
                continue;
            const float spread = m[m.size() * 9 / 10] - m[m.size() / 10];
            if (spread > 1.0f)
                continue;
            // How quickly it speaks: time to 70% of the level it settles at.
            const float settled = rmsBetween (x, end - (int64_t) (0.06 * sr), end);
            float attack = secs;
            const int64_t w = (int64_t) (0.01 * sr);
            for (int64_t i = start; i + w <= end; i += w / 2)
                if (rmsBetween (x, i, i + w) >= 0.7f * settled)
                {
                    attack = (float) ((double) (i + w / 2 - start) / sr);
                    break;
                }
            Cut c = proto;
            c.kind = Onset;
            c.root = m[m.size() / 2];
            c.seconds = secs;
            c.wobble = spread;
            c.attack = attack;
            c.audio.assign (x.begin() + start, x.begin() + end);
            out.push_back (std::move (c));
        }
    }
}

/** Keep up to `count` takes from list (already in order of preference), one per singer first. */
std::vector<const Cut*> pick (const std::vector<const Cut*>& list, int count)
{
    std::vector<const Cut*> keep;
    for (int pass = 0; pass < 2 && (int) keep.size() < count; ++pass)
        for (const Cut* c : list)
        {
            if ((int) keep.size() >= count) break;
            if (std::find (keep.begin(), keep.end(), c) != keep.end()) continue;
            const bool singerUsed = std::any_of (keep.begin(), keep.end(), [c] (const Cut* k) { return k->singer == c->singer; });
            if (pass == 0 && singerUsed) continue;   // first pass: one take per singer
            keep.push_back (c);
        }
    return keep;
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

    struct Source { const char* folder; int technique; Wanted want; };
    const Source sources[] = {
        { "long_tones/straight", Straight, { true, true, true, false } },
        { "scales/straight", Straight, { false, true, true, true } },
        { "arpeggios/straight", Straight, { false, true, true, true } },
        { "scales/fast_forte", Straight, { false, false, true, false } },
        { "scales/fast_piano", Straight, { false, false, true, false } },
        { "scales/breathy", Breathy, { false, true, true, true } },
        { "arpeggios/breathy", Breathy, { false, true, true, true } },
        { "scales/belt", Belt, { false, true, true, true } },
        { "arpeggios/belt", Belt, { false, true, true, true } },
    };

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
        for (const auto& source : sources)
        {
            const fs::path dir = singerDir.path() / source.folder;
            if (! fs::exists (dir))
                continue;
            for (const auto& f : fs::directory_iterator (dir))
            {
                const std::string name = f.path().stem().string();
                if (f.path().extension() != ".wav" || name.size() < 2 || name[name.size() - 2] != '_')
                    continue;
                const int vowel = vowelIndex (name.back());
                if (vowel >= 0)
                    cutFile (f.path(), tag, female, vowel, source.technique, source.want, cuts);
            }
        }
    }
    int found[NumTechniques][NumKinds] = {};
    for (const auto& c : cuts)
        ++found[c.technique][c.kind];
    for (int t = 0; t < NumTechniques; ++t)
        std::printf ("%-8s cut: %d held, %d onsets, %d moves\n", t == 0 ? "straight" : kTechniqueDirs[t], found[t][Sustain], found[t][Onset], found[t][Legato]);

    // Per section, vowel, technique and kind, group the takes into bins and keep the best few
    // of each: held notes per semitone (long tones first, then the steadiest), onsets per
    // semitone (the quickest to speak first), moves per starting semitone and interval.
    int written[NumTechniques][NumKinds] = {};
    double seconds = 0.0;
    for (int section = 0; section < NumSections; ++section)
        for (int vowel = 0; vowel < 5; ++vowel)
            for (int technique = 0; technique < NumTechniques; ++technique)
                for (int kind = 0; kind < NumKinds; ++kind)
                {
                    std::map<std::pair<int, int>, std::vector<const Cut*>> bins;
                    for (const auto& c : cuts)
                    {
                        if (c.vowel != vowel || c.technique != technique || c.kind != kind)
                            continue;
                        const float lo = kind == Legato ? std::min (c.root, c.to) : c.root;
                        const float hi = kind == Legato ? std::max (c.root, c.to) : c.root;
                        for (int s : sectionsFor (c.female, lo, hi))
                            if (s == section)
                                bins[{ (int) std::lround (c.root), kind == Legato ? (int) std::lround (c.to - c.root) : 0 }].push_back (&c);
                    }
                    if (bins.empty())
                        continue;

                    fs::path dir = dst / kSectionDirs[section] / kVowelDirs[vowel];
                    if (technique != Straight) dir /= kTechniqueDirs[technique];
                    if (kind != Sustain) dir /= kKindDirs[kind];
                    fs::create_directories (dir);
                    const int perBin = kind == Sustain ? (technique == Straight ? 3 : 2) : kind == Onset ? (technique == Straight ? 2 : 1) : 1;
                    int counter = 0;
                    for (auto& [key, list] : bins)
                    {
                        std::sort (list.begin(), list.end(), [] (const Cut* a, const Cut* b)
                        {
                            if (a->longTone != b->longTone) return a->longTone;
                            if (a->kind == Onset && std::abs (a->attack - b->attack) > 0.02f) return a->attack < b->attack;
                            return a->wobble < b->wobble;
                        });
                        for (const Cut* c : pick (list, perBin))
                        {
                            char name[128];
                            if (kind == Legato)
                                std::snprintf (name, sizeof (name), "%s_%s_%03d_m%04d_to%04d_x%03d.wav", c->singer.c_str(), kVowelDirs[vowel], ++counter,
                                               (int) std::lround (c->root * 100.0f), (int) std::lround (c->to * 100.0f), (int) std::lround (c->crossing * 1000.0f));
                            else
                                std::snprintf (name, sizeof (name), "%s_%s_%03d_m%04d.wav", c->singer.c_str(), kVowelDirs[vowel], ++counter,
                                               (int) std::lround (c->root * 100.0f));
                            test::Audio a;
                            a.sampleRate = c->sampleRate;
                            a.channels = { c->audio };
                            test::writeWav ((dir / name).string(), a, 16);
                            seconds += c->seconds;
                            ++written[technique][kind];
                        }
                    }
                }
    for (int t = 0; t < NumTechniques; ++t)
        std::printf ("%-8s kept: %d held, %d onsets, %d moves\n", t == 0 ? "straight" : kTechniqueDirs[t], written[t][Sustain], written[t][Onset], written[t][Legato]);
    std::printf ("%.0f s of audio\n", seconds);
    return 0;
}
