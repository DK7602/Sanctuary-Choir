#include "sanctuary/ChoirEngine.h"

#include <cstring>
#include <cstdio>

namespace sc
{
static constexpr int kSingersPerSection = 20;
// Sub-octave: the oktavists, basses an octave below the bass part, take the top bass places
// (a full choir has at most 12 basses, and they get one oktavist for every two).
static constexpr int kSubFirst = 14;
static constexpr int kChunk = 64;
static constexpr float kSampleGain = 1.37f;  // recorded notes are levelled to 0.1 RMS; this matches the synth choir
static constexpr float kMinInputHz = 75.0f;
static constexpr float kMaxInputHz = 1150.0f;
static constexpr double kMaxEchoSeconds = 10.0;

/** Words mode: four singers' voice-print EQs over a chunk, interleaved. */
static void processVoicePrintEqs (const std::array<std::array<Biquad, 6>*, 4>& eqs, float (*x)[kChunk], int lanes, int n) noexcept
{
    // Four singers' cascades interleaved: independent chains the CPU can run together (and
    // the compiler can vectorise). Unused lanes pass silence through an open filter.
    for (size_t k = 0; k < 6; ++k)
    {
        float b0[4], b1[4], b2[4], a1[4], a2[4], z1[4], z2[4];
        for (int l = 0; l < 4; ++l)
        {
            const Biquad q = l < lanes ? (*eqs[(size_t) l])[k] : Biquad{};
            b0[l] = q.b0; b1[l] = q.b1; b2[l] = q.b2; a1[l] = q.a1; a2[l] = q.a2; z1[l] = q.z1; z2[l] = q.z2;
            if (l >= lanes)
                std::fill (x[l], x[l] + n, 0.0f);
        }
        for (int i = 0; i < n; ++i)
            for (int l = 0; l < 4; ++l)
            {
                const float in = x[l][i];
                const float y = b0[l] * in + z1[l];
                z1[l] = b1[l] * in - a1[l] * y + z2[l];
                z2[l] = b2[l] * in - a2[l] * y;
                x[l][i] = y;
            }
        for (int l = 0; l < lanes; ++l)
        {
            (*eqs[(size_t) l])[k].z1 = z1[l];
            (*eqs[(size_t) l])[k].z2 = z2[l];
        }
    }
}

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

SectionParams defaultSectionPlacement (int section)
{
    static const float x[] = { -0.55f, 0.55f, -0.42f, 0.42f };
    static const float width[] = { 0.42f, 0.42f, 0.4f, 0.4f };
    static const float depth[] = { 0.25f, 0.25f, 0.75f, 0.75f };
    const int s = clampv (section, 0, NumSections - 1);
    SectionParams p;
    p.pan = x[s];
    p.width = width[s];
    p.depth = depth[s];
    return p;
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
    std::array<int, NumSections> syllable { -1, -1, -1, -1 };   // typed words: each section's syllable count
};

struct ChoirEngine::Singer
{
    int section = 0, index = 0;
    bool sub = false;            // an oktavist: sings the bass part an octave down
    bool active = false;
    float fade = 0.0f;

    // placement inside the section (its edges are the section bus's left and right)
    float panL = 0.7071f, panR = 0.7071f;
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
    float rJitter = 0.0f;
    float rTake = 0.0f;          // where it stands in the section's share of classical voices (0..1)
    SampleNote sampleNote;       // what the recorded voice sings (it carries its own vibrato: less is added)
    float noteMidi = -1.0f;      // the note sampleNote.serial counts
    float silentFor = 1.0f;      // seconds since the singer last sang
    bool wasSinging = false;

    int source = 0;              // Words mode: 0 sings the input, k the k-th extra pass

    // Words mode: the real singer this one sounds like (a voice print)
    int print = -1, printRound = 0;
    float printFormant = 1.0f;   // formant scale, at the Voices amount
    float wordsVibRate = 0.0f, wordsVibDepth = 1.0f;
    std::array<Biquad, 6> printEq;
    bool printEqOn = false;

    // Typed words
    int syllable = -1;           // the syllable count it is singing (-1 = none)
    float sylAge = 0.0f;         // seconds into it
    bool sylDecided = false;     // whether its consonants are sung from a recording is decided
    bool sylRecorded = false;    // ... and they are
    bool ending = false;         // let go: singing the syllable's closing consonants
    float endAge = 0.0f, endLevel = 0.0f;
    int vowelOverride = -1;      // the vowel its synth is set to for the words (-1 = the Vocal Type)
    float nz1 = 0.0f, nz2 = 0.0f, lp = 0.0f;   // consonant noise filter, darkening low-pass
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
    for (auto& ps : passSources)
    {
        ps.tracker.prepare (sr, ts);
        ps.analyzer.prepare (sr, maxPeriod, latency + maxSingerDelay + kChunk * 2 + echoHistory);
        ps.base = 0;
    }
    activePasses = 0;

    glottal.prepare (sr);

    singers.clear();
    for (int s = 0; s < NumSections; ++s)
    {
        for (int i = 0; i < kSingersPerSection; ++i)
        {
            auto singer = std::make_unique<Singer>();
            singer->section = s;
            singer->index = i;
            singer->sub = s == Bass && i >= kSubFirst;
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
    for (auto* v : { &roomL, &roomR, &roomZeros })
        v->assign (kChunk, 0.0f);
    room = fadingRoom = nullptr;
    roomBlend = fadingGain = 0.0f;
    processedSinceReset = false;
    revOutR.assign (kChunk, 0.0f);
    scratch.assign (kChunk, 0.0f);

    bandDetector.prepare (sr);
    bandScratch.assign (kChunk, 0.0f);
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
        sectionGain[(size_t) s].prepare (sr, smoothS, 1.0f);
    stage.prepare (sr, kChunk);
    leadStage.prepare (sr, kChunk);
    leadPlace.prepare (sr, 0.05f, params.leadOnStage ? 1.0f : 0.0f);
    leadPlacedL.assign (kChunk, 0.0f);
    leadPlacedR.assign (kChunk, 0.0f);

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
    childToneOn = false;   // its filters are set again (for this sample rate)
    tracker.reset();
    analyzer.reset();
    for (auto& ps : passSources)
    {
        ps.tracker.reset();
        ps.analyzer.reset();
        ps.base = 0;
    }
    activePasses = 0;
    harmonizer.reset();
    keyTracker.reset();
    controlCount = 0;
    restartLyrics();
    std::fill (leadDelayL.begin(), leadDelayL.end(), 0.0f);
    std::fill (leadDelayR.begin(), leadDelayR.end(), 0.0f);
    leadDelayPos = 0;
    for (auto& s : singers)
    {
        s->active = false;
        s->fade = 0.0f;
        s->amp = s->ampTarget = s->gate = 0.0f;
        s->currentMidi = s->targetMidi = s->noteMidi = -1.0f;
        s->silentFor = 1.0f;
        s->wasSinging = false;
        s->syllable = -1;
        s->ending = false;
        s->nz1 = s->nz2 = s->lp = 0.0f;
        s->psola.reset (0.0);
        s->vowel.reset();
        s->sampler.reset();
    }
    sectionCount.fill (0);
    lastSingerTarget = -1;
    tone.reset();
    stage.reset();
    leadStage.reset();
    subLevel = 0.0f;
    glue.reset();
    reverb.reset();
    reverbIdle = true;
    room = fadingRoom = nullptr;   // the owner hands a fresh one back
    roomBlend = fadingGain = 0.0f;
    processedSinceReset = false;
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
    bandDetector.reset();
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

void ChoirEngine::setRoom (ConvolutionRoom* r) noexcept
{
    if (r != nullptr && std::abs (r->getSampleRate() - sr) > 0.5)
        r = nullptr;   // made for another sample rate: being rebuilt
    if (r == room)
        return;
    if (! processedSinceReset)
    {
        // Nothing has played yet: start in this room (an export's first note, a fresh session).
        room = r;
        roomLevel = getMeasuredRoomLevel (params.venue);
        roomBlend = r != nullptr ? 1.0f : 0.0f;
        fadingRoom = nullptr;
        fadingGain = 0.0f;
        return;
    }
    // A second change within the 300 ms keeps the louder of the two rooms ringing out.
    if (room != nullptr && (fadingRoom == nullptr || roomBlend * roomLevel >= fadingGain * fadingLevel))
    {
        fadingRoom = room;
        fadingGain = roomBlend;
        fadingLevel = roomLevel;
    }
    room = r;
    roomLevel = getMeasuredRoomLevel (params.venue);
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
    static const Shape kidsFromMale[] = { { 1.26f, -13.0f, -8.0f, -6.0f, 2200.0f, 3200.0f }, { 1.25f, -13.0f, -8.0f, -6.0f, 2200.0f, 3200.0f },
                                          { 1.25f, -13.0f, -8.0f, -6.0f, 2200.0f, 3200.0f }, { 1.24f, -12.0f, -7.0f, -6.0f, 2200.0f, 3200.0f } };
    static const Shape kidsFromFemale[] = { { 1.08f, -6.0f, -5.0f, -4.0f, 2600.0f, 3000.0f }, { 1.07f, -6.0f, -5.0f, -4.0f, 2600.0f, 3000.0f },
                                            { 1.07f, -6.0f, -5.0f, -4.0f, 2600.0f, 3000.0f }, { 1.06f, -6.0f, -5.0f, -4.0f, 2600.0f, 3000.0f } };
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

void ChoirEngine::updateChildTone()
{
    // The recorded voices are trained women: a child has less of everything above about
    // 2.5 kHz (no singer's formant cluster, a softer, more sinusoidal source) and no chest.
    const bool on = ens.trebles && params.vocalType != VocalType::Words;
    if (on == childToneOn)
        return;
    childToneOn = on;
    if (! on)
        return;
    for (auto& t : childTone)
        for (int ch = 0; ch < 2; ++ch)
        {
            t.high[(size_t) ch].highShelf (sr, 2600.0, -6.0, 0.7);
            t.ring[(size_t) ch].peak (sr, 3100.0, 1.2, -4.0);
            t.low[(size_t) ch].lowShelf (sr, 260.0, -3.5);
            t.high[(size_t) ch].reset();
            t.ring[(size_t) ch].reset();
            t.low[(size_t) ch].reset();
        }
}

float ChoirEngine::vibratoShare() const noexcept
{
    // From the style's vibrato as sung (ensemble, energy and Vibrato knob included): none in a
    // chamber choir, a fifth in Sacred, most of a gospel choir.
    const float cents = style.vibratoCents * clampv (params.vibrato / 100.0f, 0.0f, 2.0f);
    return clampv ((cents - 14.0f) / 32.0f, 0.0f, 0.85f);
}

Take ChoirEngine::takeFor (const Singer& s) const noexcept
{
    if (params.singing != Singing::Natural)
        return takeOf (params.singing);
    const float energy = (params.energy - 50.0f) / 50.0f;
    const bool classical = s.rTake < vibratoShare();
    const bool airy = ens.airyEvery > 0 && s.index % ens.airyEvery == 1;
    if (energy >= 0.6f)
    {
        // Singing out: gospel and worship choirs belt, the others open into full voice
        // (chant and children stay in their own voice).
        if (params.style == Style::Gregorian || ens.trebles)
            return airy ? Take::Breathy : Take::Natural;
        const bool belts = params.style == Style::Gospel || params.style == Style::Worship || params.style == Style::Spiritual;
        return belts && ! classical ? Take::Belt : Take::Full;
    }
    if (energy <= -0.6f)
        return classical ? Take::Vibrato : Take::Breathy;
    if (airy)
        return Take::Breathy;
    if (classical)
        return energy >= 0.25f ? Take::Full : Take::Vibrato;
    return Take::Natural;
}

int ChoirEngine::choirLatency() const noexcept
{
    return params.lowLatency && params.vocalType != VocalType::Words ? liveLatency : latency;
}

Scale ChoirEngine::currentScale() const
{
    Scale s;
    s.type = params.scaleAuto ? autoScale : params.scale;
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
        fix (p.realVoices, d.realVoices);
        p.realVoices = clampv (p.realVoices, 0.0f, 150.0f);
        fix (p.energy, d.energy); fix (p.subOctave, d.subOctave);
        p.energy = clampv (p.energy, 0.0f, 100.0f);
        p.subOctave = clampv (p.subOctave, 0.0f, 100.0f);
        fix (p.leadPan, d.leadPan); fix (p.leadDepth, d.leadDepth);
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
            fix (p.sections[s].width, d.sections[s].width);
            fix (p.sections[s].depth, d.sections[s].depth);
        }
        fix (p.micCloseDb, d.micCloseDb); fix (p.micTreeDb, d.micTreeDb); fix (p.micRoomDb, d.micRoomDb);
        fix (p.rhythmLength, d.rhythmLength);
        fix (p.rhythmDepth, d.rhythmDepth);
        if ((int) p.ensemble < 0 || p.ensemble >= Ensemble::Count)
            p.ensemble = Ensemble::Choir;
        if ((int) p.singing < 0 || p.singing >= Singing::Count)
            p.singing = Singing::Natural;
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
            // Children's formants are about 10 % above women's (singing, ages 9-13), not the
            // 20 % of speech: more makes the women's recordings sound sped up. What makes a child
            // is the voice: pure, no singer's ring, no chest, airy (childTone, airyEvery).
            ens.formantSpread = 1.3f; ens.tract = 1.1f; ens.zoneOffset = -1.6f; ens.airyEvery = 3; ens.trebles = true;
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
    // So does the way of singing (the recorded voices switch to breathy or belted takes too).
    switch (p.singing)
    {
        case Singing::Breathy:      // airy and soft, a lighter vibrato
            style.breath *= 2.4f; style.vibratoCents *= 0.7f; style.attackS *= 1.3f;
            style.softness = clampv (style.softness + 0.22f, 0.0f, 1.0f);
            break;
        case Singing::Belt:         // pressed and bright, scooped into the notes
            style.breath *= 0.6f; style.scoopCents *= 1.4f; style.attackS *= 0.8f;
            style.softness = clampv (style.softness - 0.3f, 0.0f, 1.0f);
            break;
        default:
            break;
    }
    // And how hard it sings (50 % leaves the style as it is): softer, airier and slower to
    // speak below; pressed, brighter and louder above, pushing the choir bus at the very top.
    const float energy = (p.energy - 50.0f) / 50.0f;
    const float up = std::max (energy, 0.0f), down = std::max (-energy, 0.0f);
    if (energy != 0.0f)
    {
        style.softness = clampv (style.softness + 0.25f * down - 0.3f * up, 0.0f, 1.0f);
        style.breath *= 1.0f + 0.8f * down - 0.4f * up;
        style.vibratoCents *= 1.0f + 0.25f * energy;
        style.attackS *= 1.0f + 0.5f * down - 0.35f * up;
        style.scoopCents *= 1.0f + 0.4f * up;
        ens.drive = std::max (ens.drive, 0.35f * std::max (0.0f, 2.0f * up - 1.0f));
    }
    energyGain = dbToGain (3.0f * up - 7.0f * down);

    volume.setTarget (dbToGain (p.volumeDb));
    leadGain.setTarget (p.leadDb <= -59.5f ? 0.0f : dbToGain (p.leadDb));
    choirGain.setTarget (p.choirDb <= -59.5f ? 0.0f : dbToGain (p.choirDb) * energyGain);
    const float r = clampv (p.reverb / 100.0f, 0.0f, 1.0f);
    wetGain.setTarget (1.15f * std::pow (r, 1.2f));
    dryChoirGain.setTarget (p.wetOnly ? 0.0f : 1.0f - 0.45f * r);
    if (p.wetOnly)
        leadGain.setTarget (0.0f);
    leadSend.setTarget (clampv (p.leadReverb / 100.0f, 0.0f, 1.0f) * 0.8f);
    warmthAmt.setTarget (clampv (p.warmth / 100.0f, 0.0f, 1.0f));

    bool anySolo = false;
    for (const auto& s : p.sections) anySolo = anySolo || s.solo;
    ChoirStage::Settings stageSettings;
    stageSettings.spread = 0.25f + 0.75f * clampv (p.space / 100.0f, 0.0f, 1.0f);
    for (int s = 0; s < NumSections; ++s)
    {
        const auto& sp = p.sections[(size_t) s];
        const bool audible = ! sp.mute && (! anySolo || sp.solo);
        const float f = clampv (sp.level, 0.0f, 1.0f) / 0.75f;
        sectionGain[(size_t) s].setTarget (audible ? f * f : 0.0f);
        stageSettings.sections[(size_t) s] = { clampv (sp.pan, -1.0f, 1.0f), clampv (sp.width, 0.0f, 1.0f), clampv (sp.depth, 0.0f, 1.0f) };
    }
    auto mic = [] (float db) { return db <= -59.5f ? 0.0f : dbToGain (std::min (db, 12.0f)); };
    stageSettings.close = mic (p.micCloseDb);
    stageSettings.tree = mic (p.micTreeDb);
    stageSettings.room = mic (p.micRoomDb);
    stage.setSettings (stageSettings);

    // Your voice on the stage: a narrow "section" of one, heard by the same mics. The other
    // places stand at the back wall, so the lead itself is never delayed.
    ChoirStage::Settings leadSettings = stageSettings;
    for (auto& place : leadSettings.sections)
        place = { 0.0f, 0.0f, 1.0f };
    leadSettings.sections[0] = { clampv (p.leadPan, -1.0f, 1.0f), 0.04f, clampv (p.leadDepth, 0.0f, 1.0f) };
    leadStage.setSettings (leadSettings);
    leadPlace.setTarget (p.leadOnStage ? 1.0f : 0.0f);

    ChoirReverb::Settings rs;
    rs.venue = p.venue;
    rs.decayScale = clampv (p.decay / 100.0f, 0.3f, 2.5f);
    rs.preDelayMs = p.preDelayMs;
    rs.brightness = clampv (p.brightness / 100.0f, 0.0f, 1.0f);
    rs.early = 0.25f + 0.75f * clampv (p.space / 100.0f, 0.0f, 1.0f);
    rs.width = 0.5f + 0.5f * clampv (p.space / 100.0f, 0.0f, 1.0f);
    roomShape.decayScale = rs.decayScale;
    roomShape.preDelayMs = rs.preDelayMs >= 0.0f ? rs.preDelayMs : 20.0f;
    roomShape.brightness = rs.brightness;
    roomShape.early = rs.early;
    roomShape.width = rs.width;
    if (prepared)
    {
        reverb.setSettings (rs);
        // Sub-Octave opens the Low Cut in proportion, or it would take the oktavists' notes away.
        const float lowCut = p.lowCutHz * (1.0f - (ens.trebles ? 0.0f : 0.6f * clampv (p.subOctave / 100.0f, 0.0f, 1.0f)));
        tone.set (lowCut, p.highCutHz, p.toneDb, clampv (p.presence / 100.0f + 0.2f * energy, 0.0f, 1.0f));
    }
}

void ChoirEngine::setMidi (const NoteSet& held, float velocity, int newNotes)
{
    midiHeld = held;
    midiVelocity = velocity;
    // A chord's notes rarely land together: notes within 40 ms are one syllable.
    if (newNotes > 0 && lyrics != nullptr && midiClock - lastSyllableAt > (int64_t) (0.04 * sr))
    {
        ++chordSyllable;
        lastSyllableAt = midiClock;
    }
}

void ChoirEngine::setMidiParts (const std::array<NoteSet, NumSections>& parts, float velocity, const std::array<int, NumSections>& newNotes)
{
    midiParts = parts;
    partsVelocity = velocity;
    if (lyrics != nullptr)
        for (int s = 0; s < NumSections; ++s)
            if (newNotes[(size_t) s] > 0)
                ++partSyllable[(size_t) s];
}

void ChoirEngine::setLyrics (const LyricScript* script) noexcept
{
    if (script != nullptr && script->entries.empty())
        script = nullptr;
    if (script != lyrics)
    {
        lyrics = script;
        restartLyrics();
    }
}

void ChoirEngine::restartLyrics() noexcept
{
    chordSyllable = -1;
    partSyllable.fill (-1);
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
    s.sub = section == Bass && index >= kSubFirst;
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
    // Evenly spread through the section (golden ratio), so any share of it is that many singers.
    s.rTake = std::fmod ((float) (s.index + 1) * 0.6180340f + (float) section * 0.29f, 1.0f);
    s.vibPhase = s.rng.uni();
    s.drift = Wander{};
    s.drift.timeConstant = 0.9f + 0.8f * s.rng.uni();
    s.print = voicePrintFor (section, index, ens.trebles, &s.printRound);
    for (auto& f : s.printEq)
        f.reset();
    applyVoicePrint (s);
    s.shimmer = Wander{};
    s.shimmer.timeConstant = 0.35f;

    // Place in the section: the first singer in the middle, new ones joining at alternating
    // edges. The section bus's left and right are the section's edges; the stage puts the whole
    // section where it stands (so a rendered take can still be moved).
    const int k = (index + 1) / 2;
    const float side = (index % 2 == 0) ? 1.0f : -1.0f;
    float x = side * clampv ((float) k / 12.0f, 0.0f, 1.0f) + 0.1f * s.rng.bi();
    x = clampv (x, -1.0f, 1.0f);
    const float theta = (x + 1.0f) * 0.25f * kPi;
    s.panL = std::cos (theta);
    s.panR = std::sin (theta);

    s.currentMidi = s.targetMidi = s.noteMidi = -1.0f;
    s.silentFor = 1.0f;
    s.wasSinging = false;
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
    const bool subOn = params.subOctave > 0.0f && ! ens.trebles;
    if (target == lastSingerTarget && params.harmonyMode == lastAllocMode && subOn == lastSubOn)
        return;
    lastSingerTarget = target;
    lastAllocMode = params.harmonyMode;
    lastSubOn = subOn;

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
        counts[(size_t) s] = std::min (counts[(size_t) s], s == Bass ? kSubFirst : kSingersPerSection);
        sectionCount[(size_t) s] = counts[(size_t) s];
        // The oktavists stand with the basses: one for every two (at least one).
        const int subs = s == Bass && subOn && counts[(size_t) s] > 0
                             ? std::min (kSingersPerSection - kSubFirst, std::max (1, (counts[(size_t) s] + 1) / 2)) : 0;
        for (int i = 0; i < kSingersPerSection; ++i)
        {
            Singer& singer = *singers[(size_t) (s * kSingersPerSection + i)];
            const bool should = i < counts[(size_t) s] || (i >= kSubFirst && i < kSubFirst + subs);
            if (should && ! singer.active)
            {
                if (singer.fade <= 0.0f)
                {
                    configureSinger (singer, s, i, counts[(size_t) s]);
                    singer.vowel.setFormants (getFormants (vowelForVocalType (params.vocalType == VocalType::Words ? VocalType::Ah : params.vocalType), formantSection (s)),
                                              singerFormant (singer) * ens.tract * std::exp2 (clampv (params.formant, -100.0f, 100.0f) / 100.0f * 0.3f));
                    singer.vowel.normalise (glottal, midiToHz (params.sections[(size_t) s].range.centre() - (singer.sub ? 12 : 0), params.a4), style.softness);
                    singer.vowelOverride = -1;
                    const float delayMs = singer.rDelay * style.timingMs * params.humanize / 100.0f;
                    singer.delay = singer.pendingDelay = (int) (delayMs * 0.001f * (float) sr);
                    int64_t base = 0;
                    const auto& an = sourceOf (singer, base);
                    singer.psola.reset ((double) (an.getWritten() - latency - activeEchoOffset - singer.delay));
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

void ChoirEngine::articulate (Singer& s, const Articulation& a0, const Articulation& a1, float* x, int n)
{
    // Nasals and liquids darken the voice.
    if (a0.lowpass > 0.0f || a1.lowpass > 0.0f)
    {
        for (int i = 0; i < n; ++i)
        {
            const float t = (float) (i + 1) / (float) n;
            const float f0 = a0.lowpass > 0.0f ? a0.lowpass : 16000.0f, f1 = a1.lowpass > 0.0f ? a1.lowpass : 16000.0f;
            const float k = 1.0f - std::exp (-kTwoPi * (f0 + (f1 - f0) * t) / (float) sr);
            s.lp += k * (x[i] - s.lp);
            x[i] = s.lp;
        }
    }
    else
    {
        s.lp = x[n - 1];
    }

    // Hiss and bursts: band-passed noise at the singer's own loudness (held from the moment
    // the note was let go for the closing consonants).
    if (a0.noise <= 0.0f && a1.noise <= 0.0f)
        return;
    const Articulation& shape = a1.noise > 0.0f ? a1 : a0;
    const float f = clampv (shape.noiseHz, 200.0f, (float) sr * 0.45f);
    const float q = std::max (0.3f, shape.noiseQ);
    const float g = std::tan (kPi * f / (float) sr), k = 1.0f / q;
    const float c1 = 1.0f / (1.0f + g * (g + k)), c2 = g * c1, c3 = g * c2;
    // Same loudness whatever the band: white noise through a narrower band carries less.
    const float norm = std::min (8.0f, std::sqrt (0.5f * (float) sr / (1.57f * f / q)));
    const float level = (s.ending ? s.endLevel : s.ampTarget) * 0.12f * 0.5f * norm * 1.73f;
    for (int i = 0; i < n; ++i)
    {
        const float t = (float) (i + 1) / (float) n;
        const float in = s.rng.uni() * 2.0f - 1.0f;
        const float v3 = in - s.nz2;
        const float v1 = c1 * s.nz1 + c2 * v3;
        const float v2 = s.nz2 + c2 * s.nz1 + c3 * v3;
        s.nz1 = 2.0f * v1 - s.nz1;
        s.nz2 = 2.0f * v2 - s.nz2;
        x[i] += k * v1 * level * (a0.noise + (a1.noise - a0.noise) * t);
    }
}

void ChoirEngine::applyVoicePrint (Singer& s)
{
    int count = 0;
    const VoicePrint* prints = getVoicePrints (count);
    const float a = clampv (params.realVoices / 100.0f, 0.0f, 1.5f);
    if (s.print < 0 || s.print >= count || a <= 0.0f)
    {
        s.printFormant = 1.0f;
        s.wordsVibRate = s.rVibRate;
        s.wordsVibDepth = s.rVibDepth;
        s.printEqOn = false;
        return;
    }
    const VoicePrint& p = prints[s.print];
    // A print used a second time is made a touch different, as two singers would be. The
    // ensemble's spread (a congregation's voices differ more) widens the vocal tracts' range.
    const float again = s.printRound > 0 ? 1.0f + 0.018f * (float) ((s.index * 37 + s.section * 11) % 7 - 3) / 3.0f : 1.0f;
    const float spread = std::sqrt (std::max (1.0f, ens.formantSpread));
    s.printFormant = 1.0f + (p.formantScale * again - 1.0f) * a * spread;
    const float rate = p.vibratoHz - meanVibratoHz (p.female);
    const float depth = clampv (p.vibratoCents / std::max (1.0f, meanVibratoCents (p.female)), 0.6f, 1.5f);
    const float b = std::min (1.0f, a);
    s.wordsVibRate = s.rVibRate * (1.0f - b) + rate * b;
    s.wordsVibDepth = s.rVibDepth * (1.0f - b) + depth * b;
    // Children borrow the women's colour, more gently; repeats a little less of it.
    const float eqAmount = a * (ens.trebles ? 0.6f : 1.0f) * (s.printRound > 0 ? 0.8f : 1.0f);
    const auto& bands = getVoicePrintBands();
    for (size_t k = 0; k < bands.size(); ++k)
    {
        const float g = p.eqDb[k] * eqAmount;
        switch (bands[k].kind)
        {
            case VoicePrintBand::LowShelf:  s.printEq[k].lowShelf (sr, bands[k].hz, g); break;
            case VoicePrintBand::HighShelf: s.printEq[k].highShelf (sr, bands[k].hz, g); break;
            default:                        s.printEq[k].peak (sr, bands[k].hz, bands[k].q, g); break;
        }
    }
    s.printEqOn = true;
}

const PsolaAnalyzer& ChoirEngine::sourceOf (const Singer& s, int64_t& base) const noexcept
{
    if (s.source > 0 && s.source <= activePasses)
    {
        const auto& ps = passSources[(size_t) s.source - 1];
        base = ps.base;
        return ps.analyzer;
    }
    base = 0;
    return analyzer;
}

void ChoirEngine::spreadOverPasses()
{
    // Each section's singers take the input and the passes in turn, starting from a different
    // one in each section, so every pass is heard in every section.
    for (auto& sp : singers)
    {
        Singer& s = *sp;
        const int source = activePasses > 0 ? (s.index + s.section) % (activePasses + 1) : 0;
        if (source == s.source)
            continue;
        s.source = source;
        int64_t base = 0;
        const auto& an = sourceOf (s, base);
        s.psola.reset ((double) (an.getWritten() - latency - activeEchoOffset - s.delay));
    }
}

void ChoirEngine::setSingerVowel (Singer& s, VocalType vt)
{
    const float shift = std::exp2 (clampv (params.formant, -100.0f, 100.0f) / 100.0f * 0.3f);
    s.vowel.setFormants (getFormants (vowelForVocalType (vt), formantSection (s.section)), singerFormant (s) * ens.tract * shift);
    s.vowel.normalise (glottal, midiToHz (params.sections[(size_t) s.section].range.centre() - (s.sub ? 12 : 0), params.a4), style.softness);
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
        s.vowel.normalise (glottal, midiToHz (params.sections[(size_t) s.section].range.centre() - (s.sub ? 12 : 0), params.a4), style.softness);
        s.vowelOverride = -1;
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

    // The band's chord, if the choir follows it.
    const Chord* band = params.followBand && bandDetector.hearing() ? bandDetector.chord() : nullptr;

    if (params.key == 0 || params.scaleAuto)
    {
        if (g.voiced)
            keyTracker.addNote ((int) std::lround (g.midi), hop, g.clarity);
        if (band != nullptr)   // the band's chords say a lot about the key too
            for (int i = 0; i < band->numTones; ++i)
                keyTracker.addNote (band->tones[(size_t) i], hop, i == 0 ? 0.5f : 0.3f);
        // Scale Auto listens for major or minor too (only that, when the key is set by hand).
        const KeyFamily family = params.scaleAuto ? KeyFamily::Either
                                 : (Scale { 0, params.scale }.isMinorFamily() ? KeyFamily::Minor : KeyFamily::Major);
        const int tonic = keyTracker.update (hop, family, params.key > 0 ? (params.key - 1) % 12 : -1);
        if (params.key == 0)
            keyTonic = tonic;
        if (params.scaleAuto)
            autoScale = autoScaleType (keyTracker.getHistogram(), tonic, keyTracker.isMinor(), autoScale);
        if (keyTracker.getConfidence() >= 0.35f)
            detectedKey.store (tonic + (keyTracker.isMinor() ? 12 : 0), std::memory_order_relaxed);
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

    for (int s = 0; s < NumSections; ++s)
        cf.syllable[(size_t) s] = lyrics == nullptr ? -1 : (params.midiParts ? partSyllable[(size_t) s] : chordSyllable);

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
        ctx.band = band;
        ctx.bandBass = band != nullptr ? bandDetector.bass() : -1;
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
        int64_t base = 0;
        const auto& an = sourceOf (s, base);
        s.psola.reset ((double) (an.getWritten() - latency - activeEchoOffset - s.delay));
    }

    const int sec = s.section;
    const int cnt = cf.voicing.count[(size_t) sec];
    int note = cnt > 0 ? cf.voicing.notes[(size_t) sec][(size_t) (s.index % cnt)] : -1;
    if (s.sub && note >= 0)
        note -= 12;
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
    // In Words mode each singer's vibrato leans toward their voice print's.
    const bool ownVoice = params.vocalType == VocalType::Words && ! cf.midiInstrument;
    const float vibCents = style.vibratoCents * clampv (params.vibrato / 100.0f, 0.0f, 2.0f) * (ownVoice ? s.wordsVibDepth : s.rVibDepth) * onset * onset;
    s.vibPhase += (style.vibratoRateHz + (ownVoice ? s.wordsVibRate : s.rVibRate)) * dt;
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
    // The voice print's vocal tract takes over from the random spread as Voices goes up.
    const float voicesAmount = clampv (params.realVoices / 100.0f, 0.0f, 1.0f);
    const float individual = std::pow (singerFormant (s), 1.0f - voicesAmount) * s.printFormant;
    s.pctl.formant = vocalTract[(size_t) sec] * individual * formantParam * s.lastFollow;
    s.pctl.gain = s.rGain;
    s.pctl.unvoicedGain = 0.6f;

    // Vowels: synth pitch and amplitude.
    const bool followLead = s.doublesMelody && cf.voiced && correction < 0.999f;
    if (followLead)
        s.synthF0 = cf.inputHz * std::exp2 ((s.octaveShift + melodyMicro) / 12.0f);
    else
        s.synthF0 = hz;
    // Recorded voices already have their own vibrato: add only a touch on top. They also sing
    // their own slides between notes, so they get the note without the glide, and every new
    // note (or a note sung again after a gap) is counted.
    // Typed words: a new syllable starts with its consonants; letting go of the note sings the
    // ones that close it. The words carry on until a voice comes in.
    const int syl = lyrics != nullptr && cf.syllable[(size_t) sec] >= 0 && (cf.midiInstrument || (s.syllable >= 0 && ! cf.voiced))
                        ? cf.syllable[(size_t) sec] : -1;
    if (syl != s.syllable)
    {
        s.syllable = syl;
        s.sylAge = 0.0f;
        s.ending = false;
        s.sylDecided = s.sylRecorded = false;
    }
    else
    {
        s.sylAge += dt;
    }
    if (s.syllable >= 0)
    {
        if (singing)
            s.ending = false;
        else if (! s.ending && s.wasSinging)
        {
            s.ending = true;
            s.endAge = 0.0f;
            s.endLevel = s.amp;
        }
        else if (s.ending)
            s.endAge += dt;
    }

    // A recorded voice carries its own vibrato: most of the added one comes off again, all of
    // it for a singer in their classical voice (the recording's vibrato is the real thing).
    auto& sn = s.sampleNote;
    const float ownVib = takeHasVibrato (takeFor (s)) ? 1.0f : 0.75f;
    sn.hz = s.synthF0 * std::exp2 (-ownVib * (followLead ? vib * 0.35f : vib) / 12.0f);
    sn.noteHz = sn.hz * std::exp2 ((s.targetMidi - s.currentMidi - s.scoop) / 12.0f);
    sn.noteMidi = followLead || s.targetMidi < 0.0f ? -1.0f : s.targetMidi;
    if (singing)
    {
        const bool resumed = ! s.wasSinging && s.silentFor >= 0.03f;
        if (resumed || s.targetMidi != s.noteMidi)
        {
            ++sn.serial;
            sn.detached = resumed;
            s.noteMidi = s.targetMidi;
        }
        s.silentFor = 0.0f;
    }
    else
    {
        s.silentFor += dt;
    }
    s.wasSinging = singing;
    const float shimmer = 1.0f + 0.06f * s.shimmer.tick (s.rng, dt) * humanize;
    s.ampTarget = singing ? cf.level * s.rGain * shimmer : 0.0f;
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
    midiClock += n;
    if (! processedSinceReset)
    {
        // The first block after prepare() or reset(): start at the settings (an export's first
        // note at its real levels and places) instead of gliding there from the defaults.
        for (auto* g : { &volume, &leadGain, &choirGain, &wetGain, &dryChoirGain, &leadSend, &warmthAmt, &leadPlace })
            g->snap (g->target);
        for (auto& g : sectionGain)
            g.snap (g.target);
        stage.snapToSettings();
        leadStage.snapToSettings();
    }

    if (const int seed = pendingKeySeed.exchange (-1); seed >= 0)
    {
        keyTracker.seed (seed % 12, seed >= 12, 3.0f);
        keyTonic = seed % 12;
        autoScale = seed >= 12 ? ScaleType::NaturalMinor : ScaleType::Major;
    }

    // Passes added or taken away: start the new ones from here, and spread the singers again.
    // Only Words singers sing from the passes, so other voices leave them unanalysed.
    const int wantedPasses = params.vocalType == VocalType::Words ? passCount : 0;
    if (wantedPasses != activePasses)
    {
        for (int k = activePasses; k < wantedPasses; ++k)
        {
            auto& ps = passSources[(size_t) k];
            ps.tracker.reset();
            ps.analyzer.reset();
            ps.base = analyzer.getWritten();
        }
        activePasses = wantedPasses;
        spreadOverPasses();
    }

    // Reconfigure on parameter changes that affect the singers.
    allocateSingers();
    const bool styleChanged = params.style != lastStyle || params.ensemble != lastEnsemble || params.singing != lastSinging
                              || params.energy != lastEnergy;
    if (params.vocalType != lastVocal || params.formant != lastFormantParam || styleChanged)
    {
        lastVocal = params.vocalType;
        lastFormantParam = params.formant;
        refreshVowelFormants();
    }
    if (params.realVoices != lastRealVoices || styleChanged)
    {
        lastRealVoices = params.realVoices;
        for (auto& sp : singers)
        {
            sp->print = voicePrintFor (sp->section, sp->index, ens.trebles, &sp->printRound);   // children sing the women's
            applyVoicePrint (*sp);
        }
    }
    if (params.humanize != lastHumanize || params.space != lastSpace || styleChanged)
    {
        lastHumanize = params.humanize;
        lastSpace = params.space;
        lastStyle = params.style;
        lastEnsemble = params.ensemble;
        lastSinging = params.singing;
        lastEnergy = params.energy;
        for (auto& sp : singers)
        {
            const float delayMs = sp->rDelay * style.timingMs * params.humanize / 100.0f;
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
        for (int k = 0; k < activePasses; ++k)
        {
            auto& an = passSources[(size_t) k].analyzer;
            if (params.freeze)
                an.captureFreeze (an.getWritten() - latency, (int) (0.25 * sr));
            else
                an.releaseFreeze();
        }
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
    if (scL == nullptr && bandDetector.hearing())
        bandDetector.reset();   // the side-chain was disconnected
    scL = scR = nullptr;        // valid for this block only
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

    // The band on the side-chain: what chord is it playing?
    if (scL != nullptr)
    {
        for (int i = 0; i < c; ++i)
        {
            const float l = scL[offset + i], r = scR != nullptr ? scR[offset + i] : l;
            const float v = 0.5f * (l + r);
            bandScratch[(size_t) i] = isFiniteSample (v) ? v : 0.0f;
        }
        bandDetector.setSpeed (clampv (params.harmonySpeed / 100.0f, 0.0f, 1.0f));
        bandDetector.push (bandScratch.data(), c);
    }

    tracker.push (mono.data(), c);
    for (int k = 0; k < tracker.getNumNewFrames(); ++k)
        handlePitchFrame (tracker.getNewFrame (k));

    analyzer.pushInput (mono.data(), c);
    analyzer.updateMarks();

    // The other passes: analysed the same way, sample for sample with the input.
    for (int k = 0; k < activePasses; ++k)
    {
        auto& ps = passSources[(size_t) k];
        float pass[kChunk];
        const float* src = passInputs != nullptr ? passInputs[k] : nullptr;
        for (int i = 0; i < c; ++i)
            pass[i] = src != nullptr && isFiniteSample (src[offset + i]) ? src[offset + i] : 0.0f;
        ps.tracker.push (pass, c);
        for (int f = 0; f < ps.tracker.getNumNewFrames(); ++f)
            ps.analyzer.addPitchFrame (ps.tracker.getNewFrame (f));
        ps.analyzer.pushInput (pass, c);
        ps.analyzer.updateMarks();
    }
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
                    int64_t base = 0;
                    sourceOf (*sp, base);
                    sp->psola.reset ((double) (tWords - base - wantedOffset - sp->delay));
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

        // Each section's recordings of the vowel, per take (a take the set lacks falls back).
        std::array<std::array<const SampleLayer*, SampleSet::kTakes>, NumSections> sampleLayers {};
        if (sampleSet != nullptr)
        {
            const VocalType vt = words ? VocalType::Ah : params.vocalType;
            for (int s = 0; s < NumSections; ++s)
                for (int t = 0; t < SampleSet::kTakes; ++t)
                    sampleLayers[(size_t) s][(size_t) t] = sampleSet->layerFor (formantSection (s), vt, (Take) t);
        }

        // Section level by head count, eased so singers joining or leaving don't step the level
        // of the others (a section going silent keeps its last value while it fades out).
        std::array<float, NumSections> normFrom {}, normTo {};
        const float normK = 1.0f - std::exp (-dt / 0.06f);
        // The singers on one pass add up nearly in phase; different passes add up only in
        // power, and would sound quieter for it: lift them by what that costs.
        const float passLift = words && activePasses > 0 ? std::sqrt ((float) (activePasses + 1)) : 1.0f;
        for (int s = 0; s < NumSections; ++s)
        {
            float& cur = sectionNorm[(size_t) s];
            const int count = sectionCount[(size_t) s];
            const float target = count > 0 ? 0.8f * passLift / std::sqrt ((float) count) : (cur > 0.0f ? cur : 0.5f);
            if (cur <= 0.0f)
                cur = target;
            normFrom[(size_t) s] = cur;
            cur += (target - cur) * normK;
            normTo[(size_t) s] = cur;
        }
        // The oktavists' level (Sub-Octave), eased the same way: at the top of the knob a few dB
        // under the basses they sing under (an octave down, the ear hears them less).
        const float subFrom = subLevel;
        subLevel += (1.8f * clampv (params.subOctave / 100.0f, 0.0f, 1.0f) - subLevel) * normK;
        const float subTo = subLevel;

        // Into the section bus: fade (joining/leaving), section level and echo hand-over, each
        // ramped over the chunk, and the singer's place in the section.
        auto mixSinger = [&] (const Singer& s, const float* x, float gFrom, float gTo)
        {
            float* L = sec (s.section, 0);
            float* R = sec (s.section, 1);
            const float gInc = (gTo - gFrom) / (float) c;
            float gain = gFrom;
            for (int i = 0; i < c; ++i)
            {
                gain += gInc;
                const float v = x[i] * gain;
                L[i] += v * s.panL;
                R[i] += v * s.panR;
            }
        };
        // Words mode: singers with a voice print wait here in fours, so their EQs run side by
        // side (one singer's six filters in a row would each wait on the last sample).
        struct Waiting { Singer* s; float gFrom, gTo; };
        std::array<Waiting, 4> waiting {};
        alignas (16) float waitBuf[4][kChunk];
        int numWaiting = 0;
        auto flushWaiting = [&]
        {
            std::array<std::array<Biquad, 6>*, 4> eqs {};
            for (int l = 0; l < numWaiting; ++l)
                eqs[(size_t) l] = &waiting[(size_t) l].s->printEq;
            processVoicePrintEqs (eqs, waitBuf, numWaiting, c);
            for (int l = 0; l < numWaiting; ++l)
                mixSinger (*waiting[(size_t) l].s, waitBuf[l], waiting[(size_t) l].gFrom, waiting[(size_t) l].gTo);
            numWaiting = 0;
        };

        for (auto& sp : singers)
        {
            Singer& s = *sp;
            if ((! s.active && s.fade <= 0.0f) || ((renderMask >> s.section) & 1) == 0)
                continue;
            bool printed = false;   // its voice print still to apply (in the next four)

            const int64_t tSinger = tOut - echoOffset - s.delay;
            const ControlFrame& cf = controlAt (tSinger);
            const float fadeStart = s.fade;
            updateSingerControl (s, cf, dt);
            const float fadeEnd = s.fade;

            std::fill (scratch.begin(), scratch.begin() + c, 0.0f);
            if (words && ! cf.midiInstrument && s.syllable < 0)
            {
                int64_t base = 0;
                const auto& source = sourceOf (s, base);
                s.psola.render (source, tSinger - base, c, scratch.data(), s.pctl, s.rng);
                const float gateTarget = cf.sounding && ! silent ? 1.0f : 0.0f;
                float peak = 0.0f;
                for (int i = 0; i < c; ++i)
                {
                    s.gate = gateTarget + (s.gate - gateTarget) * (gateTarget > s.gate ? gateAtt : gateRel);
                    scratch[(size_t) i] *= s.gate;
                    peak = std::max (peak, std::abs (scratch[(size_t) i]));
                }
                s.amp = 0.0f;
                if (s.printEqOn)
                {
                    printed = peak >= 1.0e-9f;
                    if (! printed)   // silent: start clean next time rather than ring down into denormals
                        for (auto& f : s.printEq)
                            f.reset();
                }
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

                // Typed words: this syllable's vowel, shaped by its consonants.
                const LyricScript::Entry* entry = s.syllable >= 0 && lyrics != nullptr
                                                      ? &lyrics->entries[(size_t) (s.syllable % (int) lyrics->entries.size())] : nullptr;
                Articulation a0, a1;
                const SampleLayer* layer = sampleLayers[(size_t) s.section][(size_t) takeFor (s)];
                if (entry != nullptr)
                {
                    const float age = s.ending ? s.endAge : s.sylAge;
                    a0 = LyricScript::at (*entry, age, s.ending);
                    a1 = LyricScript::at (*entry, age + dt, s.ending);
                    if (sampleSet != nullptr)
                        layer = sampleSet->layerFor (formantSection (s.section), entry->vowel, takeFor (s));
                    // Consonants the voices were recorded singing: a real singer sings them (then
                    // the vowel takes over), in place of the synthesised ones.
                    if (layer != nullptr && ! s.ending && ! entry->hold && entry->numOnset > 0 && ! s.sylDecided && s.sylAge + dt >= entry->onsetAt)
                    {
                        s.sylDecided = true;
                        const float at = (s.targetMidi >= 0.0f ? s.targetMidi : hzToMidi (s.sampleNote.hz)) + ens.zoneOffset;
                        const int k = layer->nearestSyllable (SampleSet::phonesKey (entry->onset.data(), entry->numOnset), at, s.index);
                        if (k >= 0)
                        {
                            s.sampleNote.syllable = k;
                            ++s.sampleNote.syllableSerial;
                            s.sylRecorded = true;
                        }
                    }
                    if (s.sylRecorded && ! s.ending)
                    {
                        if (age >= entry->onsetAt) a0 = Articulation {};
                        if (age + dt >= entry->onsetAt) a1 = Articulation {};
                    }
                    if (s.vowelOverride != (int) entry->vowel)
                    {
                        setSingerVowel (s, entry->vowel);
                        s.vowelOverride = (int) entry->vowel;
                    }
                    peakAmp = 0.0f;
                    for (int i = 0; i < c; ++i)
                    {
                        ampBuf[i] *= a0.vowel + (a1.vowel - a0.vowel) * (float) (i + 1) / (float) c;
                        peakAmp = std::max (peakAmp, ampBuf[i]);
                    }
                }
                else if (s.vowelOverride >= 0)
                {
                    setSingerVowel (s, params.vocalType == VocalType::Words ? VocalType::Ah : params.vocalType);
                    s.vowelOverride = -1;
                }

                if (layer != nullptr)
                {
                    // Recorded choir voices.
                    s.sampler.render (*layer, s.sampleNote, ampBuf, scratch.data(), c, sr, s.index, s.rJitter, ens.zoneOffset);
                    for (int i = 0; i < c; ++i)
                        scratch[(size_t) i] *= kSampleGain;
                }
                else if (peakAmp > 1.0e-4f)
                    s.vowel.render (glottal, s.synthF0, style.softness, breath, s.rng, ampBuf, scratch.data(), c);
                if (entry != nullptr)
                    articulate (s, a0, a1, scratch.data(), c);
                s.gate = 0.0f;
            }

            const float gFrom = normFrom[(size_t) s.section] * fadeStart * duckFrom * (s.sub ? subFrom : 1.0f);
            const float gTo = normTo[(size_t) s.section] * fadeEnd * duckTo * (s.sub ? subTo : 1.0f);
            if (printed)
            {
                std::copy (scratch.begin(), scratch.begin() + c, waitBuf[numWaiting]);
                waiting[(size_t) numWaiting++] = { &s, gFrom, gTo };
                if (numWaiting == 4)
                    flushWaiting();
            }
            else
            {
                mixSinger (s, scratch.data(), gFrom, gTo);
            }
        }
        if (numWaiting > 0)
            flushWaiting();
    }

    // Children singing vowels: a child's tone from the recorded women.
    updateChildTone();
    if (stems == nullptr && childToneOn)
    {
        for (int s = 0; s < NumSections; ++s)
        {
            auto& t = childTone[(size_t) s];
            for (int ch = 0; ch < 2; ++ch)
            {
                float* x = sec (s, ch);
                for (int i = 0; i < c; ++i)
                    x[i] = t.low[(size_t) ch].process (t.ring[(size_t) ch].process (t.high[(size_t) ch].process (x[i])));
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

    // Section levels, then the stage: each section where it stands, heard by the microphones.
    stage.begin (c);
    for (int s = 0; s < NumSections; ++s)
    {
        const float g = sectionGain[(size_t) s].advance (c);
        float* L = sec (s, 0);
        float* R = sec (s, 1);
        float peak = 0.0f;
        for (int i = 0; i < c; ++i)
        {
            L[i] *= g;
            R[i] *= g;
            peak = std::max (peak, std::max (std::abs (L[i]), std::abs (R[i])));
        }
        peakAccum[(size_t) s] = std::max (peakAccum[(size_t) s], peak);
        stage.addSection (s, L, R, c);
    }
    stage.mix (choirL.data(), choirR.data(), c);

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
    const float dry = dryChoirGain.advance (c), vol = volume.advance (c);
    // The hall hears the choir at the same level whichever mics are up; the mics decide how
    // much of the hall comes back (the room pair far more than the spot mics).
    const float wet = wetGain.advance (c) * stage.reverbAmount();
    const float revScale = stage.reverbInputScale();

    // Your voice: at the microphones it stays dry and up front with its own reverb send; on
    // the stage it stands where it was put, heard by the mics and in the hall like the choir.
    const float placeFrom = leadPlace.current, placeTo = leadPlace.advance (c);
    const bool placed = placeFrom > 1.0e-4f || placeTo > 1.0e-4f;
    if (placed)
    {
        leadStage.begin (c);
        leadStage.addSection (0, leadL, leadR, c);
        leadStage.mix (leadPlacedL.data(), leadPlacedR.data(), c);
    }
    const float placeInc = (placeTo - placeFrom) / (float) c;
    for (int i = 0; i < c; ++i)
    {
        choirL[(size_t) i] *= cg;
        choirR[(size_t) i] *= cg;
        revInL[(size_t) i] = choirL[(size_t) i] * revScale;
        revInR[(size_t) i] = choirR[(size_t) i] * revScale;
        const float on = placed ? placeFrom + placeInc * (float) (i + 1) : 0.0f;
        const float pl = placed ? leadPlacedL[(size_t) i] * lg * on : 0.0f, pr = placed ? leadPlacedR[(size_t) i] * lg * on : 0.0f;
        leadL[i] *= 1.0f - on;
        leadR[i] *= 1.0f - on;
        revInL[(size_t) i] += leadL[i] * ls + pl * revScale;
        revInR[(size_t) i] += leadR[i] * ls + pr * revScale;
        leadPlacedL[(size_t) i] = pl;
        leadPlacedR[(size_t) i] = pr;
    }
    processReverb (c);

    for (int i = 0; i < c; ++i)
    {
        const float pl = placed ? leadPlacedL[(size_t) i] * dry : 0.0f, pr = placed ? leadPlacedR[(size_t) i] * dry : 0.0f;
        float l = (leadL[i] * lg + pl + choirL[(size_t) i] * dry + revOutL[(size_t) i] * wet) * vol;
        float r = (leadR[i] * lg + pr + choirR[(size_t) i] * dry + revOutR[(size_t) i] * wet) * vol;
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

void ChoirEngine::processReverb (int c)
{
    // The measured room at the level the algorithmic hall had, so the Reverb knob means the same
    // (each room at its own venue's level: a new venue's room takes a moment to build).
    processedSinceReset = true;
    const float blendFrom = roomBlend;
    const float step = (float) c / (0.08f * (float) sr);
    roomBlend = room != nullptr ? std::min (1.0f, roomBlend + step) : std::max (0.0f, roomBlend - step);

    if (blendFrom < 1.0f || roomBlend < 1.0f)
    {
        reverbIdle = false;
        reverb.process (revInL.data(), revInR.data(), revOutL.data(), revOutR.data(), c);
        for (int i = 0; i < c; ++i)
        {
            const float g = 1.0f - (blendFrom + (roomBlend - blendFrom) * (float) (i + 1) / (float) c);
            revOutL[(size_t) i] *= g;
            revOutR[(size_t) i] *= g;
        }
    }
    else
    {
        // Handed over to the room: clear the hall, so if it comes back it doesn't replay old sound.
        if (! reverbIdle)
        {
            reverb.reset();
            reverbIdle = true;
        }
        std::fill (revOutL.begin(), revOutL.begin() + c, 0.0f);
        std::fill (revOutR.begin(), revOutR.begin() + c, 0.0f);
    }

    if (room != nullptr)
    {
        room->process (revInL.data(), revInR.data(), roomL.data(), roomR.data(), c, roomShape);
        for (int i = 0; i < c; ++i)
        {
            const float g = roomLevel * (blendFrom + (roomBlend - blendFrom) * (float) (i + 1) / (float) c);
            revOutL[(size_t) i] += roomL[(size_t) i] * g;
            revOutR[(size_t) i] += roomR[(size_t) i] * g;
        }
    }
    if (fadingRoom != nullptr)
    {
        // The room left behind rings out what it already heard, fading over 300 ms.
        fadingRoom->process (roomZeros.data(), roomZeros.data(), roomL.data(), roomR.data(), c, roomShape);
        const float from = fadingGain;
        fadingGain = std::max (0.0f, fadingGain - (float) c / (0.3f * (float) sr));
        for (int i = 0; i < c; ++i)
        {
            const float g = fadingLevel * (from + (fadingGain - from) * (float) (i + 1) / (float) c);
            revOutL[(size_t) i] += roomL[(size_t) i] * g;
            revOutR[(size_t) i] += roomR[(size_t) i] * g;
        }
        if (fadingGain <= 0.0f)
            fadingRoom = nullptr;
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
    snap.scaleType = (int) scale.type;
    snap.keyMinor = scale.isMinorFamily();
    snap.keyConfidence = params.key == 0 ? keyTracker.getConfidence() : 1.0f;
    const std::string& name = harmonizer.getChord().name;
    std::snprintf (snap.chordName, sizeof (snap.chordName), "%s", scoreSource != nullptr ? "" : name.c_str());
    snap.frozen = params.freeze;
    snap.lyricIndex = lyrics != nullptr && cf.midiInstrument && cf.syllable[0] >= 0
                          ? cf.syllable[0] % (int) lyrics->entries.size() : -1;
    snap.bandHeard = bandDetector.hearing();
    const Chord* band = bandDetector.hearing() ? bandDetector.chord() : nullptr;
    snap.followingBand = band != nullptr && params.followBand && scoreSource == nullptr;
    if (band != nullptr)
    {
        const int bass = bandDetector.bass();
        if (bass >= 0 && bass != band->root)
            std::snprintf (snap.bandChord, sizeof (snap.bandChord), "%s/%s", band->name.c_str(), getPitchClassName (bass, band->name.find ('b') != std::string::npos));
        else
            std::snprintf (snap.bandChord, sizeof (snap.bandChord), "%s", band->name.c_str());
    }
    else
    {
        snap.bandChord[0] = 0;
    }

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
