#include "sanctuary/ChoirEngine.h"

#include <cstring>
#include <cstdio>

namespace sc
{
static constexpr int kSingersPerSection = 20;
static constexpr int kChunk = 64;
static constexpr float kSampleGain = 1.37f;  // recorded notes are levelled to 0.1 RMS; this matches the synth choir
static constexpr float kMinInputHz = 75.0f;
static constexpr float kMaxInputHz = 1150.0f;

static constexpr double kMaxEchoSeconds = 10.0;

const char* const* getResponseNames()
{
    static const char* names[] = { "With Me", "Echo (Right After)" };
    return names;
}

const char* const* getEnsembleNames()
{
    static const char* names[] = { "Choir", "Children", "Gang Vocals", "Congregation" };
    return names;
}

SectionRange ensembleRange (Ensemble e, int section, SectionRange r)
{
    if (e != Ensemble::Children)
        return r;
    // Children's choirs are all trebles: the lower parts move up an octave, and everything
    // stays between G3 and C6.
    if (section == Tenor || section == Bass)
    {
        r.low += 12;
        r.high += 12;
    }
    r.low = clampv (r.low, 55, 74);
    r.high = clampv (r.high, r.low + 9, 84);
    return r;
}

namespace
{
    // Sixteen steps per bar. 'x' starts a note, '-' holds it, '.' rests. One string for every
    // section, or one per section (soprano, alto, tenor, bass) for patterns that pass the chord
    // around the choir.
    struct RhythmPattern { const char* name; const char* all; std::array<const char*, NumSections> parts; };
    const RhythmPattern kRhythmPatterns[kNumRhythmPatterns] = {
        { "Off", nullptr, {} },
        { "Pulse 1/4", "x---x---x---x---", {} },
        { "Pulse 1/8", "x-x-x-x-x-x-x-x-", {} },
        { "Pulse 1/16", "xxxxxxxxxxxxxxxx", {} },
        { "Offbeats", "..x-..x-..x-..x-", {} },
        { "Dotted", "x--x--x--x--x-x-", {} },
        { "Gallop", "x-xxx-xxx-xxx-xx", {} },
        { "Syncopated", "x--x--x---x--x--", {} },
        { "Stutter", "xxxx..x.xxxx..x.", {} },
        { "Trance Gate", "x.xxx.xxx.x.xx.x", {} },
        { "Heartbeat", "x.x.....x.x.....", {} },
        { "Arp Up", nullptr, { "...x...x...x...x", "..x...x...x...x.", ".x...x...x...x..", "x...x...x...x..." } },
        { "Arp Down", nullptr, { "x...x...x...x...", ".x...x...x...x..", "..x...x...x...x.", "...x...x...x...x" } },
        { "Arp Bounce", nullptr, { "...x.....x.....x", "..x.x...x.x...x.", ".x...x.x...x.x..", "x.....x.....x..." } },
        { "Call & Answer", nullptr, { "x-------........", "x-------........", "........x-------", "........x-------" } },
    };

    struct RhythmStep { int8_t start = 0, len = 0; };   // the note this step belongs to (len 0 = rest)
    using RhythmTable = std::array<std::array<std::array<RhythmStep, 16>, NumSections>, kNumRhythmPatterns>;

    const RhythmTable& rhythmTable()
    {
        static const RhythmTable table = []
        {
            RhythmTable t {};
            for (int p = 0; p < kNumRhythmPatterns; ++p)
                for (int s = 0; s < NumSections; ++s)
                {
                    const char* pat = kRhythmPatterns[p].all != nullptr ? kRhythmPatterns[p].all : kRhythmPatterns[p].parts[(size_t) s];
                    if (pat == nullptr)
                        continue;
                    for (int i = 0; i < 16; ++i)
                    {
                        int start = i;
                        while (start > 0 && pat[start] == '-') --start;
                        if (pat[i] == '.' || pat[start] != 'x')
                            continue;
                        int len = 1;
                        while (start + len < 16 && pat[start + len] == '-') ++len;
                        t[(size_t) p][(size_t) s][(size_t) i] = { (int8_t) start, (int8_t) len };
                    }
                }
            return t;
        }();
        return table;
    }

    /** 1 while the pattern's note is sounding at this position (in steps), else 0. */
    float rhythmOpen (int pattern, int section, double stepPos, float length)
    {
        double b = std::fmod (stepPos, 16.0);
        if (b < 0.0) b += 16.0;
        const int step = std::min (15, (int) b);
        const RhythmStep& info = rhythmTable()[(size_t) pattern][(size_t) section][(size_t) step];
        if (info.len == 0)
            return 0.0f;
        const double into = (double) (step - info.start) + (b - (double) step);
        return into < (double) info.len * (double) length ? 1.0f : 0.0f;
    }
}

const char* const* getRhythmPatternNames()
{
    static const char* names[kNumRhythmPatterns] = {};
    for (int i = 0; i < kNumRhythmPatterns; ++i) names[i] = kRhythmPatterns[i].name;
    return names;
}

const char* const* getRhythmRateNames()
{
    static const char* names[] = { "1/8", "1/16", "1/32" };
    return names;
}

const char* const* getRhythmSectionNames()
{
    static const char* names[] = { "Whole Choir", "Sopranos & Altos", "Tenors & Basses" };
    return names;
}

int singersForChoirSize (float percent)
{
    return clampv ((int) std::lround (clampv (percent, 0.0f, 100.0f) / 100.0f * (float) kMaxSingers), 0, kMaxSingers);
}

//==============================================================================
struct ChoirEngine::ControlFrame
{
    int64_t time = 0;
    bool voiced = false;
    bool sounding = false;
    float inputMidi = 0.0f, inputHz = 0.0f;
    int leadNote = -1;
    int sopShift = 0;
    float level = 0.0f;
    bool midiInstrument = false;   // MIDI is playing the choir (no voice): always use the vowel synth
    Voicing voicing;
};

struct ChoirEngine::Singer
{
    int section = 0, index = 0;
    bool active = false;
    float fade = 0.0f;

    // placement
    float panL = 0.7071f, panR = 0.7071f, depthGain = 1.0f;
    int delay = 0, pendingDelay = 0;

    // fixed random character
    float rDetune = 0, rVibDepth = 1, rVibRate = 0, rDelay = 0, rGain = 1, rFormant = 1, rGlide = 1, rScoop = 1, rAttack = 1;
    Wander drift, shimmer;
    Rng rng;

    // pitch state
    float currentMidi = -1.0f, targetMidi = -1.0f, noteAge = 0.0f, scoop = 0.0f, expr = 0.0f, vibPhase = 0.0f;
    bool doublesMelody = false;
    float octaveShift = 0.0f;
    float lastFollow = 1.0f;

    // amplitude
    float amp = 0.0f, ampTarget = 0.0f, gate = 0.0f;

    PsolaVoice psola;
    PsolaControl pctl;
    VowelSinger vowel;
    SamplePlayer sampler;
    float synthF0 = 220.0f;
    float sampleHz = 220.0f;     // recorded voices carry their own vibrato: less is added
    float rJitter = 0.0f;
};

ChoirEngine::ChoirEngine() = default;
ChoirEngine::~ChoirEngine() = default;

//==============================================================================
void ChoirEngine::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    maxBlock = std::max (1, maxBlockSize);

    PitchTracker::Settings ts;
    ts.minHz = kMinInputHz;
    ts.maxHz = kMaxInputHz;
    tracker.prepare (sr, ts);

    const int maxPeriod = (int) std::ceil (sr / kMinInputHz) + 2;
    latency = maxPeriod + std::max (tracker.getAnalysisDelay(), (int) (1.25f * (float) maxPeriod))
              + maxPeriod / 2 + kChunk + 16;
    // Vowel singers only need the pitch analysis (no input grains), so Live mode runs them much closer.
    liveLatency = tracker.getAnalysisDelay() + kChunk;

    const int maxSingerDelay = (int) (sr * 0.075);
    const int echoHistory = (int) (sr * (kMaxEchoSeconds + 1.0));
    analyzer.prepare (sr, maxPeriod, latency + maxSingerDelay + kChunk * 2 + echoHistory);

    glottal.prepare (sr);

    singers.clear();
    for (int s = 0; s < NumSections; ++s)
    {
        for (int i = 0; i < kSingersPerSection; ++i)
        {
            auto singer = std::make_unique<Singer>();
            singer->section = s;
            singer->index = i;
            singer->psola.prepare (sr, kChunk, maxPeriod);
            singer->vowel.prepare (sr);
            singers.push_back (std::move (singer));
        }
    }

    control.assign (4096, ControlFrame{});   // ~20 s of control history (echo replays read the past)
    controlMask = 4095;

    leadDelayL.assign ((size_t) latency, 0.0f);
    leadDelayR.assign ((size_t) latency, 0.0f);

    mono.assign (kChunk, 0.0f);
    sectionBuf.assign ((size_t) NumSections * 2 * kChunk, 0.0f);
    choirL.assign (kChunk, 0.0f);
    choirR.assign (kChunk, 0.0f);
    revInL.assign (kChunk, 0.0f);
    revInR.assign (kChunk, 0.0f);
    revOutL.assign (kChunk, 0.0f);
    revOutR.assign (kChunk, 0.0f);
    scratch.assign (kChunk, 0.0f);

    tone.prepare (sr);
    glue.prepare (sr);
    reverb.prepare (sr);
    limiter.prepare (sr);

    // Smoothed per sample, advanced a chunk at a time (chunks are shorter with small host blocks).
    const float smoothS = 0.03f;
    volume.prepare (sr, smoothS, 1.0f);
    leadGain.prepare (sr, smoothS, 1.0f);
    choirGain.prepare (sr, smoothS, 1.0f);
    wetGain.prepare (sr, smoothS, 0.6f);
    dryChoirGain.prepare (sr, smoothS, 0.7f);
    leadSend.prepare (sr, smoothS, 0.1f);
    warmthAmt.prepare (sr, smoothS, 0.0f);
    for (int s = 0; s < NumSections; ++s)
    {
        sectionGain[(size_t) s].prepare (sr, smoothS, 1.0f);
        sectionPanL[(size_t) s].prepare (sr, smoothS, 1.0f);
        sectionPanR[(size_t) s].prepare (sr, smoothS, 1.0f);
        sectionWidth[(size_t) s].prepare (sr, smoothS, 1.0f);
    }

    prepared = true;
    lastSingerTarget = -1;
    lastVocal = VocalType::Count;
    lastStyle = Style::Count;
    lastFormantParam = 1.0e9f;
    lastHumanize = -1.0f;
    lastSpace = -1.0f;
    reset();
    setParams (params);
}

void ChoirEngine::reset()
{
    leadCentre = -1.0f;
    leadVoice = -1;
    shapedVoice = -2;
    tracker.reset();
    analyzer.reset();
    harmonizer.reset();
    keyTracker.reset();
    controlCount = 0;
    std::fill (leadDelayL.begin(), leadDelayL.end(), 0.0f);
    std::fill (leadDelayR.begin(), leadDelayR.end(), 0.0f);
    leadDelayPos = 0;
    for (auto& s : singers)
    {
        s->active = false;
        s->fade = 0.0f;
        s->amp = s->ampTarget = s->gate = 0.0f;
        s->currentMidi = s->targetMidi = -1.0f;
        s->psola.reset (0.0);
        s->vowel.reset();
        s->sampler.reset();
    }
    sectionCount.fill (0);
    lastSingerTarget = -1;
    tone.reset();
    glue.reset();
    reverb.reset();
    limiter.reset();
    lastLevel = 0.0f;
    unvoicedTime = 10.0f;
    lastVoiced = false;
    wasFrozen = false;
    stemTapOffset = 0;
    chunkCounter = 0;
    replayCount = 0;
    phraseStart = -1;
    lastVoicedTime = 0;
    activeEchoOffset = 0;
    echoDuck = 1.0f;
    sectionNorm.fill (-1.0f);
    gateGain.fill (1.0f);
    freeBeat = 0.0;
}

void ChoirEngine::scheduleReplay (int64_t start, int64_t end)
{
    const int64_t maxLen = (int64_t) (kMaxEchoSeconds * sr);
    end += (int64_t) (0.12 * sr);                // keep the last consonant
    start = std::max (start - (int64_t) (0.05 * sr), end - maxLen);
    const int64_t gap = (int64_t) (0.12 * sr);
    int64_t offset = end - start + gap;
    if (replayCount > 0)
    {
        // After the previous answer, with a moment for its last note to die away.
        const Replay& prev = replays[(size_t) ((replayCount - 1) % (int) replays.size())];
        offset = std::max (offset, prev.srcEnd + prev.offset + (int64_t) (0.25 * sr) - start);
    }
    // The phrase end is only known a moment later; start the answer from its beginning anyway.
    offset = std::max (offset, analyzer.getWritten() - choirLatency() + kChunk - start);
    // Never echo further back than the history we keep.
    offset = std::min (offset, maxLen);
    replays[(size_t) (replayCount % (int) replays.size())] = { start, end, offset };
    ++replayCount;
}

const ChoirEngine::Replay* ChoirEngine::findReplay (int64_t t) const
{
    // Newest first: once the next answer starts, it takes over from the previous one's tail.
    const int64_t tail = (int64_t) (1.5 * sr);
    for (int i = replayCount - 1; i >= std::max (0, replayCount - (int) replays.size()); --i)
    {
        const Replay& r = replays[(size_t) (i % (int) replays.size())];
        if (t >= r.srcStart + r.offset && t <= r.srcEnd + r.offset + tail)
            return &r;
    }
    return nullptr;
}

void ChoirEngine::setSampleSet (const SampleSet* set) noexcept
{
    const uint64_t id = set != nullptr ? set->id : 0;
    if (set == sampleSet && id == sampleSetId)
        return;
    sampleSet = set;
    sampleSetId = id;
    for (auto& s : singers)
        s->sampler.reset();   // they may point into the previous set
}

//==============================================================================
void ChoirEngine::updateVoiceCharacter()
{
    const int voice = leadVoice < 0 ? 0 : leadVoice;
    const int shape = voice + (ens.trebles ? 2 : 0);
    if (shape == shapedVoice)
        return;
    shapedVoice = shape;

    // Vocal tract length relative to the lead (formant scale), and tone per section.
    // A low (male) lead: sopranos and altos get a shorter, female tract, lose the male
    // "ring" around 3 kHz and some chest. A high (female) lead: tenors and basses get a longer
    // tract, the singer's formant and more chest.
    struct Shape { float tract, highDb, ringDb, lowDb, shelfHz, ringHz; };
    // Tuned against SATB singing-voice formant data over five vowels: a male source sung by the
    // sopranos/altos moves toward the female voices and well away from a pitched-up man.
    static const Shape fromMale[] = { { 1.2f, -12.0f, -6.0f, -2.5f, 2200.0f, 3200.0f }, { 1.14f, -9.0f, -5.0f, -1.5f, 2200.0f, 3200.0f },
                                      { 1.0f, 0, 0, 0, 2200.0f, 3200.0f }, { 0.96f, 0, 0, 0, 2200.0f, 3200.0f } };
    static const Shape fromFemale[] = { { 1.0f, 0, 0, 0, 2400.0f, 2900.0f }, { 0.97f, 0, 0, 0, 2400.0f, 2900.0f },
                                        { 0.86f, 2.0f, 4.0f, 3.0f, 2400.0f, 2900.0f }, { 0.82f, 1.0f, 3.0f, 4.0f, 2400.0f, 2900.0f } };
    // Children: a much shorter tract than either adult, little chest and no singer's formant.
    static const Shape kidsFromMale[] = { { 1.32f, -12.0f, -7.0f, -5.0f, 2200.0f, 3200.0f }, { 1.3f, -11.0f, -7.0f, -5.0f, 2200.0f, 3200.0f },
                                          { 1.3f, -11.0f, -7.0f, -5.0f, 2200.0f, 3200.0f }, { 1.28f, -10.0f, -6.0f, -5.0f, 2200.0f, 3200.0f } };
    static const Shape kidsFromFemale[] = { { 1.12f, -2.0f, -3.0f, -3.0f, 2400.0f, 2900.0f }, { 1.1f, -2.0f, -3.0f, -3.0f, 2400.0f, 2900.0f },
                                            { 1.1f, -2.0f, -3.0f, -3.0f, 2400.0f, 2900.0f }, { 1.08f, -2.0f, -3.0f, -3.0f, 2400.0f, 2900.0f } };
    const Shape* table = ens.trebles ? (voice == 0 ? kidsFromMale : kidsFromFemale) : (voice == 0 ? fromMale : fromFemale);
    for (int s = 0; s < NumSections; ++s)
    {
        const Shape& sh = table[s];
        vocalTract[(size_t) s] = sh.tract;
        auto& vs = voiceShape[(size_t) s];
        const bool wasActive = vs.active;
        vs.active = sh.highDb != 0.0f || sh.ringDb != 0.0f || sh.lowDb != 0.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            vs.high[(size_t) ch].highShelf (sr, sh.shelfHz, sh.highDb, 0.8);
            vs.ring[(size_t) ch].peak (sr, sh.ringHz, 1.4, sh.ringDb);
            vs.low[(size_t) ch].lowShelf (sr, 220.0, sh.lowDb);
            if (! wasActive)   // stale memory from long ago; a running filter keeps its state (no click)
            {
                vs.high[(size_t) ch].reset();
                vs.ring[(size_t) ch].reset();
                vs.low[(size_t) ch].reset();
            }
        }
    }
}

int ChoirEngine::choirLatency() const noexcept
{
    return params.lowLatency && params.vocalType != VocalType::Words ? liveLatency : latency;
}

Scale ChoirEngine::currentScale() const
{
    Scale s;
    s.type = params.scale;
    s.tonic = params.key > 0 ? (params.key - 1) % 12 : keyTonic;
    return s;
}

void ChoirEngine::setParams (const EngineParams& in)
{
    // A host can send anything: no NaN or infinity may reach the DSP (it would stay there).
    EngineParams p = in;
    {
        const EngineParams d;
        auto fix = [] (float& v, float def) { if (! isFiniteSample (v)) v = def; };
        fix (p.volumeDb, d.volumeDb); fix (p.choirSize, d.choirSize); fix (p.space, d.space);
        fix (p.reverb, d.reverb); fix (p.toneDb, d.toneDb); fix (p.presence, d.presence);
        fix (p.leadDb, d.leadDb); fix (p.choirDb, d.choirDb); fix (p.humanize, d.humanize);
        fix (p.vibrato, d.vibrato); fix (p.glide, d.glide); fix (p.formant, d.formant);
        fix (p.pitchCorrection, d.pitchCorrection); fix (p.breath, d.breath); fix (p.harmonySpeed, d.harmonySpeed);
        fix (p.decay, d.decay); fix (p.preDelayMs, d.preDelayMs); fix (p.brightness, d.brightness);
        fix (p.leadReverb, d.leadReverb); fix (p.lowCutHz, d.lowCutHz); fix (p.highCutHz, d.highCutHz);
        fix (p.warmth, d.warmth); fix (p.glue, d.glue); fix (p.a4, d.a4);
        p.a4 = clampv (p.a4, 380.0f, 480.0f);
        p.volumeDb = clampv (p.volumeDb, -96.0f, 24.0f);
        p.leadDb = std::min (p.leadDb, 24.0f);
        p.choirDb = std::min (p.choirDb, 24.0f);
        for (size_t s = 0; s < p.sections.size(); ++s)
        {
            fix (p.sections[s].level, d.sections[s].level);
            fix (p.sections[s].pan, d.sections[s].pan);
        }
        fix (p.rhythmLength, d.rhythmLength);
        fix (p.rhythmDepth, d.rhythmDepth);
        if ((int) p.ensemble < 0 || p.ensemble >= Ensemble::Count)
            p.ensemble = Ensemble::Choir;
    }
    for (int s = 0; s < NumSections; ++s)
        p.sections[(size_t) s].range = ensembleRange (p.ensemble, s, p.sections[(size_t) s].range);
    params = p;
    style = getStyleProfile (p.style);

    // The ensemble reshapes how the style is sung.
    ens = EnsembleProfile {};
    switch (p.ensemble)
    {
        case Ensemble::Children:    // light and straight, breathy, quick to scatter in time
            style.vibratoCents *= 0.3f; style.detuneCents *= 0.8f; style.driftCents *= 1.3f; style.timingMs *= 1.3f;
            style.scoopCents *= 0.6f; style.glideMs *= 0.8f; style.releaseS *= 0.9f; style.breath *= 1.6f;
            style.softness = clampv (style.softness + 0.12f, 0.0f, 1.0f);
            ens.formantSpread = 1.3f; ens.tract = 1.18f; ens.zoneOffset = -3.0f; ens.trebles = true;
            break;
        case Ensemble::Gang:        // shouted: pressed and bright, no vibrato, loose and punchy
            style.vibratoCents *= 0.08f; style.detuneCents *= 2.2f; style.driftCents *= 1.6f; style.timingMs *= 3.0f;
            style.scoopCents *= 1.4f; style.glideMs *= 0.5f; style.attackS *= 0.6f; style.releaseS *= 0.6f; style.breath *= 1.4f;
            style.softness = clampv (style.softness - 0.35f, 0.0f, 1.0f);
            ens.formantSpread = 2.5f; ens.drive = 0.45f;
            break;
        case Ensemble::Congregation: // untrained: wide tuning, late entrances, every voice different
            style.vibratoCents *= 0.5f; style.detuneCents *= 3.0f; style.driftCents *= 2.5f; style.timingMs *= 4.0f;
            style.scoopCents *= 2.0f; style.glideMs *= 1.5f; style.attackS *= 1.6f; style.releaseS *= 1.2f; style.breath *= 1.3f;
            style.softness = clampv (style.softness + 0.1f, 0.0f, 1.0f);
            ens.formantSpread = 2.5f;
            break;
        default:
            break;
    }

    volume.setTarget (dbToGain (p.volumeDb));
    leadGain.setTarget (p.leadDb <= -59.5f ? 0.0f : dbToGain (p.leadDb));
    choirGain.setTarget (p.choirDb <= -59.5f ? 0.0f : dbToGain (p.choirDb));
    const float r = clampv (p.reverb / 100.0f, 0.0f, 1.0f);
    wetGain.setTarget (1.15f * std::pow (r, 1.2f));
    dryChoirGain.setTarget (p.wetOnly ? 0.0f : 1.0f - 0.45f * r);
    if (p.wetOnly)
        leadGain.setTarget (0.0f);
    leadSend.setTarget (clampv (p.leadReverb / 100.0f, 0.0f, 1.0f) * 0.8f);
    warmthAmt.setTarget (clampv (p.warmth / 100.0f, 0.0f, 1.0f));

    bool anySolo = false;
    for (const auto& s : p.sections) anySolo = anySolo || s.solo;
    const float width = 0.25f + 0.75f * clampv (p.space / 100.0f, 0.0f, 1.0f);
    for (int s = 0; s < NumSections; ++s)
    {
        const auto& sp = p.sections[(size_t) s];
        const bool audible = ! sp.mute && (! anySolo || sp.solo);
        const float f = clampv (sp.level, 0.0f, 1.0f) / 0.75f;
        sectionGain[(size_t) s].setTarget (audible ? f * f : 0.0f);
        const float pan = clampv (sp.pan, -1.0f, 1.0f);
        sectionPanL[(size_t) s].setTarget (pan > 0.0f ? 1.0f - pan : 1.0f);
        sectionPanR[(size_t) s].setTarget (pan < 0.0f ? 1.0f + pan : 1.0f);
        sectionWidth[(size_t) s].setTarget (width);
    }

    ChoirReverb::Settings rs;
    rs.venue = p.venue;
    rs.decayScale = clampv (p.decay / 100.0f, 0.3f, 2.5f);
    rs.preDelayMs = p.preDelayMs;
    rs.brightness = clampv (p.brightness / 100.0f, 0.0f, 1.0f);
    rs.early = 0.25f + 0.75f * clampv (p.space / 100.0f, 0.0f, 1.0f);
    rs.width = 0.5f + 0.5f * clampv (p.space / 100.0f, 0.0f, 1.0f);
    if (prepared)
    {
        reverb.setSettings (rs);
        tone.set (p.lowCutHz, p.highCutHz, p.toneDb, clampv (p.presence / 100.0f, 0.0f, 1.0f));
    }
}

void ChoirEngine::setMidi (const NoteSet& held, float velocity)
{
    midiHeld = held;
    midiVelocity = velocity;
}

void ChoirEngine::setMidiParts (const std::array<NoteSet, NumSections>& parts, float velocity)
{
    midiParts = parts;
    partsVelocity = velocity;
}

bool ChoirEngine::partsHeld() const noexcept
{
    if (! params.midiEnabled || ! params.midiParts)
        return false;
    for (const auto& p : midiParts)
        if (! p.empty())
            return true;
    return false;
}

float ChoirEngine::singerFormant (const Singer& s) const noexcept
{
    return 1.0f + (s.rFormant - 1.0f) * ens.formantSpread;
}

int ChoirEngine::formantSection (int section) const noexcept
{
    if (! ens.trebles)
        return section;
    return section == Tenor ? Soprano : (section == Bass ? Alto : section);
}

//==============================================================================
void ChoirEngine::configureSinger (Singer& s, int section, int index, int)
{
    s.rng.reseed ((uint32_t) (section * 1000 + index * 7 + 17));
    s.rDetune = s.rng.gauss();
    s.rVibDepth = 0.6f + 0.8f * s.rng.uni();
    s.rVibRate = s.rng.bi() * 0.45f;
    s.rDelay = s.rng.uni();
    s.rGain = dbToGain (s.rng.gauss() * 1.2f);
    s.rFormant = 1.0f + 0.03f * s.rng.gauss();
    s.rGlide = 0.7f + 0.6f * s.rng.uni();
    s.rScoop = 0.5f + s.rng.uni();
    s.rAttack = 0.75f + 0.5f * s.rng.uni();
    s.rJitter = std::fmod ((float) (s.index * 4 + s.section + 1) * 0.6180340f, 1.0f);   // spread starts without using the rng
    s.vibPhase = s.rng.uni();
    s.drift = Wander{};
    s.drift.timeConstant = 0.9f + 0.8f * s.rng.uni();
    s.shimmer = Wander{};
    s.shimmer.timeConstant = 0.35f;

    // Stage placement (audience view): sopranos front-left, altos front-right,
    // tenors back-left, basses back-right. New singers join at alternating edges.
    static const float centre[] = { -0.55f, 0.55f, -0.42f, 0.42f };
    static const float halfWidth[] = { 0.42f, 0.42f, 0.4f, 0.4f };
    const int k = (index + 1) / 2;
    const float side = (index % 2 == 0) ? 1.0f : -1.0f;
    float x = centre[section] + side * halfWidth[section] * clampv ((float) k / 12.0f, 0.0f, 1.0f) + 0.04f * s.rng.bi();
    x = clampv (x, -1.0f, 1.0f);
    const float theta = (x + 1.0f) * 0.25f * kPi;
    s.panL = std::cos (theta);
    s.panR = std::sin (theta);
    const bool backRow = section == Tenor || section == Bass;
    s.depthGain = backRow ? 0.88f : 1.0f;

    s.currentMidi = s.targetMidi = -1.0f;
    s.amp = s.ampTarget = 0.0f;
    s.gate = 0.0f;
    s.noteAge = 0.0f;
    s.scoop = 0.0f;
    s.expr = 0.0f;
    s.lastFollow = 1.0f;
    s.vowel.reset();
    s.vowel.setPhase (s.rng.uni());
}

void ChoirEngine::allocateSingers()
{
    const int target = singersForChoirSize (params.choirSize);
    if (target == lastSingerTarget && params.harmonyMode == lastAllocMode)
        return;
    lastSingerTarget = target;
    lastAllocMode = params.harmonyMode;

    std::array<bool, NumSections> activeSec { true, true, true, true };
    if (params.harmonyMode == HarmonyMode::UpperVoices) activeSec = { true, true, false, false };
    if (params.harmonyMode == HarmonyMode::LowerVoices) activeSec = { false, false, true, true };

    std::array<int, NumSections> counts {};
    int remaining = target;
    // First singers go to the parts that matter most for harmony.
    static const int priority[] = { Alto, Bass, Tenor, Soprano };
    for (int s : priority)
        if (activeSec[(size_t) s] && remaining > 0) { counts[(size_t) s] = 1; --remaining; }

    static const float weights[] = { 0.28f, 0.26f, 0.22f, 0.24f };
    float wsum = 0.0f;
    for (int s = 0; s < NumSections; ++s) if (activeSec[(size_t) s]) wsum += weights[s];
    if (remaining > 0 && wsum > 0.0f)
    {
        std::array<float, NumSections> frac {};
        int given = 0;
        for (int s = 0; s < NumSections; ++s)
        {
            if (! activeSec[(size_t) s]) continue;
            const float share = (float) remaining * weights[s] / wsum;
            const int whole = (int) std::floor (share);
            counts[(size_t) s] += whole;
            given += whole;
            frac[(size_t) s] = share - (float) whole;
        }
        for (int left = remaining - given; left > 0; --left)
        {
            int best = -1;
            for (int s = 0; s < NumSections; ++s)
                if (activeSec[(size_t) s] && (best < 0 || frac[(size_t) s] > frac[(size_t) best])) best = s;
            if (best < 0) break;
            counts[(size_t) best] += 1;
            frac[(size_t) best] = -1.0f;
        }
    }

    for (int s = 0; s < NumSections; ++s)
    {
        counts[(size_t) s] = std::min (counts[(size_t) s], kSingersPerSection);
        sectionCount[(size_t) s] = counts[(size_t) s];
        for (int i = 0; i < kSingersPerSection; ++i)
        {
            Singer& singer = *singers[(size_t) (s * kSingersPerSection + i)];
            const bool should = i < counts[(size_t) s];
            if (should && ! singer.active)
            {
                if (singer.fade <= 0.0f)
                {
                    configureSinger (singer, s, i, counts[(size_t) s]);
                    singer.vowel.setFormants (getFormants (vowelForVocalType (params.vocalType == VocalType::Words ? VocalType::Ah : params.vocalType), formantSection (s)),
                                              singerFormant (singer) * ens.tract * std::exp2 (clampv (params.formant, -100.0f, 100.0f) / 100.0f * 0.3f));
                    singer.vowel.normalise (glottal, midiToHz (params.sections[(size_t) s].range.centre(), params.a4), style.softness);
                    const float delayMs = singer.rDelay * style.timingMs * params.humanize / 100.0f
                                        + ((s == Tenor || s == Bass) ? 2.5f : 0.0f) * params.space / 100.0f;
                    singer.delay = singer.pendingDelay = (int) (delayMs * 0.001f * (float) sr);
                    singer.psola.reset ((double) (analyzer.getWritten() - latency - activeEchoOffset - singer.delay));
                }
                singer.active = true;
            }
            else if (! should)
            {
                singer.active = false;
            }
        }
    }
}

void ChoirEngine::refreshVowelFormants()
{
    const VocalType vt = params.vocalType == VocalType::Words ? VocalType::Ah : params.vocalType;
    const Vowel v = vowelForVocalType (vt);
    const float shift = std::exp2 (clampv (params.formant, -100.0f, 100.0f) / 100.0f * 0.3f);
    for (auto& sp : singers)
    {
        Singer& s = *sp;
        s.vowel.setFormants (getFormants (v, formantSection (s.section)), singerFormant (s) * ens.tract * shift);
        s.vowel.normalise (glottal, midiToHz (params.sections[(size_t) s.section].range.centre(), params.a4), style.softness);
    }
}

//==============================================================================
void ChoirEngine::handlePitchFrame (const PitchFrame& f)
{
    analyzer.addPitchFrame (f);
    const float hop = (float) tracker.getHop() / (float) sr;

    PitchFrame g = f;
    if (g.voiced)
        g.midi = hzToMidi (g.hz, params.a4);

    if (params.key == 0)
    {
        if (g.voiced)
            keyTracker.addNote ((int) std::lround (g.midi), hop, g.clarity);
        const bool minorFamily = Scale { 0, params.scale }.isMinorFamily();
        keyTonic = keyTracker.update (hop, minorFamily);
        if (keyTracker.getConfidence() >= 0.35f)
            detectedKey.store (keyTonic + (minorFamily ? 12 : 0), std::memory_order_relaxed);
    }

    const Scale scale = currentScale();
    ControlFrame cf;
    cf.time = f.centre;
    cf.voiced = g.voiced;
    cf.inputMidi = g.voiced ? g.midi : lastInputMidi;
    cf.inputHz = g.voiced ? g.hz : midiToHz (lastInputMidi, params.a4);
    if (g.voiced)
    {
        lastInputMidi = g.midi;
        // Is the lead a low (male) or high (female) voice? Slow average with hysteresis.
        leadCentre = leadCentre < 0.0f ? g.midi : leadCentre + (g.midi - leadCentre) * (1.0f - std::exp (-hop / 6.0f));
        if (leadVoice != 0 && leadCentre < 58.0f) leadVoice = 0;
        else if (leadVoice != 1 && leadCentre > 62.0f) leadVoice = 1;
        else if (leadVoice < 0) leadVoice = leadCentre < 60.0f ? 0 : 1;
    }

    const bool frozen = params.freeze;
    if (scoreSource != nullptr)
    {
        Voicing v;
        int lead = -1, sop = 0;
        if (scoreSource->voicingAt (f.centre, v, lead, sop))
        {
            cf.voicing = v;
            cf.leadNote = lead;
            cf.sopShift = sop;
        }
        else if (controlCount > 0)
        {
            cf.voicing = control[(size_t) ((controlCount - 1) & controlMask)].voicing;
        }
    }
    else
    {
        HarmonyContext ctx;
        ctx.scale = scale;
        ctx.mode = params.harmonyMode;
        ctx.style = params.style;
        for (int s = 0; s < NumSections; ++s)
        {
            ctx.ranges[(size_t) s] = params.sections[(size_t) s].range;
            ctx.active[(size_t) s] = sectionCount[(size_t) s] > 0;
        }
        ctx.wordsMode = params.vocalType == VocalType::Words;
        ctx.freeze = frozen;
        ctx.midi = params.midiEnabled ? midiHeld : NoteSet{};
        ctx.midiControlsHarmony = params.midiEnabled;
        ctx.speed = clampv (params.harmonySpeed / 100.0f, 0.0f, 1.0f);
        harmonizer.process (g, ctx, hop);
        cf.voicing = harmonizer.getVoicing();
        cf.leadNote = harmonizer.getLeadNote();
        cf.sopShift = harmonizer.getSopranoShift();
    }

    // MIDI parts: channels 1-4 tell each section exactly what to sing.
    const bool parts = partsHeld();
    if (parts)
    {
        Voicing v;
        for (int s = 0; s < NumSections; ++s)
        {
            const NoteSet& ns = midiParts[(size_t) s];
            if (ns.count == 1)
                v.set (s, ns.notes[0]);
            else if (ns.count >= 2)
                v.setDivisi (s, ns.notes[(size_t) ns.count - 1], ns.notes[0]);   // top and bottom note
        }
        v.valid = true;
        cf.voicing = v;
        cf.leadNote = -1;
        cf.sopShift = 0;
    }

    // Envelope and gate: sing while the lead sings, hold briefly through consonants.
    const float level = std::pow (clampv ((f.rmsDb + 48.0f) / 33.0f, 0.0f, 1.0f), 0.8f);
    if (g.voiced)
    {
        unvoicedTime = 0.0f;
        lastLevel = std::max (level, 0.05f);
    }
    else
    {
        unvoicedTime += hop;
    }
    cf.sounding = unvoicedTime < 0.15f;
    cf.level = cf.sounding ? (g.voiced ? lastLevel : std::max (level, lastLevel * 0.8f)) : 0.0f;

    // Phrase boundaries (for echo / call & response).
    if (g.voiced)
    {
        if (phraseStart < 0)
            phraseStart = f.centre;
        lastVoicedTime = f.centre;
    }
    else if (phraseStart >= 0 && unvoicedTime > 0.35f)
    {
        if (params.response == 1)
            scheduleReplay (phraseStart, lastVoicedTime);
        phraseStart = -1;
    }

    // MIDI on its own plays the choir like an instrument.
    if (params.midiEnabled && (! midiHeld.empty() || parts) && unvoicedTime > 0.15f)
    {
        cf.sounding = true;
        cf.level = 0.18f + 0.4f * std::max (parts ? partsVelocity : 0.0f, midiHeld.empty() ? 0.0f : midiVelocity);
        cf.midiInstrument = true;
    }

    if (frozen)
    {
        if (! wasFrozen)
            frozenLevel = std::max (lastLevel, 0.55f);
        cf.sounding = true;
        cf.level = frozenLevel;
        if (controlCount > 0)
        {
            const auto& prev = control[(size_t) ((controlCount - 1) & controlMask)];
            cf.voicing = prev.voicing;
            cf.leadNote = prev.leadNote;
            cf.sopShift = prev.sopShift;
            cf.inputHz = prev.inputHz;
            cf.inputMidi = prev.inputMidi;
        }
    }

    // On a voice onset, open the gate slightly early on frames not yet played (the
    // output runs behind the analysis), so leading consonants and breaths come through.
    if (g.voiced && ! lastVoiced && ! frozen)
    {
        const int64_t lookahead = std::min<int64_t> ((int64_t) (0.03 * sr), (int64_t) (choirLatency() - tracker.getAnalysisDelay() - 2 * kChunk));
        for (int64_t i = controlCount - 1; i >= 0 && i > controlCount - 32; --i)
        {
            auto& prev = control[(size_t) (i & controlMask)];
            if (prev.time < f.centre - lookahead)
                break;
            prev.sounding = true;
            prev.level = std::max (prev.level, lastLevel * 0.6f);
            prev.voicing = cf.voicing;
            prev.leadNote = cf.leadNote;
            prev.sopShift = cf.sopShift;
        }
    }
    lastVoiced = g.voiced;

    control[(size_t) (controlCount & controlMask)] = cf;
    ++controlCount;
}

const ChoirEngine::ControlFrame& ChoirEngine::controlAt (int64_t t) const
{
    static const ControlFrame empty;
    if (controlCount == 0)
        return empty;
    const int64_t newest = controlCount - 1;
    const int64_t oldest = std::max<int64_t> (0, controlCount - (controlMask + 1));
    if (control[(size_t) (oldest & controlMask)].time > t)
        return control[(size_t) (oldest & controlMask)];
    int64_t lo = oldest, hi = newest;
    while (lo < hi)
    {
        const int64_t mid = (lo + hi + 1) / 2;
        if (control[(size_t) (mid & controlMask)].time <= t) lo = mid;
        else hi = mid - 1;
    }
    return control[(size_t) (lo & controlMask)];
}

//==============================================================================
void ChoirEngine::updateSingerControl (Singer& s, const ControlFrame& cf, float dt)
{
    // Apply a pending timing change only while silent, to avoid glitches.
    if (s.pendingDelay != s.delay && s.gate < 0.001f && s.amp < 0.001f)
    {
        s.delay = s.pendingDelay;
        s.psola.reset ((double) (analyzer.getWritten() - latency - activeEchoOffset - s.delay));
    }

    const int sec = s.section;
    const int cnt = cf.voicing.count[(size_t) sec];
    const int note = cnt > 0 ? cf.voicing.notes[(size_t) sec][(size_t) (s.index % cnt)] : -1;
    const bool singing = cf.sounding && note >= 0;

    if (note >= 0 && (float) note != s.targetMidi)
    {
        const bool fresh = s.currentMidi < 0.0f || (s.amp < 0.02f && s.gate < 0.02f);
        if (fresh || std::abs ((float) note - s.currentMidi) >= 2.0f)
            s.scoop = -style.scoopCents * s.rScoop / 100.0f;
        s.targetMidi = (float) note;
        if (fresh)
            s.currentMidi = (float) note;
        s.noteAge = 0.0f;
    }

    // Is this singer doubling the lead melody (possibly in another octave)?
    s.doublesMelody = cf.leadNote >= 0 && note >= 0 && wrapPc (note - cf.leadNote) == 0
                      && (sec == Soprano || params.harmonyMode == HarmonyMode::Unison);
    s.octaveShift = s.doublesMelody ? (float) (note - cf.leadNote) : 0.0f;

    const float humanize = clampv (params.humanize / 100.0f, 0.0f, 2.0f);
    const float glideT = std::max (0.005f, style.glideMs * 0.001f * clampv (params.glide / 100.0f, 0.0f, 3.0f) * s.rGlide);
    const float gc = std::exp (-dt / (glideT / 3.0f));
    if (s.targetMidi >= 0.0f)
        s.currentMidi = s.targetMidi + (s.currentMidi - s.targetMidi) * gc;
    s.noteAge += dt;
    s.scoop *= std::exp (-dt / 0.06f);

    // Vibrato fades in after the onset delay.
    const float onset = clampv ((s.noteAge - style.vibratoOnsetS) / 0.35f, 0.0f, 1.0f);
    const float vibCents = style.vibratoCents * clampv (params.vibrato / 100.0f, 0.0f, 2.0f) * s.rVibDepth * onset * onset;
    s.vibPhase += (style.vibratoRateHz + s.rVibRate) * dt;
    s.vibPhase -= std::floor (s.vibPhase);
    const float vib = vibCents * std::sin (kTwoPi * s.vibPhase) / 100.0f;
    const float drift = s.drift.tick (s.rng, dt) * style.driftCents * humanize / 100.0f;
    const float detune = s.rDetune * style.detuneCents * humanize / 100.0f;

    const float correction = clampv (params.pitchCorrection / 100.0f, 0.0f, 1.0f);
    const float exprTarget = (cf.voiced && cf.leadNote >= 0) ? clampv (cf.inputMidi - (float) cf.leadNote, -0.4f, 0.4f) * (1.0f - correction) : 0.0f;
    s.expr += (exprTarget - s.expr) * (1.0f - std::exp (-dt / 0.04f));

    const float midi = s.currentMidi + detune + drift + vib + s.scoop + s.expr;
    const float hz = midiToHz (midi, params.a4);
    const float melodyMicro = detune + drift + vib * 0.35f + s.scoop;

    // Words: PSOLA controls.

    const float formantParam = std::exp2 (clampv (params.formant, -100.0f, 100.0f) / 100.0f * 0.3f);
    if (cf.voiced && cf.inputHz > 30.0f)
        s.lastFollow = clampv (std::pow (hz / cf.inputHz, 0.08f), 0.9f, 1.12f);
    if (s.doublesMelody && correction < 0.999f)
    {
        s.pctl.fixedRatio = std::exp2 ((s.octaveShift + melodyMicro) / 12.0f);
    }
    else
    {
        s.pctl.fixedRatio = 0.0f;
        s.pctl.targetHz = hz;
    }
    s.pctl.formant = vocalTract[(size_t) sec] * singerFormant (s) * formantParam * s.lastFollow;
    s.pctl.gain = s.rGain * s.depthGain;
    s.pctl.unvoicedGain = 0.6f;

    // Vowels: synth pitch and amplitude.
    const bool followLead = s.doublesMelody && cf.voiced && correction < 0.999f;
    if (followLead)
        s.synthF0 = cf.inputHz * std::exp2 ((s.octaveShift + melodyMicro) / 12.0f);
    else
        s.synthF0 = hz;
    // Recorded voices already have their own vibrato: add only a touch on top.
    s.sampleHz = s.synthF0 * std::exp2 (-0.75f * (followLead ? vib * 0.35f : vib) / 12.0f);
    const float shimmer = 1.0f + 0.06f * s.shimmer.tick (s.rng, dt) * humanize;
    s.ampTarget = singing ? cf.level * s.rGain * s.depthGain * shimmer : 0.0f;
    s.ampTarget = clampv (s.ampTarget, 0.0f, 1.5f);

    // Fade for joining/leaving the choir.
    const float fadeStep = dt / 0.08f;
    s.fade = clampv (s.fade + (s.active ? fadeStep : -fadeStep), 0.0f, 1.0f);
}

//==============================================================================
void ChoirEngine::process (const float* inL, const float* inR, float* outL, float* outR, int n, const StemPlayback* stems)
{
    if (! prepared)
    {
        std::memset (outL, 0, sizeof (float) * (size_t) n);
        if (outR != nullptr) std::memset (outR, 0, sizeof (float) * (size_t) n);
        return;
    }

    if (const int seed = pendingKeySeed.exchange (-1); seed >= 0)
    {
        keyTracker.seed (seed % 12, seed >= 12, 3.0f);
        keyTonic = seed % 12;
    }

    // Reconfigure on parameter changes that affect the singers.
    allocateSingers();
    const bool styleChanged = params.style != lastStyle || params.ensemble != lastEnsemble;
    if (params.vocalType != lastVocal || params.formant != lastFormantParam || styleChanged)
    {
        lastVocal = params.vocalType;
        lastFormantParam = params.formant;
        refreshVowelFormants();
    }
    if (params.humanize != lastHumanize || params.space != lastSpace || styleChanged)
    {
        lastHumanize = params.humanize;
        lastSpace = params.space;
        lastStyle = params.style;
        lastEnsemble = params.ensemble;
        for (auto& sp : singers)
        {
            const bool back = sp->section == Tenor || sp->section == Bass;
            const float delayMs = sp->rDelay * style.timingMs * params.humanize / 100.0f + (back ? 2.5f : 0.0f) * params.space / 100.0f;
            sp->pendingDelay = (int) (delayMs * 0.001f * (float) sr);
        }
    }

    // Freeze engages on the PSOLA analyser too (Words mode sustains your own voice).
    if (params.freeze != wasFrozen)
    {
        if (params.freeze)
            analyzer.captureFreeze (analyzer.getWritten() - latency, (int) (0.25 * sr));
        else
            analyzer.releaseFreeze();
    }

    // Song position for the rhythm effects: the host's while it plays, otherwise free running.
    beatSynced = transportPlaying && transportBpm > 20.0 && transportBpm < 400.0;
    if (beatSynced)
    {
        blockBpm = lastBpm = transportBpm;
        blockBeat = transportPpq;
    }
    else
    {
        blockBpm = lastBpm;
        blockBeat = freeBeat;
    }

    for (int offset = 0; offset < n; offset += kChunk)
    {
        const int c = std::min (kChunk, n - offset);
        processChunk (inL + offset, inR != nullptr ? inR + offset : nullptr,
                      outL + offset, outR != nullptr ? outR + offset : nullptr, c, stems, offset);
    }
    freeBeat = blockBeat + (double) n * blockBpm / (60.0 * sr);
    wasFrozen = params.freeze;
    publishSnapshot();
}

void ChoirEngine::processChunk (const float* inL, const float* inR, float* outL, float* outR, int c,
                                const StemPlayback* stems, int offset)
{
    const float dt = (float) c / (float) sr;
    ++chunkCounter;

    // One NaN or infinite input sample would stay in the filters and reverb for good.
    float safeL[kChunk], safeR[kChunk];
    for (int i = 0; i < c; ++i)
    {
        safeL[i] = isFiniteSample (inL[i]) ? inL[i] : 0.0f;
        safeR[i] = inR == nullptr ? safeL[i] : (isFiniteSample (inR[i]) ? inR[i] : 0.0f);
    }
    inL = safeL;
    if (inR != nullptr)
        inR = safeR;

    for (int i = 0; i < c; ++i)
    {
        mono[(size_t) i] = inR != nullptr ? 0.5f * (inL[i] + inR[i]) : inL[i];
        inPeakAccum = std::max (inPeakAccum, std::abs (mono[(size_t) i]));
    }

    tracker.push (mono.data(), c);
    for (int k = 0; k < tracker.getNumNewFrames(); ++k)
        handlePitchFrame (tracker.getNewFrame (k));

    analyzer.pushInput (mono.data(), c);
    analyzer.updateMarks();
    const int64_t chunkStart = analyzer.getWritten() - c;
    const int64_t tOut = chunkStart - choirLatency();
    const int64_t tWords = chunkStart - latency;     // PSOLA grains always need the full lookahead

    // Lead voice, delayed to line up with the choir (Live mode passes it straight through).
    float leadL[kChunk], leadR[kChunk];
    const int ld = (int) leadDelayL.size();
    const bool live = params.lowLatency;
    for (int i = 0; i < c; ++i)
    {
        const float l = inL[i], r = inR != nullptr ? inR[i] : inL[i];
        leadL[i] = live ? l : leadDelayL[(size_t) leadDelayPos];
        leadR[i] = live ? r : leadDelayR[(size_t) leadDelayPos];
        leadDelayL[(size_t) leadDelayPos] = l;
        leadDelayR[(size_t) leadDelayPos] = r;
        leadDelayPos = (leadDelayPos + 1) % ld;
    }

    std::fill (sectionBuf.begin(), sectionBuf.end(), 0.0f);
    auto sec = [this] (int s, int ch) { return sectionBuf.data() + (size_t) (s * 2 + ch) * kChunk; };

    if (stems != nullptr)
    {
        for (int s = 0; s < NumSections; ++s)
            for (int ch = 0; ch < 2; ++ch)
                if (const float* src = stems->sections[(size_t) (s * 2 + ch)])
                    std::memcpy (sec (s, ch), src + offset, sizeof (float) * (size_t) c);
    }
    else
    {
        const bool words = params.vocalType == VocalType::Words;
        if (words)
            updateVoiceCharacter();
        const float breath = style.breath * clampv (params.breath / 100.0f, 0.0f, 2.0f) * 0.35f;
        const float attC = onePoleCoeff (style.attackS, sr);
        const float relC = onePoleCoeff (style.releaseS, sr);
        const float gateAtt = onePoleCoeff (0.01f, sr), gateRel = onePoleCoeff (0.12f, sr);

        // Echo: singers perform a phrase from the past, right after it was sung.
        bool silent = false;
        int64_t wantedOffset = 0;
        if (params.response == 1 && ! params.freeze)
        {
            if (const Replay* r = findReplay (tOut))
                wantedOffset = r->offset;
            else if (! (controlCount > 0 && control[(size_t) ((controlCount - 1) & controlMask)].midiInstrument))
                silent = true;
        }
        // Moving to another phrase: fade the singers out over ~12 ms, then restart them there.
        const float duckStep = (float) c / (0.012f * (float) sr);
        float duckFrom = echoDuck, duckTo = echoDuck;
        if (wantedOffset != activeEchoOffset)
        {
            if (echoDuck <= 0.0f)
            {
                activeEchoOffset = wantedOffset;
                for (auto& sp : singers)
                {
                    sp->psola.reset ((double) (tWords - wantedOffset - sp->delay));
                    sp->amp = sp->gate = 0.0f;
                }
                duckFrom = duckTo = 1.0f;
            }
            else
            {
                duckTo = std::max (0.0f, echoDuck - duckStep);
            }
        }
        else
        {
            duckTo = std::min (1.0f, echoDuck + duckStep);
        }
        echoDuck = duckTo;
        const int64_t echoOffset = activeEchoOffset;

        std::array<const SampleLayer*, NumSections> sampleLayers {};
        if (sampleSet != nullptr)
        {
            const VocalType vt = words ? VocalType::Ah : params.vocalType;
            for (int s = 0; s < NumSections; ++s)
                sampleLayers[(size_t) s] = sampleSet->layerFor (formantSection (s), vt);
        }

        // Section level by head count, eased so singers joining or leaving don't step the level
        // of the others (a section going silent keeps its last value while it fades out).
        std::array<float, NumSections> normFrom {}, normTo {};
        const float normK = 1.0f - std::exp (-dt / 0.06f);
        for (int s = 0; s < NumSections; ++s)
        {
            float& cur = sectionNorm[(size_t) s];
            const int count = sectionCount[(size_t) s];
            const float target = count > 0 ? 0.8f / std::sqrt ((float) count) : (cur > 0.0f ? cur : 0.5f);
            if (cur <= 0.0f)
                cur = target;
            normFrom[(size_t) s] = cur;
            cur += (target - cur) * normK;
            normTo[(size_t) s] = cur;
        }

        for (auto& sp : singers)
        {
            Singer& s = *sp;
            if ((! s.active && s.fade <= 0.0f) || ((renderMask >> s.section) & 1) == 0)
                continue;

            const int64_t tSinger = tOut - echoOffset - s.delay;
            const ControlFrame& cf = controlAt (tSinger);
            const float fadeStart = s.fade;
            updateSingerControl (s, cf, dt);
            const float fadeEnd = s.fade;

            std::fill (scratch.begin(), scratch.begin() + c, 0.0f);
            if (words && ! cf.midiInstrument)
            {
                s.psola.render (analyzer, tSinger, c, scratch.data(), s.pctl, s.rng);
                const float gateTarget = cf.sounding && ! silent ? 1.0f : 0.0f;
                for (int i = 0; i < c; ++i)
                {
                    s.gate = gateTarget + (s.gate - gateTarget) * (gateTarget > s.gate ? gateAtt : gateRel);
                    scratch[(size_t) i] *= s.gate;
                }
                s.amp = 0.0f;
            }
            else
            {
                const float att = std::pow (attC, 1.0f / s.rAttack);
                float ampBuf[kChunk];
                float peakAmp = 0.0f;
                for (int i = 0; i < c; ++i)
                {
                    const float target = silent ? 0.0f : s.ampTarget;
                    s.amp = target + (s.amp - target) * (target > s.amp ? att : relC);
                    ampBuf[i] = s.amp;
                    peakAmp = std::max (peakAmp, s.amp);
                }
                if (sampleLayers[(size_t) s.section] != nullptr)
                {
                    // Recorded choir voices.
                    s.sampler.render (*sampleLayers[(size_t) s.section], s.sampleHz, ampBuf, scratch.data(), c, sr, s.index, s.rJitter, ens.zoneOffset);
                    for (int i = 0; i < c; ++i)
                        scratch[(size_t) i] *= kSampleGain;
                }
                else if (peakAmp > 1.0e-4f)
                    s.vowel.render (glottal, s.synthF0, style.softness, breath, s.rng, ampBuf, scratch.data(), c);
                s.gate = 0.0f;
            }

            float* L = sec (s.section, 0);
            float* R = sec (s.section, 1);
            // Fade (joining/leaving), section level and echo hand-over, each ramped over the chunk.
            const float gFrom = normFrom[(size_t) s.section] * fadeStart * duckFrom;
            const float gTo = normTo[(size_t) s.section] * fadeEnd * duckTo;
            const float gInc = (gTo - gFrom) / (float) c;
            float gain = gFrom;
            for (int i = 0; i < c; ++i)
            {
                gain += gInc;
                const float v = scratch[(size_t) i] * gain;
                L[i] += v * s.panL;
                R[i] += v * s.panR;
            }
        }
    }

    // Words mode: give each section the vocal tract and tone of the voices that sing there.
    if (stems == nullptr && params.vocalType == VocalType::Words)
    {
        for (int s = 0; s < NumSections; ++s)
        {
            auto& vs = voiceShape[(size_t) s];
            if (! vs.active)
                continue;
            for (int ch = 0; ch < 2; ++ch)
            {
                float* x = sec (s, ch);
                for (int i = 0; i < c; ++i)
                    x[i] = vs.low[(size_t) ch].process (vs.ring[(size_t) ch].process (vs.high[(size_t) ch].process (x[i])));
            }
        }
    }

    // Dry stems for offline rendering.
    if (stemTap != nullptr)
    {
        for (int ch = 0; ch < NumSections * 2; ++ch)
            if (stemTap[ch] != nullptr)
                std::memcpy (stemTap[ch] + stemTapOffset, sectionBuf.data() + (size_t) ch * kChunk, sizeof (float) * (size_t) c);
        stemTapOffset += c;
    }

    // Rhythm: tempo-synced gates on the sections (live and rendered singers alike).
    applyRhythm (c, offset);

    // Section mix: width, level, balance.
    std::fill (choirL.begin(), choirL.end(), 0.0f);
    std::fill (choirR.begin(), choirR.end(), 0.0f);
    for (int s = 0; s < NumSections; ++s)
    {
        const float g = sectionGain[(size_t) s].advance (c);
        const float pl = sectionPanL[(size_t) s].advance (c), pr = sectionPanR[(size_t) s].advance (c);
        const float w = sectionWidth[(size_t) s].advance (c);
        const float* L = sec (s, 0);
        const float* R = sec (s, 1);
        float peak = 0.0f;
        for (int i = 0; i < c; ++i)
        {
            const float m = 0.5f * (L[i] + R[i]), d = 0.5f * (L[i] - R[i]) * w;
            const float l = (m + d) * g * pl, r = (m - d) * g * pr;
            choirL[(size_t) i] += l;
            choirR[(size_t) i] += r;
            peak = std::max (peak, std::max (std::abs (l), std::abs (r)));
        }
        peakAccum[(size_t) s] = std::max (peakAccum[(size_t) s], peak);
    }

    // Choir bus processing.
    tone.process (choirL.data(), choirR.data(), c);
    const float warmth = std::max (warmthAmt.advance (c), ens.drive);
    if (warmth > 0.001f)
    {
        for (int i = 0; i < c; ++i)
        {
            choirL[(size_t) i] = choirL[(size_t) i] * (1.0f - warmth) + softSaturate (choirL[(size_t) i], warmth) * warmth;
            choirR[(size_t) i] = choirR[(size_t) i] * (1.0f - warmth) + softSaturate (choirR[(size_t) i], warmth) * warmth;
        }
    }
    glue.process (choirL.data(), choirR.data(), c, clampv (params.glue / 100.0f, 0.0f, 1.0f));

    const float cg = choirGain.advance (c), lg = leadGain.advance (c), ls = leadSend.advance (c);
    const float wet = wetGain.advance (c), dry = dryChoirGain.advance (c), vol = volume.advance (c);
    for (int i = 0; i < c; ++i)
    {
        choirL[(size_t) i] *= cg;
        choirR[(size_t) i] *= cg;
        revInL[(size_t) i] = choirL[(size_t) i] + leadL[i] * ls;
        revInR[(size_t) i] = choirR[(size_t) i] + leadR[i] * ls;
    }
    reverb.process (revInL.data(), revInR.data(), revOutL.data(), revOutR.data(), c);

    for (int i = 0; i < c; ++i)
    {
        float l = (leadL[i] * lg + choirL[(size_t) i] * dry + revOutL[(size_t) i] * wet) * vol;
        float r = (leadR[i] * lg + choirR[(size_t) i] * dry + revOutR[(size_t) i] * wet) * vol;
        outL[i] = l;
        if (outR != nullptr) outR[i] = r;
        else outL[i] = 0.5f * (l + r);
    }
    if (outR != nullptr)
        limiter.process (outL, outR, c);
    else
        limiter.process (outL, outL, c);

    for (int i = 0; i < c; ++i)
    {
        if (! isFiniteSample (outL[i])) outL[i] = 0.0f;   // last line of defence for the host
        if (outR != nullptr && ! isFiniteSample (outR[i])) outR[i] = 0.0f;
        outPeakAccum = std::max (outPeakAccum, std::max (std::abs (outL[i]), outR != nullptr ? std::abs (outR[i]) : 0.0f));
    }
}

void ChoirEngine::applyRhythm (int c, int offset)
{
    const int pattern = clampv (params.rhythmPattern, 0, kNumRhythmPatterns - 1);
    const float depth = pattern == 0 ? 0.0f : clampv (params.rhythmDepth / 100.0f, 0.0f, 1.0f);
    if (depth <= 0.0f && *std::min_element (gateGain.begin(), gateGain.end()) >= 0.9999f)
        return;

    const double rate = params.rhythmRate == 0 ? 0.5 : (params.rhythmRate == 2 ? 2.0 : 1.0);
    const double stepsPerSample = 4.0 * rate * blockBpm / (60.0 * sr);
    // Synced to the host, output sample i is heard at song position (block start + i - latency).
    const int lat = beatSynced ? getReportedLatency() : 0;
    const double step0 = blockBeat * 4.0 * rate + (double) (offset - lat) * stepsPerSample;
    const float length = clampv (params.rhythmLength / 100.0f, 0.05f, 1.0f);
    const float att = 1.0f - std::exp (-1.0f / (0.0015f * (float) sr));
    const float rel = 1.0f - std::exp (-1.0f / (0.006f * (float) sr));
    const bool perSection = kRhythmPatterns[pattern].all == nullptr;

    for (int s = 0; s < NumSections; ++s)
    {
        const bool inRhythm = pattern > 0 && (perSection || params.rhythmSections == 0
                                              || (params.rhythmSections == 1 && s <= Alto) || (params.rhythmSections == 2 && s >= Tenor));
        float g = gateGain[(size_t) s];
        float* L = sectionBuf.data() + (size_t) (s * 2) * kChunk;
        float* R = sectionBuf.data() + (size_t) (s * 2 + 1) * kChunk;
        for (int i = 0; i < c; ++i)
        {
            float target = 1.0f;
            if (inRhythm)
                target = 1.0f - depth * (1.0f - rhythmOpen (pattern, s, step0 + (double) i * stepsPerSample, length));
            g += (target - g) * (target > g ? att : rel);
            L[i] *= g;
            R[i] *= g;
        }
        gateGain[(size_t) s] = g;
    }
}

//==============================================================================
void ChoirEngine::publishSnapshot()
{
    const int64_t tOut = analyzer.getWritten() - choirLatency() - activeEchoOffset;
    const ControlFrame& cf = controlAt (tOut);

    snap.totalSingers = 0;
    for (int s = 0; s < NumSections; ++s)
    {
        snap.sectionSingers[(size_t) s] = sectionCount[(size_t) s];
        snap.totalSingers += sectionCount[(size_t) s];
        const bool on = cf.sounding;
        snap.sectionNotes[(size_t) s] = { on && cf.voicing.count[(size_t) s] > 0 ? cf.voicing.notes[(size_t) s][0] : -1,
                                          on && cf.voicing.count[(size_t) s] > 1 ? cf.voicing.notes[(size_t) s][1] : -1 };
    }
    snap.leadVoiced = cf.voiced;
    snap.leadMidi = cf.inputMidi;
    snap.leadNote = cf.leadNote;
    const Scale scale = currentScale();
    snap.keyTonic = scale.tonic;
    snap.keyMinor = scale.isMinorFamily();
    snap.keyConfidence = params.key == 0 ? keyTracker.getConfidence() : 1.0f;
    const std::string& name = harmonizer.getChord().name;
    std::snprintf (snap.chordName, sizeof (snap.chordName), "%s", scoreSource != nullptr ? "" : name.c_str());
    snap.frozen = params.freeze;

    if (! snapLock.test_and_set (std::memory_order_acquire))
    {
        auto peaks = snapShared.sectionPeak;
        const float inPk = snapShared.inputPeak, outPk = snapShared.outputPeak;
        snapShared = snap;
        for (int s = 0; s < NumSections; ++s)
            snapShared.sectionPeak[(size_t) s] = std::max (peaks[(size_t) s], peakAccum[(size_t) s]);
        snapShared.inputPeak = std::max (inPk, inPeakAccum);
        snapShared.outputPeak = std::max (outPk, outPeakAccum);
        snapLock.clear (std::memory_order_release);
        peakAccum.fill (0.0f);
        inPeakAccum = outPeakAccum = 0.0f;
    }
}

EngineSnapshot ChoirEngine::takeSnapshot()
{
    while (snapLock.test_and_set (std::memory_order_acquire)) {}
    EngineSnapshot copy = snapShared;
    snapShared.sectionPeak.fill (0.0f);
    snapShared.inputPeak = snapShared.outputPeak = 0.0f;
    snapLock.clear (std::memory_order_release);
    return copy;
}
} // namespace sc
