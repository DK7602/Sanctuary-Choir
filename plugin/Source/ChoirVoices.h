// Sanctuary Choir - recorded choir voices for the vowel modes.
//
// Loads a folder of recordings (one held note per file, or takes with several notes) on a
// background thread and hands the finished set to the audio thread. Files are sorted into
// sections and vowels by their names, e.g. "Sopranos/soprano_ah_take1.wav" or "bass hum.wav".
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <sanctuary/SampleSet.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace sanctuary
{
class ChoirVoices : private juce::Timer
{
public:
    ChoirVoices();
    ~ChoirVoices() override;

    std::function<void()> onStatusChanged;   // message thread
    std::function<void()> onSetChanged;      // message thread: a different set is now live

    /** Loads every recording under the folder (message thread; returns at once). Sets that are
        already loaded by another instance of the plug-in are shared, not loaded twice. */
    void loadFolder (const juce::File& folder);
    /** Back to the built-in synthesised voices. */
    void clear();

    /** The recorded voices installed with Sanctuary Choir (Sanctuary Voices), if present. */
    static juce::File findBundledVoices();
    bool usesBundledVoices() const { return getFolder() == findBundledVoices() && getFolder() != juce::File(); }
    /** The user switched to the built-in voices on purpose (saved with the project). */
    bool userChoseBuiltIn() const noexcept { return choseBuiltIn; }

    /** The set the audio thread should sing with, or nullptr (audio thread, lock-free). */
    const sc::SampleSet* getLive() const noexcept { return live.load (std::memory_order_acquire); }

    /** The live set, kept alive for as long as the caller holds it (message thread). */
    std::shared_ptr<const sc::SampleSet> getCurrent() const;
    juce::File getFolder() const;
    /** { loading, name, text, folder, error } for the UI. */
    juce::var getStatus() const;

private:
    void timerCallback() override;
    void publish (std::shared_ptr<sc::SampleSet> set);

    std::atomic<const sc::SampleSet*> live { nullptr };
    std::shared_ptr<sc::SampleSet> current;
    std::vector<std::pair<std::shared_ptr<sc::SampleSet>, juce::uint32>> retired;   // freed once the audio thread let go

    mutable std::mutex lock;
    juce::File folder;
    juce::String error;
    std::atomic<bool> loading { false }, cancel { false };
    bool wasLoading = false, choseBuiltIn = false;
    std::atomic<float> progress { 0.0f };
    std::unique_ptr<std::thread> worker;
    std::shared_ptr<sc::SampleSet> pending;   // finished on the worker, published on the timer
};
} // namespace sanctuary
