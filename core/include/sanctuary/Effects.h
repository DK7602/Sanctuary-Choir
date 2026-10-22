// Sanctuary Choir - room and bus processing.
#pragma once

#include "Filters.h"

namespace sc
{
enum class Venue : int { GrandCathedral = 0, StoneAbbey, WoodenChapel, ConcertHall, GospelChurch, StudioRoom, Count };
const char* const* getVenueNames();

/** Stereo feedback-delay-network reverb with early reflections, voiced as sacred spaces. */
class ChoirReverb
{
public:
    struct Settings
    {
        Venue venue = Venue::GrandCathedral;
        float decayScale = 1.0f;   // multiplies the venue RT60 (0.5 .. 2)
        float preDelayMs = -1.0f;  // < 0: venue default
        float brightness = 0.5f;   // 0 dark .. 1 bright tail
        float early = 0.6f;        // early reflection level 0..1
        float width = 1.0f;        // stereo width of the tail 0..1

        bool operator== (const Settings& o) const
        {
            auto same = [] (float a, float b) { return std::abs (a - b) < 1.0e-6f; };
            return venue == o.venue && same (decayScale, o.decayScale) && same (preDelayMs, o.preDelayMs)
                && same (brightness, o.brightness) && same (early, o.early) && same (width, o.width);
        }
    };

    void prepare (double sampleRate);
    void reset();
    void setSettings (const Settings& s);
    /** Writes 100% wet output (adds nothing to the input). */
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

    float getRt60() const noexcept { return rt60; }

private:
    static constexpr int numLines = 8;

    struct Delay
    {
        std::vector<float> buf;
        int mask = 0, pos = 0;
        void prepare (int maxLen)
        {
            int size = 1;
            while (size < maxLen + 4) size <<= 1;
            buf.assign ((size_t) size, 0.0f);
            mask = size - 1;
            pos = 0;
        }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
        inline void push (float x) { buf[(size_t) pos] = x; pos = (pos + 1) & mask; }
        inline float tap (int d) const { return buf[(size_t) ((pos - 1 - d) & mask)]; }
        inline float tapFrac (float d) const
        {
            const int i = (int) d;
            const float fr = d - (float) i;
            const float a = tap (i), b = tap (i + 1);
            return a + (b - a) * fr;
        }
    };

    struct Allpass
    {
        Delay d;
        int len = 1;
        float g = 0.6f;
        inline float process (float x)
        {
            const float v = d.tap (len - 1);
            const float w = x + g * v;
            d.push (w);
            return v - g * w;
        }
    };

    void updateCoefficients();

    double sr = 48000.0;
    Settings settings;
    float rt60 = 5.0f;

    Delay preL, preR;
    int preDelaySamples = 0;
    std::array<Allpass, 4> diffL, diffR;
    std::array<Delay, numLines> lines;
    std::array<float, numLines> lineLen {}, lineGain {}, dampA {}, dampZ {};
    std::array<float, numLines> lfoPhase {}, lfoRate {}, lfoValue {};
    uint32_t modCounter = 0;
    float lfoDepth = 8.0f;
    struct Tap { int delay; float gain; };
    std::vector<Tap> earlyL, earlyR;
    float earlyGain = 0.5f, tailGain = 0.5f;
    OnePoleLP earlyToneL, earlyToneR;
    Biquad tailHpL, tailHpR;
};

/** Low cut, high cut, tilt ("Tone") and presence for the choir bus. */
class ToneStage
{
public:
    void prepare (double sampleRate);
    void reset();
    void set (float lowCutHz, float highCutHz, float tiltDb, float presence);
    void process (float* l, float* r, int n);

private:
    double sr = 48000.0;
    float cLow = -1, cHigh = -1, cTilt = -100, cPres = -1;
    std::array<Biquad, 2> hp, lp, lowShelf, highShelf, presPeak, air;
};

/** Gentle bus compressor that glues the sections together. */
class GlueCompressor
{
public:
    void prepare (double sampleRate);
    void reset() { env = 0.0f; gain = 1.0f; }
    void process (float* l, float* r, int n, float amount);
    float getGainReductionDb() const noexcept { return gainToDb (gain); }

private:
    double sr = 48000.0;
    float att = 0, rel = 0, env = 0, gain = 1, gainSmooth = 0;
};

/** Output safety limiter (ceiling about -0.3 dBFS). */
class SafetyLimiter
{
public:
    void prepare (double sampleRate);
    void reset() { gain = 1.0f; }
    void process (float* l, float* r, int n);

private:
    float rel = 0, gain = 1.0f;
    const float ceiling = 0.966f;
};

inline float softSaturate (float x, float drive)
{
    const float d = 1.0f + drive * 4.0f;
    return std::tanh (x * d) / std::tanh (d);
}
} // namespace sc
