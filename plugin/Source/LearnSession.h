// Sanctuary Choir - LEARN / LOAD / CLEAR / RENDER workflow.
//
// Captures the input in sync with the host timeline (or loads a file), analyses it on a
// background thread, and plays back either the learned harmony (live singers) or rendered
// stems in sync with the host. More passes of the same part can be added to a take: they are
// lined up with it, and the Words choir sings from all of them.
#pragma once

#include "TakeExport.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <sanctuary/Learn.h>

#include <mutex>
#include <optional>

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
        const float* sidechainL = nullptr;   // the band on the side-chain (recorded with the take)
        const float* sidechainR = nullptr;
        double bpm = 0.0;                    // the song's tempo and position while it plays
        double ppq = -1.0;                   // (so exported MIDI lines up with the song)
        int sigNum = 4, sigDen = 4;
    };

    /** The side-chain bus is enabled (audio thread sets it every block). */
    std::atomic<bool> bandConnected { false };

    /** Records the block when learning; may replace the input with the preview take. */
    void processInput (float* const* channels, int numChannels, int numSamples, BlockInfo& info);

    /** Decides how the engine should sing for this block: rendered stems, the learned score, or
        live. Singing from the score, the take's other passes come with it (for setPasses). */
    const sc::StemPlayback* preparePlayback (const BlockInfo& info, int numSamples, const sc::ScoreSource*& scoreOut,
                                             const float* const*& passesOut, int& numPassesOut);

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
    /** Records another pass of the take's part (in time with the song, like LEARN), or stops
        the one being recorded. */
    void togglePass();
    void loadPassFile (const juce::File& file);
    void removePass (int index);
    int getNumPasses() const;
    /** Settings that change the harmony were edited: re-harmonise (cheap). */
    void harmonySettingsChanged();
    /** Settings that change the rendered sound were edited. */
    void renderSettingsChanged();

    //==============================================================================
    // The chord lane (message thread)

    /** The learned chords, melody and keys for the chord lane (void without a take). */
    juce::var getChordLane() const;
    /** Ranked chords that could go under the notes between start and end (samples). */
    juce::var getChordOptions (int64_t start, int64_t end) const;
    /** Sing this chord between start and end (the harmony around it adapts). */
    void lockChord (int64_t start, int64_t end, int root, const juce::Array<int>& intervals);
    void unlockChords (int64_t start, int64_t end);
    void unlockAllChords();
    /** Changes whenever the lane does (new take, new harmony, a chord chosen). */
    int getLaneVersion() const noexcept { return laneVersion; }

    //==============================================================================
    // Files to drag into the DAW (message thread)

    enum class ExportItem { Midi, FullChoir, Sopranos, Altos, Tenors, Basses, AllStems };
    /** The item's files, ready to drag. MIDI is written there and then; audio is empty while it
        still has to be written (prepareExport starts that) or the render is out of date. */
    juce::StringArray getExportFiles (ExportItem item);
    /** Writes the audio files that are missing or out of date in the background (the room as
        well, for saving to a folder). onDone gets the folder (also when nothing was needed), or
        an invalid File if writing failed. automatic: asked by the page rather than the user, so
        a write that failed (disk full) is not tried again until the user asks. */
    void prepareExport (bool withRoom = false, std::function<void (juce::File)> onDone = {}, bool automatic = false);
    /** Changes whenever the sound of the mix does (set by the processor). */
    std::function<juce::String()> getMixSignature;
    /** The tempo the song plays at now, for takes loaded from a file (set by the processor). */
    std::function<double()> getHostBpm;
    /** Where exported files go (default: Documents/Sanctuary Choir/Exports). */
    juce::File exportRoot;

    Phase getPhase() const noexcept { return phase.load(); }
    bool hasTake() const;
    juce::var getStatus() const;

    // Persistence (take audio is stored with the project as FLAC)
    void writeState (juce::OutputStream& out) const;
    void readState (juce::InputStream& in);

private:
    struct PlaybackData;
    struct ScoreAdapter;
    struct Pass
    {
        std::shared_ptr<const std::vector<float>> audio;   // on the take's timeline
        sc::PassAlignment info;
    };

    void timerCallback() override;
    void finishRecording();
    void releaseCapture();
    void startAnalysis (std::vector<float> audio, double sampleRate, int64_t timelineStart, bool renderAfter,
                        std::vector<float> band = {}, std::vector<sc::BandChord> bandChords = {},
                        std::vector<Pass> passes = {});
    void beginCapture (bool pass);
    void addPass (std::vector<float> audio, double rate, std::optional<int64_t> hint);
    void setPasses (std::vector<Pass> passes);
    std::unique_ptr<PlaybackData> copyOfCurrent() const;
    Phase restingPhase() const;
    void notify (const juce::String& text);
    bool readAudioFile (const juce::File& file, std::vector<float>& mono);
    void reharmonise();
    sc::HarmonySettings currentHarmonySettings() const;
    juce::File exportFolder();
    void finishExport();
    juce::String exportStateName() const;
    void allocateChunk (int64_t index);
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
    std::array<std::atomic<float*>, maxChunks> bandChunks {};   // the side-chain, sample for sample
    std::atomic<bool> captureBand { false };
    std::vector<std::unique_ptr<float[]>> chunkStorage;
    std::vector<std::pair<std::vector<std::unique_ptr<float[]>>, juce::uint32>> chunkGraveyard;   // freed once the audio thread let go
    std::atomic<int64_t> captured { 0 };
    std::atomic<int64_t> captureStart { -1 };      // host position of the first sample, -1 = free running
    std::atomic<bool> captureStarted { false };
    std::atomic<bool> stopRequested { false };
    std::atomic<int64_t> lastCapturePos { 0 };
    int64_t armedLoudSamples = 0;
    double captureSampleRate = 48000.0;
    bool recordingPass = false;   // the capture is another pass of the take, not a new take
    bool passAlongPreview = false; // ... sung along with the take playing from its start
    bool aligningPass = false;

    // Published playback data (raw pointer for the audio thread; ownership on the message thread).
    std::atomic<PlaybackData*> live { nullptr };
    std::unique_ptr<PlaybackData> owned;
    std::vector<std::pair<std::unique_ptr<PlaybackData>, juce::uint32>> graveyard;

    std::unique_ptr<ScoreAdapter> adapter;
    std::array<std::vector<float>, sc::NumSections * 2> stemScratch;
    sc::StemPlayback stemPlayback;
    std::array<std::vector<float>, sc::kMaxExtraPasses> passScratch;
    std::array<const float*, sc::kMaxExtraPasses> passPointers {};

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
    juce::String notice;    // a one-off message for the page (a pass that could not be used)
    int noticeId = 0;
    double lastRenderSeconds = 0.0;
    int renderedSingers = 0;
    bool renderStale = false;
    bool renderWhenReady = false;     // RENDER pressed while learning, or a rendered project reloaded
    bool changedDuringWork = false;   // settings edited while analysing or rendering

    // Chords chosen by hand and the song position of the current take (copied into each
    // PlaybackData, which is what the audio thread and the saved state read).
    std::vector<sc::ChordLock> locks;
    TakeTiming takeTiming;
    std::atomic<double> captureBpm { 0.0 }, capturePpq { -1.0 };
    std::atomic<int> captureSigNum { 4 }, captureSigDen { 4 };
    int laneVersion = 0;
    int nextTakeId = 1;

    // Files written for dragging into the DAW, kept per take so a DAW that references them in
    // place never sees them change: a new render or mix gets new files.
    struct ExportFiles
    {
        int takeId = 0;
        juce::File folder;
        juce::StringArray sections;
        juce::String sectionsSignature, fullSignature, roomSignature;   // the render and mix each was written with
        juce::File full, room;
    };
    ExportFiles exported;
    juce::String exportFailedSignature;
    std::unique_ptr<std::thread> exportWorker;
    std::atomic<bool> exportDone { false }, exportCancel { false };
    std::function<void()> onExportDone;

    // The take as stored in the project, encoded once rather than on every save.
    mutable std::mutex encodeLock;
    mutable std::weak_ptr<const std::vector<float>> encodedTake;
    mutable juce::MemoryBlock encodedFlac;
    mutable std::vector<std::pair<std::weak_ptr<const std::vector<float>>, juce::MemoryBlock>> encodedPasses;
    std::atomic<int64_t> engineToTakeOffset { 0 };
};
} // namespace sanctuary
