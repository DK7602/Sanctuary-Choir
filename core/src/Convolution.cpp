#include "sanctuary/Convolution.h"

#include <complex>

namespace sc
{
namespace
{
    int nextPow2 (int v)
    {
        int p = 1;
        while (p < v)
            p <<= 1;
        return p;
    }

    int orderOf (int size)
    {
        int o = 0;
        while ((1 << o) < size)
            ++o;
        return o;
    }

    double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 64; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
            if (term < sum * 1.0e-12)
                break;
        }
        return sum;
    }

    constexpr double kEarlySeconds = 0.085;   // what the Space knob's "early" level covers
}

bool normaliseRoom (RoomImpulse& ir)
{
    for (auto* ch : { &ir.left, &ir.right })
    {
        double energy = 0.0;
        for (float v : *ch)
            energy += (double) v * v;
        if (energy <= 0.0)
            return false;
        const float scale = (float) (1.0 / std::sqrt (energy));
        for (auto& v : *ch)
            v *= scale;
    }
    return true;
}

std::vector<float> resampleImpulse (const std::vector<float>& x, double fromRate, double toRate)
{
    if (x.empty() || fromRate <= 0.0 || toRate <= 0.0)
        return {};
    if (std::abs (fromRate - toRate) < 1.0e-6)
        return x;
    const double ratio = toRate / fromRate;
    const double fc = std::min (1.0, ratio) * 0.97;   // cutoff, as a fraction of the input's Nyquist
    const int half = (int) std::ceil (24.0 / fc);
    const int taps = 2 * half;

    // Kaiser-windowed sinc at `phases` fractional positions (and one more, to interpolate).
    constexpr int phases = 512;
    const double beta = 8.0, norm = besselI0 (beta);
    std::vector<float> table ((size_t) (phases + 1) * (size_t) taps);
    for (int ph = 0; ph <= phases; ++ph)
    {
        const double frac = (double) ph / phases;
        for (int k = 0; k < taps; ++k)
        {
            const double d = frac + (double) (half - 1 - k);   // distance from tap k to the output point
            const double u = d / (double) half;
            double v = 0.0;
            if (std::abs (u) < 1.0)
            {
                const double arg = kPiD * fc * d;
                const double sinc = std::abs (arg) < 1.0e-9 ? 1.0 : std::sin (arg) / arg;
                v = fc * sinc * besselI0 (beta * std::sqrt (1.0 - u * u)) / norm;
            }
            table[(size_t) ph * (size_t) taps + (size_t) k] = (float) v;
        }
    }

    const auto n = (int64_t) x.size();
    const auto outN = (int64_t) std::ceil ((double) n * ratio);
    std::vector<float> y ((size_t) outN, 0.0f);
    for (int64_t j = 0; j < outN; ++j)
    {
        const double p = (double) j / ratio;
        const auto base = (int64_t) std::floor (p);
        const double fp = (p - (double) base) * phases;
        const int ph = std::min (phases - 1, (int) fp);
        const float w = (float) (fp - ph);
        const float* t0 = table.data() + (size_t) ph * (size_t) taps;
        const float* t1 = t0 + taps;
        const int64_t first = base - half + 1;
        float acc = 0.0f;
        for (int k = 0; k < taps; ++k)
        {
            const int64_t i = first + k;
            if (i < 0 || i >= n)
                continue;
            acc += x[(size_t) i] * (t0[k] + (t1[k] - t0[k]) * w);
        }
        y[(size_t) j] = acc;
    }
    return y;
}

float measureDecayTime (const float* x, int64_t n, double sampleRate)
{
    if (x == nullptr || n < 16)
        return 1.0f;
    std::array<Biquad, 4> f;
    f[0].highPass (sampleRate, 500.0);
    f[1].highPass (sampleRate, 500.0);
    f[2].lowPass (sampleRate, 2000.0);
    f[3].lowPass (sampleRate, 2000.0);
    std::vector<double> e ((size_t) n);
    for (int64_t i = 0; i < n; ++i)
    {
        float v = x[i];
        for (auto& b : f)
            v = b.process (v);
        e[(size_t) i] = (double) v * v;
    }
    for (int64_t i = n - 2; i >= 0; --i)
        e[(size_t) i] += e[(size_t) i + 1];
    if (e[0] <= 0.0)
        return 1.0f;
    auto crossing = [&] (double db)
    {
        const double level = e[0] * std::pow (10.0, db / 10.0);
        for (int64_t i = 0; i < n; ++i)
            if (e[(size_t) i] <= level)
                return (double) i;
        return -1.0;
    };
    const double a = crossing (-5.0), b = crossing (-35.0);
    if (a >= 0.0 && b > a)
        return (float) ((b - a) / sampleRate * 2.0);
    const double c = crossing (-25.0);
    if (a >= 0.0 && c > a)
        return (float) ((c - a) / sampleRate * 3.0);
    return (float) ((double) n / sampleRate);
}

//==============================================================================
std::unique_ptr<ConvolutionRoom> ConvolutionRoom::create (const RoomImpulse& ir, double sampleRate, double maxPreDelayMs)
{
    if (ir.left.empty() || sampleRate <= 0.0)
        return {};
    std::unique_ptr<ConvolutionRoom> r (new ConvolutionRoom());
    r->sr = sampleRate;

    std::array<std::vector<float>, 2> h { ir.left, ir.right.empty() ? ir.left : ir.right };
    if (std::abs (ir.sampleRate - sampleRate) > 0.5)
    {
        // At twice the rate a response has twice the samples, each as large: half the gain
        // keeps the room as loud.
        const float scale = (float) (ir.sampleRate / sampleRate);
        for (auto& c : h)
        {
            c = resampleImpulse (c, ir.sampleRate, sampleRate);
            for (auto& v : c)
                v *= scale;
        }
    }
    const size_t n = std::max (h[0].size(), h[1].size());
    for (auto& c : h)
        c.resize (n, 0.0f);
    r->length = (int64_t) n;
    {
        std::vector<float> both (n);
        for (size_t i = 0; i < n; ++i)
            both[i] = h[0][i] + h[1][i];
        r->rt60 = std::max (0.05f, measureDecayTime (both.data(), (int64_t) n, sampleRate));
    }

    // About 4 ms blocks for the first 85 ms (256 at 44.1 and 48 kHz), eight times that after.
    const int hb = nextPow2 ((int) std::lround (sampleRate * 0.004));
    r->headBlock = hb;
    auto setup = [&] (Stage& s, int block, int64_t offset, int maxParts)
    {
        s.block = block;
        s.size = 2 * block;
        s.bins = block + 1;
        s.offset = offset;
        const int64_t remaining = std::max<int64_t> (0, (int64_t) n - offset);
        s.parts = (int) std::min<int64_t> (maxParts, (remaining + block - 1) / block);
        s.fft = std::make_unique<FFT> (orderOf (s.size));
        s.work.assign ((size_t) s.size, {});
        s.energy.assign ((size_t) s.parts, 0.0f);
        s.gain.assign ((size_t) s.parts, 1.0f);
        for (int ch = 0; ch < 2; ++ch)
        {
            s.hRe[ch].assign ((size_t) s.parts * (size_t) s.bins, 0.0f);
            s.hIm[ch].assign ((size_t) s.parts * (size_t) s.bins, 0.0f);
            s.xRe[ch].assign ((size_t) s.parts * (size_t) s.bins, 0.0f);
            s.xIm[ch].assign ((size_t) s.parts * (size_t) s.bins, 0.0f);
            s.yRe[ch].assign ((size_t) s.bins, 0.0f);
            s.yIm[ch].assign ((size_t) s.bins, 0.0f);
        }
        // Each partition: block samples of the response, zero-padded, both channels in one
        // complex transform (left real, right imaginary), then pulled apart.
        for (int p = 0; p < s.parts; ++p)
        {
            double e = 0.0;
            for (int k = 0; k < s.size; ++k)
            {
                const int64_t t = offset + (int64_t) p * block + k;
                const bool in = k < block && t < (int64_t) n;
                s.work[(size_t) k] = { in ? h[0][(size_t) t] : 0.0f, in ? h[1][(size_t) t] : 0.0f };
                if (in)
                    e += (double) h[0][(size_t) t] * h[0][(size_t) t] + (double) h[1][(size_t) t] * h[1][(size_t) t];
            }
            s.energy[(size_t) p] = (float) (e / block);
            s.fft->forward (s.work.data());
            for (int m = 0; m < s.bins; ++m)
            {
                const auto z = s.work[(size_t) m];
                const auto zc = std::conj (s.work[(size_t) ((s.size - m) % s.size)]);
                const auto a = 0.5f * (z + zc);
                const auto d = z - zc;
                const size_t i = (size_t) p * (size_t) s.bins + (size_t) m;
                s.hRe[0][i] = a.real();
                s.hIm[0][i] = a.imag();
                s.hRe[1][i] = 0.5f * d.imag();
                s.hIm[1][i] = -0.5f * d.real();
            }
        }
    };
    setup (r->head, hb, 0, 16);
    if ((int64_t) n > 16 * (int64_t) hb)
        setup (r->tail, 8 * hb, 16 * (int64_t) hb, 1 << 20);
    r->tailUnits = r->tail.parts > 0 ? 2 * r->tail.fft->getOrder() + r->tail.parts + 4 : 0;

    // The room's decay as a straight line in dB (slope from rt60), placed by the partitions
    // from 50 ms on while they are within 40 dB of the loudest: the part that really decays.
    {
        float peak = 0.0f;
        for (const auto* s : { &r->head, &r->tail })
            for (float e : s->energy)
                peak = std::max (peak, e);
        double sum = 0.0;
        int count = 0;
        bool below = false;
        for (const auto* s : { &r->head, &r->tail })
            for (int p = 0; p < s->parts && ! below; ++p)
            {
                const double t = ((double) s->offset + ((double) p + 0.5) * s->block) / sampleRate;
                const float e = s->energy[(size_t) p];
                if (e < peak * 1.0e-4f)
                    below = true;
                else if (t >= 0.05 && e > 0.0f)
                {
                    sum += std::log10 ((double) e) + 6.0 * t / r->rt60;
                    ++count;
                }
            }
        r->fitted = count >= 2;
        r->fitLog10 = r->fitted ? (float) (sum / count) : 0.0f;
    }

    const int ring = nextPow2 (32 * hb);
    r->ringMask = ring - 1;
    for (auto* v : { &r->inA, &r->inB, &r->outL, &r->outR })
        v->assign ((size_t) ring, 0.0f);
    const int pre = nextPow2 ((int) std::ceil (sampleRate * maxPreDelayMs * 0.001) + 2);
    r->preMask = pre - 1;
    r->preL.assign ((size_t) pre, 0.0f);
    r->preR.assign ((size_t) pre, 0.0f);

    r->current.decayScale = -1.0f;   // forces the first update
    r->updateShape (RoomShape {});
    r->reset();
    return r;
}

ConvolutionRoom::~ConvolutionRoom() = default;

void ConvolutionRoom::reset() noexcept
{
    for (auto* v : { &inA, &inB, &outL, &outR, &preL, &preR })
        std::fill (v->begin(), v->end(), 0.0f);
    for (auto* s : { &head, &tail })
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill (s->xRe[ch].begin(), s->xRe[ch].end(), 0.0f);
            std::fill (s->xIm[ch].begin(), s->xIm[ch].end(), 0.0f);
            std::fill (s->yRe[ch].begin(), s->yRe[ch].end(), 0.0f);
            std::fill (s->yIm[ch].begin(), s->yIm[ch].end(), 0.0f);
        }
        s->newest = 0;
    }
    prePos = 0;
    now = 0;
    tailDone = 0;
    tailPeriodStart = -1;
    shelfL.reset();
    shelfR.reset();
}

void ConvolutionRoom::updateShape (const RoomShape& shape) noexcept
{
    const bool decayChanged = shape.decayScale != current.decayScale;
    if (decayChanged)
    {
        const float s = clampv (shape.decayScale, 0.3f, 2.5f);
        // exp(rate t) turns a decay of rt60 into one of s * rt60.
        decayRate = std::log (1000.0f) / rt60 * (1.0f - 1.0f / s);
        activeLength = std::min<int64_t> (length, std::max<int64_t> (16 * (int64_t) headBlock, (int64_t) (rt60 * s * 1.3f * (float) sr)));
    }
    const float early = 0.5f + 0.5f * clampv (shape.early, 0.0f, 1.0f);
    if (decayChanged || early != earlyGain)
    {
        earlyGain = early;
        updateGains (head);
        updateGains (tail);
    }
    preDelay = clampv ((int) std::lround (shape.preDelayMs * 0.001 * sr) - headBlock, 0, preMask - 1);
    const float db = (clampv (shape.brightness, 0.0f, 1.0f) - 0.5f) * 12.0f;
    if (db != shelfDb || decayChanged)
    {
        shelfDb = db;
        shelfL.highShelf (sr, 3000.0, db);
        shelfR.highShelf (sr, 3000.0, db);
    }
    current = shape;
}

float ConvolutionRoom::partGain (const Stage& s, int part) const noexcept
{
    const double secs = ((double) s.offset + ((double) part + 0.5) * s.block) / sr;
    float g = (float) std::exp ((double) decayRate * secs);
    if (decayRate > 0.0f && fitted)
    {
        // Lengthened: lift the partition no higher than the decay line, lengthened (but never
        // below where the room has it).
        const float e = s.energy[(size_t) part];
        const double line = std::pow (10.0, (double) fitLog10 - 6.0 * secs / rt60);
        if (e > 0.0f)
            g = clampv ((float) (g * std::sqrt (line / (double) e)), 1.0f, g);
    }
    if (secs < kEarlySeconds)
        g *= earlyGain + (1.0f - earlyGain) * (float) (secs / kEarlySeconds);
    return g;
}

void ConvolutionRoom::updateGains (Stage& s) noexcept
{
    for (int p = 0; p < s.parts; ++p)
        s.gain[(size_t) p] = partGain (s, p);
}

bool ConvolutionRoom::partActive (int64_t t) const noexcept
{
    return t < activeLength;
}

void ConvolutionRoom::gatherInput (Stage& s, int64_t end) noexcept
{
    // Both inputs in one complex transform: a real, b imaginary.
    const int64_t start = end - s.size;
    for (int k = 0; k < s.size; ++k)
    {
        const size_t i = (size_t) ((start + k) & ringMask);
        s.work[(size_t) k] = { inA[i], inB[i] };
    }
}

void ConvolutionRoom::storeInput (Stage& s) noexcept
{
    // Pull the two inputs' spectra apart into the ring of past blocks; a new sum starts.
    s.newest = (s.newest + 1) % s.parts;
    const size_t base = (size_t) s.newest * (size_t) s.bins;
    for (int m = 0; m < s.bins; ++m)
    {
        const auto z = s.work[(size_t) m];
        const auto zc = std::conj (s.work[(size_t) ((s.size - m) % s.size)]);
        const auto a = 0.5f * (z + zc);
        const auto d = z - zc;
        s.xRe[0][base + (size_t) m] = a.real();
        s.xIm[0][base + (size_t) m] = a.imag();
        s.xRe[1][base + (size_t) m] = 0.5f * d.imag();
        s.xIm[1][base + (size_t) m] = -0.5f * d.real();
    }
    for (int ch = 0; ch < 2; ++ch)
    {
        std::fill (s.yRe[ch].begin(), s.yRe[ch].end(), 0.0f);
        std::fill (s.yIm[ch].begin(), s.yIm[ch].end(), 0.0f);
    }
}

void ConvolutionRoom::accumulate (Stage& s, int part, float gain) noexcept
{
    const int slot = (s.newest - part + s.parts) % s.parts;
    const int bins = s.bins;
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* xr = s.xRe[ch].data() + (size_t) slot * (size_t) bins;
        const float* xi = s.xIm[ch].data() + (size_t) slot * (size_t) bins;
        const float* hr = s.hRe[ch].data() + (size_t) part * (size_t) bins;
        const float* hi = s.hIm[ch].data() + (size_t) part * (size_t) bins;
        float* yr = s.yRe[ch].data();
        float* yi = s.yIm[ch].data();
        for (int m = 0; m < bins; ++m)
        {
            yr[m] += gain * (xr[m] * hr[m] - xi[m] * hi[m]);
            yi[m] += gain * (xr[m] * hi[m] + xi[m] * hr[m]);
        }
    }
}

void ConvolutionRoom::sumToWork (Stage& s) noexcept
{
    // Both channels back in one inverse transform: left real, right imaginary.
    const float* lr = s.yRe[0].data();
    const float* li = s.yIm[0].data();
    const float* rr = s.yRe[1].data();
    const float* ri = s.yIm[1].data();
    for (int m = 0; m < s.bins; ++m)
        s.work[(size_t) m] = { lr[m] - ri[m], li[m] + rr[m] };
    for (int m = s.bins; m < s.size; ++m)
    {
        const int k = s.size - m;
        s.work[(size_t) m] = { lr[k] + ri[k], rr[k] - li[k] };
    }
}

void ConvolutionRoom::writeOutput (Stage& s, int64_t outStart, float scale) noexcept
{
    for (int k = 0; k < s.block; ++k)
    {
        const auto v = s.work[(size_t) (s.block + k)];
        const size_t i = (size_t) ((outStart + k) & ringMask);
        outL[i] += v.real() * scale;
        outR[i] += v.imag() * scale;
    }
}

void ConvolutionRoom::runTailUnits (int upTo) noexcept
{
    // A period's work, in units of about the same size: the input's transform a pass at a
    // time, one partition each, then the inverse a pass at a time.
    if (tailPeriodStart < 0)
        return;
    const int order = tail.fft->getOrder();
    const int multiply = order + 2, inverse = multiply + tail.parts, write = inverse + order + 1;
    upTo = std::min (upTo, tailUnits);
    for (; tailDone < upTo; ++tailDone)
    {
        const int u = tailDone;
        if (u == 0)
        {
            gatherInput (tail, tailPeriodStart);
            tail.fft->bitReverse (tail.work.data());
        }
        else if (u <= order)
        {
            tail.fft->pass (tail.work.data(), 1 << u, false);
        }
        else if (u == order + 1)
        {
            storeInput (tail);
        }
        else if (u < inverse)
        {
            const int part = u - multiply;
            const int64_t t = tail.offset + (int64_t) part * tail.block;
            if (partActive (t))
                accumulate (tail, part, tail.gain[(size_t) part]);
        }
        else if (u == inverse)
        {
            sumToWork (tail);
            tail.fft->bitReverse (tail.work.data());
        }
        else if (u < write)
        {
            tail.fft->pass (tail.work.data(), 1 << (u - inverse), true);
        }
        else
        {
            // This period's input reaches the output two tail blocks later, plus the head's
            // own latency, so the tail lines up with the head.
            writeOutput (tail, tailPeriodStart + tail.block + headBlock, 1.0f / (float) tail.size);
        }
    }
}

void ConvolutionRoom::process (const float* inL, const float* inR, float* oL, float* oR, int n, const RoomShape& shape) noexcept
{
    updateShape (shape);
    const float width = clampv (shape.width, 0.0f, 1.0f);
    for (int i = 0; i < n; ++i)
    {
        preL[(size_t) prePos] = inL[i];
        preR[(size_t) prePos] = inR != nullptr ? inR[i] : inL[i];
        const int rd = (prePos - preDelay) & preMask;
        const float l = preL[(size_t) rd], r = preR[(size_t) rd];
        prePos = (prePos + 1) & preMask;

        const int64_t t = now++;
        inA[(size_t) (t & ringMask)] = l + kCrossFeed * r;
        inB[(size_t) (t & ringMask)] = r + kCrossFeed * l;

        if (now % head.block == 0)
        {
            gatherInput (head, now);
            head.fft->forward (head.work.data());
            storeInput (head);
            for (int p = 0; p < head.parts; ++p)
                accumulate (head, p, head.gain[(size_t) p]);
            sumToWork (head);
            head.fft->inverse (head.work.data());
            writeOutput (head, now, 1.0f);
        }
        if (tail.parts > 0 && now % tail.block == 0)
        {
            runTailUnits (tailUnits);   // whatever the last period still owes
            tailPeriodStart = now;
            tailDone = 0;
        }

        const size_t o = (size_t) (t & ringMask);
        const float yl = shelfL.process (outL[o]), yr = shelfR.process (outR[o]);
        outL[o] = outR[o] = 0.0f;
        const float mid = 0.5f * (yl + yr), side = 0.5f * (yl - yr) * width;
        oL[i] = mid + side;
        oR[i] = mid - side;
    }
    // The tail's work, spread over its period in proportion to the time gone by.
    if (tailPeriodStart >= 0)
        runTailUnits ((int) ((now - tailPeriodStart) * tailUnits / tail.block));
}
} // namespace sc
