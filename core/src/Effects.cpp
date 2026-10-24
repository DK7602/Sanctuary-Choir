#include "sanctuary/Effects.h"

namespace sc
{
const char* const* getVenueNames()
{
    static const char* names[] = { "Grand Cathedral", "Stone Abbey", "Wooden Chapel", "Concert Hall", "Gospel Church", "Studio Room" };
    static_assert (sizeof (names) / sizeof (names[0]) == (size_t) Venue::Count);
    return names;
}

const char* getVenueSlug (Venue v)
{
    static const char* slugs[] = { "grand-cathedral", "stone-abbey", "wooden-chapel", "concert-hall", "gospel-church", "studio-room" };
    static_assert (sizeof (slugs) / sizeof (slugs[0]) == (size_t) Venue::Count);
    const int i = (int) v;
    return i >= 0 && i < (int) Venue::Count ? slugs[i] : "";
}

float getMeasuredRoomLevel (Venue v)
{
    // 0.42 is unit energy against the halls on average; the rest, per room, measured with the
    // same choir through both (Tree mics, Reverb 100%), so each venue keeps the level it had.
    static const float trimDb[] = { -5.8f, 0.5f, -1.5f, 0.7f, -2.6f, 7.9f };
    static_assert (sizeof (trimDb) / sizeof (trimDb[0]) == (size_t) Venue::Count);
    const int i = clampv ((int) v, 0, (int) Venue::Count - 1);
    return 0.42f * dbToGain (trimDb[i]);
}

struct VenueSpec { float size, rt60, hfRatio, preDelayMs, earlySpanMs, earlyLevel; };

static VenueSpec venueSpec (Venue v)
{
    switch (v)
    {
        case Venue::GrandCathedral: return { 2.2f, 6.2f, 0.45f, 38.0f, 95.0f, 0.55f };
        case Venue::StoneAbbey:     return { 1.7f, 4.0f, 0.50f, 26.0f, 70.0f, 0.60f };
        case Venue::WoodenChapel:   return { 1.0f, 1.9f, 0.55f, 12.0f, 38.0f, 0.70f };
        case Venue::ConcertHall:    return { 1.4f, 2.4f, 0.62f, 20.0f, 52.0f, 0.60f };
        case Venue::GospelChurch:   return { 1.15f, 1.6f, 0.65f, 14.0f, 40.0f, 0.65f };
        case Venue::StudioRoom:     return { 0.5f, 0.7f, 0.70f, 4.0f, 16.0f, 0.75f };
        default:                    return { 1.0f, 2.0f, 0.6f, 10.0f, 40.0f, 0.6f };
    }
}

static constexpr float kMaxSize = 2.3f;
static constexpr float kLineMs[8] = { 31.7f, 37.9f, 41.3f, 47.1f, 53.9f, 59.3f, 67.7f, 73.1f };

void ChoirReverb::prepare (double sampleRate)
{
    sr = sampleRate;
    const int maxPre = (int) (sr * 0.25);
    preL.prepare (maxPre + (int) (sr * 0.12 * kMaxSize));
    preR.prepare (maxPre + (int) (sr * 0.12 * kMaxSize));

    static constexpr float diffMsL[4] = { 4.77f, 3.59f, 12.73f, 9.30f };
    static constexpr float diffMsR[4] = { 4.93f, 3.71f, 12.91f, 9.07f };
    for (int i = 0; i < 4; ++i)
    {
        diffL[(size_t) i].d.prepare ((int) (sr * diffMsL[i] * 0.001 * 1.6) + 4);
        diffR[(size_t) i].d.prepare ((int) (sr * diffMsR[i] * 0.001 * 1.6) + 4);
    }

    lfoDepth = (float) (sr * 0.00035);
    for (int i = 0; i < numLines; ++i)
    {
        lines[(size_t) i].prepare ((int) (sr * kLineMs[i] * 0.001 * kMaxSize) + (int) lfoDepth * 2 + 8);
        lfoRate[(size_t) i] = 0.11f + 0.097f * (float) i;
        lfoPhase[(size_t) i] = 0.37f * (float) i;
    }
    tailHpL.highPass (sr, 90.0);
    tailHpR.highPass (sr, 90.0);
    updateCoefficients();
    reset();
}

void ChoirReverb::reset()
{
    preL.clear(); preR.clear();
    for (auto& a : diffL) a.d.clear();
    for (auto& a : diffR) a.d.clear();
    for (auto& l : lines) l.clear();
    dampZ.fill (0.0f);
    earlyToneL.reset(); earlyToneR.reset();
    tailHpL.reset(); tailHpR.reset();
}

void ChoirReverb::setSettings (const Settings& s)
{
    if (s == settings)
        return;
    settings = s;
    updateCoefficients();
}

void ChoirReverb::updateCoefficients()
{
    const VenueSpec v = venueSpec (settings.venue);
    rt60 = clampv (v.rt60 * settings.decayScale, 0.2f, 14.0f);
    const float hf = clampv (v.hfRatio * (0.6f + 0.8f * settings.brightness), 0.15f, 0.95f);
    const float preMs = settings.preDelayMs >= 0.0f ? settings.preDelayMs : v.preDelayMs;
    preDelaySamples = clampv ((int) (sr * preMs * 0.001), 0, (int) (sr * 0.25));

    static constexpr float diffMsL[4] = { 4.77f, 3.59f, 12.73f, 9.30f };
    static constexpr float diffMsR[4] = { 4.93f, 3.71f, 12.91f, 9.07f };
    const float dscale = std::sqrt (std::min (v.size, 1.6f));
    for (int i = 0; i < 4; ++i)
    {
        diffL[(size_t) i].len = std::max (1, (int) (sr * diffMsL[i] * 0.001 * dscale));
        diffR[(size_t) i].len = std::max (1, (int) (sr * diffMsR[i] * 0.001 * dscale));
        diffL[(size_t) i].g = diffR[(size_t) i].g = i < 2 ? 0.7f : 0.6f;
    }

    for (int i = 0; i < numLines; ++i)
    {
        const float len = (float) (sr * kLineMs[i] * 0.001 * v.size);
        lineLen[(size_t) i] = len;
        const float g = std::pow (10.0f, -3.0f * len / (rt60 * (float) sr));
        lineGain[(size_t) i] = g;
        const float q = std::pow (g, 1.0f / hf - 1.0f);  // extra attenuation wanted at Nyquist
        dampA[(size_t) i] = clampv ((1.0f - q) / (1.0f + q), 0.0f, 0.97f);
    }

    // Early reflections: sparse, decaying taps spread over the venue's early span.
    Rng rng (1234u + (uint32_t) settings.venue);
    earlyL.clear();
    earlyR.clear();
    const int numTaps = 14;
    for (int side = 0; side < 2; ++side)
    {
        auto& taps = side == 0 ? earlyL : earlyR;
        for (int k = 0; k < numTaps; ++k)
        {
            const float frac = std::pow (((float) k + 0.5f + 0.4f * rng.bi()) / (float) numTaps, 1.35f);
            const float ms = 3.0f + v.earlySpanMs * frac;
            const float gain = std::exp (-2.6f * frac) * (rng.uni() < 0.5f ? -1.0f : 1.0f) * (0.6f + 0.4f * rng.uni());
            taps.push_back ({ (int) (sr * ms * 0.001), gain * 0.42f });
        }
    }
    earlyToneL.setCutoff (sr, 3500.0 + 4000.0 * settings.brightness);
    earlyToneR.setCutoff (sr, 3500.0 + 4000.0 * settings.brightness);
    earlyGain = v.earlyLevel * clampv (settings.early, 0.0f, 1.0f) * 1.4f;
    tailGain = 0.55f / std::sqrt (std::max (rt60, 0.4f)) * 1.6f;
}

void ChoirReverb::process (const float* inL, const float* inR, float* outL, float* outR, int n)
{
    const float dt = (float) (1.0 / sr);
    const float width = clampv (settings.width, 0.0f, 1.0f);

    for (int i = 0; i < n; ++i)
    {
        preL.push (inL[i]);
        preR.push (inR[i]);
        const float pl = preL.tap (preDelaySamples);
        const float pr = preR.tap (preDelaySamples);

        // Early reflections.
        float el = 0.0f, er = 0.0f;
        for (const auto& t : earlyL) el += preL.tap (preDelaySamples + t.delay) * t.gain;
        for (const auto& t : earlyR) er += preR.tap (preDelaySamples + t.delay) * t.gain;
        el = earlyToneL.process (el + 0.25f * er);
        er = earlyToneR.process (er + 0.25f * el);

        // Input diffusion.
        float dl = pl + 0.3f * el, dr = pr + 0.3f * er;
        for (auto& a : diffL) dl = a.process (dl);
        for (auto& a : diffR) dr = a.process (dr);

        // Slow delay modulation, updated every 16 samples.
        if ((modCounter++ & 15) == 0)
        {
            for (int k = 1; k < numLines; k += 2)
            {
                auto& ph = lfoPhase[(size_t) k];
                ph += lfoRate[(size_t) k] * dt * 16.0f;
                if (ph >= 1.0f) ph -= 1.0f;
                lfoValue[(size_t) k] = lfoDepth * (0.5f + 0.5f * std::sin (kTwoPi * ph));
            }
        }

        // Read the delay lines (modulated).
        std::array<float, numLines> y {};
        float sum = 0.0f;
        for (int k = 0; k < numLines; ++k)
        {
            float v = lines[(size_t) k].tapFrac (lineLen[(size_t) k] - 1.0f - lfoValue[(size_t) k]);
            // Frequency dependent decay.
            auto& z = dampZ[(size_t) k];
            z = v * (1.0f - dampA[(size_t) k]) + z * dampA[(size_t) k];
            v = z * lineGain[(size_t) k];
            y[(size_t) k] = v;
            sum += v;
        }

        // Householder feedback and injection.
        const float h = sum * (2.0f / (float) numLines);
        for (int k = 0; k < numLines; ++k)
        {
            const float inj = (k < 4 ? dl : dr) * ((k & 2) ? -0.5f : 0.5f);
            lines[(size_t) k].push (y[(size_t) k] - h + inj);
        }

        float tl = y[0] + y[1] - y[2] - y[3] + y[4] - y[5] + y[6] - y[7];
        float tr = y[0] - y[1] + y[2] - y[3] - y[4] - y[5] + y[6] + y[7];
        const float mid = 0.5f * (tl + tr), side = 0.5f * (tl - tr) * width;
        tl = tailHpL.process (mid + side) * tailGain;
        tr = tailHpR.process (mid - side) * tailGain;

        outL[i] = tl + el * earlyGain;
        outR[i] = tr + er * earlyGain;
    }
}

//==============================================================================
//==============================================================================
void ChoirStage::Ring::prepare (int maxLen)
{
    int size = 1;
    while (size < maxLen + 4) size <<= 1;
    buf.assign ((size_t) size, 0.0f);
    mask = size - 1;
    pos = 0;
}

void ChoirStage::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    const float smoothS = 0.06f;
    for (size_t s = 0; s < sections.size(); ++s)
    {
        auto& st = sections[s];
        const auto& p = settings.sections[s];
        st.x.prepare (sr, smoothS, p.x);
        st.width.prepare (sr, smoothS, p.width);
        st.depth.prepare (sr, smoothS, p.depth);
        st.delayL.prepare ((int) (0.006 * sr) + 2);
        st.delayR.prepare ((int) (0.006 * sr) + 2);
    }
    close.prepare (sr, smoothS, settings.close);
    tree.prepare (sr, smoothS, settings.tree);
    room.prepare (sr, smoothS, settings.room);
    spread.prepare (sr, smoothS, settings.spread);
    for (auto* b : { &closeL, &closeR, &treeL, &treeR, &roomL, &roomR })
        b->assign ((size_t) std::max (1, maxBlock), 0.0f);
    for (auto& f : closePresence)
        f.highShelf (sr, 4500.0, 2.5);
    for (auto& f : roomTone)
        f.lowPass (sr, 6500.0, 0.6);
    // The room pair stands far out in the hall: the choir reaches it later (a little more on one
    // side, so the two mics don't hear the same thing).
    roomDelayLeft = (float) (0.017 * sr);
    roomDelayRight = (float) (0.023 * sr);
    roomDelayL.prepare ((int) (0.025 * sr) + 2);
    roomDelayR.prepare ((int) (0.025 * sr) + 2);
    setSettings (settings);
    reset();
}

void ChoirStage::reset()
{
    for (auto& st : sections)
    {
        st.delayL.clear();
        st.delayR.clear();
        st.delay = st.delayTarget;
        st.airL.reset();
        st.airR.reset();
    }
    roomDelayL.clear();
    roomDelayR.clear();
    for (auto& f : closePresence) f.reset();
    for (auto& f : roomTone) f.reset();
}

void ChoirStage::setSettings (const Settings& s)
{
    settings = s;
    // Rows further back are heard later, measured from the front-most section (so the front of
    // the choir stays in time with the lead).
    float front = 1.0f;
    for (const auto& p : s.sections)
        front = std::min (front, clampv (p.depth, 0.0f, 1.0f));
    const float stageSize = clampv (s.spread, 0.0f, 1.0f);
    for (size_t i = 0; i < sections.size(); ++i)
    {
        auto& st = sections[i];
        const auto& p = s.sections[i];
        st.x.setTarget (clampv (p.x, -1.0f, 1.0f));
        st.width.setTarget (clampv (p.width, 0.0f, 1.0f));
        st.depth.setTarget (clampv (p.depth, 0.0f, 1.0f));
        st.delayTarget = (float) (0.0045 * sr) * (clampv (p.depth, 0.0f, 1.0f) - front) * stageSize;
    }
    close.setTarget (std::max (0.0f, s.close));
    tree.setTarget (std::max (0.0f, s.tree));
    room.setTarget (std::max (0.0f, s.room));
    spread.setTarget (clampv (s.spread, 0.0f, 1.0f));
}

void ChoirStage::snapToSettings()
{
    for (auto& st : sections)
    {
        st.x.snap (st.x.target);
        st.width.snap (st.width.target);
        st.depth.snap (st.depth.target);
        st.delay = st.delayTarget;
    }
    for (auto* g : { &close, &tree, &room, &spread })
        g->snap (g->target);
}

float ChoirStage::sectionDelaySeconds (int section) const noexcept
{
    return sections[(size_t) clampv (section, 0, NumSections - 1)].delayTarget / (float) sr;
}

void ChoirStage::begin (int n)
{
    gClose = close.advance (n);
    gTree = tree.advance (n);
    gRoom = room.advance (n);
    gSpread = spread.advance (n);
    for (auto* b : { &closeL, &closeR, &treeL, &treeR, &roomL, &roomR })
        std::fill (b->begin(), b->begin() + n, 0.0f);
}

void ChoirStage::addSection (int section, const float* L, const float* R, int n)
{
    auto& st = sections[(size_t) section];
    const float x = st.x.advance (n), w = st.width.advance (n), d = st.depth.advance (n);
    constexpr float quarter = 0.25f * kPi;

    // Where the section sits (Space scales the whole stage), kept inside the stage's edges.
    const float xe = clampv (x * gSpread, -1.0f, 1.0f);
    const float we = clampv (std::min (w * gSpread, 1.0f - std::abs (xe)), 0.0f, 1.0f);
    const float ca = std::cos (xe * quarter), sa = std::sin (xe * quarter);
    const float car = std::cos (xe * 0.55f * quarter), sar = std::sin (xe * 0.55f * quarter);   // the room hears the stage narrower

    // The section's edges are its bus's left and right: narrowing it (mid/side) to its width,
    // then turning the picture to its place. A narrow section keeps its loudness.
    auto widthK = [] (float halfWidth) { return std::tan (clampv (halfWidth, 0.0f, 1.0f) * quarter); };
    auto keepLevel = [] (float k) { return 1.0f / std::sqrt (0.818f + 0.182f * k * k); };
    const float kt = widthK (we), kc = widthK (std::min (we, 0.3f) * 0.6f), kr = widthK (we * 0.6f);
    const float gt = keepLevel (kt) * 1.05f / (1.0f + 0.4f * d);  // the tree: back rows further away
    const float gc = keepLevel (kc);                                // spot mics: every row close
    const float gr = keepLevel (kr) * 0.8f;
    st.airL.setCutoff (sr, 20000.0 - 12000.0 * d);
    st.airR.setCutoff (sr, 20000.0 - 12000.0 * d);
    const bool useClose = gClose > 1.0e-5f || close.target > 1.0e-5f;
    const bool useRoom = gRoom > 1.0e-5f || room.target > 1.0e-5f;

    constexpr float slew = 1.0f / 16.0f;   // moving a row back or forth glides its delay
    for (int i = 0; i < n; ++i)
    {
        st.delayL.push (L[i]);
        st.delayR.push (R[i]);
        if (st.delay != st.delayTarget)
            st.delay += clampv (st.delayTarget - st.delay, -slew, slew);
        const float l = st.delayL.tap (st.delay), r = st.delayR.tap (st.delay);
        {
            const float fl = st.airL.process (l), fr = st.airR.process (r);
            const float m = 0.5f * (fl + fr), side = 0.5f * (fl - fr) * kt;
            const float a = (m + side) * gt, b = (m - side) * gt;
            treeL[(size_t) i] += a * ca - b * sa;
            treeR[(size_t) i] += a * sa + b * ca;
        }
        if (useClose)
        {
            const float m = 0.5f * (l + r), side = 0.5f * (l - r) * kc;
            const float a = (m + side) * gc, b = (m - side) * gc;
            closeL[(size_t) i] += a * ca - b * sa;
            closeR[(size_t) i] += a * sa + b * ca;
        }
        if (useRoom)
        {
            const float m = 0.5f * (l + r), side = 0.5f * (l - r) * kr;
            const float a = (m + side) * gr, b = (m - side) * gr;
            roomL[(size_t) i] += a * car - b * sar;
            roomR[(size_t) i] += a * sar + b * car;
        }
    }
}

void ChoirStage::mix (float* outL, float* outR, int n)
{
    for (int i = 0; i < n; ++i)
    {
        const float cl = closePresence[0].process (closeL[(size_t) i]), cr = closePresence[1].process (closeR[(size_t) i]);
        roomDelayL.push (roomL[(size_t) i]);
        roomDelayR.push (roomR[(size_t) i]);
        const float rl = roomTone[0].process (roomDelayL.tap (roomDelayLeft));
        const float rr = roomTone[1].process (roomDelayR.tap (roomDelayRight));
        outL[i] = gClose * cl + gTree * treeL[(size_t) i] + gRoom * rl;
        outR[i] = gClose * cr + gTree * treeR[(size_t) i] + gRoom * rr;
    }
}

void ToneStage::prepare (double sampleRate)
{
    sr = sampleRate;
    cLow = cHigh = cPres = -1.0f;
    cTilt = -100.0f;
    set (40.0f, 20000.0f, 0.0f, 0.0f);
    reset();
}

void ToneStage::reset()
{
    for (int c = 0; c < 2; ++c)
    {
        hp[(size_t) c].reset(); lp[(size_t) c].reset(); lowShelf[(size_t) c].reset();
        highShelf[(size_t) c].reset(); presPeak[(size_t) c].reset(); air[(size_t) c].reset();
    }
}

void ToneStage::set (float lowCutHz, float highCutHz, float tiltDb, float presence)
{
    if (lowCutHz != cLow)
    {
        cLow = lowCutHz;
        for (auto& f : hp) f.highPass (sr, lowCutHz, 0.7071);
    }
    if (highCutHz != cHigh)
    {
        cHigh = highCutHz;
        for (auto& f : lp) f.lowPass (sr, std::min ((double) highCutHz, sr * 0.45), 0.7071);
    }
    if (tiltDb != cTilt)
    {
        cTilt = tiltDb;
        for (auto& f : lowShelf) f.lowShelf (sr, 280.0, -0.5 * tiltDb, 0.7);
        for (auto& f : highShelf) f.highShelf (sr, 2600.0, 0.5 * tiltDb, 0.7);
    }
    if (presence != cPres)
    {
        cPres = presence;
        for (auto& f : presPeak) f.peak (sr, 3600.0, 0.9, 6.5 * presence);
        for (auto& f : air) f.highShelf (sr, 10500.0, 4.0 * presence, 0.8);
    }
}

void ToneStage::process (float* l, float* r, int n)
{
    float* ch[2] = { l, r };
    for (int c = 0; c < 2; ++c)
    {
        auto& h = hp[(size_t) c]; auto& lo = lp[(size_t) c]; auto& ls = lowShelf[(size_t) c];
        auto& hs = highShelf[(size_t) c]; auto& pp = presPeak[(size_t) c]; auto& ai = air[(size_t) c];
        float* x = ch[c];
        for (int i = 0; i < n; ++i)
            x[i] = ai.process (pp.process (hs.process (ls.process (lo.process (h.process (x[i]))))));
    }
}

//==============================================================================
void GlueCompressor::prepare (double sampleRate)
{
    sr = sampleRate;
    att = onePoleCoeff (0.012f, sr);
    rel = onePoleCoeff (0.180f, sr);
    gainSmooth = onePoleCoeff (0.005f, sr);
    reset();
}

void GlueCompressor::process (float* l, float* r, int n, float amount)
{
    if (amount <= 0.001f)
    {
        gain = 1.0f;
        return;
    }
    const float thresholdDb = -14.0f - 10.0f * amount;
    const float ratio = 1.0f + 3.0f * amount;
    const float makeup = dbToGain (-thresholdDb * (1.0f - 1.0f / ratio) * 0.45f);
    for (int i = 0; i < n; ++i)
    {
        const float a = std::max (std::abs (l[i]), std::abs (r[i]));
        env = a + (env - a) * (a > env ? att : rel);
        const float levelDb = gainToDb (env);
        const float over = levelDb - thresholdDb;
        // 6 dB soft knee
        float grDb = 0.0f;
        if (over > 3.0f) grDb = over * (1.0f - 1.0f / ratio);
        else if (over > -3.0f) grDb = (1.0f - 1.0f / ratio) * (over + 3.0f) * (over + 3.0f) / 12.0f;
        const float target = dbToGain (-grDb);
        gain = target + (gain - target) * gainSmooth;
        l[i] *= gain * makeup;
        r[i] *= gain * makeup;
    }
}

//==============================================================================
void SafetyLimiter::prepare (double sampleRate)
{
    look = std::max (1, (int) std::lround (0.0015 * sampleRate));
    // Held past the longest waveform cycle a choir sings (a 40 Hz bass), so a steady loud
    // chord gets one steady gain rather than a dip at every cycle.
    hold = (int) (0.03 * sampleRate);
    rel = std::exp (-1.0 / (0.15 * sampleRate));
    delayL.assign ((size_t) look, 0.0f);
    delayR.assign ((size_t) look, 0.0f);
    minVal.assign ((size_t) look + 2, 1.0f);
    minAt.assign ((size_t) look + 2, 0);
    held.assign ((size_t) look + 1, 1.0f);
    reset();
}

void SafetyLimiter::reset()
{
    std::fill (delayL.begin(), delayL.end(), 0.0f);
    std::fill (delayR.begin(), delayR.end(), 0.0f);
    std::fill (held.begin(), held.end(), 1.0f);
    heldSum = (double) held.size();
    minHead = minCount = 0;
    pos = 0;
    t = 0;
    env = 1.0;
    holdLeft = 0;
}

void SafetyLimiter::process (float* l, float* r, int n) noexcept
{
    // The gain each sample needs; its minimum over the last look + 1 samples (so every sample
    // still in the delay line is covered), averaged over look + 1 samples: a ramp down that
    // reaches each peak's gain exactly when that peak leaves the delay line, and never more.
    const int len = look + 1;
    const int cap = (int) minVal.size();
    for (int i = 0; i < n; ++i)
    {
        const float inL = l[i], inR = r[i];
        const float peak = std::max (std::abs (inL), std::abs (inR));
        const float need = peak > ceiling ? ceiling / peak : 1.0f;

        // Sliding minimum (a deque, oldest first, values increasing).
        while (minCount > 0 && minVal[(size_t) ((minHead + minCount - 1) % cap)] >= need)
            --minCount;
        minVal[(size_t) ((minHead + minCount) % cap)] = need;
        minAt[(size_t) ((minHead + minCount) % cap)] = t;
        ++minCount;
        while (minAt[(size_t) minHead] <= t - len)
        {
            minHead = (minHead + 1) % cap;
            --minCount;
        }
        const float m = minVal[(size_t) minHead];

        const int slot = (int) (t % len);
        heldSum += (double) m - (double) held[(size_t) slot];
        held[(size_t) slot] = m;
        const double ramp = std::min (1.0, heldSum / (double) len);

        // Down with the ramp; back up only once the peaks have stopped coming (a peak within
        // half a dB of the gain in force counts: sampled peaks of one held note differ a little).
        if (m < 1.0f && ramp < env * 1.06)
            holdLeft = hold;
        if (ramp < env)
            env = ramp;
        else if (holdLeft > 0)
            --holdLeft;
        else
            env = ramp - env < 1.0e-7 ? ramp : ramp + (env - ramp) * rel;

        const float g = (float) env;
        float a = delayL[(size_t) pos] * g, b = delayR[(size_t) pos] * g;
        delayL[(size_t) pos] = inL;
        delayR[(size_t) pos] = inR;
        pos = pos + 1 < look ? pos + 1 : 0;
        ++t;
        // Rounding aside nothing gets past the ramp; anything that does stops at the ceiling.
        a = clampv (a, -ceiling, ceiling);
        b = clampv (b, -ceiling, ceiling);
        l[i] = a;
        r[i] = b;
    }
    // Keep the running sum exact over long sessions.
    if ((t & 0xffff) < n)
    {
        heldSum = 0.0;
        for (float v : held)
            heldSum += v;
    }
}
} // namespace sc
