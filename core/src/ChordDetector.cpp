#include "sanctuary/ChordDetector.h"

#include <algorithm>
#include <limits>

namespace sc
{
namespace
{
    constexpr float kQuietLevel = 0.0025f;   // ~ -52 dBFS RMS: below this the band is silent
}

const std::array<Chord, kBandChords>& bandChordTable()
{
    static const std::array<Chord, kBandChords> table = []
    {
        std::array<Chord, kBandChords> t;
        for (int r = 0; r < 12; ++r)
        {
            const bool flats = r == 1 || r == 3 || r == 8 || r == 10;   // Db, Eb, Ab, Bb
            for (int q = 0; q < kBandChordTypes; ++q)
                t[(size_t) (r * kBandChordTypes + q)] = makeChordOfQuality (r, (ChordQuality) q, flats);
        }
        return t;
    }();
    return table;
}

float bandBassFit (int chordIndex, int bassPc) noexcept
{
    if (bassPc < 0 || chordIndex < 0)
        return 0.0f;
    const Chord& ch = bandChordTable()[(size_t) chordIndex];
    return bassPc == ch.root ? 0.04f : (ch.contains (bassPc) ? 0.01f : -0.02f);
}

//==============================================================================
void BandAnalyzer::Spectrum::prepare (int order, double rate, int low, int high, float peakAt)
{
    size = 1 << order;
    lowNote = low;
    highNote = high;
    // Rises to its peak, then falls: peakAt 0.5 is a Hann window; later peaks favour the
    // newest audio.
    window.resize ((size_t) size);
    float weightSum = 0.0f, weightedPos = 0.0f;
    for (int i = 0; i < size; ++i)
    {
        const float x = (float) i / (float) size;
        const float w = x < peakAt ? 0.5f - 0.5f * std::cos (kPi * x / peakAt)
                                   : 0.5f + 0.5f * std::cos (kPi * (x - peakAt) / (1.0f - peakAt));
        window[(size_t) i] = w;
        weightSum += w;
        weightedPos += w * x;
    }
    centreFraction = weightedPos / weightSum;
    buf.assign ((size_t) size, {});
    fft = std::make_unique<FFT> (order);
    amp.assign ((size_t) (high - low + 1), 0.0f);
    prevAmp = amp;

    // The FFT bins of every semitone: triangular weights over half a semitone each side, or the
    // two nearest bins where a semitone is narrower than a bin (the lowest notes).
    bins.clear();
    start.assign ((size_t) (high - low + 2), 0);
    for (int m = low; m <= high; ++m)
    {
        start[(size_t) (m - low)] = (int) bins.size();
        const double f = 440.0 * std::pow (2.0, (m - 69) / 12.0);
        const double kc = f * size / rate;
        const double half = kc * (std::pow (2.0, 0.5 / 12.0) - 1.0);
        if (half < 1.0)
        {
            const int k0 = (int) std::floor (kc);
            const float fr = (float) (kc - k0);
            bins.push_back ({ k0, 1.0f - fr });
            bins.push_back ({ k0 + 1, fr });
        }
        else
        {
            for (int k = (int) std::ceil (kc - half); k <= (int) std::floor (kc + half); ++k)
                bins.push_back ({ k, (float) (1.0 - std::abs (k - kc) / half) });
        }
    }
    start[(size_t) (high - low + 1)] = (int) bins.size();
}

void BandAnalyzer::Spectrum::compute (const std::vector<float>& ring, int ringPos)
{
    const int ringSize = (int) ring.size();
    const int first = ringPos - size;   // the newest `size` samples
    for (int i = 0; i < size; ++i)
        buf[(size_t) i] = { ring[(size_t) ((first + i + ringSize) & (ringSize - 1))] * window[(size_t) i], 0.0f };
    fft->forward (buf.data());
    for (size_t m = 0; m < amp.size(); ++m)
    {
        float e = 0.0f;
        for (int j = start[m]; j < start[m + 1]; ++j)
            e += bins[(size_t) j].w * std::norm (buf[(size_t) bins[(size_t) j].bin]);
        prevAmp[m] = amp[m];
        amp[m] = std::sqrt (e);
    }
}

void BandAnalyzer::prepare (double sr, bool liveIn)
{
    sampleRate = sr;
    live = liveIn;
    hop = live ? kHop / 2 : kHop;
    decim = std::max (1, (int) std::lround (sr / 11025.0));
    rate = sr / decim;
    // Fourth-order Butterworth below the decimated Nyquist.
    aa1.lowPass (sr, 0.42 * rate, 0.5412);
    aa2.lowPass (sr, 0.42 * rate, 1.3066);
    ring.assign (kSize, 0.0f);
    // The bass needs a long window (low notes are only a few Hz apart); the chord notes are
    // higher, so live they come from a window half as long that leans on the newest audio.
    longSpec.prepare (12, rate, 28, 96, live ? 0.8f : 0.5f);
    shortSpec.prepare (11, rate, 45, 96, 0.8f);

    // Chord templates: every chord note with its first six overtones (octave, fifth, octave,
    // third, fifth), so a real instrument's spectrum matches its chord.
    static const int harmonicPc[] = { 0, 0, 7, 0, 4, 7 };
    const auto& chords = bandChordTable();
    for (int c = 0; c < kBandChords; ++c)
    {
        auto& t = templates[(size_t) c];
        t.fill (0.0f);
        const Chord& ch = chords[(size_t) c];
        for (int i = 0; i < ch.numTones; ++i)
        {
            float a = 1.0f;
            for (int h = 0; h < 6; ++h, a *= 0.6f)
                t[(size_t) wrapPc (ch.tones[(size_t) i] + harmonicPc[h])] += a;
        }
        float norm = 0.0f;
        for (float v : t) norm += v * v;
        norm = std::sqrt (norm);
        for (auto& v : t) v /= norm;
    }
    reset();
}

void BandAnalyzer::reset()
{
    aa1.reset();
    aa2.reset();
    std::fill (ring.begin(), ring.end(), 0.0f);
    ringPos = decimCount = sinceHop = 0;
    pushed = 0;
    for (auto* sp : { &longSpec, &shortSpec })
    {
        std::fill (sp->amp.begin(), sp->amp.end(), 0.0f);
        std::fill (sp->prevAmp.begin(), sp->prevAmp.end(), 0.0f);
    }
    frame = BandFrame {};
}

void BandAnalyzer::analyse()
{
    longSpec.compute (ring, ringPos);
    if (live)
        shortSpec.compute (ring, ringPos);
    const Spectrum& notes = live ? shortSpec : longSpec;

    double energy = 0.0;
    for (int i = 0; i < notes.size; ++i)
    {
        const float v = ring[(size_t) ((ringPos - notes.size + i + kSize) & (kSize - 1))];
        energy += (double) v * v;
    }
    frame.level = (float) std::sqrt (energy / notes.size);
    frame.centre = pushed - (int64_t) ((1.0f - notes.centreFraction) * (float) notes.size) * decim;

    // Chroma from E2 (or A2 in the short window) up, log-compressed so one loud note doesn't
    // hide the rest of the chord; and how much the notes rose since the last frame (onsets).
    float flux = 0.0f, total = 1.0e-9f;
    for (size_t m = 0; m < notes.amp.size(); ++m)
    {
        flux += std::max (0.0f, notes.amp[m] - notes.prevAmp[m]);
        total += notes.amp[m];
    }
    frame.flux = flux / total;
    auto chromaOf = [] (const Spectrum& sp)
    {
        std::array<float, 12> c {};
        float pk = 1.0e-9f;
        for (float a : sp.amp) pk = std::max (pk, a);
        for (int m = std::max (40, sp.lowNote); m <= sp.highNote; ++m)
            c[(size_t) (m % 12)] += std::max (0.0f, std::log1p (20.0f * sp.at (m) / pk) - 0.15f);
        float nrm = 0.0f;
        for (float v : c) nrm += v * v;
        nrm = std::sqrt (nrm);
        if (nrm > 0.0f)
            for (auto& v : c) v /= nrm;
        return c;
    };
    // Live, the long window (whole chord, bass included) and the short one (quick to change)
    // vote together.
    std::array<float, 12> chroma = chromaOf (longSpec);
    if (live)
    {
        const auto quick = chromaOf (shortSpec);
        float nrm = 0.0f;
        for (int k = 0; k < 12; ++k)
        {
            chroma[(size_t) k] = 0.5f * (chroma[(size_t) k] + quick[(size_t) k]);
            nrm += chroma[(size_t) k] * chroma[(size_t) k];
        }
        nrm = std::sqrt (nrm);
        if (nrm > 0.0f)
            for (auto& v : chroma) v /= nrm;
    }

    // Bass: the strongest pitch class between E1 and E3, weighted towards the lowest notes.
    std::array<float, 12> bassE {};
    float bassTotal = 1.0e-9f, bassPeak = 0.0f, longPeak = 1.0e-9f;
    for (float a : longSpec.amp) longPeak = std::max (longPeak, a);
    for (int m = 28; m <= 52; ++m)
    {
        const float a = longSpec.at (m) * (1.0f - 0.025f * (float) (m - 28));
        bassE[(size_t) (m % 12)] += a;
        bassTotal += a;
        bassPeak = std::max (bassPeak, longSpec.at (m));
    }
    const int bassPc = (int) (std::max_element (bassE.begin(), bassE.end()) - bassE.begin());
    frame.bass = (bassPeak > 0.2f * longPeak && bassE[(size_t) bassPc] > 0.3f * bassTotal) ? bassPc : -1;

    const auto& chords = bandChordTable();
    for (int c = 0; c < kBandChords; ++c)
    {
        const auto& t = templates[(size_t) c];
        float s = 0.0f;
        for (int k = 0; k < 12; ++k)
            s += chroma[(size_t) k] * t[(size_t) k];
        if (chords[(size_t) c].numTones > 3)
            s -= 0.035f;   // a plain triad is the simpler answer unless the seventh is clearly there
        frame.score[(size_t) c] = s;
    }
}

//==============================================================================
void ChordDetector::prepare (double sampleRate)
{
    analyzer.prepare (sampleRate, true);
    reset();
}

void ChordDetector::reset()
{
    analyzer.reset();
    smooth.fill (0.0f);
    bassSmooth.fill (0.0f);
    quietTime = 10.0f;
    current = currentBass = leader = -1;
    leadTime = 0.0f;
    framesSinceStart = 0;
}

void ChordDetector::push (const float* x, int n)
{
    analyzer.push (x, n, [this] (const BandFrame& f) { onFrame (f); });
}

void ChordDetector::onFrame (const BandFrame& f)
{
    const float dt = (float) analyzer.frameSeconds();
    if (f.level < kQuietLevel)
    {
        quietTime += dt;
        if (quietTime > 0.4f)
        {
            current = currentBass = -1;
            framesSinceStart = 0;
        }
        return;
    }
    quietTime = 0.0f;

    // Smoothing: slow settings hold chords through busy playing, fast ones follow every change.
    // A fresh attack (a chord struck) is listened to straight away.
    const float tau = 0.3f - 0.22f * speed;
    float k = 1.0f - std::exp (-dt / tau);
    if (f.flux > 0.25f) k = std::max (k, 0.5f);
    if (framesSinceStart == 0) k = 1.0f;
    // The bass is smoothed more slowly: a walking bass passes through notes outside the chord.
    const float kb = 1.0f - std::exp (-dt / (tau * 1.5f));
    for (int pc = 0; pc < 12; ++pc)
        bassSmooth[(size_t) pc] += ((pc == f.bass ? 1.0f : 0.0f) - bassSmooth[(size_t) pc]) * (framesSinceStart == 0 ? 1.0f : kb);
    const int b = (int) (std::max_element (bassSmooth.begin(), bassSmooth.end()) - bassSmooth.begin());
    currentBass = bassSmooth[(size_t) b] > 0.5f ? b : -1;

    float mean = 0.0f;
    for (int c = 0; c < kBandChords; ++c)
    {
        smooth[(size_t) c] += (f.score[(size_t) c] + bandBassFit (c, currentBass) - smooth[(size_t) c]) * k;
        mean += smooth[(size_t) c];
    }
    mean /= (float) kBandChords;
    ++framesSinceStart;
    if (framesSinceStart < 2)
        return;

    const int best = (int) (std::max_element (smooth.begin(), smooth.end()) - smooth.begin());
    // Only a clear chord counts (drums or noise score every chord about the same), and a new
    // chord has to stay ahead for a moment: passing notes in a busy part don't change it.
    const bool clear = smooth[(size_t) best] - mean > 0.06f && smooth[(size_t) best] > 0.5f;
    const float margin = 0.035f - 0.02f * speed;
    const float dwell = 0.2f - 0.14f * speed;
    if (! clear || best == current)
    {
        leader = -1;
        leadTime = 0.0f;
        return;
    }
    if (current >= 0 && smooth[(size_t) best] <= smooth[(size_t) current] + margin)
    {
        leader = -1;
        leadTime = 0.0f;
        return;
    }
    if (best != leader)
    {
        leader = best;
        leadTime = 0.0f;
    }
    leadTime += dt;
    if (current < 0 || leadTime >= dwell)
    {
        current = best;
        leader = -1;
        leadTime = 0.0f;
    }
}

//==============================================================================
std::vector<BandChord> analyseBandChords (const float* x, int64_t n, double sampleRate, float speed,
                                          const std::atomic<bool>* cancel)
{
    std::vector<BandChord> out;
    if (x == nullptr || n <= 0 || sampleRate <= 0.0)
        return out;

    BandAnalyzer an;
    an.prepare (sampleRate);
    std::vector<BandFrame> frames;
    frames.reserve ((size_t) (n / (int64_t) std::max (1.0, an.frameSeconds() * sampleRate)) + 16);
    auto collect = [&frames] (const BandFrame& f) { frames.push_back (f); };
    constexpr int block = 16384;
    for (int64_t pos = 0; pos < n; pos += block)
    {
        if (cancel != nullptr && cancel->load())
            return {};
        an.push (x + pos, (int) std::min<int64_t> (block, n - pos), collect);
    }
    // Flush with silence so the last frames are centred over the end of the recording.
    std::vector<float> zeros ((size_t) (sampleRate * 0.25), 0.0f);
    an.push (zeros.data(), (int) zeros.size(), collect);
    if (frames.empty())
        return out;

    // Viterbi: states are the chords plus "nothing" (the band is silent). Staying costs nothing,
    // changing chord costs a fixed price, so a change needs a run of frames that agree.
    const int F = (int) frames.size();
    constexpr int S = kBandChords + 1, none = kBandChords;
    const float hop = (float) an.frameSeconds();
    const float switchCost = (0.9f - 0.6f * clampv (speed, 0.0f, 1.0f)) * (0.046f / hop);
    auto emission = [&frames] (int f, int s)
    {
        const BandFrame& fr = frames[(size_t) f];
        const bool quiet = fr.level < kQuietLevel;
        if (s == none)
            return quiet ? 0.0f : 0.9f;
        return quiet ? 1.0f : 1.0f - fr.score[(size_t) s] - bandBassFit (s, fr.bass);
    };
    std::vector<float> cost (S), next (S);
    std::vector<uint8_t> back ((size_t) F * S);
    for (int s = 0; s < S; ++s)
        cost[(size_t) s] = emission (0, s);
    for (int f = 1; f < F; ++f)
    {
        const int bestPrev = (int) (std::min_element (cost.begin(), cost.end()) - cost.begin());
        for (int s = 0; s < S; ++s)
        {
            const float stay = cost[(size_t) s];
            const float change = cost[(size_t) bestPrev] + switchCost;
            const bool keep = stay <= change;
            next[(size_t) s] = (keep ? stay : change) + emission (f, s);
            back[(size_t) f * S + (size_t) s] = (uint8_t) (keep ? s : bestPrev);
        }
        std::swap (cost, next);
    }
    std::vector<int> state ((size_t) F);
    int s = (int) (std::min_element (cost.begin(), cost.end()) - cost.begin());
    for (int f = F - 1; f >= 0; --f)
    {
        state[(size_t) f] = s;
        if (f > 0)
            s = back[(size_t) f * S + (size_t) s];
    }

    // Runs of one chord become the chord lane; the bass is the note most of the run agrees on.
    const int64_t halfHop = (int64_t) (hop * sampleRate * 0.5);
    for (int f = 0; f < F;)
    {
        int g = f;
        while (g < F && state[(size_t) g] == state[(size_t) f])
            ++g;
        if (state[(size_t) f] != none)
        {
            BandChord bc;
            bc.index = state[(size_t) f];
            bc.start = std::max<int64_t> (0, frames[(size_t) f].centre - halfHop);
            bc.end = std::min<int64_t> (n, g < F ? frames[(size_t) g].centre - halfHop : n);
            std::array<int, 12> votes {};
            for (int k = f; k < g; ++k)
                if (frames[(size_t) k].bass >= 0)
                    ++votes[(size_t) frames[(size_t) k].bass];
            const int b = (int) (std::max_element (votes.begin(), votes.end()) - votes.begin());
            bc.bass = votes[(size_t) b] >= 3 && votes[(size_t) b] * 10 >= (g - f) * 4 ? b : -1;
            if (bc.end > bc.start)
                out.push_back (bc);
        }
        f = g;
    }
    return out;
}
} // namespace sc
