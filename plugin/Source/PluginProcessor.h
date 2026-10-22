// Sanctuary Choir - audio processor.
#pragma once

#include "ChoirVoices.h"
#include "LearnSession.h"
#include "Parameters.h"
#include "Presets.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace sanctuary
{
class SanctuaryProcessor : public juce::AudioProcessor,
                           private juce::AudioProcessorValueTreeState::Listener,
                           private juce::Timer
{
public:
    SanctuaryProcessor();
    ~SanctuaryProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    /** Bypass keeps the reported latency, so the DAW's delay compensation stays correct. */
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sanctuary Choir"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState& getState() noexcept { return state; }
    PresetManager& getPresets() noexcept { return presets; }
    LearnSession& getLearn() noexcept { return learn; }
    ChoirVoices& getVoices() noexcept { return voices; }
    sc::EngineSnapshot takeSnapshot() { return engine.takeSnapshot(); }
    sc::EngineParams currentParams() const;
    /** Latency reported to the host: the full alignment delay in Studio mode, 0 in Live mode. */
    int reportedLatency() const;
    int getAlignedLatency() const noexcept { return engine.getLatencySamples(); }
    int getLiveChoirLatency() const noexcept { return engine.getLiveChoirLatency(); }
    bool isStandaloneApp() const noexcept { return standaloneApp; }
    /** Song tempo while the host plays (0 when stopped or unknown), the beat position at the last
        block, and when that block ran (Time::getMillisecondCounterHiRes). */
    std::atomic<double> songBpm { 0.0 }, songBeat { 0.0 }, songBeatTime { 0.0 };

    /** Parameter ids changed since the last call (message thread). */
    juce::StringArray takeChangedParameters();
    /** Bumped whenever presets/learn status change, so the editor knows to refresh. */
    std::atomic<int> statusVersion { 0 };

    juce::String uiTab { "perform" };
    int editorWidth = 0, editorHeight = 0;

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void timerCallback() override;
    void updateMidi (const juce::MidiBuffer& midi);

    juce::AudioProcessorValueTreeState state;
    ParameterCache cache;
    PresetManager presets;
    LearnSession learn;
    ChoirVoices voices;
    sc::ChoirEngine engine;

    // MIDI state (audio thread)
    std::array<float, 128> heldVelocity {};
    bool sustain = false;
    std::array<bool, 128> sustained {};

    // Change tracking for the UI and the learn session
    juce::StringArray paramIds;
    std::vector<std::atomic<bool>> dirty;
    std::atomic<bool> harmonyDirty { false }, renderDirty { false };
    juce::uint32 lastHarmonyChange = 0;
    int64_t engineWritten = 0;
    bool standaloneApp = false;

    std::atomic<int> rememberedKey { -1 };   // last auto-detected key, saved with the project

    // Dry delay used while bypassed
    juce::AudioBuffer<float> bypassDelay;
    int bypassPos = 0;
    void writeBypassDelay (const juce::AudioBuffer<float>& buffer, int n, int numIn);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SanctuaryProcessor)
};
} // namespace sanctuary
