#include "sanctuary/Passes.h"
#include "sanctuary/Common.h"
#include "sanctuary/FFT.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>

namespace sc
{
namespace
{
    constexpr int kBands = 24;
    constexpr float kSilentDb = 35.0f;   // this far below the loud parts counts as not singing

    bool cancelled (const std::atomic<bool>* c) { return c != nullptr && c->load (std::memory_order_relaxed); }

    /** The sound of a voice every hop: its spectral shape (24 bands, dB against the frame's
        level) and its level against its own loud parts. */
    struct Features
    {
        int frames = 0;
        std::vector<float> shape, level;
        bool ok = false;

        bool voiced (int i) const { return level[(size_t) i] > -kSilentDb; }
    };

    Features analyse (const float* x, int64_t n, double sr, int hop, const std::atomic<bool>* cancel)
    {
        Features f;
        const int order = sr > 60000.0 ? 12 : 11;
        const int N = 1 << order;
        FFT fft (order);
        std::vector<float> win ((size_t) N);
        for (int i = 0; i < N; ++i)
            win[(size_t) i] = 0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) N);
        std::array<int, kBands + 1> edge {};
        for (int b = 0; b <= kBands; ++b)
        {
            const double hz = 100.0 * std::pow (7000.0 / 100.0, (double) b / kBands);
            edge[(size_t) b] = std::clamp ((int) (hz * N / sr), 1, N / 2);
            if (b > 0)
                edge[(size_t) b] = std::max (edge[(size_t) b], edge[(size_t) b - 1] + 1);
        }

        f.frames = (int) (n / hop) + 1;
        f.shape.assign ((size_t) f.frames * kBands, 0.0f);
        f.level.assign ((size_t) f.frames, -120.0f);
        std::vector<std::complex<float>> buf ((size_t) N);
        for (int k = 0; k < f.frames; ++k)
        {
            if ((k & 255) == 0 && cancelled (cancel))
                return {};
            const int64_t start = (int64_t) k * hop - N / 2;
            for (int i = 0; i < N; ++i)
            {
                const int64_t idx = start + i;
                buf[(size_t) i] = idx >= 0 && idx < n ? x[idx] * win[(size_t) i] : 0.0f;
            }
            fft.forward (buf.data());
            double total = 0.0;
            float bandDb[kBands];
            for (int b = 0; b < kBands; ++b)
            {
                double e = 0.0;
                for (int j = edge[(size_t) b]; j < edge[(size_t) b + 1]; ++j)
                    e += (double) std::norm (buf[(size_t) j]);
                total += e;
                bandDb[b] = 10.0f * std::log10 ((float) e + 1.0e-12f);
            }
            const float lvl = 10.0f * std::log10 ((float) total + 1.0e-12f);
            f.level[(size_t) k] = lvl;
            for (int b = 0; b < kBands; ++b)
                f.shape[(size_t) k * kBands + (size_t) b] = bandDb[b] - lvl;
        }
        // Levels against the loud parts (95th percentile), so two passes sung at different
        // levels or distances from the microphone compare.
        auto sorted = f.level;
        std::sort (sorted.begin(), sorted.end());
        const float ref = sorted[(size_t) ((double) (sorted.size() - 1) * 0.95)];
        for (auto& l : f.level)
            l -= ref;
        f.ok = true;
        return f;
    }

    /** How different the take sounds at frame i from the pass at frame j. */
    float cost (const Features& a, int i, const Features& b, int j)
    {
        if (j < 0 || j >= b.frames)
            return 2.0f;   // outside the pass
        const float la = a.level[(size_t) i], lb = b.level[(size_t) j];
        const bool va = la > -kSilentDb, vb = lb > -kSilentDb;
        if (! va && ! vb)
            return 0.05f;
        if (va != vb)
            return 1.0f + 0.02f * std::abs (la - lb);   // one sings, the other does not
        const float* sa = &a.shape[(size_t) i * kBands];
        const float* sb = &b.shape[(size_t) j * kBands];
        float d = 0.0f;
        for (int k = 0; k < kBands; ++k)
            d += std::abs (sa[k] - sb[k]);
        return d / (float) kBands / 6.0f + std::abs (la - lb) / 20.0f;
    }

    /** Where the pass starts against the take (frames), from when each one sings. */
    int findOffset (const Features& a, const Features& b, int maxLag)
    {
        auto activity = [] (const Features& f)
        {
            std::vector<float> v ((size_t) f.frames);
            double mean = 0.0;
            for (int i = 0; i < f.frames; ++i) { v[(size_t) i] = std::clamp ((f.level[(size_t) i] + kSilentDb) / kSilentDb, 0.0f, 1.0f); mean += v[(size_t) i]; }
            mean /= std::max (1, f.frames);
            for (auto& x : v) x -= (float) mean;
            return v;
        };
        const auto va = activity (a), vb = activity (b);
        int best = 0;
        double bestScore = -std::numeric_limits<double>::infinity();
        for (int lag = -maxLag; lag <= maxLag; ++lag)
        {
            double s = 0.0;
            const int i0 = std::max (0, -lag), i1 = std::min (a.frames, b.frames - lag);
            for (int i = i0; i < i1; ++i)
                s += (double) va[(size_t) i] * vb[(size_t) (i + lag)];
            if (s > bestScore) { bestScore = s; best = lag; }
        }
        return best;
    }
}

std::vector<float> alignPass (const float* take, int64_t n, const float* pass, int64_t m, double sr,
                              const int64_t* hint, PassAlignment* info, const std::atomic<bool>* cancel)
{
    if (n <= 0 || m <= 0 || sr <= 0.0)
        return std::vector<float> ((size_t) std::max<int64_t> (0, n), 0.0f);
    const int hop = std::max (16, (int) std::lround (0.01 * sr));
    const Features a = analyse (take, n, sr, hop, cancel);
    const Features b = analyse (pass, m, sr, hop, cancel);
    if (! a.ok || ! b.ok)
        return {};

    // 1. Where the pass starts: known from the song position, or searched for.
    const int off = hint != nullptr ? (int) std::lround ((double) *hint / hop)
                                    : findOffset (a, b, std::min (std::max (a.frames, b.frames), (int) (30.0 * sr / hop)));
    if (cancelled (cancel))
        return {};

    // 2. Note by note: the cheapest path through a band around that start (dynamic time
    //    warping; a stall costs a little, so the path only waits where the singer did).
    const int B = (int) ((hint != nullptr ? 0.6 : 1.0) * sr / hop);
    const int W = 2 * B + 1;
    const int I = a.frames;
    constexpr float kInf = 1.0e30f, kStall = 0.15f;
    std::vector<float> prev ((size_t) W, kInf), cur ((size_t) W, kInf);
    std::vector<uint8_t> from ((size_t) I * (size_t) W, 0);   // 0 diagonal, 1 the take waits, 2 the pass waits
    for (int i = 0; i < I; ++i)
    {
        if ((i & 1023) == 0 && cancelled (cancel))
            return {};
        uint8_t* fr = &from[(size_t) i * (size_t) W];
        for (int k = 0; k < W; ++k)
        {
            const int j = i + off - B + k;
            const float c = cost (a, i, b, j);
            if (i == 0)
            {
                cur[(size_t) k] = c;   // the pass may start anywhere in the band
                fr[k] = 0;
                continue;
            }
            float best = prev[(size_t) k] + c;
            uint8_t f = 0;
            if (k + 1 < W && prev[(size_t) k + 1] + c + kStall < best) { best = prev[(size_t) k + 1] + c + kStall; f = 1; }
            if (k > 0 && cur[(size_t) k - 1] + c + kStall < best) { best = cur[(size_t) k - 1] + c + kStall; f = 2; }
            cur[(size_t) k] = best;
            fr[k] = f;
        }
        std::swap (prev, cur);
    }
    // Back along the path from the cheapest end: the pass frame for every take frame.
    std::vector<double> matched ((size_t) I, 0.0);
    {
        int k = (int) (std::min_element (prev.begin(), prev.end()) - prev.begin());
        int i = I - 1;
        double sum = 0.0;
        int count = 0;
        while (i >= 0)
        {
            sum += i + off - B + k;
            ++count;
            const uint8_t f = from[(size_t) i * (size_t) W + (size_t) k];
            if (f == 2 && k > 0)
            {
                --k;   // same take frame, earlier pass frame
                continue;
            }
            matched[(size_t) i] = sum / count;
            sum = 0.0;
            count = 0;
            if (f == 1) ++k;
            --i;
        }
    }
    // Smooth the path, keep it moving forward.
    {
        std::vector<double> s ((size_t) I);
        for (int i = 0; i < I; ++i)
        {
            double sum = 0.0;
            int cnt = 0;
            for (int d = -4; d <= 4; ++d)
                if (i + d >= 0 && i + d < I) { sum += matched[(size_t) (i + d)]; ++cnt; }
            s[(size_t) i] = sum / cnt;
        }
        for (int i = 1; i < I; ++i)
            s[(size_t) i] = std::max (s[(size_t) i], s[(size_t) i - 1]);
        matched = std::move (s);
    }
    auto passPos = [&] (double t)   // take sample -> pass sample
    {
        const double u = t / hop;
        const int i0 = std::clamp ((int) std::floor (u), 0, I - 1);
        const int i1 = std::min (i0 + 1, I - 1);
        const double fr = std::clamp (u - i0, 0.0, 1.0);
        return (matched[(size_t) i0] * (1.0 - fr) + matched[(size_t) i1] * fr) * hop;
    };

    // 3. Time-stretch the pass onto the take (WSOLA): overlapping windows read where the path
    //    says, each nudged by up to a few milliseconds to continue the waveform of the last.
    std::vector<float> out ((size_t) n, 0.0f);
    const int Hs = hop, N = 2 * hop;
    std::vector<float> win ((size_t) N);
    for (int i = 0; i < N; ++i)
        win[(size_t) i] = 0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) N);
    auto at = [&] (int64_t i) { return i >= 0 && i < m ? pass[i] : 0.0f; };
    const int maxShift = (int) (0.006 * sr);
    int64_t follow = -1;   // where the last window's waveform carries on
    for (int64_t t0 = -Hs; t0 < n; t0 += Hs)
    {
        if (((t0 / Hs) & 4095) == 0 && cancelled (cancel))
            return {};
        int64_t s = (int64_t) std::llround (passPos ((double) (t0 + Hs))) - Hs;
        if (follow >= 0)
        {
            // Coarse search on every fourth sample, then the exact best nearby.
            auto corr = [&] (int64_t cand, int step)
            {
                double c = 0.0;
                for (int i = 0; i < N; i += step)
                    c += (double) at (cand + i) * at (follow + i);
                return c;
            };
            int64_t best = s;
            double bestC = -std::numeric_limits<double>::infinity();
            for (int d = -maxShift; d <= maxShift; d += 4)
                if (const double c = corr (s + d, 4); c > bestC) { bestC = c; best = s + d; }
            const int64_t coarse = best;
            bestC = -std::numeric_limits<double>::infinity();
            for (int d = -3; d <= 3; ++d)
                if (const double c = corr (coarse + d, 1); c > bestC) { bestC = c; best = coarse + d; }
            s = best;
        }
        for (int i = 0; i < N; ++i)
        {
            const int64_t o = t0 + i;
            if (o >= 0 && o < n)
                out[(size_t) o] += at (s + i) * win[(size_t) i];
        }
        follow = s + Hs;
    }

    // 4. Level: the pass as loud as the take where both sing.
    double ea = 0.0, eb = 0.0;
    int both = 0, takeSings = 0;
    for (int i = 0; i < I; ++i)
    {
        if (! a.voiced (i))
            continue;
        ++takeSings;
        const int j = (int) std::lround (matched[(size_t) i]);
        if (j < 0 || j >= b.frames || ! b.voiced (j))
            continue;
        ++both;
        const int64_t c0 = (int64_t) i * hop, c1 = std::min<int64_t> (n, c0 + hop);
        for (int64_t k = c0; k < c1; ++k)
        {
            ea += (double) take[k] * take[k];
            eb += (double) out[(size_t) k] * out[(size_t) k];
        }
    }
    float gainDb = eb > 1.0e-12 && ea > 1.0e-12 ? (float) (10.0 * std::log10 (ea / eb)) : 0.0f;
    gainDb = std::clamp (gainDb, -12.0f, 12.0f);
    const float g = dbToGain (gainDb);
    for (auto& v : out)
        v *= g;

    if (info != nullptr)
    {
        info->offset = (int64_t) off * hop;
        info->coverage = takeSings > 0 ? (float) both / (float) takeSings : 0.0f;
        double drift = 0.0;
        for (int i = 0; i < I; ++i)
            drift = std::max (drift, std::abs (matched[(size_t) i] - (double) (i + off)));
        info->drift = (float) (drift * hop / sr);
        info->gainDb = gainDb;
    }
    return out;
}
} // namespace sc
