#include "sanctuary/PitchTracker.h"

#include <algorithm>

namespace sc
{
void PitchTracker::prepare (double sampleRate, Settings s)
{
    sr = sampleRate;
    settings = s;
    decimation = std::max (1, (int) std::floor (sr / 20000.0));
    dsr = sr / decimation;

    tauMin = std::max (2, (int) std::floor (dsr / settings.maxHz));
    tauMax = (int) std::ceil (dsr / settings.minHz) + 2;
    window = tauMax;
    bufLen = window + tauMax + 2;
    hop = std::max (16, (int) std::lround (dsr * 0.005));

    const double cutoff = std::min (0.4 * dsr, 6000.0);
    aa1.lowPass (sr, cutoff, 0.5411961);
    aa2.lowPass (sr, cutoff, 1.3065630);

    ring.assign ((size_t) bufLen, 0.0f);
    frameBuf.assign ((size_t) bufLen, 0.0f);
    diff.assign ((size_t) tauMax + 2, 0.0f);
    cmnd.assign ((size_t) tauMax + 2, 1.0f);
    newFrames.assign (64, PitchFrame{});

    analysisDelay = (bufLen / 2) * decimation + 2;
    reset();
}

void PitchTracker::reset()
{
    aa1.reset();
    aa2.reset();
    std::fill (ring.begin(), ring.end(), 0.0f);
    ringPos = 0;
    decimPhase = 0;
    decimCount = 0;
    sinceHop = 0;
    fullCount = 0;
    numNew = 0;
    latest = {};
    lastMidi = 0.0f;
    voicedRun = 0;
    sinceVoiced = 1000;
    backgroundDb = 1000.0f;
    minRing.assign (60, 1000.0f);   // 6 s in tenths
    minPos = minFilled = blockFrames = 0;
    blockMin = 1000.0f;
}

int PitchTracker::push (const float* x, int n)
{
    numNew = 0;
    for (int i = 0; i < n; ++i)
    {
        const float y = aa2.process (aa1.process (x[i]));
        ++fullCount;
        if (++decimPhase < decimation)
            continue;
        decimPhase = 0;

        ring[(size_t) ringPos] = y;
        ringPos = (ringPos + 1) % bufLen;
        ++decimCount;

        if (++sinceHop >= hop && decimCount >= bufLen)
        {
            sinceHop = 0;
            analyseWindow();
        }
    }
    return numNew;
}

void PitchTracker::analyseWindow()
{
    // Unroll the ring into chronological order.
    for (int i = 0; i < bufLen; ++i)
        frameBuf[(size_t) i] = ring[(size_t) ((ringPos + i) % bufLen)];

    const float* xb = frameBuf.data();
    double energy = 0.0;
    for (int i = 0; i < bufLen; ++i)
        energy += (double) xb[i] * xb[i];
    const float rmsDb = gainToDb ((float) std::sqrt (energy / bufLen));

    PitchFrame f;
    f.centre = fullCount - analysisDelay;
    f.rmsDb = rmsDb;

    if (rmsDb > settings.gateDb - 12.0f)
    {
        // Difference function d(tau) = sum (x[j] - x[j+tau])^2
        diff[0] = 0.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float acc = 0.0f;
            const float* a = xb;
            const float* b = xb + tau;
            for (int j = 0; j < window; ++j)
            {
                const float d = a[j] - b[j];
                acc += d * d;
            }
            diff[(size_t) tau] = acc;
        }

        // Cumulative mean normalised difference.
        cmnd[0] = 1.0f;
        double running = 0.0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            running += diff[(size_t) tau];
            cmnd[(size_t) tau] = running > 0.0 ? (float) (diff[(size_t) tau] * tau / running) : 1.0f;
        }

        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (cmnd[(size_t) tau] < settings.threshold)
            {
                while (tau + 1 < tauMax && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau])
                    ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0)
        {
            // Nothing that clean: a voice over other sound (the band in the headphones, a pad).
            // The deepest dip is then often at twice the period, an octave down: take the first
            // dip nearly as deep as the deepest.
            float m = 1e9f;
            for (int tau = tauMin; tau < tauMax; ++tau)
                if (cmnd[(size_t) tau] < m) { m = cmnd[(size_t) tau]; best = tau; }
            for (int tau = tauMin + 1; tau < best; ++tau)
                if (cmnd[(size_t) tau] <= cmnd[(size_t) tau - 1] && cmnd[(size_t) tau] <= cmnd[(size_t) tau + 1]
                    && cmnd[(size_t) tau] < m + 0.1f)
                {
                    best = tau;
                    break;
                }
        }

        auto refine = [this] (int tau)
        {
            if (tau <= 1 || tau >= tauMax)
                return (float) tau;
            const float a = cmnd[(size_t) tau - 1], b = cmnd[(size_t) tau], c = cmnd[(size_t) tau + 1];
            const float den = a - 2.0f * b + c;
            const float delta = std::abs (den) > 1e-9f ? clampv (0.5f * (a - c) / den, -0.5f, 0.5f) : 0.0f;
            return (float) tau + delta;
        };

        // Octave continuity: if the previous note was an octave away and that period is
        // also strongly periodic, stick with continuity.
        if (best > 0 && lastMidi > 0.0f && voicedRun > 3)
        {
            const float midiBest = hzToMidi ((float) (dsr / refine (best)));
            const float jump = midiBest - lastMidi;
            if (std::abs (std::abs (jump) - 12.0f) < 0.8f)
            {
                const int alt = jump > 0 ? best * 2 : best / 2;
                if (alt >= tauMin && alt < tauMax)
                {
                    int a = alt;
                    for (int k = -2; k <= 2; ++k)
                        if (alt + k >= tauMin && alt + k < tauMax && cmnd[(size_t) (alt + k)] < cmnd[(size_t) a])
                            a = alt + k;
                    if (cmnd[(size_t) a] < settings.threshold * 2.0f)
                        best = a;
                }
            }
        }

        const float aperiodicity = cmnd[(size_t) best];
        const float voicedLimit = latest.voiced ? settings.threshold * 2.2f : settings.threshold * 1.6f;
        f.clarity = clampv (1.0f - aperiodicity, 0.0f, 1.0f);
        f.hz = (float) (dsr / refine (best));
        // Other sound under the voice makes it look less periodic than it is. Once the voice is
        // found, keep it while the reading stays near the note it was on and it is clearly
        // louder than what plays without it.
        const bool continuing = sinceVoiced <= 2 && lastMidi > 0.0f && aperiodicity < 0.5f
                                && std::abs (hzToMidi (f.hz) - lastMidi) < 1.5f && rmsDb > backgroundDb + 6.0f;
        f.voiced = rmsDb > settings.gateDb && (aperiodicity < voicedLimit || continuing)
                   && f.hz >= settings.minHz * 0.95f && f.hz <= settings.maxHz * 1.05f;
    }

    // The level of what plays when nobody sings (the band in the headphones, room noise): the
    // quietest moment of the last few seconds, kept as the minimum of each tenth of a second.
    blockMin = std::min (blockMin, rmsDb);
    if (++blockFrames * hop >= (int) (dsr * 0.1))
    {
        minRing[(size_t) minPos] = blockMin;
        minPos = (minPos + 1) % (int) minRing.size();
        minFilled = std::min (minFilled + 1, (int) minRing.size());
        backgroundDb = *std::min_element (minRing.begin(), minRing.begin() + minFilled);
        blockMin = 1000.0f;
        blockFrames = 0;
    }

    if (f.voiced)
    {
        f.midi = hzToMidi (f.hz);
        lastMidi = f.midi;
        ++voicedRun;
        sinceVoiced = 0;
    }
    else
    {
        f.hz = 0.0f;
        voicedRun = 0;
        ++sinceVoiced;
    }

    latest = f;
    if (numNew < (int) newFrames.size())
        newFrames[(size_t) numNew++] = f;
}

std::vector<PitchFrame> PitchTracker::analyse (const float* x, int64_t n, double sampleRate, Settings s)
{
    PitchTracker t;
    t.prepare (sampleRate, s);
    std::vector<PitchFrame> out;
    out.reserve ((size_t) (n / std::max (1, t.getHop()) + 8));
    const int block = 512;
    std::vector<float> pad ((size_t) block, 0.0f);
    for (int64_t i = 0; i < n; i += block)
    {
        const int len = (int) std::min<int64_t> (block, n - i);
        t.push (x + i, len);
        for (int k = 0; k < t.getNumNewFrames(); ++k)
            out.push_back (t.getNewFrame (k));
    }
    // Flush so frames reach the end of the material.
    for (int k = 0; k < (t.getAnalysisDelay() / block) + 2; ++k)
    {
        t.push (pad.data(), block);
        for (int j = 0; j < t.getNumNewFrames(); ++j)
            if (t.getNewFrame (j).centre < n)
                out.push_back (t.getNewFrame (j));
    }
    return out;
}
} // namespace sc
