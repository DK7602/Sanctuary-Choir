// Sanctuary Choir - "Learn": offline analysis of a sung take, smarter harmonisation with
// look-ahead (phrases, cadences, consistent octaves) and offline rendering to stems.
#pragma once

#include "ChoirEngine.h"
#include "ChordDetector.h"

#include <atomic>
#include <functional>
#include <string>

namespace sc
{
struct LearnedNote
{
    int64_t start = 0, end = 0;  // samples
    float pitch = 0.0f;          // median MIDI pitch (relative to the tuning reference)
    int note = -1;               // quantised to the key
    int phrase = 0;
};

struct LearnedPhrase
{
    int64_t start = 0, end = 0;
    int firstNote = 0, lastNote = 0;
};

/** Stage 1: everything that depends only on the audio. */
struct TakeAnalysis
{
    double sampleRate = 48000.0;
    int64_t length = 0;
    std::vector<PitchFrame> frames;
    std::vector<LearnedNote> notes;
    std::vector<LearnedPhrase> phrases;
    std::array<float, 12> pcHistogram {};
    float voicedSeconds = 0.0f;
    std::vector<BandChord> bandChords;   // what the band played during the take (side-chain), if anything

    bool isEmpty() const { return notes.empty(); }
};

TakeAnalysis analyseTake (const float* x, int64_t n, double sampleRate, float a4 = 440.0f,
                          const std::atomic<bool>* cancel = nullptr);

/** A chord chosen by hand for a stretch of the take: the notes whose middle falls inside sing
    it, and the harmony around it is worked out again to lead into it and away from it. */
struct ChordLock
{
    int64_t start = 0, end = 0;   // samples
    Chord chord;
};

/** Adds a lock, replacing whatever was locked where it overlaps. */
void addChordLock (std::vector<ChordLock>& locks, const ChordLock& lock);
/** Unlocks everything between start and end (cutting locks that reach past either side). */
void removeChordLocks (std::vector<ChordLock>& locks, int64_t start, int64_t end);

struct HarmonySettings
{
    int key = 0;                 // 0 = auto
    ScaleType scale = ScaleType::Major;
    HarmonyMode mode = HarmonyMode::FullChoir;
    Style style = Style::Sacred;
    std::array<SectionRange, NumSections> ranges = defaultSectionRanges();
    std::array<bool, NumSections> active { true, true, true, true };
    bool wordsMode = false;
    float speed = 0.5f;          // harmony speed 0..1 (see HarmonyContext::speed)
    bool followBand = true;      // use the band's chords when the take has them
    std::vector<ChordLock> locks;   // chords chosen by hand (they win over the band's)
};

struct LearnedEvent
{
    int64_t start = 0, end = 0;
    Chord chord;
    Voicing voicing;
    int melody = -1;
    int sopShift = 0;
    bool locked = false;     // the chord was chosen by hand
    bool fromBand = false;   // the chord was heard from the band
};

/** A stretch of the take sung over one chord (the chord lane). */
struct ChordSpan
{
    int64_t start = 0, end = 0;
    Chord chord;
    bool locked = false, fromBand = false;
};

/** Stage 2: the harmonised score (cheap to recompute when settings change). */
class LearnedScore : public ScoreSource
{
public:
    struct KeyRegion { int64_t start = 0; Scale scale; };

    Scale scale;                       // the key at the start
    std::vector<KeyRegion> keys;       // every key in order (more than one when the song modulates)
    bool keyWasDetected = false;
    bool followsBand = false;          // harmonised with the band's chords (side-chain)
    std::vector<LearnedEvent> events;
    int numPhrases = 0, numNotes = 0;
    float durationSeconds = 0.0f;
    double sampleRate = 48000.0;

    bool voicingAt (int64_t t, Voicing& out, int& leadNote, int& sopShift) const override;
    /** e.g. "LEARNED 0:43 · Ab MAJOR · 4 PHRASES · 105 NOTES" */
    std::string describe() const;
    /** e.g. "G MAJOR", or "G MAJOR → A MAJOR" when the key changes. */
    std::string keyName() const;
    /** The key sung at time t. */
    const Scale& scaleAt (int64_t t) const;
    /** The chords in order, one span per change (a pause of more than half a second starts a
        new span even on the same chord). */
    std::vector<ChordSpan> chordLane() const;
};

LearnedScore harmoniseTake (const TakeAnalysis& take, const HarmonySettings& settings);

struct ChordOption
{
    Chord chord;
    float fit = 0.0f;       // share of the sung time on notes of the chord (0..1)
    bool current = false;   // the chord the choir sings there now
};

/** Chords that could go under the notes between start and end, best first: the key's chords,
    their colours (sevenths, sus) and common borrowed chords, ranked by how well they fit the
    melody, the chords either side and the band. The current chord is always included. */
std::vector<ChordOption> chordAlternatives (const TakeAnalysis& take, const LearnedScore& score,
                                            const HarmonySettings& settings, int64_t start, int64_t end,
                                            int maxOptions = 8);

/** Section stems: 8 channels (S L/R, A L/R, T L/R, B L/R), aligned to the take timeline. */
struct RenderedStems
{
    double sampleRate = 48000.0;
    int64_t length = 0;
    std::array<std::vector<float>, NumSections * 2> channels;
    bool isEmpty() const { return length == 0; }
};

using ProgressFn = std::function<void (float)>;

/** Render the learned take with the given settings. Runs one engine per section in parallel. */
bool renderTake (const float* x, int64_t n, double sampleRate, const EngineParams& params,
                 const LearnedScore& score, RenderedStems& out, const ProgressFn& progress = {},
                 const std::atomic<bool>* cancel = nullptr, const SampleSet* voices = nullptr);

/** Mix rendered stems through the choir bus and room (reverb). Returns stereo (2 channels). */
std::array<std::vector<float>, 2> mixdownStems (const RenderedStems& stems, const EngineParams& params,
                                                bool roomOnly, float tailSeconds = 4.0f);

/** The same mix a block at a time, for long takes: fill writes the 8 stem channels for
    [pos, pos + len) (zeros past the end), out takes each mixed stereo block and returns false
    to stop. Returns false if stopped. */
using StemFill = std::function<void (int64_t pos, int len, float* const* channels)>;
using MixOut = std::function<bool (const float* left, const float* right, int len)>;
bool mixdownStemsStreaming (double sampleRate, int64_t length, const EngineParams& params, bool roomOnly,
                            float tailSeconds, const StemFill& fill, const MixOut& out);
} // namespace sc
