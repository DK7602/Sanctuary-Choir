// Builds the Sanctuary voice pack from the VocalSet singing dataset (CC BY 4.0).
//
// usage: sanctuary_voicepack <VocalSet FULL folder> <output folder> [<Annotated-VocalSet folder>]
//
// Cuts every clean, steady held note out of each singer's long tones, scales and arpeggios,
// sorts it into a choir section by voice and pitch (women: sopranos/altos, men: tenors/basses,
// the countertenor: altos), keeps the best takes per semitone with as many different singers as
// possible, and writes one WAV per note named <singer>_<vowel>_<n>_m<pitch*100>.wav under
// <Section>/<vowel>/ (the pitch in the name means the plugin need not analyse it again). Next
// to those held notes:
//   short/    notes sung from silence, cut with their onset: how a short or detached note
//             begins (the held notes above start mid-note);
//   legato/   one note moving to the next, out of the scales and arpeggios, named
//             <singer>_<vowel>_<n>_m<from*100>_to<to*100>_x<ms>.wav (x: when the pitch is
//             halfway there);
//   breathy/, belt/   the same three, sung breathy or belted;
//   vibrato/, fullvoice/   the same three in the singers' own classical voice, with its vibrato:
//             sung softly (slow piano scales and arpeggios, pianissimo long tones) and in full
//             voice (slow forte scales and arpeggios, forte long tones);
//   syllables/   with Annotated-VocalSet's note and lyric times: the syllables of the three
//             songs every singer sang in straight tone (Dona nobis pacem, Caro mio ben, Row row
//             row your boat), each cut from its consonants to the start of its vowel, named
//             ..._on<consonants>.wav ("ond", "onkxr" for k-r), so typed words open with a real
//             singer's consonant.
#include "TestSignals.h"

#include <sanctuary/Learn.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <map>
#include <string>
#include <tuple>
#include <vector>

using namespace sc;
namespace fs = std::filesystem;

namespace
{
enum Technique { Straight = 0, Breathy, Belt, Vibrato, Full, NumTechniques };
enum Kind { Sustain = 0, Onset, Legato, SyllableCut, NumKinds };
const char* kTechniqueDirs[] = { "", "breathy", "belt", "vibrato", "fullvoice" };
const char* kKindDirs[] = { "", "short", "legato", "syllables" };
const char* kSectionDirs[] = { "Sopranos", "Altos", "Tenors", "Basses" };
const char* kVowelDirs[] = { "ah", "oh", "oo", "ee", "eh" };

// The classical voice moves around its note (VocalSet's slow scales: about 45 cents of standard
// deviation, against 15 for straight tone): it is steady when its centre is.
bool hasVibrato (int technique) { return technique == Vibrato || technique == Full; }

struct Cut
{
    std::string singer;
    bool female = false, countertenor = false;
    int vowel = 0, technique = Straight, kind = Sustain;
    float root = 0.0f, seconds = 0.0f, wobble = 0.0f;
    float to = 0.0f, crossing = 0.0f;   // legato: the next note, and when the pitch is halfway there (s)
    float attack = 0.0f;                // onsets: seconds to reach 70% of the note's level
    std::string phones;                 // syllables: the consonants, phone names joined by x
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
std::vector<int> sectionsFor (bool female, bool countertenor, float lo, float hi)
{
    std::vector<int> s;
    if (countertenor)
    {
        // A male alto (falsetto): VocalSet's countertenor sings about G#3 to C5 there.
        if (lo >= 53.5f && hi <= 73.5f) s.push_back (Alto);
    }
    else if (female)
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

Cut makeCut (const std::string& singer, bool female, bool countertenor, int vowel, int technique, int kind, double sr)
{
    Cut c;
    c.singer = singer;
    c.female = female;
    c.countertenor = countertenor;
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
        if (wobble > (hasVibrato (proto.technique) ? 3.2f : 0.9f))
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

void cutFile (const fs::path& file, const std::string& singer, bool female, bool countertenor, int vowel, int technique, Wanted want, std::vector<Cut>& out)
{
    test::Audio in;
    std::string err;
    if (! test::readWav (file.string(), in, err))
        return;
    const auto& x = in.channels[0];
    const double sr = in.sampleRate;
    const auto take = analyseTake (x.data(), (int64_t) x.size(), sr);
    const Cut proto = makeCut (singer, female, countertenor, vowel, technique, Sustain, sr);
    if (want.legato)
        cutMoves (take, x, sr, proto, out);

    float loudest = 0.0f;
    std::vector<float> rmsOf (take.notes.size(), 0.0f);
    for (size_t k = 0; k < take.notes.size(); ++k)
    {
        rmsOf[k] = rmsBetween (x, take.notes[k].start, take.notes[k].end);
        loudest = std::max (loudest, rmsOf[k]);
    }
    const bool vibrato = hasVibrato (technique);
    const float maxWobble = technique == Straight ? 0.6f : vibrato ? 2.6f : 0.75f;

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
                // With vibrato, the centre must not drift from the first half to the second.
                bool centred = true;
                if (vibrato)
                {
                    const int64_t mid = (start + end) / 2;
                    const auto a = pitchesBetween (take, start + (end - start) / 5, mid);
                    const auto b = pitchesBetween (take, mid, end - (end - start) / 5);
                    centred = a.size() >= 3 && b.size() >= 3 && std::abs (a[a.size() / 2] - b[b.size() / 2]) <= 0.35f;
                }
                if (spread <= maxWobble && centred)
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
            if (spread > (vibrato ? 2.4f : 1.0f))
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

//==============================================================================
// Syllables from the songs. Each lyric (as Annotated-VocalSet writes it) to the consonants it
// opens with and the vowel it holds, sung the way these singers sing them: Latin, Italian,
// English. Syllables opening on a vowel (melismas, "a", "o") have nothing to give.
struct SungSyllable { const char* song; const char* lyric; const char* phones; int vowel; };
const SungSyllable kSongSyllables[] = {
    { "dona", "do", "d", 1 }, { "dona", "na", "n", 0 }, { "dona", "no", "n", 1 }, { "dona", "bis", "b", 3 },
    { "dona", "pa", "p", 0 }, { "dona", "cem", "ch", 4 },
    { "caro", "ca", "k", 0 }, { "caro", "ro", "r", 1 }, { "caro", "mio", "m", 3 }, { "caro", "ben", "b", 4 },
    { "caro", "cre", "kxr", 4 }, { "caro", "di", "d", 3 }, { "caro", "mi'al", "m", 3 }, { "caro", "men", "m", 4 },
    { "caro", "sen", "s", 4 }, { "caro", "te", "t", 4 }, { "caro", "lan", "l", 0 }, { "caro", "cor", "k", 1 },
    { "row", "row", "r", 1 }, { "row", "your", "y", 1 }, { "row", "boat", "b", 1 }, { "row", "gent", "j", 4 },
    { "row", "ly", "l", 3 }, { "row", "down", "d", 0 }, { "row", "stream", "sxtxr", 3 }, { "row", "mer", "m", 4 },
    { "row", "ri", "r", 3 }, { "row", "life", "l", 0 }, { "row", "but", "b", 0 }, { "row", "dream", "dxr", 3 },
};

std::vector<std::string> splitCsv (const std::string& line)
{
    std::vector<std::string> f;
    std::string cur;
    std::stringstream ss (line);
    while (std::getline (ss, cur, ','))
        f.push_back (cur);
    return f;
}

/** One song sung by one singer: its annotated notes cut into syllables. */
void cutSongSyllables (const fs::path& wav, const fs::path& csv, const std::string& song, const std::string& singer,
                       bool female, bool countertenor, std::vector<Cut>& out)
{
    test::Audio in;
    std::string err;
    std::ifstream f (csv);
    if (! f || ! test::readWav (wav.string(), in, err))
        return;
    const auto& x = in.channels[0];
    const double sr = in.sampleRate;
    struct Row { double start = 0, end = 0; std::string type, lyric; float midi = 0; };
    std::vector<Row> rows;
    std::string line;
    std::getline (f, line);   // the header
    while (std::getline (f, line))
    {
        const auto c = splitCsv (line);
        if (c.size() < 16)
            continue;
        Row r;
        r.start = std::atof (c[1].c_str());
        r.end = std::atof (c[2].c_str());
        r.type = c[4];
        r.midi = (float) std::atof (c[11].c_str());
        r.lyric = c[15];
        rows.push_back (r);
    }
    auto at = [sr] (double t) { return (int64_t) std::llround (t * sr); };
    const int64_t win = at (0.01);
    for (size_t k = 0; k < rows.size(); ++k)
    {
        const Row& r = rows[k];
        if (r.type != "Sound" || r.midi < 30.0f)
            continue;
        const SungSyllable* sy = nullptr;
        for (const auto& s : kSongSyllables)
            if (song == s.song && r.lyric == s.lyric)
                sy = &s;
        if (sy == nullptr)
            continue;
        const int64_t sound = at (r.start);
        const float vowelRms = rmsBetween (x, sound + at (0.04), sound + at (0.14));
        if (vowelRms <= 1.0e-4f)
            continue;
        int64_t start = -1;
        const bool fromSilence = k == 0 || rows[k - 1].type == "Rest";
        if (fromSilence)
        {
            // Back from the vowel to where the singer starts to sound (an unvoiced consonant
            // comes before the voiced note).
            for (int64_t i = sound; i >= std::max<int64_t> (win, sound - at (0.3)); i -= win / 2)
                if (rmsBetween (x, i - win, i) < 0.06f * vowelRms)
                {
                    start = i - win / 2;
                    break;
                }
            if (start < 0)
                start = sound - at (0.2);
        }
        else
        {
            // Between two notes: the consonant is where the voice dips; start inside it.
            const int64_t from = rows[k - 1].type == "Transition" ? at (rows[k - 1].start) : sound - at (0.15);
            float lowest = 1.0e9f;
            for (int64_t i = std::max<int64_t> (0, from); i + win <= sound + at (0.02); i += win / 4)
            {
                const float e = rmsBetween (x, i, i + win);
                if (e < lowest) { lowest = e; start = i; }
            }
            if (start < 0 || lowest > 0.7f * vowelRms)
                continue;   // no consonant to be heard
        }
        start = std::max<int64_t> (0, start);
        const int64_t end = std::min<int64_t> ((int64_t) x.size(), sound + at (0.13));
        const float secs = (float) ((double) (end - start) / sr);
        if (secs < 0.07f || secs > 0.5f)
            continue;
        // The consonant must be in the cut: before the vowel speaks, a hiss or a burst for an
        // unvoiced one (s, t, k, p, ch), a dip for a voiced one (m, n, l, r, d, b...).
        {
            const int64_t fr = at (0.01);
            std::vector<float> e, z;
            for (int64_t i = start; i + fr <= end; i += fr)
            {
                e.push_back (rmsBetween (x, i, i + fr));
                int crossings = 0;
                for (int64_t j = i + 1; j < i + fr; ++j)
                    crossings += (x[(size_t) j] >= 0.0f) != (x[(size_t) (j - 1)] >= 0.0f);
                z.push_back ((float) crossings / (float) fr);
            }
            if (e.size() < 6)
                continue;
            const size_t n = e.size();
            const float vowelE = (e[n - 1] + e[n - 2] + e[n - 3]) / 3.0f, vowelZ = (z[n - 1] + z[n - 2] + z[n - 3]) / 3.0f;
            size_t v = 0;
            while (v < n && ! (e[v] >= 0.5f * vowelE && z[v] < 2.0f * vowelZ + 0.01f))
                ++v;
            const char first = sy->phones[0];
            const bool unvoiced = first == 's' || first == 't' || first == 'k' || first == 'p' || (first == 'c' && sy->phones[1] == 'h');
            bool heard = false;
            if (unvoiced)
            {
                float zMax = 0.0f;
                for (size_t i = 0; i < v; ++i) zMax = std::max (zMax, z[i]);
                heard = v >= 3 && zMax >= 2.5f * vowelZ;
            }
            else
            {
                float eMin = 1.0e9f;
                for (size_t i = 0; i < v; ++i) eMin = std::min (eMin, e[i]);
                heard = v >= 2 && eMin <= 0.6f * vowelE;
            }
            if (! heard)
                continue;
        }
        Cut c = makeCut (singer, female, countertenor, sy->vowel, Straight, SyllableCut, sr);
        c.root = r.midi;
        c.seconds = secs;
        c.phones = sy->phones;
        c.audio.assign (x.begin() + start, x.begin() + end);
        out.push_back (std::move (c));
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

// Held notes kept per semitone (straight tone; the other ways of singing).
constexpr int kHeldStraight = 5, kHeldOther = 2;

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: %s <VocalSet FULL folder> <output folder> [<Annotated-VocalSet folder>]\n", argv[0]);
        return 2;
    }
    const fs::path src = argv[1], dst = argv[2];
    const fs::path annotated = argc > 3 ? fs::path (argv[3]) : fs::path();

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
        { "arpeggios/fast_forte", Straight, { false, false, true, false } },
        { "arpeggios/fast_piano", Straight, { false, false, true, false } },
        { "long_tones/pp", Vibrato, { true, true, true, false } },
        { "scales/slow_piano", Vibrato, { false, true, true, true } },
        { "arpeggios/slow_piano", Vibrato, { false, true, true, true } },
        { "long_tones/forte", Full, { true, true, true, false } },
        { "scales/slow_forte", Full, { false, true, true, true } },
        { "arpeggios/slow_forte", Full, { false, true, true, true } },
    };

    std::vector<Cut> cuts;
    for (const auto& singerDir : fs::directory_iterator (src))
    {
        if (! singerDir.is_directory())
            continue;
        const std::string singer = singerDir.path().filename().string();   // female3, male7...
        const bool countertenor = singer == "male9";   // sings with the altos
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
                    cutFile (f.path(), tag, female, countertenor, vowel, source.technique, source.want, cuts);
            }
        }
        if (! annotated.empty())
            for (const char* song : { "dona", "caro", "row" })
            {
                const std::string stem = tag + "_" + song + "_straight";
                cutSongSyllables (singerDir.path() / "excerpts" / "straight" / (stem + ".wav"),
                                  annotated / "extended 1" / "without file header" / "straight" / (stem + ".csv"),
                                  song, tag, female, countertenor, cuts);
            }
    }
    int found[NumTechniques][NumKinds] = {};
    for (const auto& c : cuts)
        ++found[c.technique][c.kind];
    for (int t = 0; t < NumTechniques; ++t)
        std::printf ("%-8s cut: %d held, %d onsets, %d moves, %d syllables\n", t == 0 ? "straight" : kTechniqueDirs[t], found[t][Sustain], found[t][Onset], found[t][Legato], found[t][SyllableCut]);

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
                    std::map<std::tuple<int, int, std::string>, std::vector<const Cut*>> bins;
                    for (const auto& c : cuts)
                    {
                        if (c.vowel != vowel || c.technique != technique || c.kind != kind)
                            continue;
                        const float lo = kind == Legato ? std::min (c.root, c.to) : c.root;
                        const float hi = kind == Legato ? std::max (c.root, c.to) : c.root;
                        for (int s : sectionsFor (c.female, c.countertenor, lo, hi))
                            if (s == section)
                                bins[{ (int) std::lround (c.root), kind == Legato ? (int) std::lround (c.to - c.root) : 0, c.phones }].push_back (&c);
                    }
                    if (bins.empty())
                        continue;

                    fs::path dir = dst / kSectionDirs[section] / kVowelDirs[vowel];
                    if (technique != Straight) dir /= kTechniqueDirs[technique];
                    if (kind != Sustain) dir /= kKindDirs[kind];
                    fs::create_directories (dir);
                    // As many different singers per note as the engine can tell apart: a section of
                    // ten should not hear the same three takes over and over.
                    // The other takes are sung by a share of the choir only, and the plug-in keeps
                    // everything in memory: fewer of them.
                    const int perBin = kind == Sustain ? (technique == Straight ? kHeldStraight : kHeldOther)
                                     : kind == Onset ? (technique == Straight ? 3 : hasVibrato (technique) ? 2 : 1)
                                     : kind == SyllableCut ? 2
                                                     : (technique == Straight ? 2 : 1);
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
                            else if (kind == SyllableCut)
                                std::snprintf (name, sizeof (name), "%s_%s_%03d_m%04d_on%s.wav", c->singer.c_str(), kVowelDirs[vowel], ++counter,
                                               (int) std::lround (c->root * 100.0f), c->phones.c_str());
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
        std::printf ("%-8s kept: %d held, %d onsets, %d moves, %d syllables\n", t == 0 ? "straight" : kTechniqueDirs[t], written[t][Sustain], written[t][Onset], written[t][Legato], written[t][SyllableCut]);
    std::printf ("%.0f s of audio\n", seconds);
    return 0;
}
