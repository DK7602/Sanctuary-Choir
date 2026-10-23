#include "sanctuary/Psola.h"

namespace sc
{
static int64_t nextPow2 (int64_t v)
{
    int64_t p = 1;
    while (p < v)
        p <<= 1;
    return p;
}

//==============================================================================
void ClickSoftener::prepare (double sampleRate)
{
    const double sr = sampleRate;
    detect.highPass (sr, 3000.0);
    low1.lowPass (sr, 1500.0);
    low2.lowPass (sr, 1500.0);
    high1.highPass (sr, 1500.0);
    high2.highPass (sr, 1500.0);
    lookahead = clampv ((int) std::lround (sr * 0.0013), 8, (int) lowDelay.size() - 1);
    auto coeff = [sr] (double seconds) { return (float) std::exp (-1.0 / (seconds * sr)); };
    fastRel = coeff (0.0003);
    slowAtt = 1.0f - coeff (0.012);   // the voice's recent peaks: rise slowly (a click doesn't count)...
    slowRel = coeff (0.04);           // ...and fall slowly between glottal pulses
    gainAtt = (float) std::exp (-3.0 / (double) lookahead);
    gainRel = coeff (0.005);
    reset();
}

void ClickSoftener::reset()
{
    for (auto* f : { &detect, &low1, &low2, &high1, &high2 })
        f->reset();
    lowDelay.fill (0.0f);
    highDelay.fill (0.0f);
    pos = holdLeft = 0;
    fast = slow = 0.0f;
    hold = gain = 1.0f;
}

float ClickSoftener::process (float x) noexcept
{
    const float a = std::abs (detect.process (x));
    fast = std::max (a, fast * fastRel);
    if (fast > slow) slow += (fast - slow) * slowAtt;
    else             slow = std::max (fast, slow * slowRel);
    constexpr float allowed = 4.0f;   // how far above the voice's own peaks a spike may stand
    const float target = fast > 3.0e-5f ? std::min (1.0f, allowed * slow / fast) : 1.0f;

    // Hold the lowest gain across the look-ahead either side, then glide: down within the
    // look-ahead (so the spike's start is caught), back up over ~5 ms.
    if (target <= hold)
    {
        hold = target;
        holdLeft = 2 * lookahead;
    }
    else if (--holdLeft <= 0)
    {
        hold = target;
    }
    gain = hold + (gain - hold) * (hold < gain ? gainAtt : gainRel);

    const int size = (int) lowDelay.size();
    lowDelay[(size_t) pos] = low2.process (low1.process (x));
    highDelay[(size_t) pos] = high2.process (high1.process (x));
    const int old = (pos - lookahead + size) % size;
    pos = (pos + 1) % size;
    return lowDelay[(size_t) old] + gain * highDelay[(size_t) old];
}

//==============================================================================
void PsolaAnalyzer::prepare (double sampleRate, int maxPer, int historySamples)
{
    sr = sampleRate;
    maxPeriod = std::max (32, maxPer);
    unvoicedHop = std::max (32, (int) std::lround (sr * 0.004));

    const int64_t ringSize = nextPow2 ((int64_t) historySamples + 6 * maxPeriod + 8192);
    ring.assign ((size_t) ringSize, 0.0f);
    ringMask = ringSize - 1;

    marks.assign (16384, PsolaMark{});
    markMask = 16383;
    frames.assign (1024, PitchFrame{});
    frameMask = 1023;

    frozenData.reserve ((size_t) (sr * 0.5) + (size_t) maxPeriod * 4);
    frozenMarks.reserve (2048);
    softener.prepare (sr);
    reset();
}

void PsolaAnalyzer::reset()
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    softener.reset();
    written = 0;
    markCount = 0;
    frameCount = 0;
    heldHz = 0.0f;
    octaveRun = unvoicedRun = 0;
    frozen = false;
}

void PsolaAnalyzer::pushInput (const float* x, int n)
{
    // Each sample goes in as it is, then comes back softened a moment later. Voices read far
    // behind the newest input, so they only ever hear the softened voice.
    const int la = softener.getLookahead();
    for (int i = 0; i < n; ++i)
    {
        const int64_t w = written + i;
        ring[(size_t) (w & ringMask)] = x[i];
        const float y = softener.process (x[i]);
        if (w >= la)
            ring[(size_t) ((w - la) & ringMask)] = y;
    }
    written += n;
}

void PsolaAnalyzer::addPitchFrame (const PitchFrame& f)
{
    // A voice that creaks or splits flips the tracker an octave up and down frame by frame.
    // Marks that followed it would give every other grain twice (or half) the pulses: a rough,
    // crackly choir. Keep to the octave the voice is in until a new one lasts (~15 ms).
    PitchFrame g = f;
    if (g.voiced && g.hz > 0.0f)
    {
        unvoicedRun = 0;
        if (heldHz > 0.0f)
        {
            const float jump = std::log2 (g.hz / heldHz);
            const float octaves = std::round (jump);
            if (octaves != 0.0f && std::abs (jump - octaves) < 0.3f && ++octaveRun < 3)
                g.hz *= std::exp2 (-octaves);
            else
                octaveRun = 0;
        }
        heldHz = g.hz;
    }
    else if (++unvoicedRun > 8)
    {
        heldHz = 0.0f;   // a new phrase starts wherever it starts
        octaveRun = 0;
    }
    frames[(size_t) (frameCount & frameMask)] = g;
    ++frameCount;
}

bool PsolaAnalyzer::pitchAt (int64_t pos, float& hz, bool& voiced) const
{
    if (frameCount == 0)
        return false;
    const int64_t newest = frameCount - 1;
    const int64_t oldest = std::max<int64_t> (0, frameCount - (frameMask + 1));
    const PitchFrame& last = frames[(size_t) (newest & frameMask)];
    if (last.centre < pos)
        return false;

    // Binary search for the last frame with centre <= pos.
    int64_t lo = oldest, hi = newest;
    if (frames[(size_t) (oldest & frameMask)].centre > pos)
    {
        const auto& f = frames[(size_t) (oldest & frameMask)];
        hz = f.hz; voiced = f.voiced;
        return true;
    }
    while (lo < hi)
    {
        const int64_t mid = (lo + hi + 1) / 2;
        if (frames[(size_t) (mid & frameMask)].centre <= pos) lo = mid;
        else hi = mid - 1;
    }
    const PitchFrame& a = frames[(size_t) (lo & frameMask)];
    if (lo == newest)
    {
        hz = a.hz; voiced = a.voiced;
        return true;
    }
    const PitchFrame& b = frames[(size_t) ((lo + 1) & frameMask)];
    const float span = (float) std::max<int64_t> (1, b.centre - a.centre);
    const float t = clampv ((float) (pos - a.centre) / span, 0.0f, 1.0f);

    if (a.voiced && b.voiced)
    {
        voiced = true;
        hz = std::exp2 (std::log2 (a.hz) + (std::log2 (b.hz) - std::log2 (a.hz)) * t);
    }
    else if (a.voiced || b.voiced)
    {
        const PitchFrame& nearer = t < 0.5f ? a : b;
        voiced = nearer.voiced;
        hz = a.voiced ? a.hz : b.hz;
    }
    else
    {
        voiced = false;
        hz = 0.0f;
    }
    return true;
}

float PsolaAnalyzer::correlationRefine (int64_t prevPos, int64_t predicted, int period) const
{
    const int half = std::max (4, period / 2);
    const int range = std::max (1, (int) (period * 0.2f));
    const int coarse = std::max (1, period / 120);

    auto score = [&] (int64_t cand)
    {
        double num = 0.0, en = 0.0;
        for (int k = -half; k < half; k += (period > 400 ? 2 : 1))
        {
            const float a = ring[(size_t) ((prevPos + k) & ringMask)];
            const float b = ring[(size_t) ((cand + k) & ringMask)];
            num += (double) a * b;
            en += (double) b * b;
        }
        return en > 1.0e-12 ? num / std::sqrt (en) : -1.0e9;
    };

    int64_t best = predicted;
    double bestScore = -1.0e18;
    for (int d = -range; d <= range; d += coarse)
    {
        const double s = score (predicted + d);
        if (s > bestScore) { bestScore = s; best = predicted + d; }
    }
    if (coarse > 1)
    {
        const int64_t centre = best;
        for (int d = -coarse; d <= coarse; ++d)
        {
            const double s = score (centre + d);
            if (s > bestScore) { bestScore = s; best = centre + d; }
        }
    }
    return (float) best;
}

void PsolaAnalyzer::updateMarks()
{
    const int64_t ringSize = ringMask + 1;
    for (int guard = 0; guard < 2048; ++guard)
    {
        if (markCount > 0)
        {
            // Fell too far behind the input (e.g. after a long silence of pitch data): restart.
            const PsolaMark& last = marks[(size_t) ((markCount - 1) & markMask)];
            if (last.pos < written - ringSize / 2)
                markCount = 0;
        }

        if (markCount == 0)
        {
            if (frameCount == 0)
                return;
            const int64_t oldest = std::max<int64_t> (0, frameCount - (frameMask + 1));
            int64_t start = frames[(size_t) (oldest & frameMask)].centre;
            start = std::max<int64_t> (start, written - ringSize / 4);
            start = std::max<int64_t> (start, (int64_t) maxPeriod * 2);
            float hz = 0.0f;
            bool v = false;
            if (! pitchAt (start, hz, v) || start + 2 * maxPeriod > written)
                return;
            marks[0] = { start, v ? clampv ((float) (sr / hz), 8.0f, (float) maxPeriod) : (float) unvoicedHop, v };
            markCount = 1;
            continue;
        }

        const PsolaMark last = marks[(size_t) ((markCount - 1) & markMask)];
        float hz = 0.0f;
        bool v = false;
        const int64_t probe = last.pos + (int64_t) std::lround (last.period);
        if (! pitchAt (probe, hz, v))
            return;

        const float period = v ? clampv ((float) (sr / hz), 8.0f, (float) maxPeriod) : (float) unvoicedHop;
        PsolaMark m;
        m.voiced = v;
        m.period = period;

        if (v && last.voiced)
        {
            const float avg = 0.5f * (period + last.period);
            const int64_t predicted = last.pos + (int64_t) std::lround (avg);
            if (predicted + (int64_t) (period * 1.25f) + 2 > written)
                return;
            m.pos = (int64_t) correlationRefine (last.pos, predicted, (int) period);
        }
        else if (v)
        {
            // Voice onset: anchor the first mark on the strongest peak of the next period.
            const int64_t from = last.pos + (int64_t) std::lround (last.period);
            if (from + 2 * (int64_t) period + 2 > written)
                return;
            int64_t best = from;
            float bestAbs = -1.0f;
            for (int64_t i = from; i < from + (int64_t) period; ++i)
            {
                const float a = std::abs (ring[(size_t) (i & ringMask)]);
                if (a > bestAbs) { bestAbs = a; best = i; }
            }
            m.pos = best;
        }
        else
        {
            m.pos = last.pos + (last.voiced ? (int64_t) std::lround (0.5f * (last.period + period)) : (int64_t) unvoicedHop);
            if (m.pos + unvoicedHop + 2 > written)
                return;
        }

        if (m.pos <= last.pos)
            m.pos = last.pos + 1;
        marks[(size_t) (markCount & markMask)] = m;
        ++markCount;
    }
}

const PsolaMark* PsolaAnalyzer::nearestMark (double t, int64_t& cursor) const
{
    if (markCount == 0)
        return nullptr;
    const int64_t oldest = std::max<int64_t> (0, markCount - (markMask + 1));
    cursor = clampv (cursor, oldest, markCount - 1);

    while (cursor > oldest && (double) marks[(size_t) (cursor & markMask)].pos > t)
        --cursor;
    while (cursor + 1 < markCount && (double) marks[(size_t) ((cursor + 1) & markMask)].pos <= t)
        ++cursor;

    const PsolaMark& a = marks[(size_t) (cursor & markMask)];
    if (cursor + 1 < markCount)
    {
        const PsolaMark& b = marks[(size_t) ((cursor + 1) & markMask)];
        if ((double) b.pos - t < t - (double) a.pos)
            return &b;
    }
    return &a;
}

bool PsolaAnalyzer::captureFreeze (int64_t endPos, int lengthSamples)
{
    frozenMarks.clear();
    const int64_t oldest = std::max<int64_t> (0, markCount - (markMask + 1));
    const int64_t from = endPos - lengthSamples;
    for (int64_t i = oldest; i < markCount; ++i)
    {
        const PsolaMark& m = marks[(size_t) (i & markMask)];
        if (m.voiced && m.pos >= from && m.pos <= endPos && m.pos - (int64_t) m.period > written - (ringMask + 1))
            frozenMarks.push_back (m);
    }
    if (frozenMarks.size() < 3)
    {
        frozen = false;
        return false;
    }

    frozenStart = frozenMarks.front().pos - maxPeriod - 2;
    const int64_t end = std::min (written, frozenMarks.back().pos + (int64_t) maxPeriod + 2);
    frozenData.resize ((size_t) std::max<int64_t> (2, end - frozenStart));
    for (size_t i = 0; i < frozenData.size(); ++i)
        frozenData[i] = ring[(size_t) ((frozenStart + (int64_t) i) & ringMask)];
    frozen = true;
    return true;
}

//==============================================================================
float PsolaVoice::hann (float x)
{
    static const auto table = []
    {
        std::array<float, 1026> t {};
        for (int i = 0; i < 1025; ++i)
            t[(size_t) i] = 0.5f + 0.5f * std::cos (kPi * (float) i / 1024.0f);
        t[1025] = 0.0f;
        return t;
    }();
    const float p = std::min (std::abs (x), 1.0f) * 1024.0f;
    const int i = (int) p;
    const float fr = p - (float) i;
    return table[(size_t) i] + (table[(size_t) i + 1] - table[(size_t) i]) * fr;
}

void PsolaVoice::prepare (double sr, int maxBlock, int maxPeriod)
{
    sampleRate = (float) sr;
    const int64_t size = nextPow2 ((int64_t) maxBlock + 6 * (int64_t) maxPeriod + 64);
    accum.assign ((size_t) size, 0.0f);
    accumMask = size - 1;
    maxHalf = (float) maxPeriod;
}

void PsolaVoice::reset (double startTime)
{
    std::fill (accum.begin(), accum.end(), 0.0f);
    nextSynth = startTime;
    cursor = 0;
    frozenIndex = 0;
}

void PsolaVoice::addGrain (const PsolaAnalyzer& an, double centre, double src, float halfLen,
                           float formant, float gain, bool frozenSrc)
{
    // The head of a grain that starts before the output read position (the first grain after a
    // reset) would only sit in the ring and play one lap later as a stale click.
    const int64_t j0 = std::max ((int64_t) std::ceil (centre - halfLen), readFrom);
    const int64_t j1 = (int64_t) std::floor (centre + halfLen);
    if (j1 < j0)
        return;

    // Hann window 0.5 + 0.5 cos(pi k / halfLen) via a cosine recurrence (no per-sample trig).
    const double delta = kPiD / (double) halfLen;
    const double k0 = (double) j0 - centre;
    const double twoCos = 2.0 * std::cos (delta);
    double cPrev = std::cos (delta * (k0 - 1.0));
    double c = std::cos (delta * k0);
    double pos = src + k0 * (double) formant;
    const float g = 0.5f * gain;

    for (int64_t j = j0; j <= j1; ++j)
    {
        const float w = (float) (1.0 + c);
        const float smp = frozenSrc ? an.frozenSampleAt (pos) : an.sampleAt (pos);
        accum[(size_t) (j & accumMask)] += g * w * smp;
        const double cNext = twoCos * c - cPrev;
        cPrev = c;
        c = cNext;
        pos += formant;
    }
}

void PsolaVoice::render (const PsolaAnalyzer& an, int64_t t0, int n, float* out, const PsolaControl& ctl, Rng& rng)
{
    const double horizon = (double) (t0 + n) + maxHalf + 1.0;
    readFrom = t0;

    // If we fell behind (e.g. after a reset), jump forward rather than synthesising history.
    if (nextSynth < (double) t0 - maxHalf * 2.0)
        nextSynth = (double) t0;

    for (int guard = 0; nextSynth < horizon && guard < 8192; ++guard)
    {
        const PsolaMark* m = nullptr;
        bool frozenSrc = false;

        if (an.isFrozen() && an.getNumFrozenMarks() > 0)
        {
            const int count = an.getNumFrozenMarks();
            frozenIndex += rng.uni() < 0.5f ? -1 : 1;
            if (frozenIndex < 0) frozenIndex = std::min (1, count - 1);
            if (frozenIndex >= count) frozenIndex = std::max (0, count - 2);
            m = &an.getFrozenMark (frozenIndex);
            frozenSrc = true;
        }
        else
        {
            m = an.nearestMark (nextSynth, cursor);
        }

        if (m == nullptr)
        {
            nextSynth += an.getUnvoicedHop();
            continue;
        }

        float ratio = 1.0f, formant = 1.0f, gain = ctl.gain * ctl.unvoicedGain;
        if (m->voiced)
        {
            ratio = ctl.fixedRatio > 0.0f ? ctl.fixedRatio : ctl.targetHz * m->period / sampleRate;
            ratio = clampv (ratio, 0.25f, 4.0f);
            formant = clampv (ctl.formant, 0.6f, 1.6f);
            gain = ctl.gain;
        }

        const float half = std::min (m->period / formant, maxHalf);
        const float overlap = ratio * half / m->period;
        const float norm = 1.0f / std::sqrt (std::max (overlap, 0.35f));
        addGrain (an, nextSynth, (double) m->pos, half, formant, gain * norm, frozenSrc);
        nextSynth += (double) (m->period / ratio);
    }

    for (int i = 0; i < n; ++i)
    {
        const size_t idx = (size_t) ((t0 + i) & accumMask);
        out[i] += accum[idx];
        accum[idx] = 0.0f;
    }
}
} // namespace sc
