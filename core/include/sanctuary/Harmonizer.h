// Sanctuary Choir - real-time harmony: lead note tracking, chord choice, voicing, key tracking.
#pragma once

#include "PitchTracker.h"
#include "Theory.h"

namespace sc
{
/** Performance character of each style. */
struct StyleProfile
{
    float vibratoCents, vibratoRateHz, vibratoOnsetS;
    float detuneCents, driftCents, timingMs, glideMs, scoopCents;
    float attackS, releaseS, breath, softness;
};

StyleProfile getStyleProfile (Style s);

/** A small fixed-capacity list of MIDI notes (no allocation on the audio thread). */
struct NoteSet
{
    std::array<int, 16> notes {};
    int count = 0;

    void clear() { count = 0; }
    void add (int n) { if (count < 16) notes[(size_t) count++] = n; }
    bool empty() const { return count == 0; }
    bool operator== (const NoteSet& o) const
    {
        if (count != o.count) return false;
        for (int i = 0; i < count; ++i) if (notes[(size_t) i] != o.notes[(size_t) i]) return false;
        return true;
    }
};

struct HarmonyContext
{
    Scale scale;
    HarmonyMode mode = HarmonyMode::FullChoir;
    Style style = Style::Sacred;
    std::array<SectionRange, NumSections> ranges = defaultSectionRanges();
    std::array<bool, NumSections> active { true, true, true, true };
    bool wordsMode = false;
    bool freeze = false;
    NoteSet midi;          // held MIDI notes (sorted ascending), empty if none
    bool midiControlsHarmony = true;
};

class LiveHarmonizer
{
public:
    void reset();

    /** Process one pitch frame (midi already relative to the tuning reference). */
    void process (const PitchFrame& f, const HarmonyContext& ctx, float frameSeconds);

    const Voicing& getVoicing() const noexcept { return voicing; }
    const Chord& getChord() const noexcept { return chord; }
    int getLeadNote() const noexcept { return leadNote; }
    int getSopranoShift() const noexcept { return sopShift; }
    bool isInPhrase() const noexcept { return leadNote >= 0; }
    uint32_t getVoicingId() const noexcept { return voicingId; }

private:
    void choose (const HarmonyContext& ctx, bool newPhrase);
    void ensureVocabulary (const HarmonyContext& ctx);
    /** Keep the chord through a short stepwise non-chord note (only the melody voice moves). */
    bool holdThroughPassingNote (const HarmonyContext& ctx, int previousLead);

    int leadNote = -1, candidate = -1;
    float melodyCentre = -1.0f;      // where the melody has been sitting (MIDI), for octave placement
    float candidateTime = 0.0f, silence = 10.0f, outOfChordTime = 0.0f;
    int sopShift = 0;
    Chord chord;
    bool haveChord = false;
    Voicing voicing;
    uint32_t voicingId = 0;
    NoteSet lastMidi;
    HarmonyMode lastMode = HarmonyMode::Count;
    std::array<bool, NumSections> lastActive { true, true, true, true };

    std::vector<Chord> vocab;
    int vocabTonic = -1;
    ScaleType vocabScale = ScaleType::Count;
    Style vocabStyle = Style::Count;
};

/** Running key detection from sung notes. */
class KeyTracker
{
public:
    void reset();
    /** Start from a known key (e.g. the one detected last time this project played). */
    void seed (int tonic, bool minor, float weight);
    void addNote (int midiNote, float seconds, float weight);
    /** Re-estimates occasionally; returns the current tonic for the requested scale family. */
    int update (float seconds, bool minorFamily);
    int getTonic() const noexcept { return tonic; }
    float getConfidence() const noexcept { return confidence; }
    const std::array<float, 12>& getHistogram() const noexcept { return hist; }

private:
    std::array<float, 12> hist {};
    float sinceEstimate = 0.0f, totalWeight = 0.0f, confidence = 0.0f;
    int tonic = 0, pending = -1, pendingCount = 0;
    bool lastFamilyMinor = false;
};
} // namespace sc
