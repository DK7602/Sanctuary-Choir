#pragma once

// Sanctuary Choir - AI singers: the singers trained for VoxaVersa (RVC voice models), run through ONNX Runtime
// to sing a section's part with the lead's words. Ported from VoxaVersa's engine (Source/neural/), offline only.
//
// Nothing is bundled: the runtime, the content encoder and the voice packs are the ones VoxaVersa installed
// (its plug-in folder, and %APPDATA%\VoxaVersa or ~/Library/Application Support/VoxaVersa).

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace sanctuary::ai
{
/** VoxaVersa's user data folder (Voices, Encoders, Runtime), or the one the SANCTUARY_VOXAVERSA_DIR
    environment variable names. */
juce::File getVoxaVersaDataDirectory();
inline juce::File getVoicesDirectory()   { return getVoxaVersaDataDirectory().getChildFile ("Voices"); }
inline juce::File getEncodersDirectory() { return getVoxaVersaDataDirectory().getChildFile ("Encoders"); }

/** Dynamically loads the ONNX Runtime shared library at run time (no link-time dependency).
    If the library cannot be found, AI singers are unavailable and the choir sings as before. */
class OnnxRuntime
{
public:
    static OnnxRuntime& getInstance();

    /** Attempts to load the runtime from the default search locations (idempotent). */
    bool ensureLoaded();
    bool isAvailable() const noexcept;
    juce::String getStatusMessage() const;
    juce::File getLoadedLibraryFile() const;

    /** Locations searched, in order: next to Sanctuary's binary, VoxaVersa's Runtime folder, VoxaVersa's
        plug-ins and app, then the system's paths. */
    static juce::Array<juce::File> getSearchLocations();

    struct Impl;
    Impl& getImpl() const noexcept { return *impl; }

private:
    OnnxRuntime();
    ~OnnxRuntime();
    std::unique_ptr<Impl> impl;
};

/** An RVC-compatible voice conversion model (content encoder + generator), loaded from a voice pack folder.
    Immutable after loading and safe to share between threads. */
class NeuralVoiceModel
{
public:
    ~NeuralVoiceModel();

    /** Loads the model of a voice pack folder (voice.json + model). Returns nullptr and sets `error` on
        failure. `cancel` (optional) stops it between steps. */
    static std::shared_ptr<NeuralVoiceModel> load (const juce::File& packDirectory, juce::String& error,
                                                   const std::atomic<bool>* cancel = nullptr);

    int getModelSampleRate() const noexcept;
    juce::String getDescription() const;

    /** Offline conversion of a complete mono clip: the singer sings what the clip says, at the pitch
        `f0Hz` gives at `f0FrameRate` frames/second (0 = unvoiced). The result is written to `output` at
        `inputSampleRate`, lined up with the input. `progress` (optional) gets 0..1 as it goes. */
    bool convert (const juce::AudioBuffer<float>& monoInput, double inputSampleRate,
                  const std::vector<float>& f0Hz, double f0FrameRate,
                  juce::AudioBuffer<float>& output,
                  const std::atomic<bool>* cancel, juce::String& error,
                  const std::function<void (float)>& progress = {}) const;

    struct Impl;
    Impl& getImpl() const noexcept { return *impl; }

private:
    NeuralVoiceModel();
    std::unique_ptr<Impl> impl;
};
} // namespace sanctuary::ai
