// Sanctuary Choir - streaming YIN pitch tracker.
//
// The input is low-passed and decimated to roughly 20-24 kHz, then analysed with
// the YIN cumulative-mean-normalised difference function every ~5 ms.
#pragma once

#include "Filters.h"

namespace sc
{
struct PitchFrame
{
    int64_t centre = 0;     // input sample index (full rate) the estimate refers to
    float hz = 0.0f;        // fundamental, 0 when unvoiced
    float midi = 0.0f;      // fundamental as fractional MIDI note (valid when voiced)
    float clarity = 0.0f;   // 1 - aperiodicity (0..1)
    float rmsDb = -120.0f;  // level of the analysis window
    bool voiced = false;
};

struct PitchTrackerSettings
{
    float minHz = 65.0f;
    float maxHz = 1100.0f;
    float threshold = 0.15f;     // YIN absolute threshold
    float gateDb = -52.0f;       // windows quieter than this are unvoiced
};

class PitchTracker
{
public:
    using Settings = PitchTrackerSettings;

    void prepare (double sampleRate, Settings s = {});
    void reset();

    /** Feed one block. Returns the number of new frames available via getFrame(). */
    int push (const float* x, int n);

    int getNumNewFrames() const noexcept { return numNew; }
    const PitchFrame& getNewFrame (int i) const { return newFrames[(size_t) i]; }
    const PitchFrame& getLatest() const noexcept { return latest; }

    /** Delay (full-rate samples) between the newest input and the centre of the newest frame. */
    int getAnalysisDelay() const noexcept { return analysisDelay; }
    int getHop() const noexcept { return hop * decimation; }

    /** Offline helper: analyse a whole buffer, one frame per hop. */
    static std::vector<PitchFrame> analyse (const float* x, int64_t n, double sampleRate, Settings s = {});

private:
    void analyseWindow();

    double sr = 48000.0;
    Settings settings;
    int decimation = 2;
    double dsr = 24000.0;
    int tauMin = 20, tauMax = 400, window = 400, bufLen = 800, hop = 120;
    int analysisDelay = 0;

    Biquad aa1, aa2;
    int decimPhase = 0;
    std::vector<float> ring;    // decimated input history (circular)
    int ringPos = 0;
    int64_t decimCount = 0;     // number of decimated samples written
    int sinceHop = 0;
    int64_t fullCount = 0;      // number of full-rate samples consumed

    std::vector<float> frameBuf, diff, cmnd;
    std::vector<PitchFrame> newFrames;
    int numNew = 0;
    PitchFrame latest;
    float lastMidi = 0.0f;
    int voicedRun = 0;
};
} // namespace sc
