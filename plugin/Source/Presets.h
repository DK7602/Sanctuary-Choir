// Sanctuary Choir - factory and user presets.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace sanctuary
{
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& state);

    struct Entry
    {
        juce::String name;
        juce::String category;                   // factory presets: "Worship", "Gospel", ...
        bool factory = true;
        juce::File file;                         // user presets only
        std::vector<std::pair<juce::String, float>> values;
    };

    const std::vector<Entry>& getEntries() const noexcept { return entries; }
    int getCurrentIndex() const noexcept { return current; }
    juce::String getCurrentName() const;

    void load (int index);
    void next();
    void previous();
    bool saveUser (const juce::String& name);
    bool deleteUser (int index);
    void refresh();

    /** Restore the selection after the plugin state is loaded. */
    void setCurrentByName (const juce::String& name);

    /** The current preset changed (loaded, restored or renamed in the list): the host is told,
        so the program it saves with the song is this one. */
    std::function<void()> onCurrentChanged;

    /** Preset list for the UI: [{ name, factory, category }]. */
    juce::var toVar() const;

    /** Part of a sound (presets and scenes), as opposed to settings or the song's key. */
    static bool isSoundParameter (const juce::String& paramID);

    static juce::File getUserPresetFolder();

private:
    juce::AudioProcessorValueTreeState& state;
    std::vector<Entry> entries;
    int numFactory = 0;
    int current = 0;
    juce::String currentName;
};
} // namespace sanctuary
