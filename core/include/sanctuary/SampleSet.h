// Sanctuary Choir - recorded choir voices.
//
// A SampleSet holds sustained notes recorded by real singers, per section (S/A/T/B), vowel and
// way of singing (natural, breathy, belted). Recordings can be single notes or long takes with
// several held notes separated by breaths: they are sliced, pitched and looped automatically.
// Next to the held notes a set can hold onsets (notes sung from silence, so a short or detached
// note starts the way a singer starts it) and moves (one note sliding into the next, so legato
// lines are sung, not crossfaded). A SamplePlayer then sings one choir member from those
// recordings, in place of the vowel synthesiser.
#pragma once

#include "Theory.h"

#include <memory>
#include <string>
#include <vector>

namespace sc
{
/** One recording: a held note, an onset or a move (16-bit to keep big sets small in memory). */
struct SampleZone
{
    std::vector<int16_t> pcm;    // mono, at sampleRate
    float scale = 1.0f / 32767.0f;   // pcm -> level-normalised float
    double sampleRate = 48000.0;
    float rootMidi = 60.0f;      // pitch it was sung at (fractional MIDI, A4 = 69); a move: where it starts
    float toMidi = 0.0f;         // a move: the pitch it arrives at
    int64_t crossing = 0;        // a move: where the pitch is halfway there
    int64_t loopStart = 0, loopEnd = 0, crossfade = 0;   // held notes: the sustain loop
    int singer = -1;             // who sang it, if known: a choir member keeps to one singer

    int64_t length() const noexcept { return (int64_t) pcm.size(); }
    float seconds() const noexcept { return (float) ((double) pcm.size() / sampleRate); }
};

/** All the recordings of one section singing one vowel one way, each list sorted by pitch. */
struct SampleLayer
{
    std::vector<SampleZone> zones;    // held notes
    std::vector<SampleZone> onsets;   // notes sung from silence, cut short: how a note begins
    std::vector<SampleZone> moves;    // one note moving to the next

    bool empty() const noexcept { return zones.empty(); }
    /** Index of the held note to play for a pitch. variant (the choir member) picks between
        the recordings close enough to it, so different members sing different singers' notes;
        singer (if >= 0) is preferred among those, so one member stays one voice. */
    int nearest (float midi, int variant, int singer = -1) const noexcept;
    /** Index of an onset for a pitch, or -1 if none is close enough. */
    int nearestOnset (float midi, int variant, int singer = -1) const noexcept;
    /** Index of a move from about `from` to `to` over the same interval, or -1 if none. */
    int nearestMove (float from, float to, int variant, int singer = -1) const noexcept;
};

/** What a choir member is singing, for its SamplePlayer. */
struct SampleNote
{
    float hz = 220.0f;        // the pitch now, glides and vibrato included
    float noteHz = 220.0f;    // the note it is on or moving to: the same, without the glide
    float noteMidi = -1.0f;   // that note (MIDI, A4 = 69), or < 0 when the pitch follows a voice freely
    int serial = 0;           // changes with every new note
    bool detached = false;    // the new note starts after a gap: sung afresh, not slid into
};

enum class SampleKind { Held, Onset, Move };

/** What an already-cut recording holds, beyond its pitch. */
struct SampleNoteInfo
{
    Singing singing = Singing::Natural;
    SampleKind kind = SampleKind::Held;
    float toMidi = 0.0f;          // a move: the pitch it arrives at
    float crossingSeconds = 0.0f; // a move: when the pitch is halfway there
    int singer = -1;
};

class SampleSet
{
public:
    static constexpr int kVowels = 6;   // Ah, Oh, Oo, Ee, Eh, Mm (the VocalType order)
    static constexpr int kSingings = (int) Singing::Count;
    using Kind = SampleKind;
    using NoteInfo = SampleNoteInfo;

    /** Adds a recording (one held note, or a take with several). Notes shorter than 0.6 s are
        skipped. Returns how many notes were kept. */
    int addRecording (int section, int vowel, const float* x, int64_t n, double sampleRate, float a4 = 440.0f);
    /** Adds an already-cut note at a known pitch. */
    void addNote (int section, int vowel, const float* x, int64_t n, double sampleRate, float rootMidi, const NoteInfo& info = {});
    /** Sorts the notes and sets their loops and levels. Call once after adding everything. */
    void finalise();

    /** Notes for a section and vowel (falls back to Ah, then to any vowel; humming falls back
        to nothing, i.e. the synthesised hum), or nullptr. A way of singing the set has no
        recordings of falls back to natural singing. */
    const SampleLayer* layerFor (int section, VocalType vowel, Singing singing = Singing::Natural) const noexcept;
    /** Notes for exactly this section, vowel and way of singing, or nullptr (no fallback). */
    const SampleLayer* exactLayer (int section, VocalType vowel, Singing singing) const noexcept;
    /** True if some section has held notes sung this way. */
    bool has (Singing singing) const noexcept;
    /** Held notes (onsets and moves are extras on top of them). */
    int numNotes() const noexcept;
    int numSections() const noexcept;
    int numVowels() const noexcept;
    bool isEmpty() const noexcept { return numNotes() == 0; }
    /** e.g. "4 SECTIONS · 2 VOWELS · 96 NOTES" */
    std::string describe() const;

    std::string name;
    /** Unique per set, so a new set that happens to reuse an old one's address is still new. */
    const uint64_t id = nextId();

    /** Section (0-3) or vowel (0-5) named in a file path, -1 if none. */
    static int sectionFromPath (const std::string& path);
    static int vowelFromPath (const std::string& path);
    /** Pitch written in a file name as "m6312" (= MIDI 63.12), or -1 if none: such files are
        already cut to one note and need no analysis. */
    static float rootFromPath (const std::string& path);
    /** The rest of what the Sanctuary Voices file names say: "breathy" or "belt" in the path,
        a "short" (onset) or "legato" (move) folder, a move's "to6500" and "x152" (ms), and the
        singer ("f3", "m11"). */
    static NoteInfo infoFromPath (const std::string& path);

private:
    static uint64_t nextId() noexcept;
    std::array<std::array<std::array<SampleLayer, kSingings>, kVowels>, NumSections> layers;
};

/** One choir member singing recorded notes: pitch by resampling and a crossfaded sustain loop.
    A note sung from silence starts with a recorded onset; a legato step to the next note is a
    recorded move between two notes, played at the new note's pitch, which then hands over to
    the held note. Without those it crossfades between held notes when the melody moves far
    enough to need another recording. */
class SamplePlayer
{
public:
    void reset() noexcept;

    /** Renders n samples of the note into out (overwrites), scaled per sample by amp. variant
        picks between takes; jitter (0..1) staggers this singer's start inside the note.
        zoneOffset (semitones) sings from recordings that much lower or higher, which moves the
        formants: -3 gives a shorter, child-like vocal tract. */
    void render (const SampleLayer& layer, const SampleNote& note, const float* amp, float* out, int n,
                 double outRate, int variant, float jitter, float zoneOffset = 0.0f) noexcept;
    /** The same, for a pitch alone (no note changes to sing). */
    void render (const SampleLayer& layer, float hz, const float* amp, float* out, int n,
                 double outRate, int variant, float jitter, float zoneOffset = 0.0f) noexcept;

private:
    enum class Mode { Held, Onset, Move };
    struct Head
    {
        const SampleZone* zone = nullptr;
        Mode mode = Mode::Held;
        double pos = 0.0;
        int64_t loopStart = 0;      // held: this singer's own loop point in the zone
        bool onNote = false;        // pitched at the note, not the gliding pitch (after a move)
        float stretch = 0.0f;       // a move: semitones added before its slide, so a recorded
                                    // step can sing a slightly wider or narrower one
        double rate = 0.0;          // last playback rate
        bool frozen = false;        // fading out under a new onset or move: keeps its pitch
        float level = 0.0f;         // crossfade position 0..1 (weight sin(level * pi/2))
        float step = 0.0f;          // per output sample, + fading in, - fading out
    };
    static constexpr int kHeads = 4;
    static int64_t loopPointFor (const SampleZone& z, float jitter) noexcept;
    /** Makes h the sounding head, fading every other head out over the same time (at the
        pitch they had, if `freeze`). */
    void start (const Head& h, float fadeSeconds, double outRate, bool freeze = false) noexcept;
    Head* current() noexcept { return cur >= 0 ? &heads[cur] : nullptr; }

    Head heads[kHeads];
    int cur = -1;
    const SampleZone* next = nullptr;   // the held note an onset or a move hands over to
    double handover = 0.0;              // ... at this position in the current head's zone
    int lastSerial = 0;
    float lastNote = -1.0f;
    const SampleLayer* lastLayer = nullptr;
};
} // namespace sc
