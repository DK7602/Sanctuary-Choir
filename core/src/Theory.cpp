#include "sanctuary/Theory.h"

#include <limits>
#include <algorithm>
#include <functional>

namespace sc
{
//==============================================================================
const char* const* getScaleNames()
{
    static const char* names[] = { "Major", "Minor", "Harmonic Minor", "Dorian", "Mixolydian",
                                   "Lydian", "Phrygian", "Gospel (Blues)", "Chromatic" };
    static_assert (sizeof (names) / sizeof (names[0]) == (size_t) ScaleType::Count);
    return names;
}

const char* const* getHarmonyModeNames()
{
    static const char* names[] = { "Full Choir", "Gospel Stack", "Open Voicing", "Unison & Octaves",
                                   "Thirds & Sixths", "Organum", "Upper Voices", "Lower Voices",
                                   "Drone Pad", "MIDI Chords" };
    static_assert (sizeof (names) / sizeof (names[0]) == (size_t) HarmonyMode::Count);
    return names;
}

const char* const* getStyleNames()
{
    static const char* names[] = { "Sacred", "Gospel", "Gregorian", "Cinematic", "Worship", "Chamber", "Spiritual" };
    static_assert (sizeof (names) / sizeof (names[0]) == (size_t) Style::Count);
    return names;
}

const char* const* getVocalTypeNames()
{
    static const char* names[] = { "Ah (Vowels)", "Oh (Vowels)", "Oo (Vowels)", "Ee (Vowels)",
                                   "Eh (Vowels)", "Mm (Hum)", "Words (Your Voice)" };
    static_assert (sizeof (names) / sizeof (names[0]) == (size_t) VocalType::Count);
    return names;
}

const char* const* getKeyNames()
{
    static const char* names[] = { "Auto", "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return names;
}

const char* getPitchClassName (int pc, bool preferFlats)
{
    static const char* sharps[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* flats[]  = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    return (preferFlats ? flats : sharps)[wrapPc (pc)];
}

std::string getNoteName (int midi)
{
    return std::string (getPitchClassName (midi, false)) + std::to_string (midi / 12 - 1);
}

//==============================================================================
static const int* scaleIntervals (ScaleType t)
{
    static const int major[]    = { 0, 2, 4, 5, 7, 9, 11 };
    static const int natMinor[] = { 0, 2, 3, 5, 7, 8, 10 };
    static const int harMinor[] = { 0, 2, 3, 5, 7, 8, 11 };
    static const int dorian[]   = { 0, 2, 3, 5, 7, 9, 10 };
    static const int mixo[]     = { 0, 2, 4, 5, 7, 9, 10 };
    static const int lydian[]   = { 0, 2, 4, 6, 7, 9, 11 };
    static const int phryg[]    = { 0, 1, 3, 5, 7, 8, 10 };

    switch (t)
    {
        case ScaleType::NaturalMinor:  return natMinor;
        case ScaleType::HarmonicMinor: return harMinor;
        case ScaleType::Dorian:        return dorian;
        case ScaleType::Mixolydian:    return mixo;
        case ScaleType::Lydian:        return lydian;
        case ScaleType::Phrygian:      return phryg;
        default:                       return major;
    }
}

bool Scale::contains (int pc) const
{
    const int rel = wrapPc (pc - tonic);
    if (type == ScaleType::Chromatic)
        return true;
    if (type == ScaleType::Gospel && (rel == 3 || rel == 10))
        return true;
    const int* iv = scaleIntervals (type);
    for (int i = 0; i < 7; ++i)
        if (iv[i] == rel)
            return true;
    return false;
}

bool Scale::isMinorFamily() const
{
    return type == ScaleType::NaturalMinor || type == ScaleType::HarmonicMinor
        || type == ScaleType::Dorian || type == ScaleType::Phrygian;
}

std::array<int, 7> Scale::diatonic() const
{
    std::array<int, 7> r {};
    const int* iv = scaleIntervals (type);
    for (int i = 0; i < 7; ++i)
        r[(size_t) i] = wrapPc (tonic + iv[i]);
    return r;
}

int Scale::quantise (float midi) const
{
    const int centre = (int) std::lround (midi);
    int best = centre;
    float bestDist = 1.0e9f;
    for (int k = centre - 6; k <= centre + 6; ++k)
    {
        if (! contains (k))
            continue;
        const float d = std::abs (midi - (float) k);
        if (d < bestDist) { bestDist = d; best = k; }
    }
    return best;
}

int Scale::stepDiatonic (int midi, int steps) const
{
    const auto dia = diatonic();
    // Snap down to a diatonic note.
    int base = midi;
    for (int k = 0; k < 12; ++k)
    {
        const int pc = wrapPc (base);
        if (std::find (dia.begin(), dia.end(), pc) != dia.end())
            break;
        --base;
    }
    int degree = 0;
    for (int i = 0; i < 7; ++i)
        if (dia[(size_t) i] == wrapPc (base))
            degree = i;

    int note = base;
    const int dir = steps >= 0 ? 1 : -1;
    for (int s = 0; s != steps; s += dir)
    {
        const int nextDegree = ((degree + dir) % 7 + 7) % 7;
        int interval = wrapPc (dia[(size_t) nextDegree] - dia[(size_t) degree]);
        if (dir < 0)
            interval = wrapPc (dia[(size_t) degree] - dia[(size_t) nextDegree]);
        note += dir * (interval == 0 ? 12 : interval);
        degree = nextDegree;
    }
    return note;
}

//==============================================================================
bool Chord::contains (int pc) const { return roleOf (pc) >= 0; }

int Chord::roleOf (int pc) const
{
    pc = wrapPc (pc);
    for (int i = 0; i < numTones; ++i)
        if (tones[(size_t) i] == pc)
            return i;
    return -1;
}

uint16_t Chord::mask() const
{
    uint16_t m = 0;
    for (int i = 0; i < numTones; ++i)
        m |= (uint16_t) (1u << tones[(size_t) i]);
    return m;
}

static std::string qualitySuffix (int third, int fifth, int seventh)
{
    // intervals relative to root, seventh = -1 when absent
    std::string q;
    if (third == 4 && fifth == 7) q = "";
    else if (third == 3 && fifth == 7) q = "m";
    else if (third == 3 && fifth == 6) q = "dim";
    else if (third == 4 && fifth == 8) q = "+";
    else if (third == 5) q = "sus4";
    else if (third == 2) q = "sus2";
    else q = "";

    if (seventh < 0)
        return q;
    if (seventh == 9) return q + "6";
    if (q == "" && seventh == 11) return "maj7";
    if (q == "" && seventh == 10) return "7";
    if (q == "m" && seventh == 10) return "m7";
    if (q == "m" && seventh == 11) return "m(maj7)";
    if (q == "dim" && seventh == 10) return "m7b5";
    if (q == "dim" && seventh == 9) return "dim7";
    return q + "7";
}

static Chord makeChord (int root, std::initializer_list<int> intervals, int degree, float prior, bool flats)
{
    Chord c;
    c.root = wrapPc (root);
    c.numTones = 0;
    for (int iv : intervals)
        c.tones[(size_t) c.numTones++] = wrapPc (root + iv);
    c.degree = degree;
    c.prior = prior;
    const int third = c.numTones > 1 ? wrapPc (c.tones[1] - c.root) : 4;
    const int fifth = c.numTones > 2 ? wrapPc (c.tones[2] - c.root) : 7;
    const int seventh = c.numTones > 3 ? wrapPc (c.tones[3] - c.root) : -1;
    c.name = std::string (getPitchClassName (c.root, flats)) + qualitySuffix (third, fifth, seventh);
    return c;
}

static bool keyPrefersFlats (const Scale& s)
{
    // Rough rule: flat keys for F, Bb, Eb, Ab, Db, Gb majors and their relative minors.
    const int majorTonic = s.isMinorFamily() ? wrapPc (s.tonic + 3) : s.tonic;
    return majorTonic == 5 || majorTonic == 10 || majorTonic == 3 || majorTonic == 8 || majorTonic == 1 || majorTonic == 6;
}

std::vector<Chord> buildChordVocabulary (const Scale& scale, Style style)
{
    std::vector<Chord> v;
    buildChordVocabulary (scale, style, v);
    return v;
}

void buildChordVocabulary (const Scale& scale, Style style, std::vector<Chord>& v)
{
    v.clear();
    const auto dia = scale.diatonic();
    const bool minor = scale.isMinorFamily();
    const bool flats = keyPrefersFlats (scale);
    const int t = scale.tonic;

    static const float majorPriors[] = { 1.0f, 0.55f, 0.3f, 0.85f, 0.9f, 0.65f, 0.15f };
    static const float minorPriors[] = { 1.0f, 0.25f, 0.6f, 0.8f, 0.6f, 0.7f, 0.55f };

    const bool sevenths = style == Style::Gospel;
    const bool extraDominant7 = style == Style::Sacred || style == Style::Chamber || style == Style::Spiritual || style == Style::Worship;

    for (int d = 0; d < 7; ++d)
    {
        const int root = dia[(size_t) d];
        const int third = wrapPc (dia[(size_t) ((d + 2) % 7)] - root);
        const int fifth = wrapPc (dia[(size_t) ((d + 4) % 7)] - root);
        const int seventh = wrapPc (dia[(size_t) ((d + 6) % 7)] - root);

        float prior = (minor ? minorPriors : majorPriors)[d];
        if (fifth == 6) prior *= 0.5f;   // diminished
        if (fifth == 8) prior *= 0.2f;   // augmented

        if (sevenths && (d == 0 || d == 3) && third == 4)
            v.push_back (makeChord (root, { 0, third, fifth, 9 }, d, prior, flats));   // gospel I6 / IV6
        else if (sevenths)
            v.push_back (makeChord (root, { 0, third, fifth, seventh }, d, prior, flats));
        else
            v.push_back (makeChord (root, { 0, third, fifth }, d, prior, flats));

        if (extraDominant7 && d == 4 && third == 4 && seventh == 10)
            v.push_back (makeChord (root, { 0, 4, 7, 10 }, d, prior * 0.45f, flats));
    }

    if (minor && wrapPc (dia[6] - t) == 10)
        v.push_back (makeChord (t + 7, { 0, 4, 7 }, 16, 0.75f, flats));   // raised V in natural minor modes

    if (! minor && scale.type != ScaleType::Chromatic)
    {
        if (style == Style::Sacred || style == Style::Cinematic || style == Style::Spiritual || style == Style::Chamber)
            v.push_back (makeChord (t + 5, { 0, 3, 7 }, 10, 0.18f, flats));  // minor iv
        if (style != Style::Gregorian)
            v.push_back (makeChord (t + 2, { 0, 4, 7 }, 11, 0.2f, flats));   // V/V
        if (style == Style::Worship || style == Style::Cinematic || style == Style::Gospel)
            v.push_back (makeChord (t + 10, { 0, 4, 7 }, 12, 0.25f, flats)); // bVII
        if (style == Style::Gospel)
        {
            v.push_back (makeChord (t, { 0, 4, 7, 10 }, 13, 0.35f, flats));      // I7
            v.push_back (makeChord (t + 5, { 0, 4, 7, 10 }, 14, 0.35f, flats));  // IV7
            v.push_back (makeChord (t + 9, { 0, 4, 7, 10 }, 15, 0.25f, flats));  // VI7 (V/ii)
            v.push_back (makeChord (t + 2, { 0, 4, 7, 10 }, 11, 0.3f, flats));   // II7 (V/V)
        }
    }

    if (minor && style == Style::Cinematic)
        v.push_back (makeChord (t + 5, { 0, 4, 7 }, 17, 0.2f, flats)); // major IV (dorian colour)
}

std::array<int, 4> chordQualityIntervals (ChordQuality q)
{
    switch (q)
    {
        case ChordQuality::Minor:      return { 0, 3, 7, -1 };
        case ChordQuality::Dominant7:  return { 0, 4, 7, 10 };
        case ChordQuality::Major7:     return { 0, 4, 7, 11 };
        case ChordQuality::Minor7:     return { 0, 3, 7, 10 };
        case ChordQuality::Sus4:       return { 0, 5, 7, -1 };
        case ChordQuality::Diminished: return { 0, 3, 6, -1 };
        default:                       return { 0, 4, 7, -1 };
    }
}

Chord makeChordOfQuality (int root, ChordQuality q, bool preferFlats)
{
    const auto iv = chordQualityIntervals (q);
    if (iv[3] >= 0)
        return makeChord (root, { iv[0], iv[1], iv[2], iv[3] }, 20, 1.0f, preferFlats);
    return makeChord (root, { iv[0], iv[1], iv[2] }, 20, 1.0f, preferFlats);
}

Chord chordFromPitchClasses (const int* notes, int count)
{
    std::array<int, 12> pcsBuf {};
    int numPcs = 0;
    for (int i = 0; i < count; ++i)
    {
        const int w = wrapPc (notes[i]);
        if (std::find (pcsBuf.begin(), pcsBuf.begin() + numPcs, w) == pcsBuf.begin() + numPcs)
            pcsBuf[(size_t) numPcs++] = w;
    }
    if (numPcs == 0)
        return makeChord (0, { 0, 4, 7 }, 0, 1.0f, false);
    struct View { const int* b; const int* e; const int* begin() const { return b; } const int* end() const { return e; } };
    const View pcs { pcsBuf.data(), pcsBuf.data() + numPcs };

    // Try each pitch class as root; prefer roots that give a third and a fifth.
    int bestRoot = pcsBuf[0];
    int bestScore = -1;
    for (int r : pcs)
    {
        int score = 0;
        for (int p : pcs)
        {
            const int iv = wrapPc (p - r);
            if (iv == 3 || iv == 4) score += 3;
            if (iv == 7) score += 2;
            if (iv == 10 || iv == 11) score += 1;
        }
        if (score > bestScore) { bestScore = score; bestRoot = r; }
    }

    int third = -1, fifth = -1, seventh = -1;
    for (int p : pcs)
    {
        const int iv = wrapPc (p - bestRoot);
        if ((iv == 3 || iv == 4) && third < 0) third = iv;
        else if ((iv == 7 || iv == 6 || iv == 8) && fifth < 0) fifth = iv;
        else if ((iv == 10 || iv == 11 || iv == 9) && seventh < 0) seventh = iv;
        else if ((iv == 2 || iv == 5) && third < 0) third = iv;
    }
    if (third < 0) third = 4;
    if (fifth < 0) fifth = 7;

    if (seventh >= 0)
        return makeChord (bestRoot, { 0, third, fifth, seventh }, 20, 1.0f, false);
    return makeChord (bestRoot, { 0, third, fifth }, 20, 1.0f, false);
}

float progressionBonus (const Chord& from, const Chord& to, bool /*minorKey*/)
{
    static const float table[7][7] = {
        //  I      ii     iii    IV     V      vi     vii
        {  0.0f,  0.1f,  0.0f,  0.15f, 0.12f, 0.1f,  0.0f  }, // from I
        { -0.05f, 0.0f,  0.0f,  0.05f, 0.3f,  0.0f,  0.1f  }, // from ii
        {  0.0f,  0.0f,  0.0f,  0.15f, 0.0f,  0.2f,  0.0f  }, // from iii
        {  0.2f,  0.1f,  0.0f,  0.0f,  0.22f, 0.0f,  0.1f  }, // from IV
        {  0.35f,-0.05f, 0.0f, -0.1f,  0.0f,  0.18f, 0.0f  }, // from V
        {  0.0f,  0.15f, 0.0f,  0.15f, 0.08f, 0.0f,  0.0f  }, // from vi
        {  0.3f,  0.0f,  0.1f,  0.0f,  0.0f,  0.0f,  0.0f  }, // from vii
    };

    const int f = from.degree, t = to.degree;
    if (f < 7 && t < 7)
        return table[f][t];

    // Borrowed and applied chords resolve to their targets.
    switch (f)
    {
        case 10: return t == 0 ? 0.3f : (t == 4 ? 0.1f : 0.0f);
        case 11: return t == 4 ? 0.4f : 0.0f;
        case 12: return t == 0 ? 0.25f : (t == 3 ? 0.1f : 0.0f);
        case 13: return t == 3 || t == 14 ? 0.4f : 0.0f;
        case 14: return t == 0 ? 0.3f : (t == 13 ? 0.2f : 0.0f);
        case 15: return t == 1 ? 0.4f : 0.0f;
        case 16: return t == 0 ? 0.4f : (t == 5 ? 0.15f : 0.0f);
        case 17: return t == 0 ? 0.2f : 0.0f;
        default: break;
    }
    if (f == 0 && (t == 13 || t == 12)) return 0.1f;
    if (f == 3 && t == 10) return 0.15f;
    if ((f == 0 || f == 5 || f == 3) && t == 11) return 0.1f;
    return 0.0f;
}

//==============================================================================
std::array<SectionRange, NumSections> defaultSectionRanges()
{
    // Realistic SATB ranges: soprano C4-A5, alto F3-D5, tenor C3-A4, bass E2-E4.
    return { SectionRange { 60, 81 }, SectionRange { 53, 74 }, SectionRange { 48, 69 }, SectionRange { 40, 64 } };
}

int chooseOctaveShift (int melody, const SectionRange& range, float extraOctavePenalty)
{
    int best = 0;
    float bestDist = 1.0e9f;
    for (int k = -4; k <= 4; ++k)
    {
        const int n = melody + 12 * k;
        float d = std::abs ((float) n - range.centre());
        if (n < range.low) d += (float) (range.low - n) * 3.0f;
        if (n > range.high) d += (float) (n - range.high) * 3.0f;
        d += extraOctavePenalty * (float) std::max (0, std::abs (k) - 1);
        if (d < bestDist) { bestDist = d; best = 12 * k; }
    }
    return best;
}

static int placeInRange (int pc, const SectionRange& r, float target)
{
    int best = -1;
    float bestDist = 1.0e9f;
    for (int n = r.low - 12; n <= r.high + 12; ++n)
    {
        if (wrapPc (n) != wrapPc (pc))
            continue;
        float d = std::abs ((float) n - target);
        if (n < r.low) d += (float) (r.low - n) * 2.0f;
        if (n > r.high) d += (float) (n - r.high) * 2.0f;
        if (d < bestDist) { bestDist = d; best = n; }
    }
    return best;
}

static int chordToneBelow (int note, const Chord& c)
{
    for (int k = note - 1; k >= note - 12; --k)
        if (c.contains (k))
            return k;
    return note - 12;
}

static void fitBelow (int& note, int ceiling, const SectionRange& r)
{
    while (note >= ceiling && note - 12 >= r.low - 5)
        note -= 12;
    while (note < r.low - 5 && note + 12 < ceiling)
        note += 12;
}

Voicing voiceChord (const VoicingRequest& req)
{
    Voicing v;
    v.valid = true;
    const auto& R = req.ranges;
    const Chord& chord = req.chord;
    const bool haveMelody = req.melody >= 0;
    const int sopMel = haveMelody ? req.melody + req.sopranoOctaveShift : -1;
    const bool strict = req.style == Style::Sacred || req.style == Style::Chamber || req.style == Style::Spiritual;

    auto active = [&req] (int s) { return req.active[(size_t) s]; };

    //--------------------------------------------------------------------------
    // Explicit MIDI voicing: map held notes to sections from the top down.
    if (req.numExplicit >= 3)
    {
        std::array<int, 16> notes = req.explicitNotes;
        std::sort (notes.begin(), notes.begin() + req.numExplicit, std::greater<int>());
        std::array<int, NumSections> sections {};
        int numSections = 0;
        for (int s = 0; s < NumSections; ++s)
            if (active (s) && ! (s == Soprano && haveMelody))
                sections[(size_t) numSections++] = s;
        if (haveMelody && active (Soprano))
            v.set (Soprano, sopMel);
        for (int i = 0; i < numSections; ++i)
        {
            const int s = sections[(size_t) i];
            const int src = notes[(size_t) std::min (i, req.numExplicit - 1)];
            v.set (s, placeInRange (src, R[(size_t) s], (float) src));
        }
        return v;
    }

    switch (req.mode)
    {
        case HarmonyMode::Unison:
        {
            const int pc = haveMelody ? req.melody : chord.root;
            for (int s = 0; s < NumSections; ++s)
                if (active (s))
                    v.set (s, s == Soprano && haveMelody ? sopMel
                                                         : placeInRange (pc, R[(size_t) s],
                                                                         haveMelody ? (float) req.melody + (float) chooseOctaveShift (req.melody, R[(size_t) s])
                                                                                    : R[(size_t) s].centre()));
            return v;
        }

        case HarmonyMode::Organum:
        {
            const int top = haveMelody ? sopMel : placeInRange (req.scale.tonic + 7, R[Soprano], R[Soprano].centre());
            if (active (Soprano)) v.set (Soprano, top);
            if (active (Alto))
            {
                int a = req.scale.stepDiatonic (top, -3);
                fitBelow (a, top, R[Alto]);
                v.set (Alto, a);
            }
            if (active (Tenor))
            {
                int t = top - 12;
                while (t > R[Tenor].high + 2) t -= 12;
                v.set (Tenor, t);
            }
            if (active (Bass))
            {
                const int root = placeInRange (req.scale.tonic, R[Bass], (float) R[Bass].low + 5.0f);
                v.setDivisi (Bass, root, root + 7);
            }
            return v;
        }

        case HarmonyMode::ThirdsAndSixths:
        {
            const int top = haveMelody ? sopMel : placeInRange (chord.tones[1], R[Soprano], R[Soprano].centre());
            if (active (Soprano)) v.set (Soprano, top);
            int a = req.scale.stepDiatonic (top, -2);
            if (active (Alto)) { fitBelow (a, top, R[Alto]); v.set (Alto, a); }
            if (active (Tenor))
            {
                int t = req.scale.stepDiatonic (top, -5);
                fitBelow (t, std::min (a, top), R[Tenor]);
                v.set (Tenor, t);
            }
            if (active (Bass))
                v.set (Bass, placeInRange (chord.root, R[Bass], R[Bass].centre() - 3.0f));
            return v;
        }

        case HarmonyMode::UpperVoices:
        {
            // SSAA close stack down from the melody.
            const int s1 = haveMelody ? sopMel : placeInRange (chord.tones[1], R[Soprano], R[Soprano].centre());
            const int s2 = chordToneBelow (s1, chord);
            const int a1 = chordToneBelow (s2, chord);
            const int a2 = chordToneBelow (a1, chord);
            if (active (Soprano)) v.setDivisi (Soprano, s1, s2);
            if (active (Alto)) v.setDivisi (Alto, a1, a2);
            return v;
        }

        case HarmonyMode::LowerVoices:
        {
            // TTBB: melody in the first tenors, root in the low basses.
            const int t1 = haveMelody ? req.melody + chooseOctaveShift (req.melody, R[Tenor])
                                      : placeInRange (chord.tones[1], R[Tenor], R[Tenor].centre());
            const int t2 = chordToneBelow (t1, chord);
            int b1 = chordToneBelow (t2, chord);
            int b2 = placeInRange (chord.root, R[Bass], (float) R[Bass].low + 6.0f);
            if (b2 >= b1) b2 -= 12;
            if (b2 < R[Bass].low - 2) b2 += 12;
            if (active (Tenor)) v.setDivisi (Tenor, t1, t2);
            if (active (Bass)) v.setDivisi (Bass, b1, b2);
            return v;
        }

        case HarmonyMode::GospelStack:
        {
            const int s = haveMelody ? sopMel : placeInRange (chord.tones[1], R[Soprano], R[Soprano].centre());
            int a = chordToneBelow (s, chord);
            int t = chordToneBelow (a, chord);
            if (active (Soprano)) v.set (Soprano, s);
            if (active (Alto)) v.set (Alto, a);
            if (active (Tenor)) { if (t < R[Tenor].low - 3) t += 12; v.set (Tenor, t); }
            if (active (Bass))
            {
                int b = placeInRange (chord.root, R[Bass], R[Bass].centre() - 2.0f);
                const int ceiling = active (Tenor) ? t : (active (Alto) ? a : s);
                while (b >= ceiling) b -= 12;
                if (b < R[Bass].low - 4) b += 12;
                v.set (Bass, b);
            }
            return v;
        }

        default:
            break;
    }

    //--------------------------------------------------------------------------
    // General search (Full Choir, Open Voicing, MIDI chords, Drone).
    struct Cands
    {
        std::array<int, 48> v {};
        int n = 0;
        void push (int x) { if (n < 48) v[(size_t) n++] = x; }
        const int* begin() const { return v.data(); }
        const int* end() const { return v.data() + n; }
    };
    std::array<Cands, NumSections> cands;
    std::array<bool, NumSections> fixed {};
    for (int s = 0; s < NumSections; ++s)
    {
        if (! active (s))
            continue;
        if (s == Soprano && haveMelody && req.mode != HarmonyMode::Drone)
        {
            cands[(size_t) s].push (sopMel);
            fixed[(size_t) s] = true;
            continue;
        }
        for (int n = R[(size_t) s].low; n <= R[(size_t) s].high; ++n)
            if (chord.contains (n))
                cands[(size_t) s].push (n);
        if (cands[(size_t) s].n == 0)
            cands[(size_t) s].push (placeInRange (chord.root, R[(size_t) s], R[(size_t) s].centre()));

        // Very wide ranges give dozens of candidates per part and the search below grows with
        // the fourth power: keep the ones nearest the middle of the range (the far ones lose on
        // tessitura anyway), so a chord change never stalls the audio thread.
        constexpr int kMaxCands = 10;
        auto& c = cands[(size_t) s];
        if (c.n > kMaxCands)
        {
            const float centre = R[(size_t) s].centre();
            std::sort (c.v.begin(), c.v.begin() + c.n, [centre] (int a, int b) { return std::abs ((float) a - centre) < std::abs ((float) b - centre); });
            c.n = kMaxCands;
            std::sort (c.v.begin(), c.v.begin() + c.n);
        }
    }

    Cands none;
    none.push (-1);
    auto candList = [&cands, &none] (int s) -> const Cands&
    {
        return cands[(size_t) s].n == 0 ? none : cands[(size_t) s];
    };

    const bool open = req.mode == HarmonyMode::OpenVoicing;
    const float parallelWeight = strict ? 2.5f : 0.6f;
    const int leadingTone = wrapPc (req.scale.tonic - 1);
    const int leadPc = haveMelody ? wrapPc (req.melody) : -1;

    float bestCost = std::numeric_limits<float>::max();
    std::array<int, NumSections> best { -1, -1, -1, -1 };
    std::array<int, NumSections> cur {};

    for (int sn : candList (Soprano))
    for (int an : candList (Alto))
    for (int tn : candList (Tenor))
    for (int bn : candList (Bass))
    {
        cur = { sn, an, tn, bn };
        float cost = 0.0f;

        // Ordering and spacing between adjacent sounding voices.
        int upper = -1, upperSection = -1;
        for (int s = 0; s < NumSections; ++s)
        {
            const int n = cur[(size_t) s];
            if (n < 0) continue;
            if (upper >= 0)
            {
                if (n > upper) cost += 8.0f;
                else if (n == upper) cost += 2.5f;
                const int gap = upper - n;
                const int maxGap = (s == Bass) ? 19 : 12;
                if (gap > maxGap) cost += (float) (gap - maxGap) * 0.5f;
                if (open && s != Bass && gap < 5) cost += 0.6f;
                if (! open && s != Bass && upperSection != Soprano && gap > 9) cost += 0.2f;
            }
            upper = n;
            upperSection = s;
        }
        if (cost >= bestCost) continue;

        // Bass role.
        int lowest = -1;
        for (int s = NumSections - 1; s >= 0; --s)
            if (cur[(size_t) s] >= 0) { lowest = cur[(size_t) s]; break; }
        if (lowest >= 0)
        {
            const int role = chord.roleOf (lowest);
            static const float rolePenalty[] = { 0.0f, 1.2f, 2.0f, 2.8f };
            float p = role >= 0 ? rolePenalty[role] : 3.0f;
            if (role == 1 && (req.style == Style::Gospel || req.style == Style::Worship)) p = 0.6f;
            if (req.bassPc >= 0)   // the band's bass decides the inversion (G/B: B in the bass)
                p = wrapPc (lowest) == req.bassPc ? 0.0f : 3.5f;
            cost += cur[Bass] >= 0 ? p : p * 0.5f;
        }

        // Chord coverage (the lead melody counts as present).
        // Coverage counts the lead melody; doubling counts only the choir's own voices
        // (a soprano doubling the melody is not an extra third).
        std::array<int, 4> roleCount {}, choirCount {};
        int sounding = 0;
        if (leadPc >= 0)
            if (const int role = chord.roleOf (leadPc); role >= 0) ++roleCount[(size_t) role];
        for (int s = 0; s < NumSections; ++s)
        {
            const int n = cur[(size_t) s];
            if (n < 0) continue;
            ++sounding;
            const int role = chord.roleOf (n);
            if (role < 0) continue;
            ++roleCount[(size_t) role];
            if (! (s == Soprano && fixed[Soprano]))
                ++choirCount[(size_t) role];
        }
        const int voicesAvailable = sounding + (leadPc >= 0 ? 1 : 0);
        if (voicesAvailable >= 2)
        {
            if (roleCount[0] == 0) cost += 4.0f;
            if (roleCount[1] == 0 && chord.numTones > 1) cost += 3.5f;
            if (voicesAvailable >= 3 && roleCount[2] == 0 && chord.numTones > 2) cost += sounding >= 4 ? 0.9f : 0.4f;
            if (chord.numTones > 3 && roleCount[3] == 0) cost += voicesAvailable >= 4 ? 1.5f : 0.6f;
        }
        const int leadRole = leadPc >= 0 ? chord.roleOf (leadPc) : -1;
        const int thirds = choirCount[1] + (leadRole == 1 ? 1 : 0);
        if (strict && thirds > 1) cost += 0.8f;
        if (choirCount[3] + (leadRole == 3 ? 1 : 0) > 1) cost += 3.0f;
        {
            int lt = 0;
            for (int s = 0; s < NumSections; ++s)
                if (cur[(size_t) s] >= 0 && wrapPc (cur[(size_t) s]) == leadingTone) ++lt;
            if (leadPc == leadingTone) ++lt;
            if (lt > 1 && ! req.scale.isMinorFamily()) cost += 2.0f;
        }
        if (cost >= bestCost) continue;

        // Tessitura, voice leading, shift size.
        for (int s = 0; s < NumSections; ++s)
        {
            const int n = cur[(size_t) s];
            if (n < 0 || fixed[(size_t) s]) continue;
            const float d = std::abs ((float) n - R[(size_t) s].centre());
            cost += d * 0.06f + (d > 7.0f ? (d - 7.0f) * 0.25f : 0.0f);

            if (req.previous != nullptr && req.previous->valid)
            {
                const int p = req.previous->primary (s);
                if (p >= 0)
                {
                    const int m = std::abs (n - p);
                    cost += (float) m * (s == Bass ? 0.16f : 0.24f);
                    if (m > 7 && s != Bass) cost += 1.0f;
                    if (m > 12) cost += 2.0f;
                }
            }

            if (req.wordsMode && haveMelody)
            {
                const int shift = std::abs (n - req.melody);
                if (shift > 12) cost += (float) (shift - 12) * 0.15f;
                if (shift > 19) cost += (float) (shift - 19) * 0.4f;
            }

            if (haveMelody && n == req.melody && s != Soprano) cost += 0.5f;
        }
        if (cost >= bestCost) continue;

        // Parallel fifths and octaves against the previous chord.
        if (req.previous != nullptr && req.previous->valid)
        {
            for (int i = 0; i < NumSections; ++i)
            {
                const int ci = cur[(size_t) i], pi = req.previous->primary (i);
                if (ci < 0 || pi < 0 || ci == pi) continue;
                for (int j = i + 1; j < NumSections; ++j)
                {
                    const int cj = cur[(size_t) j], pj = req.previous->primary (j);
                    if (cj < 0 || pj < 0 || cj == pj) continue;
                    if ((ci > pi) != (cj > pj)) continue;
                    const int prevIv = wrapPc (pi - pj), curIv = wrapPc (ci - cj);
                    if (prevIv == curIv && (curIv == 0 || curIv == 7))
                        cost += parallelWeight;
                }
            }
        }

        if (cost < bestCost)
        {
            bestCost = cost;
            best = cur;
        }
    }

    for (int s = 0; s < NumSections; ++s)
        if (best[(size_t) s] >= 0)
            v.set (s, best[(size_t) s]);
    return v;
}

//==============================================================================
namespace
{
    const float majorProfile[] = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
    const float minorProfile[] = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };

    float correlateKey (const std::array<float, 12>& h, const float* profile, int tonic)
    {
        float mh = 0, mp = 0;
        for (int i = 0; i < 12; ++i) { mh += h[(size_t) i]; mp += profile[i]; }
        mh /= 12.0f; mp /= 12.0f;
        float num = 0, dh = 0, dp = 0;
        for (int i = 0; i < 12; ++i)
        {
            const float a = h[(size_t) wrapPc (i + tonic)] - mh;
            const float b = profile[i] - mp;
            num += a * b; dh += a * a; dp += b * b;
        }
        return dh > 0 && dp > 0 ? num / std::sqrt (dh * dp) : 0.0f;
    }

    // A harmoniser must never pick a key that leaves out notes the singer keeps singing:
    // those notes would be snapped to a neighbour and harmonised with clashing chords.
    float coveragePenalty (const std::array<float, 12>& h, int tonic, bool minor)
    {
        float total = 0.0f;
        for (float v : h) total += v;
        if (total <= 0.0f)
            return 0.0f;
        static const int majorSteps[] = { 0, 2, 4, 5, 7, 9, 11 };
        static const int minorSteps[] = { 0, 2, 3, 5, 7, 8, 10, 11 };   // natural + raised 7th
        const int* steps = minor ? minorSteps : majorSteps;
        const int numSteps = minor ? 8 : 7;
        float inside = 0.0f;
        for (int i = 0; i < numSteps; ++i)
            inside += h[(size_t) wrapPc (tonic + steps[i])];
        const float outside = 1.0f - inside / total;
        return 2.0f * std::max (0.0f, outside - 0.06f);
    }
}

float keyFitness (const std::array<float, 12>& h, int tonic, bool minor)
{
    return correlateKey (h, minor ? minorProfile : majorProfile, tonic) - coveragePenalty (h, tonic, minor);
}

KeyEstimate estimateKey (const std::array<float, 12>& h, bool restrictToMinor, bool restrictToMajor)
{
    KeyEstimate best;
    best.score = -2.0f;
    float bestRank = -1.0e9f;
    for (int tonic = 0; tonic < 12; ++tonic)
    {
        if (! restrictToMinor)
        {
            const float rank = keyFitness (h, tonic, false);
            if (rank > bestRank) { bestRank = rank; best = { tonic, false, correlateKey (h, majorProfile, tonic) }; }
        }
        if (! restrictToMajor)
        {
            const float rank = keyFitness (h, tonic, true);
            if (rank > bestRank) { bestRank = rank; best = { tonic, true, correlateKey (h, minorProfile, tonic) }; }
        }
    }
    return best;
}
} // namespace sc
