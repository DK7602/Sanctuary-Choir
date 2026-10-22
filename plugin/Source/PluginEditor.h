// Sanctuary Choir - editor hosting the web UI.
#pragma once

#include "PluginProcessor.h"

#include <juce_gui_extra/juce_gui_extra.h>
#include <set>

namespace sanctuary
{
class SanctuaryEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SanctuaryEditor (SanctuaryProcessor&);
    ~SanctuaryEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int designWidth = 1448;
    static constexpr int designHeight = 1086;

private:
    struct Browser : public juce::WebBrowserComponent
    {
        using WebBrowserComponent::WebBrowserComponent;
        bool pageAboutToLoad (const juce::String& url) override;
    };

    void timerCallback() override;
    void handleUiMessage (const juce::var& message);
    void handleCommand (const juce::String& name, const juce::var& message);
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    void sendInit();
    void sendParams (const juce::StringArray& ids);
    void sendMeters();
    void sendStatus();
    void addLatencyInfo (juce::DynamicObject& o);
    void toast (const juce::String& text);
    juce::var describeParameter (juce::RangedAudioParameter& p) const;

    SanctuaryProcessor& processor;
    std::unique_ptr<juce::MemoryInputStream> zipStream;
    std::unique_ptr<juce::ZipFile> zip;
    std::unique_ptr<Browser> web;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ComponentBoundsConstrainer constrainer;
    std::set<juce::String> activeGestures;
    bool pageReady = false;
    int lastStatusVersion = -1;
    int sentLatency = -1;            // latency and sample rate last sent to the UI (the audio device may start later)
    double sentRate = -1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SanctuaryEditor)
};
} // namespace sanctuary
