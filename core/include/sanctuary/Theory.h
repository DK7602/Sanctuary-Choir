// Sanctuary Choir - music theory: scales, chords and SATB voicing.
#pragma once

#include "Common.h"
#include <string>

namespace sc
{
//==============================================================================
enum class ScaleType : int
{
    Major = 0, NaturalMinor, HarmonicMinor, Dorian, Mixolydian, Lydian, Phrygian, Gospel, Chromatic, Count
};

enum class HarmonyMode : int
{
    FullChoir = 0, GospelStack, OpenVoicing, Unison, ThirdsAndSixths, Organum, UpperVoices, LowerVoices, Drone, MidiChords, Count
};

enum class Style : int
{
    Sacred = 0, Gospel, Gregorian, Cinematic, Worship, Chamber, Spiritual, Count
};

enum class VocalType : int
{
    Ah = 0, Oh, Oo, Ee, Eh, Mm, Words, Count
};

const char* const* getScaleNames();       // ScaleType::Count entries
const char* const* getHarmonyModeNames(); // HarmonyMode::Count entries
const char* const* getStyleNames();       // Style::Count entries
const char* const* getVocalTypeNames();   // VocalType::Count entries
const char* const* getKeyNames();         // 13 entries: "Auto", "C", ... "B"
const char* getPitchClassName (int pc, bool preferFlats);
std::string getNoteName (int midi);       // e.g. "C4"

//==============================================================================
struct Scale
{
    int tonic = 0;                 // pitch class 0..11
    ScaleType type = ScaleType::Major;

    /** True for the 7-note (or extended) set used to quantise the melody. */
    bool contains (int pc) const;
    /** True if the scale is minor-flavoured (for key detection and chord priors). */
    bool isMinorFamily() const;
    /** The seven diatonic pitch classes (absolute) used for building chords. */
    std::array<int, 7> diatonic() const;
    /** Nearest scale note (integer MIDI) to a fractional MIDI pitch. */
    int quantise (float midi) const;
    /** Move a MIDI note by a number of diatonic steps (e.g. -2 = a third below). */
    int stepDiatonic (int midi, int steps) const;
};

//==============================================================================
struct Chord
{
    int root = 0;               // absolute pitch class
    std::array<int, 4> tones {}; // absolute pitch classes: root, third, fifth, seventh (if any)
    int numTones = 3;
    int degree = 0;             // scale degree 0..6 (for function); 7+ for borrowed chords
    float prior = 1.0f;
    std::string name;           // e.g. "Am7"

    bool contains (int pc) const;
    /** 0 root, 1 third, 2 fifth, 3 seventh, -1 not a chord tone. */
    int roleOf (int pc) const;
    uint16_t mask() const;
};

/** Chord vocabulary for a scale and style (diatonic triads/sevenths plus a few borrowed chords). */
std::vector<Chord> buildChordVocabulary (const Scale& scale, Style style);
/** The same, into an existing vector (no allocation once it has room for ~20 chords). */
void buildChordVocabulary (const Scale& scale, Style style, std::vector<Chord>& out);
/** Build a chord from arbitrary pitch classes (e.g. MIDI chord input). */
Chord chordFromPitchClasses (const int* notes, int count);

/** Chord qualities recognised in a band's audio. */
enum class ChordQuality : int { Major = 0, Minor, Dominant7, Major7, Minor7, Sus4, Diminished, Count };
/** Intervals above the root for a quality (3 or 4 notes; unused entries -1). */
std::array<int, 4> chordQualityIntervals (ChordQuality q);
Chord makeChordOfQuality (int root, ChordQuality q, bool preferFlats = false);
/** Functional progression bonus (positive = idiomatic) between chord degrees. */
float progressionBonus (const Chord& from, const Chord& to, bool minorKey);

//==============================================================================
struct SectionRange
{
    int low = 60, high = 81;
    float centre() const { return 0.5f * (float) (low + high); }
};

std::array<SectionRange, NumSections> defaultSectionRanges();

/** Target notes for each section. Each section may split (divisi) into two notes. */
struct Voicing
{
    std::array<std::array<int, 2>, NumSections> notes {};  // MIDI notes, -1 = none
    std::array<int, NumSections> count {};                 // 0, 1 or 2 notes in use
    bool valid = false;

    Voicing() { for (auto& n : notes) n = { -1, -1 }; }
    int primary (int s) const { return count[(size_t) s] > 0 ? notes[(size_t) s][0] : -1; }
    void set (int s, int note) { notes[(size_t) s] = { note, -1 }; count[(size_t) s] = note >= 0 ? 1 : 0; }
    void setDivisi (int s, int a, int b) { notes[(size_t) s] = { a, b }; count[(size_t) s] = 2; }
};

struct VoicingRequest
{
    HarmonyMode mode = HarmonyMode::FullChoir;
    Style style = Style::Sacred;
    Scale scale;
    Chord chord;
    int melody = -1;            // quantised lead note (MIDI), -1 if none
    int sopranoOctaveShift = 0; // octave shift applied to the melody for the soprano line
    std::array<SectionRange, NumSections> ranges = defaultSectionRanges();
    std::array<bool, NumSections> active { true, true, true, true };
    const Voicing* previous = nullptr;
    bool wordsMode = false;     // penalise very large shifts away from the lead voice
    std::array<int, 16> explicitNotes {}; // MIDI chord with 3+ notes: voice these directly
    int numExplicit = 0;
    int bassPc = -1;            // the band's bass note (pitch class): the choir's bass sings it if it can
};

Voicing voiceChord (const VoicingRequest& req);

/** Pick the octave shift (multiple of 12) that places a melody note comfortably in a section. */
/** Octave shift that brings a melody note into a range. extraOctavePenalty (in semitones) is
    added for every octave beyond the first, to prefer the smallest musical shift. */
int chooseOctaveShift (int melody, const SectionRange& range, float extraOctavePenalty = 0.0f);

//==============================================================================
/** Krumhansl-Kessler key estimation from a pitch-class duration histogram. */
struct KeyEstimate { int tonic = 0; bool minor = false; float score = 0.0f; };
KeyEstimate estimateKey (const std::array<float, 12>& histogram, bool restrictToMinor, bool restrictToMajor);
/** How well a pitch-class histogram fits one key (higher is better; what estimateKey ranks by). */
float keyFitness (const std::array<float, 12>& histogram, int tonic, bool minor);

} // namespace sc
