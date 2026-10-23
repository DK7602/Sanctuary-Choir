// Sanctuary Choir - plugin parameters.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <sanctuary/ChoirEngine.h>

namespace sanctuary
{
namespace ParamIDs
{
    // Perform
    inline constexpr const char* volume = "volume";
    inline constexpr const char* choirSize = "choirSize";
    inline constexpr const char* space = "space";
    inline constexpr const char* reverb = "reverb";
    inline constexpr const char* tone = "tone";
    inline constexpr const char* presence = "presence";
    inline constexpr const char* harmonyMode = "harmonyMode";
    inline constexpr const char* key = "key";
    inline constexpr const char* scale = "scale";
    inline constexpr const char* style = "style";
    inline constexpr const char* vocalType = "vocalType";
    inline constexpr const char* freeze = "freeze";
    inline constexpr const char* response = "response";
    inline constexpr const char* ensemble = "ensemble";
    inline constexpr const char* scene = "scene";
    // Stage page: the microphones
    inline constexpr const char* micClose = "micClose";
    inline constexpr const char* micTree = "micTree";
    inline constexpr const char* micRoom = "micRoom";
    // ... and where your own voice stands
    inline constexpr const char* leadOnStage = "leadOnStage";
    inline constexpr const char* leadPan = "leadPan";
    inline constexpr const char* leadDepth = "leadDepth";
    // Mix
    inline constexpr const char* leadLevel = "leadLevel";
    inline constexpr const char* choirLevel = "choirLevel";
    inline constexpr const char* humanize = "humanize";
    inline constexpr const char* vibrato = "vibrato";
    inline constexpr const char* glide = "glide";
    inline constexpr const char* formant = "formant";
    inline constexpr const char* pitchCorrection = "pitchCorrection";
    inline constexpr const char* harmonySpeed = "harmonySpeed";
    inline constexpr const char* breath = "breath";
    inline constexpr const char* singing = "singing";
    inline constexpr const char* realVoices = "realVoices";
    inline constexpr const char* energy = "energy";
    inline constexpr const char* subOctave = "subOctave";
    // FX
    inline constexpr const char* venue = "venue";
    inline constexpr const char* decay = "decay";
    inline constexpr const char* preDelay = "preDelay";
    inline constexpr const char* brightness = "brightness";
    inline constexpr const char* leadReverb = "leadReverb";
    inline constexpr const char* lowCut = "lowCut";
    inline constexpr const char* highCut = "highCut";
    inline constexpr const char* warmth = "warmth";
    inline constexpr const char* glue = "glue";
    inline constexpr const char* rhythmPattern = "rhythmPattern";
    inline constexpr const char* rhythmRate = "rhythmRate";
    inline constexpr const char* rhythmLength = "rhythmLength";
    inline constexpr const char* rhythmDepth = "rhythmDepth";
    inline constexpr const char* rhythmSections = "rhythmSections";
    // Settings
    inline constexpr const char* midiEnabled = "midiEnabled";
    inline constexpr const char* midiParts = "midiParts";
    inline constexpr const char* followBand = "followBand";
    inline constexpr const char* tuning = "tuning";
    inline constexpr const char* latencyMode = "latencyMode";
}

/** Section parameter ids: prefix + "Level" / "Pan" / "Width" / "Depth" / "Mute" / "Solo" / "Low" / "High".
    Pan is where the section stands on the stage (-1 left .. 1 right); before the stage (state
    and presets without a stage version) it was an offset from the section's home. */
juce::String sectionParamID (int section, const char* suffix);
const char* sectionPrefix (int section);   // "sop", "alt", "ten", "bas"
const char* sectionName (int section);     // "Soprano", ...

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
/** The Scale parameter's last choice, "Auto": listen for major or minor. It comes after the
    scales so saved projects keep their scale. */
constexpr int kScaleAuto = (int) sc::ScaleType::Count;

/** Fast access to the raw parameter values for the audio thread. */
class ParameterCache
{
public:
    explicit ParameterCache (juce::AudioProcessorValueTreeState& state);
    void fill (sc::EngineParams& p) const;

private:
    struct Ptrs;
    std::shared_ptr<Ptrs> ptrs;
};

/** Saved states, scenes and presets from before the stage stored Pan as an offset from the
    section's home: where that section stands now. */
float panFromOffset (int section, float offset);
constexpr int kStageVersion = 2;

/** Parameters that change the learned harmony (score) when edited. */
bool affectsHarmony (const juce::String& paramID);
/** Parameters that make a rendered take stale when edited. */
bool affectsRender (const juce::String& paramID);
} // namespace sanctuary
