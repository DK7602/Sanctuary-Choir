// Sanctuary Choir - the venues' reverb from measured rooms (partitioned convolution).
#pragma once

#include "FFT.h"
#include "Filters.h"

#include <memory>

namespace sc
{
/** A room's measured response: what a spaced pair of microphones in the hall hears from the
    stage (direct sound removed), at its own sample rate. */
struct RoomImpulse
{
    double sampleRate = 48000.0;
    std::vector<float> left, right;
};

/** Windowed-sinc resampling, for impulse responses (off the audio thread). */
std::vector<float> resampleImpulse (const std::vector<float>& x, double fromRate, double toRate);

/** Decay time of a response: T30 between 500 Hz and 2 kHz, extrapolated to 60 dB, seconds. */
float measureDecayTime (const float* x, int64_t n, double sampleRate);

/** How the room is heard: the FX page's reverb knobs. */
struct RoomShape
{
    float decayScale = 1.0f;   // multiplies the room's decay time (0.3 .. 2.5)
    float preDelayMs = 0.0f;
    float brightness = 0.5f;   // 0 dark, 0.5 as measured, 1 bright
    float early = 1.0f;        // level of the first reflections (0.25 .. 1)
    float width = 1.0f;        // 0 mono .. 1 as measured
};

/** A measured room ready to play at one sample rate: the response cut into partitions and
    transformed, plus the convolution's running state. create() allocates and runs a few
    hundred FFTs, so it belongs off the audio thread; process() never allocates.

    Uniformly partitioned convolution in two sizes. The first 85 ms of the response run in
    short blocks (256 samples at 48 kHz: the room answers 5 ms late, which the pre-delay
    absorbs), the rest in blocks eight times as long. Their transforms and products are spread
    evenly over the period they are computed in, so no audio block carries a burst of work.
    The Decay knob scales each partition by the envelope that stretches the room's decay, and
    partitions the decay has left behind are skipped. */
class ConvolutionRoom
{
public:
    static std::unique_ptr<ConvolutionRoom> create (const RoomImpulse& ir, double sampleRate, double maxPreDelayMs = 250.0);
    ~ConvolutionRoom();

    double getSampleRate() const noexcept { return sr; }
    float getDecayTime() const noexcept { return rt60; }
    int getLatency() const noexcept { return headBlock; }
    int64_t getLength() const noexcept { return length; }

    void reset() noexcept;
    /** Writes the room's sound (wet only) for n samples of input. Each input channel also
        reaches the other side's microphone, a little less (the stage is one room). */
    void process (const float* inL, const float* inR, float* outL, float* outR, int n, const RoomShape& shape) noexcept;

    static constexpr float kCrossFeed = 0.5f;

private:
    ConvolutionRoom() = default;

    struct Stage
    {
        int block = 0, size = 0, bins = 0, parts = 0;
        int64_t offset = 0;                       // where in the response this stage starts
        std::unique_ptr<FFT> fft;
        std::vector<float> hRe[2], hIm[2];       // the response's partitions [part * bins + bin]
        std::vector<float> xRe[2], xIm[2];       // the input's last `parts` blocks, a ring
        std::vector<float> yRe[2], yIm[2];       // the sum being built
        std::vector<std::complex<float>> work;
        int newest = 0;                           // ring slot of the newest input block
    };

    void transformInput (Stage& s, int64_t end) noexcept;
    void accumulate (Stage& s, int part, float gain) noexcept;
    void finish (Stage& s, int64_t outStart) noexcept;
    float partGain (int64_t t) const noexcept;
    bool partActive (int64_t t) const noexcept;
    void runTailUnits (int upTo) noexcept;
    void updateShape (const RoomShape& shape) noexcept;

    double sr = 48000.0;
    float rt60 = 2.0f;
    int64_t length = 0;
    int headBlock = 256;
    Stage head, tail;

    // Input after the pre-delay and the cross-feed (a: to the left mic, b: to the right), and
    // the output, both rings indexed by absolute sample time.
    std::vector<float> inA, inB, outL, outR;
    int64_t ringMask = 0;
    std::vector<float> preL, preR;
    int preMask = 0, prePos = 0;
    int64_t now = 0;           // input samples taken in

    // The tail's spread-out work: unit 0 transforms the input, 1..parts multiply, the last
    // writes the output.
    int tailDone = 0, tailUnits = 0;
    int64_t tailPeriodStart = -1;

    RoomShape current;
    float decayRate = 0.0f;    // nepers per second added back (or taken away) by the Decay knob
    int64_t activeLength = 0;
    float earlyGain = 1.0f;
    int preDelay = 0;
    Biquad shelfL, shelfR;
    float shelfDb = 0.0f;
};
} // namespace sc
