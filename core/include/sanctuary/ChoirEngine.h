// Sanctuary Choir - the real-time choir engine.
//
// Signal flow:
//   input -> pitch tracker -> harmoniser (live or learned score) -> control timeline
//   input -> PSOLA analyser -> per-singer PSOLA voices ("Words") or vowel synths ("Ah", "Oh"...)
//   singers -> section buses (S/A/T/B) -> level/pan/width -> tone/glue -> + reverb -> + lead -> limiter
#pragma once

#include "Effects.h"
#include "Harmonizer.h"
#include "Psola.h"
#include "SampleSet.h"
#include "VowelSynth.h"

#include <atomic>
#include <memory>

namespace sc
{
struct SectionParams
{
    float level = 0.75f;   // 0..1 fader position (0.75 = unity)
    float pan = 0.0f;      // -1..1 offset
    bool mute = false, solo = false;
    SectionRange range;
};

struct EngineParams
{
    EngineParams()
    {
        const auto r = defaultSectionRanges();
        for (size_t s = 0; s < sections.size(); ++s)
            sections[s].range = r[s];
    }

    // Perform page
    float volumeDb = 0.0f;
    float choirSize = 70.0f;     // %
    float space = 60.0f;         // %
    float reverb = 65.0f;        // %
    float toneDb = 0.0f;
    float presence = 30.0f;      // %
    HarmonyMode harmonyMode = HarmonyMode::FullChoir;
    int key = 0;                 // 0 = auto, 1..12 = C..B
    ScaleType scale = ScaleType::Major;
    Style style = Style::Sacred;
    VocalType vocalType = VocalType::Ah;
    bool freeze = false;
    int response = 0;            // 0 = sing with me, 1 = echo each phrase right after it
    std::array<SectionParams, NumSections> sections;

    // Mix page
    float leadDb = 0.0f;
    float choirDb = 0.0f;
    float humanize = 100.0f;     // % of the style's ensemble looseness
    float vibrato = 100.0f;      // % of the style's vibrato
    float glide = 100.0f;        // % of the style's portamento
    float formant = 0.0f;        // -100..100 (% darker .. brighter)
    float pitchCorrection = 60.0f; // % how strictly harmony voices sit on pitch
    float breath = 100.0f;       // % breath noise in vowel modes
    float harmonySpeed = 50.0f;  // % how readily the choir changes chord (0 holds chords, 100 follows every note)

    // FX page
    Venue venue = Venue::GrandCathedral;
    float decay = 100.0f;        // %
    float preDelayMs = -1.0f;    // < 0 venue default
    float brightness = 50.0f;    // %
    float leadReverb = 15.0f;    // % lead send
    float lowCutHz = 70.0f;
    float highCutHz = 18000.0f;
    float warmth = 0.0f;         // %
    float glue = 25.0f;          // %

    // Settings page
    bool midiEnabled = true;     // MIDI chords steer harmony; MIDI alone plays the choir
    float a4 = 440.0f;
    bool lowLatency = false;     // Live monitoring: lead undelayed, vowel choir ~15 ms, Words keep full latency

    // Internal (offline export)
    bool wetOnly = false;        // output only the room (reverb) signal
};

/** Everything the UI shows that comes from the audio thread. */
struct EngineSnapshot
{
    std::array<float, NumSections> sectionPeak {};
    std::array<std::array<int, 2>, NumSections> sectionNotes {};
    std::array<int, NumSections> sectionSingers {};
    int totalSingers = 0;
    float leadMidi = 0.0f;
    bool leadVoiced = false;
    int leadNote = -1;
    int keyTonic = 0;
    bool keyMinor = false;
    float keyConfidence = 0.0f;
    char chordName[16] {};
    float inputPeak = 0.0f, outputPeak = 0.0f;
    bool frozen = false;
};

/** Pre-rendered section stems to play instead of live singers (learned + rendered takes). */
struct StemPlayback
{
    std::array<const float*, NumSections * 2> sections {}; // L/R per section, already aligned to this block
};

/** Lookup of learned harmony for scripted (offline) rendering. */
class ScoreSource
{
public:
    virtual ~ScoreSource() = default;
    /** Voicing in force at input-timeline sample t; returns false if none (silence). */
    virtual bool voicingAt (int64_t t, Voicing& out, int& leadNote, int& sopShift) const = 0;
};

const char* const* getResponseNames(); // 2 entries

/** Number of singers for a Choir Size percentage. */
int singersForChoirSize (float percent);
constexpr int kMaxSingers = 40;   // matches the 40 singers drawn in the full choir loft

class ChoirEngine
{
public:
    ChoirEngine();
    ~ChoirEngine();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    /** Delay that lines the whole choir (Words included) up with the lead: the offline/aligned latency. */
    int getLatencySamples() const noexcept { return latency; }
    /** Latency to report to the host: 0 in Live (low latency) mode, where the lead is not delayed. */
    int getReportedLatency() const noexcept { return params.lowLatency ? 0 : latency; }
    /** In Live mode, how far the vowel choir runs behind the voice. */
    int getLiveChoirLatency() const noexcept { return liveLatency; }
    double getSampleRate() const noexcept { return sr; }

    void setParams (const EngineParams& p);
    /** Held MIDI notes (ascending) and the loudest velocity 0..1. */
    void setMidi (const NoteSet& held, float velocity);
    /** Scripted harmony for offline rendering (nullptr = live harmoniser). */
    void setScore (const ScoreSource* score) { scoreSource = score; }
    /** When set, dry section buses (pre level/pan/width) are written here: 8 channels (S L/R, A L/R, ...). */
    void setStemTap (float* const* tap) { stemTap = tap; stemTapOffset = 0; }
    /** Recorded choir voices to sing the vowel modes (nullptr = the built-in synthesised voices).
        Call from the audio thread; the set must stay alive while it is in use. */
    void setSampleSet (const SampleSet* set) noexcept;
    /** Only render singers of these sections (bit mask, used to parallelise offline renders). */
    void setRenderSections (uint8_t mask) { renderMask = mask; }

    void process (const float* inL, const float* inR, float* outL, float* outR, int n, const StemPlayback* stems = nullptr);

    /** Thread-safe copy of the latest UI snapshot; peak levels reset once read. */
    EngineSnapshot takeSnapshot();

    /** Start auto key detection from a known key (thread-safe; applied on the next process call). */
    void seedKey (int tonic, bool minor) { pendingKeySeed.store (wrapPc (tonic) + (minor ? 12 : 0)); }
    /** The key auto-detection has settled on: tonic + 12 if minor, or -1 while unsure (thread-safe). */
    int getDetectedKey() const noexcept { return detectedKey.load (std::memory_order_relaxed); }

private:
    struct Singer;
    struct ControlFrame;

    void processChunk (const float* inL, const float* inR, float* outL, float* outR, int n, const StemPlayback* stems, int offset);
    void handlePitchFrame (const PitchFrame& f);
    const ControlFrame& controlAt (int64_t t) const;
    void allocateSingers();
    void configureSinger (Singer& s, int section, int index, int countInSection);
    void updateSingerControl (Singer& s, const ControlFrame& cf, float dt);
    void refreshVowelFormants();
    Scale currentScale() const;
    void publishSnapshot();
    /** How far the live singers run behind the input (shorter for vowels in Live mode). */
    int choirLatency() const noexcept;

    double sr = 48000.0;
    int maxBlock = 512;
    int latency = 2048;
    int liveLatency = 708;    // Live mode, vowel singers: pitch analysis only
    EngineParams params;
    StyleProfile style {};
    bool prepared = false;

    PitchTracker tracker;
    PsolaAnalyzer analyzer;
    LiveHarmonizer harmonizer;
    KeyTracker keyTracker;
    GlottalTables glottal;
    const ScoreSource* scoreSource = nullptr;
    const SampleSet* sampleSet = nullptr;
    uint64_t sampleSetId = 0;
    float* const* stemTap = nullptr;
    int64_t stemTapOffset = 0;
    uint8_t renderMask = 0x0F;

    std::vector<ControlFrame> control;
    int64_t controlCount = 0;
    int64_t controlMask = 0;

    std::vector<std::unique_ptr<Singer>> singers;   // NumSections * kSingersPerSection
    std::array<int, NumSections> sectionCount {};
    int lastSingerTarget = -1;
    HarmonyMode lastAllocMode = HarmonyMode::Count;
    VocalType lastVocal = VocalType::Count;
    float lastFormantParam = 1.0e9f;
    float lastHumanize = -1.0f;
    float lastSpace = -1.0f;
    std::array<float, NumSections> sectionNorm { -1.0f, -1.0f, -1.0f, -1.0f };   // smoothed 1/sqrt(singers)
    Style lastStyle = Style::Count;
    bool wasFrozen = false;

    NoteSet midiHeld;
    float midiVelocity = 0.0f;

    // Lead (dry) delay so it lines up with the choir (bypassed in Live mode).
    std::vector<float> leadDelayL, leadDelayR;
    int leadDelayPos = 0;

    // Buses
    std::vector<float> mono, sectionBuf, choirL, choirR, revInL, revInR, revOutL, revOutR, scratch;
    ToneStage tone;
    GlueCompressor glue;
    ChoirReverb reverb;
    SafetyLimiter limiter;
    Smoothed volume, leadGain, choirGain, wetGain, dryChoirGain, leadSend, warmthAmt;
    std::array<Smoothed, NumSections> sectionGain, sectionPanL, sectionPanR, sectionWidth;

    // Words mode voice character: every singer is the lead's own voice, so sections that sing
    // in the other gender's register get that gender's vocal tract and tone.
    struct VoiceShape
    {
        std::array<Biquad, 2> high, ring, low;   // L/R
        bool active = false;
    };
    std::array<VoiceShape, NumSections> voiceShape;
    std::array<float, NumSections> vocalTract { 1.0f, 1.0f, 1.0f, 1.0f };
    float leadCentre = -1.0f;    // where the lead sings (MIDI), slowly averaged
    int leadVoice = -1;          // 0 = low (male) lead, 1 = high (female) lead, -1 unknown
    int shapedVoice = -2;
    void updateVoiceCharacter();

    // Analysis state
    int keyTonic = 0;
    std::atomic<int> pendingKeySeed { -1 }, detectedKey { -1 };
    float lastInputMidi = 0.0f;
    uint64_t chunkCounter = 0;

    // Snapshot: built on the audio thread, handed to the UI under a try-lock.
    EngineSnapshot snap, snapShared;
    std::atomic_flag snapLock = ATOMIC_FLAG_INIT;
    std::array<float, NumSections> peakAccum {};
    float inPeakAccum = 0.0f, outPeakAccum = 0.0f;

    // Echo (call & response): phrases replayed right after they are sung
    struct Replay { int64_t srcStart = 0, srcEnd = 0, offset = 0; };
    std::array<Replay, 16> replays {};
    int replayCount = 0;
    int64_t phraseStart = -1, lastVoicedTime = 0;
    int64_t activeEchoOffset = 0;
    float echoDuck = 1.0f;   // quick fade before the singers jump to another phrase
    void scheduleReplay (int64_t start, int64_t end);
    const Replay* findReplay (int64_t t) const;

    // Envelope / gate state used when building control frames
    float lastLevel = 0.0f, unvoicedTime = 10.0f, frozenLevel = 0.0f;
    bool lastVoiced = false;
};
} // namespace sc
