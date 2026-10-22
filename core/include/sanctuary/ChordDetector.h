// Sanctuary Choir - hearing the band.
//
// Recognises the chord a band, a keyboard or a backing track is playing, from audio on the
// side-chain input, so the choir can harmonise with the song's real chords rather than guess
// them from the melody alone. The audio is decimated to ~11 kHz and analysed with a 4096-point
// FFT every 512 samples (~45 ms): semitone energies give a chroma (which pitch classes sound)
// and a bass profile (which note is lowest), matched against chord templates that include
// each note's overtones.
#pragma once

#include "FFT.h"
#include "Filters.h"
#include "Theory.h"

#include <atomic>
#include <complex>
#include <memory>
#include <vector>

namespace sc
{
constexpr int kBandChordTypes = (int) ChordQuality::Count;
constexpr int kBandChords = 12 * kBandChordTypes;   // index = root * kBandChordTypes + quality

/** The chords a band can be heard playing (built once, shared). */
const std::array<Chord, kBandChords>& bandChordTable();
/** Score adjustment for a chord over a bass note: the root in the bass fits best, another chord
    note (an inversion) nearly as well, a note outside the chord (a passing note) less. */
float bandBassFit (int chordIndex, int bassPc) noexcept;

/** One analysis frame. */
struct BandFrame
{
    std::array<float, kBandChords> score {};   // how well each chord's notes fit (cosine, 0..1), bass aside
    int bass = -1;                             // pitch class of the lowest strong note, -1 if none
    float level = 0.0f;                        // RMS of the frame (linear)
    float flux = 0.0f;                         // how much the spectrum changed (onsets)
    int64_t centre = 0;                        // input sample at the middle of the frame's window
};

/** Streaming spectral analysis of the band (no allocation after prepare). */
class BandAnalyzer
{
public:
    /** live: twice the frame rate, and the chord notes are read from a shorter window weighted
        towards the newest audio, so chord changes are heard sooner (the bass keeps the long
        window it needs). Offline analysis uses long, centred windows throughout. */
    void prepare (double sampleRate, bool live = false);
    void reset();

    /** Pushes input samples; calls onFrame (const BandFrame&) for every new frame. */
    template <typename OnFrame>
    void push (const float* x, int n, OnFrame&& onFrame)
    {
        for (int i = 0; i < n; ++i)
        {
            ++pushed;
            const float y = aa2.process (aa1.process (x[i]));
            if (++decimCount < decim)
                continue;
            decimCount = 0;
            ring[(size_t) ringPos] = y;
            ringPos = (ringPos + 1) & (kSize - 1);
            if (++sinceHop >= hop)
            {
                sinceHop = 0;
                analyse();
                onFrame (frame);
            }
        }
    }

    double frameSeconds() const noexcept { return (double) hop * decim / sampleRate; }
    double getSampleRate() const noexcept { return sampleRate; }

private:
    static constexpr int kSize = 4096, kHop = 512;

    /** Semitone amplitudes from an FFT over the newest samples of the ring. */
    struct Spectrum
    {
        int size = kSize, lowNote = 28, highNote = 96;
        float centreFraction = 0.5f;   // where in the window the frame's moment is
        std::vector<float> window, amp, prevAmp;
        std::vector<std::complex<float>> buf;
        std::unique_ptr<FFT> fft;
        struct BinWeight { int bin; float w; };
        std::vector<BinWeight> bins;
        std::vector<int> start;

        void prepare (int order, double rate, int low, int high, float peakAt);
        void compute (const std::vector<float>& ring, int ringPos);
        float at (int midi) const noexcept { return midi >= lowNote && midi <= highNote ? amp[(size_t) (midi - lowNote)] : 0.0f; }
    };
    void analyse();

    double sampleRate = 48000.0, rate = 12000.0;
    int decim = 4, decimCount = 0, ringPos = 0, sinceHop = 0, hop = kHop;
    bool live = false;
    int64_t pushed = 0;
    Biquad aa1, aa2;
    std::vector<float> ring;
    Spectrum longSpec, shortSpec;
    std::array<std::array<float, 12>, kBandChords> templates {};
    BandFrame frame;
};

/** Live chord recognition: decisions are smoothed and held, so the choir doesn't flicker. */
class ChordDetector
{
public:
    void prepare (double sampleRate);
    void reset();
    /** 0 = hold chords firmly (slow), 1 = follow every change quickly. */
    void setSpeed (float speed01) noexcept { speed = clampv (speed01, 0.0f, 1.0f); }
    void push (const float* x, int n);

    /** The chord the band plays now, or nullptr while it is quiet or unclear. */
    const Chord* chord() const noexcept { return current >= 0 ? &bandChordTable()[(size_t) current] : nullptr; }
    int chordIndex() const noexcept { return current; }
    /** The band's bass note (pitch class), -1 if none is clear. */
    int bass() const noexcept { return current >= 0 ? currentBass : -1; }
    /** True while the side-chain carries audio. */
    bool hearing() const noexcept { return quietTime < 0.3f; }

private:
    void onFrame (const BandFrame& f);

    BandAnalyzer analyzer;
    std::array<float, kBandChords> smooth {};
    std::array<float, 12> bassSmooth {};
    float speed = 0.5f, quietTime = 10.0f, leadTime = 0.0f;
    int current = -1, currentBass = -1, framesSinceStart = 0, leader = -1;
};

/** A chord the band plays over a stretch of a recording (LEARN). */
struct BandChord
{
    int64_t start = 0, end = 0;   // samples
    int index = -1;               // into bandChordTable()
    int bass = -1;                // pitch class, -1 if unclear
    const Chord& chord() const { return bandChordTable()[(size_t) index]; }
};

/** Offline recognition over a whole recording, with a Viterbi path so chords change only where
    the band really changes (speed 0..1 as for the live detector). */
std::vector<BandChord> analyseBandChords (const float* x, int64_t n, double sampleRate, float speed = 0.5f,
                                          const std::atomic<bool>* cancel = nullptr);
} // namespace sc
