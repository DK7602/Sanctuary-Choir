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
} // namespace sc::test
