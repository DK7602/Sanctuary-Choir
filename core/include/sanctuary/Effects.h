// Sanctuary Choir - room and bus processing.
#pragma once

#include "Filters.h"

namespace sc
{
enum class Venue : int { GrandCathedral = 0, StoneAbbey, WoodenChapel, ConcertHall, GospelChurch, StudioRoom, Count };
const char* const* getVenueNames();
/** File names of the venues' measured rooms ("grand-cathedral" for <slug>.ogg/.wav). */
const char* getVenueSlug (Venue v);
/** Level of a venue's measured room (unit energy) that matches the algorithmic hall's, so a
    preset's Reverb amount sounds the same with either. */
float getMeasuredRoomLevel (Venue v);

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

/** The stage and its microphones. Each section arrives as a stereo bus whose left and right
    edges are the section's own edges; the stage puts it at its place (left/right, how wide,
    how far back) and three pairs of microphones pick the choir up:
      - Close: spot mics in front of each section. Dry, near and detailed; every row as loud.
      - Tree: the main pair above the conductor. The natural picture: back rows a little later,
        quieter and darker.
      - Room: a pair far out in the hall. Wide and distant, with much more of the hall.
    The mics are mixed by level; the hall's reverb is added afterwards by the engine (how much
    of it the mics hear is reverbAmount()). */
class ChoirStage
{
public:
    struct Placement
    {
        float x = 0.0f;       // -1 stage left .. 1 stage right (audience view)
        float width = 0.42f;  // half the section's width, in the same units
        float depth = 0.25f;  // 0 front edge .. 1 back wall
    };
    struct Settings
    {
        std::array<Placement, NumSections> sections;
        float spread = 0.7f;                             // how wide the whole stage sounds (Space)
        float close = 0.0f, tree = 1.0f, room = 0.0f;    // mic levels (linear gain)
    };

    void prepare (double sampleRate, int maxBlock);
    void reset();
    void setSettings (const Settings& s);
    /** Starts a block: call once, then addSection() for each section, then mix(). */
    void begin (int n);
    /** Adds a section's bus (already at its level) to the microphones. */
    void addSection (int section, const float* left, const float* right, int n);
    /** The mic mix (overwrites out). */
    void mix (float* outL, float* outR, int n);
    /** How much of the hall the mics hear, relative to the Tree alone (0.35 Close + Tree + 1.8 Room). */
    float reverbAmount() const noexcept { return 0.35f * gClose + gTree + 1.8f * gRoom; }
    /** Scales the mic mix back to a Tree-like level for the hall's reverb input. */
    float reverbInputScale() const noexcept { const float g = gClose + gTree + gRoom; return g > 1.0e-3f ? 1.0f / g : 0.0f; }
    /** Where a section sits, in seconds behind the front-most section (for tests). */
    float sectionDelaySeconds (int section) const noexcept;

private:
    struct Ring
    {
        std::vector<float> buf;
        int mask = 0, pos = 0;
        void prepare (int maxLen);
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }
        inline void push (float x) { buf[(size_t) pos] = x; pos = (pos + 1) & mask; }
        inline float tap (float d) const
        {
            const int i = (int) d;
            const float fr = d - (float) i;
            const float a = buf[(size_t) ((pos - 1 - i) & mask)], b = buf[(size_t) ((pos - 2 - i) & mask)];
            return a + (b - a) * fr;
        }
    };
    struct SectionState
    {
        Smoothed x, width, depth;
        Ring delayL, delayR;
        float delay = 0.0f, delayTarget = 0.0f;   // samples
        OnePoleLP airL, airR;                     // the tree hears the back rows darker
    };

    double sr = 48000.0;
    Settings settings;
    std::array<SectionState, NumSections> sections;
    Smoothed close, tree, room, spread;
    float gClose = 0.0f, gTree = 1.0f, gRoom = 0.0f, gSpread = 0.7f;
    std::vector<float> closeL, closeR, treeL, treeR, roomL, roomR;
    std::array<Biquad, 2> closePresence, roomTone;
    Ring roomDelayL, roomDelayR;
    float roomDelayLeft = 0.0f, roomDelayRight = 0.0f;
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

/** Output limiter (ceiling about -0.3 dBFS) that looks ahead: it sees each peak 1.5 ms before
    it plays and turns the gain down in a straight ramp that arrives at the peak's level with the
    peak, holds it while the peaks keep coming, then lets it back up slowly. A choir pushed far
    past full scale gets quieter instead of crackling (gain that jumps sample to sample, or
    rises and falls with every cycle of the waveform, is heard as static).
    The output is getLatency() samples late; l and r may be the same buffer (mono). */
class SafetyLimiter
{
public:
    void prepare (double sampleRate);
    void reset();
    int getLatency() const noexcept { return look; }
    void process (float* l, float* r, int n) noexcept;
    /** The gain in force (1 = not limiting), for tests and meters. */
    float getGain() const noexcept { return env; }

private:
    int look = 72;                       // look-ahead (and latency), samples
    std::vector<float> delayL, delayR;   // the input, look samples back
    std::vector<float> minVal;           // sliding minimum of the gain each sample needs (a deque)
    std::vector<int64_t> minAt;
    int minHead = 0, minCount = 0;
    std::vector<float> held;             // that minimum, the last look + 1 samples (for the ramp)
    double heldSum = 0.0;
    int pos = 0;
    int64_t t = 0;
    float env = 1.0f, rel = 0.0f;
    int hold = 0, holdLeft = 0;
    const float ceiling = 0.966f;
};

inline float softSaturate (float x, float drive)
{
    const float d = 1.0f + drive * 4.0f;
    return std::tanh (x * d) / std::tanh (d);
}
} // namespace sc
