// Test helpers: WAV I/O and a synthetic singing voice.
#pragma once

#include <sanctuary/Common.h>
#include <string>
#include <vector>

namespace sc::test
{
struct Audio
{
    double sampleRate = 48000.0;
    std::vector<std::vector<float>> channels;
    int64_t length() const { return channels.empty() ? 0 : (int64_t) channels[0].size(); }
};

bool readWav (const std::string& path, Audio& out, std::string& error);
bool writeWav (const std::string& path, const Audio& a, int bitsPerSample = 24);

struct SungNote
{
    float midi;       // pitch
    float seconds;    // duration
    float gapAfter;   // silence after (seconds); consonant noise is placed in gaps
};

/** Synthesises a sung melody: glottal source, vowel formants, vibrato, jitter, consonants. */
std::vector<float> singMelody (const std::vector<SungNote>& notes, double sampleRate, int voiceSection,
                               float vibratoCents = 25.0f, uint32_t seed = 7);

/** "Amazing Grace" opening in G major (useful for learning tests). */
std::vector<SungNote> amazingGrace (float transpose = 0.0f);
/** C major scale up and down. */
std::vector<SungNote> cMajorScale (float baseMidi = 60.0f, float noteSeconds = 0.4f);

/** A chord the test band plays: its notes (MIDI), the bass note (MIDI, -1 for none) and how long. */
struct BandChordSpec
{
    std::vector<int> notes;
    int bass = -1;
    float seconds = 2.0f;
};

/** A band for the side-chain: a piano-like instrument striking each chord on every beat, a
    bass playing the bass note, and a little noise. Starts after leadIn seconds of silence. */
std::vector<float> playBand (const std::vector<BandChordSpec>& chords, double sampleRate, float beatSeconds = 0.5f,
                             float leadIn = 0.0f, uint32_t seed = 3);
} // namespace sc::test
