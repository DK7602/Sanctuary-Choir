// Sanctuary Choir - the real-time choir engine.
//
// Signal flow:
//   input -> pitch tracker -> harmoniser (live or learned score) -> control timeline
//   input -> PSOLA analyser -> per-singer PSOLA voices ("Words") or vowel synths ("Ah", "Oh"...)
//   singers -> section buses (S/A/T/B) -> level -> stage & mics (Close/Tree/Room) -> tone/glue
//          -> + reverb -> + lead (at the mics, or on the stage and in the hall) -> limiter
#pragma once

#include "ChordDetector.h"
#include "Effects.h"
#include "Harmonizer.h"
#include "Lyrics.h"
#include "Passes.h"
#include "Psola.h"
#include "SampleSet.h"
#include "VoicePrints.h"
#include "VowelSynth.h"

#include <atomic>
#include <memory>

namespace sc
{
struct SectionParams
{
    float level = 0.75f;   // 0..1 fader position (0.75 = unity)
    float pan = 0.0f;      // where the section stands: -1 stage left .. 1 stage right (audience view)
    float width = 0.42f;   // how wide it stands: half its width, in the same units (0..1)
    float depth = 0.25f;   // how far back: 0 front edge .. 1 back wall
    bool mute = false, solo = false;
    SectionRange range;
};

/** Where the sections stand by default: sopranos front left, altos front right, tenors back
    left, basses back right. */
SectionParams defaultSectionPlacement (int section);

/** Who is singing: an adult choir, children, a shouting gang, or a whole congregation. */
enum class Ensemble : int { Choir = 0, Children, Gang, Congregation, Count };
const char* const* getEnsembleNames();   // Ensemble::Count entries
/** The range a section really sings in for an ensemble: children sing the tenor and bass parts
    an octave up, everything inside a child's treble range. */
SectionRange ensembleRange (Ensemble e, int section, SectionRange r);

/** Rhythm effects on the choir, synced to the song's tempo (FX page). */
constexpr int kNumRhythmPatterns = 15;
const char* const* getRhythmPatternNames();   // kNumRhythmPatterns entries, 0 = Off
const char* const* getRhythmRateNames();      // 3 entries: 1/8, 1/16, 1/32
const char* const* getRhythmSectionNames();   // 3 entries: all, upper, lower

struct EngineParams
{
    EngineParams()
    {
        const auto r = defaultSectionRanges();
        for (size_t s = 0; s < sections.size(); ++s)
        {
            sections[s] = defaultSectionPlacement ((int) s);
            sections[s].range = r[s];
        }
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
    bool scaleAuto = false;      // Scale "Auto": listen for major or minor (scale is then not used)
    Style style = Style::Sacred;
    VocalType vocalType = VocalType::Ah;
    Singing singing = Singing::Natural;   // breathy or belted vowels (recorded voices, and the synth's tone)
    bool freeze = false;
    int response = 0;            // 0 = sing with me, 1 = echo each phrase right after it
    Ensemble ensemble = Ensemble::Choir;
    std::array<SectionParams, NumSections> sections;

    // Stage page: the microphones (dB, -60 = off)
    float micCloseDb = -60.0f;
    float micTreeDb = 0.0f;
    float micRoomDb = -60.0f;
    // ... and where your own voice stands: at the microphones (dry, up front) or on the stage
    bool leadOnStage = false;
    float leadPan = 0.0f;        // -1 stage left .. 1 stage right (audience view)
    float leadDepth = 0.0f;      // 0 front edge .. 1 back wall

    // Mix page
    float leadDb = 0.0f;
    float choirDb = 0.0f;
    float humanize = 100.0f;     // % of the style's ensemble looseness
    float vibrato = 100.0f;      // % of the style's vibrato
    float glide = 100.0f;        // % of the style's portamento
    float formant = 0.0f;        // -100..100 (% darker .. brighter)
    float pitchCorrection = 60.0f; // % how strictly harmony voices sit on pitch
    float breath = 100.0f;       // % breath noise in vowel modes
    float realVoices = 100.0f;   // % Words mode: how far each singer takes on a real singer's voice (0 = all your own)
    float harmonySpeed = 50.0f;  // % how readily the choir changes chord (0 holds chords, 100 follows every note)
    float energy = 50.0f;        // % how hard the choir sings: 0 hushed, 50 as the style sings, 100 massive
    float subOctave = 0.0f;      // % basses doubled an octave below (oktavists); 0 = off

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
    int rhythmPattern = 0;       // 0 = off (see getRhythmPatternNames)
    int rhythmRate = 1;          // 0 = eighths, 1 = sixteenths, 2 = thirty-seconds
    float rhythmLength = 60.0f;  // % of each note the gate stays open
    float rhythmDepth = 100.0f;  // %
    int rhythmSections = 0;      // 0 = all, 1 = sopranos & altos, 2 = tenors & basses

    // Settings page
    bool midiEnabled = true;     // MIDI chords steer harmony; MIDI alone plays the choir
    bool midiParts = false;      // MIDI channels 1-4 sing the soprano, alto, tenor and bass parts
    bool followBand = true;      // harmonise with the chords heard on the side-chain (band, keys, track)
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
    int scaleType = 0;             // the scale sung (with Scale Auto, the one heard)
    float keyConfidence = 0.0f;
    char chordName[16] {};
    float inputPeak = 0.0f, outputPeak = 0.0f;
    bool frozen = false;
    bool bandHeard = false;        // audio is arriving on the side-chain
    bool followingBand = false;    // ... and the choir is singing the band's chord
    char bandChord[16] {};         // e.g. "G/B", empty if none
    int lyricIndex = -1;           // typed words: the syllable the MIDI choir is on (-1 = none)
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
    /** Held MIDI notes (ascending), the loudest velocity 0..1 and how many notes started in
        this block (each new note, or chord, sings the next syllable of the typed words). */
    void setMidi (const NoteSet& held, float velocity, int newNotes = 0);
    /** Notes held on MIDI channels 1-4 (soprano, alto, tenor, bass), used when params.midiParts;
        each part steps through the typed words on its own. */
    void setMidiParts (const std::array<NoteSet, NumSections>& parts, float velocity, const std::array<int, NumSections>& newNotes = {});
    /** Typed words for the MIDI choir (nullptr = none): it sings them, one syllable per note,
        from the start of the text whenever it changes or the host starts playing. Call from the
        audio thread; the script must stay alive while it is in use. */
    void setLyrics (const LyricScript* script) noexcept;
    /** More passes of the input (the same part sung again, lined up with it) for this block:
        in Words mode the singers are spread over the input and the passes, so the choir sings
        real doubles (other voices ignore them). Same timeline as the input; call before
        process(), the pointers are read during that call only. A different count spreads the
        singers again. */
    void setPasses (const float* const* passes, int count) noexcept
    {
        passInputs = passes;
        passCount = passes != nullptr ? std::clamp (count, 0, kMaxExtraPasses) : 0;
    }
    /** Side-chain audio for this block (the band, keys or a backing track), or nullptr. Call
        before process(); the pointers are used during that call only. */
    void setSidechain (const float* left, const float* right) noexcept { scL = left; scR = right; }
    /** The host's transport for the rhythm effects (call before process). Not playing: the
        rhythm runs freely at the last tempo. ppq is the song position at the block start. */
    void setTransport (bool playing, double bpm, double ppq) noexcept
    {
        if (playing && ! transportPlaying)
            restartLyrics();
        transportPlaying = playing;
        transportBpm = bpm;
        transportPpq = ppq;
    }
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
    void setSingerVowel (Singer& s, VocalType vt);
    /** Which recorded takes the voices sing from: the Singing choice, or breathy and belted
        takes at the ends of the Energy range when Singing is Natural. */
    /** The recorded take this singer sings from: the way of singing chosen, or (Natural) each
        singer's own: straight tone, or their classical voice with its vibrato (a share of the
        choir that grows with the style's vibrato), soft or in full voice with the Energy;
        breathy for a child's airy voice. */
    Take takeFor (const Singer& s) const noexcept;
    /** The share of the choir singing in their classical voice (0..1). */
    float vibratoShare() const noexcept;
    /** The PSOLA analyser a singer sings from (the input's or a pass's), and how far behind the
        input's that one's timeline runs. */
    const PsolaAnalyzer& sourceOf (const Singer& s, int64_t& base) const noexcept;
    void spreadOverPasses();
    void articulate (Singer& s, const Articulation& a0, const Articulation& a1, float* x, int n);
    /** Words mode: the singer's voice print, at the Voices amount. */
    void applyVoicePrint (Singer& s);
    float lastRealVoices = -1.0f;
    void restartLyrics() noexcept;
    void applyRhythm (int c, int offset);
    float singerFormant (const Singer& s) const noexcept;
    int formantSection (int section) const noexcept;
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

    // The other passes (multi-take): each analysed like the input, from when it was added.
    struct PassSource
    {
        PitchTracker tracker;
        PsolaAnalyzer analyzer;
        int64_t base = 0;   // input samples written when this pass started
    };
    std::array<PassSource, kMaxExtraPasses> passSources;
    const float* const* passInputs = nullptr;
    int passCount = 0, activePasses = 0;
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
    float lastEnergy = -1.0f;
    bool lastSubOn = false;
    float energyGain = 1.0f;          // Energy's level change on the choir
    float subLevel = 0.0f;            // the oktavists' gain, eased a chunk at a time
    bool wasFrozen = false;

    NoteSet midiHeld;
    float midiVelocity = 0.0f;
    // Typed words: where each section is in the text (raw counts; -1 = before the first note).
    const LyricScript* lyrics = nullptr;
    int chordSyllable = -1;
    std::array<int, NumSections> partSyllable { -1, -1, -1, -1 };
    int64_t midiClock = 0, lastSyllableAt = -1000000;
    std::array<NoteSet, NumSections> midiParts;
    float partsVelocity = 0.0f;

    // The band on the side-chain.
    ChordDetector bandDetector;
    const float* scL = nullptr;
    const float* scR = nullptr;
    std::vector<float> bandScratch;
    bool partsHeld() const noexcept;

    // How the ensemble changes the style (set with the parameters).
    struct EnsembleProfile
    {
        float formantSpread = 1.0f;   // how different the singers' voices are
        float tract = 1.0f;           // vocal tract scale against an adult choir (children ~1.18)
        float drive = 0.0f;           // choir bus saturation (shouting)
        float zoneOffset = 0.0f;      // recorded voices: sing from notes this far below (raises formants)
        int airyEvery = 0;            // recorded voices: every Nth singer sings the breathy takes
        bool trebles = false;         // tenors and basses sing as trebles
    } ens;
    Ensemble lastEnsemble = Ensemble::Count;
    Singing lastSinging = Singing::Count;

    // Rhythm: song position and per-section gates.
    bool transportPlaying = false;
    double transportBpm = 0.0, transportPpq = 0.0;
    double blockBeat = 0.0, blockBpm = 120.0, freeBeat = 0.0, lastBpm = 120.0;
    bool beatSynced = false;
    std::array<float, NumSections> gateGain { 1.0f, 1.0f, 1.0f, 1.0f };

    // Lead (dry) delay so it lines up with the choir (bypassed in Live mode).
    std::vector<float> leadDelayL, leadDelayR;
    int leadDelayPos = 0;
    // Your voice on the stage: placed and heard by the mics like a section.
    ChoirStage leadStage;
    Smoothed leadPlace;               // 0 at the microphones .. 1 on the stage
    std::vector<float> leadPlacedL, leadPlacedR;

    // Buses
    std::vector<float> mono, sectionBuf, choirL, choirR, revInL, revInR, revOutL, revOutR, scratch;
    ToneStage tone;
    GlueCompressor glue;
    ChoirReverb reverb;
    SafetyLimiter limiter;
    Smoothed volume, leadGain, choirGain, wetGain, dryChoirGain, leadSend, warmthAmt;
    std::array<Smoothed, NumSections> sectionGain;
    ChoirStage stage;

    // Words mode voice character: every singer is the lead's own voice, so sections that sing
    // in the other gender's register get that gender's vocal tract and tone.
    struct VoiceShape
    {
        std::array<Biquad, 2> high, ring, low;   // L/R
        bool active = false;
    };
    std::array<VoiceShape, NumSections> voiceShape;
    // Children singing vowels: the recorded (adult) voices made a child's (purer, no ring,
    // no chest). In Words mode the voice shapes above do it.
    std::array<VoiceShape, NumSections> childTone;
    bool childToneOn = false;
    void updateChildTone();
    std::array<float, NumSections> vocalTract { 1.0f, 1.0f, 1.0f, 1.0f };
    float leadCentre = -1.0f;    // where the lead sings (MIDI), slowly averaged
    int leadVoice = -1;          // 0 = low (male) lead, 1 = high (female) lead, -1 unknown
    int shapedVoice = -2;
    void updateVoiceCharacter();

    // Analysis state
    int keyTonic = 0;
    ScaleType autoScale = ScaleType::Major;   // Scale Auto: what the choir hears the song in
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
