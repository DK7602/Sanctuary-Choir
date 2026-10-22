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
};

struct LearnedEvent
{
    int64_t start = 0, end = 0;
    Chord chord;
    Voicing voicing;
    int melody = -1;
    int sopShift = 0;
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

    bool voicingAt (int64_t t, Voicing& out, int& leadNote, int& sopShift) const override;
    /** e.g. "LEARNED 0:43 · Ab MAJOR · 4 PHRASES · 105 NOTES" */
    std::string describe() const;
    /** e.g. "G MAJOR", or "G MAJOR → A MAJOR" when the key changes. */
    std::string keyName() const;
};

LearnedScore harmoniseTake (const TakeAnalysis& take, const HarmonySettings& settings);

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
} // namespace sc
