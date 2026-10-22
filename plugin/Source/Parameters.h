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
    // Settings
    inline constexpr const char* midiEnabled = "midiEnabled";
    inline constexpr const char* tuning = "tuning";
    inline constexpr const char* latencyMode = "latencyMode";
}

/** Section parameter ids: prefix + "Level" / "Pan" / "Mute" / "Solo" / "Low" / "High". */
juce::String sectionParamID (int section, const char* suffix);
const char* sectionPrefix (int section);   // "sop", "alt", "ten", "bas"
const char* sectionName (int section);     // "Soprano", ...

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

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

/** Parameters that change the learned harmony (score) when edited. */
bool affectsHarmony (const juce::String& paramID);
/** Parameters that make a rendered take stale when edited. */
bool affectsRender (const juce::String& paramID);
} // namespace sanctuary
