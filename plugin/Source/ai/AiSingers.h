#pragma once

// Sanctuary Choir - AI singers: the singers trained for VoxaVersa sing the choir's parts with the words of
// a learned take. After LEARN, RENDER has each cast singer sing their section's part (the notes the choir
// worked out, in the octave that suits the singer, wherever the lead sings) with the lead's words; the
// section's Words singers then sing from those voices, spread over the stage as usual.

#include "NeuralVoice.h"

#include <sanctuary/Learn.h>

#include <map>
#include <mutex>

namespace sanctuary::ai
{
/** One of VoxaVersa's trained singers (a voice pack with an AI model). */
struct Singer
{
    juce::String id;            // the pack's folder name (stable: casts refer to it)
    juce::String name;          // as VoxaVersa shows it (the voice assignment's name, else voice.json's)
    bool female = false;
    int low = 48, high = 72;    // the range the singer was trained on (MIDI)
    float median = 60.0f;       // the singer's middle note: the model sounds most like them around it
    bool classical = false;     // classically trained (the VocalSet singers)
    juce::String licence;
    juce::File folder;
};

/** The singers in VoxaVersa's Voices folder (or `voicesDir`), with the names and ranges of its voice
    assignment, sorted by name. Packs without an AI model are left out. */
std::vector<Singer> findSingers (const juce::File& voicesDir = getVoicesDirectory());

/** Up to this many AI singers per section (each sings the part once; the section's singers share them). */
inline constexpr int kMaxPerSection = sc::kMaxExtraPasses;

/** The singers (ids) cast in each section. An empty section takes the automatic choice. */
using Cast = std::array<juce::StringArray, sc::NumSections>;

/** Who sings each section when nobody was chosen: `perSection` singers per section of the right voice
    (women for sopranos and altos, men for tenors and basses, a high man or a low woman when there is no one
    else) whose middle note is nearest the section's, contemporary singers for the modern styles (Gospel,
    Worship, Spiritual, Cinematic) and classical ones for Sacred, Gregorian and Chamber; each singer once while
    there are enough. Sections already cast in `chosen` keep their singers. */
Cast completeCast (const std::vector<Singer>& singers, const Cast& chosen, int perSection, sc::Style style,
                   const std::array<sc::SectionRange, sc::NumSections>& ranges);

/** The pitch (Hz, 100 frames a second, 0 = silent) the singer in place `slot` of `section` sings over the
    take: the section's notes from the score (the slot-th of a divisi), moved by whole octaves into the
    singer's range, wherever the lead sings; unpitched where the lead's sound has no pitch (consonants,
    breaths), so the singer says them as the lead does. */
std::vector<float> partPitch (const sc::LearnedScore& score, const sc::TakeAnalysis& analysis, int64_t length,
                              double sampleRate, int section, int slot, const Singer& singer, float a4 = 440.0f);

/** An AI singer's recording of a part, lined up with the take (16-bit). */
struct Voice
{
    std::shared_ptr<const std::vector<int16_t>> pcm;
    float scale = 1.0f / 32767.0f;
    juce::String singerName;
};

/** What an AI render needs from the session. */
struct Plan
{
    bool enabled = false;
    int perSection = 2;
    Cast cast;
};

/** Sings the cast's parts for a take: each voice from the cache on disk if it was sung before (same take,
    same notes, same singer), else through the singer's model (minutes for a song: the content of the take
    is heard once per singer). Thread-safe; one render at a time. */
class Renderer
{
public:
    struct Result
    {
        std::array<std::vector<Voice>, sc::NumSections> voices;
        juce::StringArray problems;   // singers that could not sing (missing model, no engine, ...)
        int sung = 0, fromCache = 0;
    };

    /** `progress` gets 0..1 and the singer being sung. False if cancelled. */
    bool render (const std::vector<float>& take, double sampleRate, const sc::TakeAnalysis& analysis,
                 const sc::LearnedScore& score, const Cast& cast, const std::vector<Singer>& singers, float a4,
                 Result& result, const std::function<void (float, const juce::String&)>& progress,
                 const std::atomic<bool>* cancel);

    /** Where finished voices are kept between renders and sessions. */
    static juce::File getCacheDirectory();
    /** Deletes the oldest cached voices beyond `maxBytes`. */
    static void trimCache (int64_t maxBytes = (int64_t) 3 << 30);

private:
    std::mutex lock;
};

/** Status for the UI: the engine, the singers found and where VoxaVersa keeps them. */
juce::var describeAvailability (const std::vector<Singer>& singers);
} // namespace sanctuary::ai
