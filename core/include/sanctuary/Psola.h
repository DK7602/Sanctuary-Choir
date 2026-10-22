// Sanctuary Choir - pitch-synchronous overlap-add (TD-PSOLA) pitch shifting.
//
// One PsolaAnalyzer holds the input history and pitch marks; many PsolaVoices
// read grains from it, each with its own pitch ratio, formant factor and delay.
#pragma once

#include "PitchTracker.h"

namespace sc
{
struct PsolaMark
{
    int64_t pos = 0;     // centre sample (input timeline)
    float period = 0.0f; // samples
    bool voiced = false;
};

class PsolaAnalyzer
{
public:
    /** maxPeriod: longest pitch period in samples; history: how far back voices may read. */
    void prepare (double sampleRate, int maxPeriod, int historySamples);
    void reset();

    void pushInput (const float* x, int n);
    void addPitchFrame (const PitchFrame& f);
    /** Place as many pitch marks as the available input and pitch data allow. */
    void updateMarks();

    int64_t getWritten() const noexcept { return written; }
    int getMaxPeriod() const noexcept { return maxPeriod; }
    int getUnvoicedHop() const noexcept { return unvoicedHop; }
    /** How far ahead of a synthesis time marks are guaranteed to exist, given the input written. */
    int64_t getMarkedUntil() const noexcept { return markCount > 0 ? marks[(size_t) ((markCount - 1) & markMask)].pos : 0; }

    inline float sampleAt (double pos) const
    {
        const double fl = std::floor (pos);
        const int64_t i = (int64_t) fl;
        const float fr = (float) (pos - fl);
        const float a = ring[(size_t) (i & ringMask)];
        const float b = ring[(size_t) ((i + 1) & ringMask)];
        return a + (b - a) * fr;
    }

    /** Nearest mark to time t, using and updating a caller-owned cursor. nullptr if none. */
    const PsolaMark* nearestMark (double t, int64_t& cursor) const;

    // Freeze: snapshot recent voiced material so voices can sustain it indefinitely.
    bool captureFreeze (int64_t endPos, int lengthSamples);
    void releaseFreeze() { frozen = false; }
    bool isFrozen() const noexcept { return frozen; }
    int getNumFrozenMarks() const noexcept { return (int) frozenMarks.size(); }
    const PsolaMark& getFrozenMark (int i) const { return frozenMarks[(size_t) i]; }
    inline float frozenSampleAt (double pos) const
    {
        const double rel = pos - (double) frozenStart;
        if (rel < 0.0 || rel >= (double) frozenData.size() - 1)
            return 0.0f;
        const int i = (int) rel;
        const float fr = (float) (rel - i);
        return frozenData[(size_t) i] + (frozenData[(size_t) i + 1] - frozenData[(size_t) i]) * fr;
    }

private:
    bool pitchAt (int64_t pos, float& hz, bool& voiced) const;
    float correlationRefine (int64_t prevPos, int64_t predicted, int period) const;

    double sr = 48000.0;
    int maxPeriod = 740, unvoicedHop = 240;
    std::vector<float> ring;
    int64_t ringMask = 0, written = 0;

    std::vector<PsolaMark> marks;
    int64_t markMask = 0, markCount = 0;

    std::vector<PitchFrame> frames;
    int64_t frameMask = 0, frameCount = 0;

    bool frozen = false;
    std::vector<float> frozenData;
    std::vector<PsolaMark> frozenMarks;
    int64_t frozenStart = 0;
};

/** Per-voice synthesis controls (updated at control rate). */
struct PsolaControl
{
    float targetHz = 220.0f;     // absolute target pitch (used when fixedRatio <= 0)
    float fixedRatio = 0.0f;     // > 0: follow the input with this exact ratio (melody doubling)
    float formant = 1.0f;        // formant scale (>1 brighter/smaller, <1 darker/larger)
    float gain = 1.0f;
    float unvoicedGain = 1.0f;   // level of consonants/breaths
};

class PsolaVoice
{
public:
    void prepare (double sampleRate, int maxBlock, int maxPeriod);
    void reset (double startTime);

    /** Adds this voice's output for voice-time [t0, t0+n) to out. */
    void render (const PsolaAnalyzer& analyzer, int64_t t0, int n, float* out, const PsolaControl& ctl, Rng& rng);

    static float hann (float x); // x in [-1, 1]

private:
    void addGrain (const PsolaAnalyzer& an, double centre, double src, float halfLen, float formant, float gain, bool frozenSrc);

    std::vector<float> accum;
    int64_t accumMask = 0;
    double nextSynth = 0.0;
    int64_t cursor = 0;
    int frozenIndex = 0;
    float maxHalf = 740.0f;
    float sampleRate = 48000.0f;
};
} // namespace sc
