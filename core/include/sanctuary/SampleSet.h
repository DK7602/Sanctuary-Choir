// Sanctuary Choir - recorded choir voices.
//
// A SampleSet holds sustained notes recorded by real singers, per section (S/A/T/B) and vowel.
// Recordings can be single notes or long takes with several held notes separated by breaths:
// they are sliced, pitched and looped automatically. A SamplePlayer then sings one choir
// member from those notes, in place of the vowel synthesiser.
#pragma once

#include "Theory.h"

#include <memory>
#include <string>
#include <vector>

namespace sc
{
/** One recorded sustained note (16-bit to keep big sets small in memory). */
struct SampleZone
{
    std::vector<int16_t> pcm;    // mono, at sampleRate
    float scale = 1.0f / 32767.0f;   // pcm -> level-normalised float
    double sampleRate = 48000.0;
    float rootMidi = 60.0f;      // pitch it was sung at (fractional MIDI, A4 = 69)
    int64_t loopStart = 0, loopEnd = 0, crossfade = 0;

    int64_t length() const noexcept { return (int64_t) pcm.size(); }
    float seconds() const noexcept { return (float) ((double) pcm.size() / sampleRate); }
};

/** All the notes of one section singing one vowel, sorted by pitch. */
struct SampleLayer
{
    std::vector<SampleZone> zones;

    bool empty() const noexcept { return zones.empty(); }
    /** Index of the zone to play for a pitch. variant (the choir member) picks between the
        recordings close enough to it, so different members sing different singers' notes. */
    int nearest (float midi, int variant) const noexcept;
};

class SampleSet
{
public:
    static constexpr int kVowels = 6;   // Ah, Oh, Oo, Ee, Eh, Mm (the VocalType order)

    /** Adds a recording (one held note, or a take with several). Notes shorter than 0.6 s are
        skipped. Returns how many notes were kept. */
    int addRecording (int section, int vowel, const float* x, int64_t n, double sampleRate, float a4 = 440.0f);
    /** Adds an already-cut note at a known pitch. */
    void addNote (int section, int vowel, const float* x, int64_t n, double sampleRate, float rootMidi);
    /** Sorts the notes and sets their loops and levels. Call once after adding everything. */
    void finalise();

    /** Notes for a section and vowel (falls back to Ah, then to any vowel; humming falls back
        to nothing, i.e. the synthesised hum), or nullptr. */
    const SampleLayer* layerFor (int section, VocalType vowel) const noexcept;
    int numNotes() const noexcept;
    int numSections() const noexcept;
    int numVowels() const noexcept;
    bool isEmpty() const noexcept { return numNotes() == 0; }
    /** e.g. "4 SECTIONS · 2 VOWELS · 96 NOTES" */
    std::string describe() const;

    std::string name;

    /** Section (0-3) or vowel (0-5) named in a file path, -1 if none. */
    static int sectionFromPath (const std::string& path);
    static int vowelFromPath (const std::string& path);
    /** Pitch written in a file name as "m6312" (= MIDI 63.12), or -1 if none: such files are
        already cut to one note and need no analysis. */
    static float rootFromPath (const std::string& path);

private:
    std::array<std::array<SampleLayer, kVowels>, NumSections> layers;
};

/** One choir member singing recorded notes: pitch by resampling, a crossfaded sustain loop,
    and a short crossfade when the melody moves far enough to need another recorded note. */
class SamplePlayer
{
public:
    void reset() noexcept;

    /** Renders n samples at hz into out (overwrites), scaled per sample by amp. variant picks
        between takes; jitter (0..1) staggers this singer's start inside the note. */
    void render (const SampleLayer& layer, float hz, const float* amp, float* out, int n,
                 double outRate, int variant, float jitter) noexcept;

private:
    struct Head
    {
        const SampleZone* zone = nullptr;
        double pos = 0.0;
        float gain = 0.0f;
        int64_t loopStart = 0;   // this singer's own loop point in the zone
    };
    static int64_t loopPointFor (const SampleZone& z, float jitter) noexcept;
    Head cur, old;
    float fade = 1.0f;
    const SampleLayer* lastLayer = nullptr;
};
} // namespace sc
