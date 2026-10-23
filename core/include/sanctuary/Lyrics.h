// Sanctuary Choir - typed words for the MIDI choir.
//
// Text such as "Kyrie eleison" or "Glory to God" is split into sung syllables: the consonants
// that start each one, the vowel the choir holds on it (one of the five sung vowels, the way a
// choir sings them: pure, never a diphthong) and the consonants that close it. Latin is sung
// the church way (Kyrie = ky-ri-e, Christe = kri-ste, excelsis = ek-shel-sis); anything else
// is read as English. Each new MIDI note then sings the next syllable.
//
// Write "-" inside a word to choose the syllables yourself (Ho-san-na), and "_" to hold a
// syllable over one more note (A _ _ men).
#pragma once

#include "Theory.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sc
{
enum class Phone : uint8_t
{
    P, B, T, D, K, G,         // stops
    F, V, S, Z, Sh, Zh, H, Th, // fricatives
    Ch, J,                    // affricates
    M, N, Ng,                 // nasals
    L, R, Y, W,               // liquids and glides
    Count
};
const char* phoneName (Phone p);

struct Syllable
{
    std::string text;                 // as written, e.g. "Ky"
    std::array<Phone, 4> onset {};    // consonants before the vowel
    std::array<Phone, 4> coda {};     // consonants after it
    uint8_t numOnset = 0, numCoda = 0;
    VocalType vowel = VocalType::Ah;  // Ah, Oh, Oo, Ee or Eh
    bool hold = false;                // "_": the previous syllable carries on over this note
    bool wordStart = false;
};

class Lyrics
{
public:
    static Lyrics parse (const std::string& text);

    const std::vector<Syllable>& syllables() const noexcept { return syl; }
    bool empty() const noexcept { return syl.empty(); }
    int size() const noexcept { return (int) syl.size(); }
    /** "Ky-ri-e e-le-i-son" */
    std::string hyphenated() const;
    /** True if the text was read as Latin. */
    bool isLatin() const noexcept { return latin; }

private:
    std::vector<Syllable> syl;
    bool latin = false;
};

/** How a consonant (or the run-up to a vowel) sounds at one moment, for the singers. */
struct Articulation
{
    float vowel = 1.0f;       // gain on the sung vowel
    float lowpass = 0.0f;     // Hz, 0 = open (nasals and liquids darken the voice)
    float noiseHz = 0.0f;     // centre of the hiss or burst
    float noiseQ = 1.0f;
    float noise = 0.0f;       // its level, against the voice
};

/** A syllable turned into sound: what to do from the moment its note starts (the end of the
    previous syllable, then this one's onset, then the vowel), and when the note is let go
    (its coda). Built off the audio thread, read on it. */
struct LyricScript
{
    struct Segment
    {
        float seconds = 0.0f;
        Articulation a;
    };
    struct Entry
    {
        VocalType vowel = VocalType::Ah;
        bool hold = false;
        std::array<Segment, 16> start {};
        std::array<Segment, 8> end {};
        uint8_t numStart = 0, numEnd = 0;
        std::array<Phone, 4> onset {};   // this syllable's own opening consonants
        uint8_t numOnset = 0;
        float onsetAt = 0.0f;            // seconds into `start` where they begin (after the
                                         // syllable before has closed)
    };

    static LyricScript make (const Lyrics& lyrics);
    /** How the singer sounds `t` seconds into the entry's start (or, with `ending`, into its
        coda after the note is let go). Past the consonants: the open vowel. */
    static Articulation at (const Entry& e, float t, bool ending) noexcept;

    std::vector<Entry> entries;
    std::string text;   // the hyphenated syllables, for the UI
};
} // namespace sc
