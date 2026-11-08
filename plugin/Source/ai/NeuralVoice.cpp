#include "NeuralVoice.h"
#include "RvcModel.h"

#include <limits>
#include <mutex>

namespace sanctuary::ai
{
juce::File getVoxaVersaDataDirectory()
{
    const auto custom = juce::SystemStats::getEnvironmentVariable ("SANCTUARY_VOXAVERSA_DIR", {});
    if (custom.isNotEmpty() && juce::File::isAbsolutePath (custom))
        return juce::File (custom);

    // Where VoxaVersa keeps its voices (VoiceLibrary::getUserDataDirectory there).
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
#if JUCE_MAC
        .getChildFile ("Application Support")
#endif
        .getChildFile ("VoxaVersa");
}

juce::File getSanctuaryAiDirectory()
{
    const auto custom = juce::SystemStats::getEnvironmentVariable ("SANCTUARY_AI_DIR", {});
    if (custom.isNotEmpty() && juce::File::isAbsolutePath (custom))
        return juce::File (custom);
    // Next to Sanctuary's presets.
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Sanctuary Choir").getChildFile ("AI Singers");
}

juce::File getSanctuaryRuntimeDirectory()
{
   #if JUCE_MAC
    return juce::File ("/Library/Application Support/Sanctuary Choir/Runtime");
   #elif JUCE_WINDOWS
    return juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory).getChildFile ("Sanctuary Choir").getChildFile ("Runtime");
   #else
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".local/share/Sanctuary Choir/Runtime");
   #endif
}


//==============================================================================
NeuralVoiceModel::NeuralVoiceModel() : impl (std::make_unique<Impl>()) {}
NeuralVoiceModel::~NeuralVoiceModel() = default;

std::shared_ptr<NeuralVoiceModel> NeuralVoiceModel::load (const juce::File& packDirectory, juce::String& error,
                                                          const std::atomic<bool>* cancel)
{
    error.clear();
    auto& runtime = OnnxRuntime::getInstance();

    if (! runtime.ensureLoaded())
    {
        error = runtime.getStatusMessage();
        return nullptr;
    }

    auto config = ModelConfig::fromPack (packDirectory, error);
    if (error.isNotEmpty())
        return nullptr;

    std::shared_ptr<NeuralVoiceModel> model (new NeuralVoiceModel());
    if (! model->impl->initialise (config, error, cancel))
        return nullptr;

    return model;
}

int NeuralVoiceModel::getModelSampleRate() const noexcept   { return impl->sampleRate; }
juce::String NeuralVoiceModel::getDescription() const       { return impl->description; }

bool NeuralVoiceModel::convert (const juce::AudioBuffer<float>& monoInput, double inputSampleRate,
                                const std::vector<float>& f0Hz, double f0FrameRate,
                                juce::AudioBuffer<float>& output,
                                const std::atomic<bool>* cancel, juce::String& error,
                                const std::function<void (float)>& progress) const
{
    error.clear();
    return impl->convert (monoInput, inputSampleRate, f0Hz, f0FrameRate, output, cancel, error, progress);
}

} // namespace sanctuary::ai
