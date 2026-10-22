// Sanctuary Choir - LEARN / LOAD / CLEAR / RENDER workflow.
//
// Captures the input in sync with the host timeline (or loads a file), analyses it on a
// background thread, and plays back either the learned harmony (live singers) or rendered
// stems in sync with the host.
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <sanctuary/Learn.h>

#include <mutex>

namespace sanctuary
{
class LearnSession : private juce::Timer
{
public:
    enum class Phase { Empty, Recording, Analysing, Learned, Rendering, Rendered, Failed };

    LearnSession();
    ~LearnSession() override;

    std::function<sc::EngineParams()> getParams;   // set by the processor
    std::function<std::shared_ptr<const sc::SampleSet>()> getVoices;   // recorded choir voices, if any
    std::function<bool()> voicesBusy;               // recorded voices still loading: renders wait for them
    bool standalone = false;                        // capture immediately instead of waiting for the host
    std::function<void()> onStatusChanged;          // message thread

    void prepare (double sampleRate);

    //==============================================================================
    // Audio thread

    struct BlockInfo
    {
        bool hostPlaying = false;
        bool hasPosition = false;
        int64_t hostPosition = 0;   // samples
        int64_t engineWritten = 0;  // engine input samples consumed before this block
        int latency = 0;
    };

    /** Records the block when learning; may replace the input with the preview take. */
    void processInput (float* const* channels, int numChannels, int numSamples, BlockInfo& info);

    /** Decides how the engine should sing for this block: rendered stems, the learned score, or live. */
    const sc::StemPlayback* preparePlayback (const BlockInfo& info, int numSamples, const sc::ScoreSource*& scoreOut);

    //==============================================================================
    // Message thread commands
    void toggleLearn();
    void startRecording();
    void stopRecording();
    void loadFile (const juce::File& file);
    void clear();
    void render();
    void cancelWork();
    void togglePreview();
    void exportStems (std::function<void (juce::File)> onDone);
    /** Settings that change the harmony were edited: re-harmonise (cheap). */
    void harmonySettingsChanged();
    /** Settings that change the rendered sound were edited. */
    void renderSettingsChanged();

    Phase getPhase() const noexcept { return phase.load(); }
    bool hasTake() const;
    juce::var getStatus() const;

    // Persistence (take audio is stored with the project as FLAC)
    void writeState (juce::OutputStream& out) const;
    void readState (juce::InputStream& in);

private:
    struct PlaybackData;
    struct ScoreAdapter;

    void timerCallback() override;
    void finishRecording();
    void releaseCapture();
    void startAnalysis (std::vector<float> audio, double sampleRate, int64_t timelineStart, bool renderAfter);
    void matchSessionRate();
    void publish (std::unique_ptr<PlaybackData> data);
    void setPhase (Phase p);
    std::shared_ptr<const std::vector<float>> currentTake() const;
    juce::String describe() const;

    double sr = 48000.0;
    bool prepared = false;
    std::atomic<Phase> phase { Phase::Empty };

    // Capture: a fixed table of chunks filled by the audio thread, allocated ahead by the timer.
    static constexpr int chunkSize = 1 << 15;
    static constexpr int maxChunks = 1024;   // ~11 minutes at 48 kHz
    static constexpr int kChunksAhead = 32;  // ~20 s allocated ahead, for faster-than-realtime bounces
    std::array<std::atomic<float*>, maxChunks> chunks {};
    std::vector<std::unique_ptr<float[]>> chunkStorage;
    std::vector<std::pair<std::vector<std::unique_ptr<float[]>>, juce::uint32>> chunkGraveyard;   // freed once the audio thread let go
    std::atomic<int64_t> captured { 0 };
    std::atomic<int64_t> captureStart { -1 };      // host position of the first sample, -1 = free running
    std::atomic<bool> captureStarted { false };
    std::atomic<bool> stopRequested { false };
    std::atomic<int64_t> lastCapturePos { 0 };
    int64_t armedLoudSamples = 0;
    double captureSampleRate = 48000.0;

    // Published playback data (raw pointer for the audio thread; ownership on the message thread).
    std::atomic<PlaybackData*> live { nullptr };
    std::unique_ptr<PlaybackData> owned;
    std::vector<std::pair<std::unique_ptr<PlaybackData>, juce::uint32>> graveyard;

    std::unique_ptr<ScoreAdapter> adapter;
    std::array<std::vector<float>, sc::NumSections * 2> stemScratch;
    sc::StemPlayback stemPlayback;

    // Preview transport (play the learned take through the choir without the host)
    std::atomic<bool> previewPlaying { false };
    std::atomic<int64_t> previewPos { 0 };

    // Background work
    std::unique_ptr<std::thread> worker;
    std::atomic<bool> cancel { false };
    std::atomic<float> progress { 0.0f };
    std::atomic<bool> workDone { false };
    std::function<void()> onWorkDone;   // runs on the message thread after the worker finishes
    juce::String lastError;
    double lastRenderSeconds = 0.0;
    int renderedSingers = 0;
    bool renderStale = false;
    bool renderWhenReady = false;     // RENDER pressed while learning, or a rendered project reloaded
    bool changedDuringWork = false;   // settings edited while analysing or rendering

    // The take as stored in the project, encoded once rather than on every save.
    mutable std::mutex encodeLock;
    mutable std::weak_ptr<const std::vector<float>> encodedTake;
    mutable juce::MemoryBlock encodedFlac;
    std::atomic<int64_t> engineToTakeOffset { 0 };
};
} // namespace sanctuary
