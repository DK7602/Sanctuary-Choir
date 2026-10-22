#include "LearnSession.h"

#include <mutex>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace sanctuary
{
using namespace juce;

//==============================================================================
/** Section stems stored as 16-bit to keep memory reasonable for long takes. */
struct StemSet
{
    int id = 0;   // each render gets its own (exports know which render they came from)
    int64_t length = 0;
    std::array<std::vector<int16_t>, sc::NumSections * 2> data;
    std::array<float, sc::NumSections * 2> scale {};
};

struct LearnSession::PlaybackData
{
    double sampleRate = 48000.0;
    int64_t timelineStart = -1;
    std::shared_ptr<const std::vector<float>> take;
    std::shared_ptr<const sc::TakeAnalysis> analysis;
    sc::LearnedScore score;
    std::shared_ptr<const StemSet> stems;
    std::atomic<bool> stale { false };
    int takeId = 0;
    std::vector<sc::ChordLock> locks;   // the chords chosen by hand that the score was written with
    TakeTiming timing;
    int64_t length() const { return take != nullptr ? (int64_t) take->size() : 0; }
};

struct LearnSession::ScoreAdapter : public sc::ScoreSource
{
    const sc::LearnedScore* score = nullptr;
    std::atomic<int64_t> offset { 0 };

    bool voicingAt (int64_t t, sc::Voicing& out, int& lead, int& sop) const override
    {
        return score != nullptr && score->voicingAt (t + offset.load (std::memory_order_relaxed), out, lead, sop);
    }
};

static std::shared_ptr<StemSet> toStemSet (const sc::RenderedStems& r)
{
    static std::atomic<int> nextId { 1 };
    auto s = std::make_shared<StemSet>();
    s->id = nextId++;
    s->length = r.length;
    for (size_t ch = 0; ch < s->data.size(); ++ch)
    {
        const auto& src = r.channels[ch];
        float peak = 1.0e-6f;
        for (float v : src) peak = std::max (peak, std::abs (v));
        const float scale = peak / 32767.0f;
        s->scale[ch] = scale;
        s->data[ch].resize (src.size());
        for (size_t i = 0; i < src.size(); ++i)
            s->data[ch][i] = (int16_t) jlimit (-32767, 32767, roundToInt (src[i] / scale));
    }
    return s;
}

static sc::HarmonySettings harmonySettingsFor (const sc::EngineParams& p)
{
    sc::HarmonySettings hs;
    hs.key = p.key;
    hs.scale = p.scale;
    hs.mode = p.harmonyMode;
    hs.style = p.style;
    for (int s = 0; s < sc::NumSections; ++s)
        hs.ranges[(size_t) s] = sc::ensembleRange (p.ensemble, s, p.sections[(size_t) s].range);   // as the engine sings them
    hs.wordsMode = p.vocalType == sc::VocalType::Words;
    hs.speed = p.harmonySpeed / 100.0f;
    hs.followBand = p.followBand;

    // Mirror the engine's singer allocation so small choirs get sensible parts.
    const int n = sc::singersForChoirSize (p.choirSize);
    std::array<bool, sc::NumSections> sec { true, true, true, true };
    if (p.harmonyMode == sc::HarmonyMode::UpperVoices) sec = { true, true, false, false };
    if (p.harmonyMode == sc::HarmonyMode::LowerVoices) sec = { false, false, true, true };
    std::array<bool, sc::NumSections> active { false, false, false, false };
    int given = 0;
    for (int s : { (int) sc::Alto, (int) sc::Bass, (int) sc::Tenor, (int) sc::Soprano })
        if (sec[(size_t) s] && given < n) { active[(size_t) s] = true; ++given; }
    hs.active = active;
    return hs;
}

//==============================================================================
LearnSession::LearnSession() : adapter (std::make_unique<ScoreAdapter>())
{
    for (auto& c : chunks) c.store (nullptr);
    for (auto& c : bandChunks) c.store (nullptr);
    startTimerHz (20);
}

LearnSession::~LearnSession()
{
    stopTimer();
    cancel = true;
    exportCancel = true;
    if (worker != nullptr && worker->joinable())
        worker->join();
    if (exportWorker != nullptr && exportWorker->joinable())
        exportWorker->join();
    live.store (nullptr);
}

void LearnSession::prepare (double sampleRate)
{
    sr = sampleRate;
    prepared = true;
    for (auto& ch : stemScratch)
        ch.assign (16384, 0.0f);
}

//==============================================================================
void LearnSession::processInput (float* const* channels, int numChannels, int n, BlockInfo& info)
{
    if (phase.load (std::memory_order_relaxed) == Phase::Recording && ! stopRequested.load())
    {
        bool write = true;
        if (! captureStarted.load())
        {
            if (captureStart.load() == -2)           // armed: wait for the host transport
            {
                write = info.hostPlaying && info.hasPosition;
                if (write)
                {
                    captureStart = info.hostPosition;
                }
                else
                {
                    // No transport? Start anyway once someone is clearly singing.
                    float peak = 0.0f;
                    for (int ch = 0; ch < numChannels; ++ch)
                        for (int i = 0; i < n; ++i)
                            peak = std::max (peak, std::abs (channels[ch][i]));
                    armedLoudSamples = peak > 0.03f ? armedLoudSamples + n : 0;
                    if (armedLoudSamples > (int64_t) (captureSampleRate * 0.3))
                    {
                        captureStart = -1;
                        write = true;
                    }
                }
            }
            else
            {
                captureStart = (info.hostPlaying && info.hasPosition) ? info.hostPosition : -1;
            }
            if (write)
            {
                captureStarted = true;
                lastCapturePos = info.hostPosition;
                // Where the take starts in the song, so its MIDI lines up with the song.
                captureBpm = info.bpm;
                capturePpq = captureStart.load() >= 0 ? info.ppq : -1.0;
                captureSigNum = info.sigNum;
                captureSigDen = info.sigDen;
            }
        }
        else if (captureStart.load() >= 0)
        {
            // Tied to the timeline: stop when the transport stops or jumps (loop, relocate).
            if (! info.hostPlaying || ! info.hasPosition || std::llabs (info.hostPosition - lastCapturePos.load()) > 2)
            {
                stopRequested = true;
                write = false;
            }
        }

        if (write && captureStarted.load())
        {
            int64_t idx = captured.load();
            const bool band = captureBand.load (std::memory_order_relaxed);
            for (int i = 0; i < n; ++i, ++idx)
            {
                const int64_t c = idx / chunkSize;
                float* chunk = c < maxChunks ? chunks[(size_t) c].load (std::memory_order_acquire) : nullptr;
                if (chunk == nullptr)
                {
                    stopRequested = true;
                    break;
                }
                float v = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                    v += channels[ch][i];
                chunk[idx % chunkSize] = numChannels > 0 ? v / (float) numChannels : 0.0f;
                if (band)
                {
                    if (float* bc = bandChunks[(size_t) c].load (std::memory_order_acquire))
                    {
                        const float l = info.sidechainL != nullptr ? info.sidechainL[i] : 0.0f;
                        bc[idx % chunkSize] = info.sidechainR != nullptr ? 0.5f * (l + info.sidechainR[i]) : l;
                    }
                }
            }
            captured = idx;
            lastCapturePos = info.hostPosition + n;
        }
    }

    // Preview: feed the learned take through the choir instead of the live input.
    if (previewPlaying.load (std::memory_order_relaxed))
    {
        PlaybackData* d = live.load (std::memory_order_acquire);
        const std::vector<float>* take = d != nullptr ? d->take.get() : nullptr;
        if (take == nullptr)
        {
            previewPlaying = false;
            return;
        }
        const int64_t pos = previewPos.load();
        for (int i = 0; i < n; ++i)
        {
            const int64_t k = pos + i;
            const float v = (k >= 0 && k < (int64_t) take->size()) ? (*take)[(size_t) k] : 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                channels[ch][i] = v;
        }
        info.hostPlaying = true;
        info.hasPosition = true;
        info.hostPosition = pos;   // the preview runs on the take's own timeline
        previewPos = pos + n;
        if (pos > (int64_t) take->size() + (int64_t) (d->sampleRate * 4.0))
            previewPlaying = false;
    }
}

const sc::StemPlayback* LearnSession::preparePlayback (const BlockInfo& info, int n, const sc::ScoreSource*& scoreOut)
{
    scoreOut = nullptr;
    PlaybackData* d = live.load (std::memory_order_acquire);
    if (d == nullptr || d->analysis == nullptr || d->analysis->isEmpty())
        return nullptr;
    if (std::abs (d->sampleRate - sr) > 1.0)
        return nullptr;   // learned at another sample rate: being converted (see matchSessionRate)

    const bool preview = previewPlaying.load (std::memory_order_relaxed);
    const int64_t timelineStart = preview ? 0 : d->timelineStart;
    if (timelineStart < 0 || ! info.hostPlaying || ! info.hasPosition)
        return nullptr;

    const int64_t takePos = info.hostPosition - timelineStart;   // take time of the input at block start
    const int64_t len = std::max (d->length(), d->stems != nullptr ? d->stems->length : (int64_t) 0);
    if (takePos + n < -(int64_t) (d->sampleRate * 0.5) || takePos - info.latency > len + (int64_t) (d->sampleRate * 1.0))
        return nullptr;

    adapter->score = &d->score;
    adapter->offset.store (takePos - info.engineWritten, std::memory_order_relaxed);
    scoreOut = adapter.get();

    const StemSet* stems = d->stems.get();
    if (stems == nullptr || d->stale.load() || n > (int) stemScratch[0].size())
        return nullptr;

    // Output sample i corresponds to take time takePos + i - latency.
    const int64_t t0 = takePos - info.latency;
    for (size_t ch = 0; ch < stemScratch.size(); ++ch)
    {
        float* dst = stemScratch[ch].data();
        const auto& src = stems->data[ch];
        const float scale = stems->scale[ch];
        for (int i = 0; i < n; ++i)
        {
            const int64_t k = t0 + i;
            dst[i] = (k >= 0 && k < (int64_t) src.size()) ? (float) src[(size_t) k] * scale : 0.0f;
        }
        stemPlayback.sections[ch] = dst;
    }
    return &stemPlayback;
}

//==============================================================================
void LearnSession::setPhase (Phase p)
{
    phase = p;
    if (onStatusChanged)
        onStatusChanged();
}

void LearnSession::toggleLearn()
{
    if (phase.load() == Phase::Recording)
        stopRecording();
    else
        startRecording();
}

void LearnSession::startRecording()
{
    cancelWork();
    previewPlaying = false;
    renderWhenReady = false;

    // Pre-allocate the first few chunks; the timer keeps allocating ahead. With a band on the
    // side-chain, it is recorded too, so LEARN can harmonise with its chords.
    captureBand = bandConnected.load();
    for (int i = 0; i < kChunksAhead; ++i)
        allocateChunk (i);
    captured = 0;
    captureStarted = false;
    stopRequested = false;
    captureSampleRate = sr;
    armedLoudSamples = 0;
    captureBpm = 0.0;
    capturePpq = -1.0;
    const bool freeRunning = standalone || JUCEApplicationBase::isStandaloneApp();
    captureStart = freeRunning ? -1 : -2;   // -2 = armed, wait for the host to play
    lastError.clear();
    setPhase (Phase::Recording);
}

void LearnSession::stopRecording()
{
    if (phase.load() != Phase::Recording)
        return;
    stopRequested = true;
    finishRecording();
}

void LearnSession::allocateChunk (int64_t k)
{
    if (k < 0 || k >= maxChunks)
        return;
    if (chunks[(size_t) k].load() == nullptr)
    {
        chunkStorage.push_back (std::make_unique<float[]> ((size_t) chunkSize));
        chunks[(size_t) k].store (chunkStorage.back().get(), std::memory_order_release);
    }
    if (captureBand.load() && bandChunks[(size_t) k].load() == nullptr)
    {
        chunkStorage.push_back (std::make_unique<float[]> ((size_t) chunkSize));
        std::fill (chunkStorage.back().get(), chunkStorage.back().get() + chunkSize, 0.0f);
        bandChunks[(size_t) k].store (chunkStorage.back().get(), std::memory_order_release);
    }
}

void LearnSession::finishRecording()
{
    const int64_t n = captured.load();
    std::vector<float> audio ((size_t) std::max<int64_t> (0, n));
    std::vector<float> band (captureBand.load() ? audio.size() : 0);
    for (int64_t i = 0; i < n; i += chunkSize)
    {
        const int64_t c = i / chunkSize;
        const int64_t len = std::min<int64_t> (chunkSize, n - i);
        if (float* src = chunks[(size_t) c].load())
            std::memcpy (audio.data() + i, src, sizeof (float) * (size_t) len);
        if (! band.empty())
            if (float* src = bandChunks[(size_t) c].load())
                std::memcpy (band.data() + i, src, sizeof (float) * (size_t) len);
    }
    releaseCapture();
    // Only keep the band if something was actually playing on the side-chain.
    if (! band.empty())
    {
        double e = 0.0;
        for (float v : band) e += (double) v * v;
        if (std::sqrt (e / (double) band.size()) < 1.0e-4)
            band.clear();
    }

    if (n < (int64_t) (captureSampleRate * 0.5))
    {
        renderWhenReady = false;
        lastError = captureStarted.load() ? "Take too short - sing for a little longer" : "Nothing was captured - press play in your DAW while LEARN is lit";
        setPhase (live.load() != nullptr ? Phase::Learned : Phase::Empty);
        return;
    }
    const int64_t start = captureStart.load();
    locks.clear();
    takeTiming = {};
    takeTiming.bpm = captureBpm.load();
    takeTiming.ppqStart = start >= 0 ? capturePpq.load() : -1.0;
    takeTiming.sigNum = captureSigNum.load();
    takeTiming.sigDen = captureSigDen.load();
    startAnalysis (std::move (audio), captureSampleRate, start >= 0 ? start : -1, false, std::move (band));
}

void LearnSession::releaseCapture()
{
    // The audio thread may still be inside a block that is writing to the chunks, so keep the
    // memory for a moment instead of freeing it here.
    for (auto& c : chunks) c.store (nullptr, std::memory_order_release);
    for (auto& c : bandChunks) c.store (nullptr, std::memory_order_release);
    if (! chunkStorage.empty())
        chunkGraveyard.push_back ({ std::move (chunkStorage), Time::getMillisecondCounter() });
    chunkStorage.clear();
    captured = 0;
}

void LearnSession::startAnalysis (std::vector<float> audio, double sampleRate, int64_t timelineStart, bool renderAfter,
                                  std::vector<float> band, std::vector<sc::BandChord> bandChords)
{
    cancelWork();
    setPhase (Phase::Analysing);
    progress = 0.0f;
    changedDuringWork = false;
    if (renderAfter)
        renderWhenReady = true;
    const sc::EngineParams params = getParams ? getParams() : sc::EngineParams{};
    sc::HarmonySettings hs = currentHarmonySettings();
    const int takeId = nextTakeId++;
    const TakeTiming timing = takeTiming;

    auto result = std::make_shared<std::unique_ptr<PlaybackData>>();
    cancel = false;
    workDone = false;
    worker = std::make_unique<std::thread> ([this, result, params, hs, takeId, timing, sampleRate, timelineStart, audio = std::move (audio),
                                             band = std::move (band), bandChords = std::move (bandChords)]() mutable
    {
        auto take = std::make_shared<std::vector<float>> (std::move (audio));
        auto analysis = std::make_shared<sc::TakeAnalysis> (sc::analyseTake (take->data(), (int64_t) take->size(), sampleRate, params.a4, &cancel));
        // The band's chords: heard from the side-chain recording, or restored with the project.
        analysis->bandChords = band.empty() ? std::move (bandChords)
                                            : sc::analyseBandChords (band.data(), (int64_t) band.size(), sampleRate, params.harmonySpeed / 100.0f, &cancel);
        if (! cancel.load())
        {
            auto d = std::make_unique<PlaybackData>();
            d->sampleRate = sampleRate;
            d->timelineStart = timelineStart;
            d->take = take;
            d->analysis = analysis;
            d->takeId = takeId;
            d->locks = hs.locks;
            d->timing = timing;
            d->score = sc::harmoniseTake (*analysis, hs);
            *result = std::move (d);
        }
        progress = 1.0f;
        workDone = true;
    });

    onWorkDone = [this, result]
    {
        if (*result == nullptr)
        {
            renderWhenReady = false;
            setPhase (live.load() != nullptr ? Phase::Learned : Phase::Empty);
            return;
        }
        const bool empty = (*result)->analysis->isEmpty();
        publish (std::move (*result));
        renderStale = false;
        if (empty)
        {
            renderWhenReady = false;
            lastError = "No singing found in the take";
            setPhase (Phase::Failed);
            return;
        }
        setPhase (Phase::Learned);
        if (std::exchange (changedDuringWork, false))
            harmonySettingsChanged();   // the harmony was written with the settings from before the edit
        // A pending render starts from the timer, once any recorded voices have loaded.
    };
}

void LearnSession::publish (std::unique_ptr<PlaybackData> data)
{
    PlaybackData* raw = data.get();
    if (owned != nullptr)
        graveyard.push_back ({ std::move (owned), Time::getMillisecondCounter() });
    owned = std::move (data);
    live.store (raw, std::memory_order_release);
    ++laneVersion;
}

std::shared_ptr<const std::vector<float>> LearnSession::currentTake() const
{
    return owned != nullptr ? owned->take : nullptr;
}

bool LearnSession::hasTake() const { return owned != nullptr && owned->take != nullptr && ! owned->take->empty(); }

void LearnSession::loadFile (const File& file)
{
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr || reader->sampleRate < 1000.0 || reader->lengthInSamples <= 0)
    {
        lastError = "Could not open " + file.getFileName();
        setPhase (Phase::Failed);
        return;
    }
    const int64_t maxLen = (int64_t) (reader->sampleRate * 60.0 * 12.0);
    const int64_t len = std::min<int64_t> (reader->lengthInSamples, maxLen);
    AudioBuffer<float> buf ((int) reader->numChannels, (int) len);
    reader->read (&buf, 0, (int) len, 0, true, true);

    std::vector<float> mono ((size_t) len, 0.0f);
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        FloatVectorOperations::addWithMultiply (mono.data(), buf.getReadPointer (ch), 1.0f / (float) buf.getNumChannels(), (int) len);

    // Resample to the session rate so playback lines up.
    if (std::abs (reader->sampleRate - sr) > 1.0)
    {
        const double ratio = reader->sampleRate / sr;
        const int outLen = (int) std::floor ((double) len / ratio);
        std::vector<float> out ((size_t) outLen, 0.0f);
        LagrangeInterpolator interp;
        interp.process (ratio, mono.data(), out.data(), outLen);
        mono = std::move (out);
    }
    previewPlaying = false;
    renderWhenReady = false;
    locks.clear();
    takeTiming = {};
    takeTiming.bpm = getHostBpm ? getHostBpm() : 0.0;   // so its MIDI comes out at the song's tempo
    startAnalysis (std::move (mono), sr, -1, false);
}

void LearnSession::clear()
{
    cancelWork();
    previewPlaying = false;
    renderWhenReady = false;
    if (phase.load() == Phase::Recording)
    {
        stopRequested = true;
        releaseCapture();
    }
    live.store (nullptr, std::memory_order_release);
    if (owned != nullptr)
        graveyard.push_back ({ std::move (owned), Time::getMillisecondCounter() });
    lastError.clear();
    renderStale = false;
    locks.clear();
    takeTiming = {};
    ++laneVersion;
    setPhase (Phase::Empty);
}

void LearnSession::render()
{
    // Pressed while a new take is still being learned: render that take once it is ready,
    // rather than throwing it away for the previous one.
    if (phase.load() == Phase::Recording)
    {
        renderWhenReady = true;
        stopRecording();
        return;
    }
    if (phase.load() == Phase::Analysing)
    {
        renderWhenReady = true;
        return;
    }
    renderWhenReady = false;
    if (owned == nullptr || owned->analysis == nullptr || owned->analysis->isEmpty())
    {
        lastError = "Learn or load a vocal take first";
        if (onStatusChanged) onStatusChanged();
        return;
    }
    cancelWork();
    setPhase (Phase::Rendering);
    progress = 0.0f;
    changedDuringWork = false;

    sc::EngineParams params = getParams ? getParams() : sc::EngineParams{};
    const sc::HarmonySettings hs = currentHarmonySettings();
    auto voices = getVoices ? getVoices() : nullptr;   // held for the whole render
    auto take = owned->take;
    auto analysis = owned->analysis;
    const double rate = owned->sampleRate;
    const int64_t timelineStart = owned->timelineStart;
    const int takeId = owned->takeId;
    const TakeTiming timing = owned->timing;
    auto result = std::make_shared<std::unique_ptr<PlaybackData>>();
    const auto t0 = Time::getMillisecondCounterHiRes();

    cancel = false;
    workDone = false;
    worker = std::make_unique<std::thread> ([this, result, params, hs, voices, take, analysis, rate, timelineStart, takeId, timing]
    {
        auto d = std::make_unique<PlaybackData>();
        d->sampleRate = rate;
        d->timelineStart = timelineStart;
        d->take = take;
        d->analysis = analysis;
        d->takeId = takeId;
        d->locks = hs.locks;
        d->timing = timing;
        d->score = sc::harmoniseTake (*analysis, hs);
        sc::RenderedStems stems;
        if (sc::renderTake (take->data(), (int64_t) take->size(), rate, params, d->score, stems,
                            [this] (float f) { progress = f; }, &cancel, voices.get()))
        {
            d->stems = toStemSet (stems);
            *result = std::move (d);
        }
        workDone = true;
    });

    onWorkDone = [this, result, t0, params]
    {
        if (*result == nullptr)
        {
            setPhase (Phase::Learned);
            return;
        }
        publish (std::move (*result));
        lastRenderSeconds = (Time::getMillisecondCounterHiRes() - t0) / 1000.0;
        renderedSingers = sc::singersForChoirSize (params.choirSize);
        renderStale = false;
        setPhase (Phase::Rendered);
        if (std::exchange (changedDuringWork, false))
            harmonySettingsChanged();   // edited while rendering: these stems are already out of date
    };
}

void LearnSession::cancelWork()
{
    if (worker != nullptr)
    {
        cancel = true;
        if (worker->joinable())
            worker->join();
        worker.reset();
        cancel = false;
        if (workDone.exchange (false) && onWorkDone)
        {
            // Discard the cancelled result but restore a sensible phase.
            onWorkDone = nullptr;
            if (phase.load() == Phase::Analysing || phase.load() == Phase::Rendering)
                setPhase (owned != nullptr ? (owned->stems != nullptr && ! owned->stale.load() ? Phase::Rendered : Phase::Learned) : Phase::Empty);
        }
        onWorkDone = nullptr;
    }
}

void LearnSession::togglePreview()
{
    if (previewPlaying.load())
    {
        previewPlaying = false;
    }
    else if (hasTake())
    {
        previewPos = 0;
        previewPlaying = true;
    }
    if (onStatusChanged) onStatusChanged();
}

void LearnSession::harmonySettingsChanged()
{
    if (owned == nullptr || owned->analysis == nullptr)
        return;
    const Phase p = phase.load();
    if (p == Phase::Analysing || p == Phase::Rendering)
    {
        changedDuringWork = true;   // applied when the work finishes
        return;
    }
    if (p == Phase::Recording)
        return;

    const sc::HarmonySettings hs = currentHarmonySettings();
    auto d = std::make_unique<PlaybackData>();
    d->sampleRate = owned->sampleRate;
    d->timelineStart = owned->timelineStart;
    d->take = owned->take;
    d->analysis = owned->analysis;
    d->takeId = owned->takeId;
    d->locks = hs.locks;
    d->timing = owned->timing;
    d->score = sc::harmoniseTake (*owned->analysis, hs);
    d->stems = owned->stems;
    d->stale = true;
    publish (std::move (d));
    renderSettingsChanged();
}

void LearnSession::renderSettingsChanged()
{
    if (phase.load() == Phase::Analysing || phase.load() == Phase::Rendering)
        changedDuringWork = true;
    if (owned == nullptr || owned->stems == nullptr)
        return;
    owned->stale = true;
    if (! renderStale)
    {
        renderStale = true;
        if (phase.load() == Phase::Rendered)
            setPhase (Phase::Learned);
        else if (onStatusChanged)
            onStatusChanged();
    }
}

sc::HarmonySettings LearnSession::currentHarmonySettings() const
{
    auto hs = harmonySettingsFor (getParams ? getParams() : sc::EngineParams{});
    hs.locks = locks;
    return hs;
}

//==============================================================================
static const char* const sectionFileNames[] = { "Sopranos", "Altos", "Tenors", "Basses" };

/** The intervals of a chord above its root, as the UI sends them back. */
static Array<var> intervalsOf (const sc::Chord& c)
{
    Array<var> iv;
    for (int k = 0; k < c.numTones; ++k)
        iv.add (sc::wrapPc (c.tones[(size_t) k] - c.root));
    return iv;
}

var LearnSession::getChordLane() const
{
    if (owned == nullptr || owned->analysis == nullptr || owned->analysis->isEmpty())
        return {};
    const auto& score = owned->score;
    const auto& take = *owned->analysis;
    auto o = std::make_unique<DynamicObject>();
    o->setProperty ("version", laneVersion);
    o->setProperty ("rate", owned->sampleRate);
    o->setProperty ("length", (int64) owned->length());
    o->setProperty ("key", String (CharPointer_UTF8 (score.keyName().c_str())));
    o->setProperty ("band", score.followsBand);
    const auto mode = getParams ? getParams().harmonyMode : sc::HarmonyMode::FullChoir;
    o->setProperty ("editable", mode != sc::HarmonyMode::Drone && mode != sc::HarmonyMode::MidiChords
                                    && score.scale.type != sc::ScaleType::Chromatic);
    o->setProperty ("locks", (int) owned->locks.size());

    // Where the take starts in the song (bar.beat when the song's tempo was known).
    if (owned->timelineStart >= 0)
    {
        o->setProperty ("songStart", (double) owned->timelineStart / owned->sampleRate);
        if (owned->timing.ppqStart >= 0.0)
        {
            const double beatsPerBar = 4.0 * owned->timing.sigNum / std::max (1, owned->timing.sigDen);
            const double beatLength = 4.0 / std::max (1, owned->timing.sigDen);
            const int bar = (int) std::floor (owned->timing.ppqStart / beatsPerBar + 1.0e-6);
            const int beat = (int) std::floor ((owned->timing.ppqStart - bar * beatsPerBar) / beatLength + 1.0e-6);
            o->setProperty ("bar", bar + 1);
            o->setProperty ("beat", beat + 1);
        }
    }

    Array<var> spans;
    for (const auto& span : score.chordLane())
    {
        auto so = std::make_unique<DynamicObject>();
        so->setProperty ("s", (int64) span.start);
        so->setProperty ("e", (int64) span.end);
        so->setProperty ("name", String (CharPointer_UTF8 (span.chord.name.c_str())));
        so->setProperty ("root", span.chord.root);
        so->setProperty ("iv", intervalsOf (span.chord));
        so->setProperty ("locked", span.locked);
        so->setProperty ("band", span.fromBand);
        spans.add (var (so.release()));
    }
    o->setProperty ("spans", spans);

    Array<var> melody;
    const bool aligned = score.events.size() == take.notes.size();
    for (size_t i = 0; i < take.notes.size(); ++i)
    {
        const auto& n = take.notes[i];
        auto no = std::make_unique<DynamicObject>();
        no->setProperty ("s", (int64) n.start);
        no->setProperty ("e", (int64) n.end);
        no->setProperty ("p", aligned && score.events[i].melody >= 0 ? score.events[i].melody : (int) std::lround (n.pitch));
        melody.add (var (no.release()));
    }
    o->setProperty ("melody", melody);
    return var (o.release());
}

var LearnSession::getChordOptions (int64_t start, int64_t end) const
{
    Array<var> list;
    if (owned == nullptr || owned->analysis == nullptr || owned->analysis->isEmpty())
        return list;
    for (const auto& opt : sc::chordAlternatives (*owned->analysis, owned->score, currentHarmonySettings(), start, end))
    {
        auto o = std::make_unique<DynamicObject>();
        o->setProperty ("name", String (CharPointer_UTF8 (opt.chord.name.c_str())));
        o->setProperty ("root", opt.chord.root);
        o->setProperty ("iv", intervalsOf (opt.chord));
        o->setProperty ("fit", opt.fit);
        o->setProperty ("current", opt.current);
        list.add (var (o.release()));
    }
    return list;
}

void LearnSession::lockChord (int64_t start, int64_t end, int root, const Array<int>& intervals)
{
    if (owned == nullptr || owned->analysis == nullptr || end <= start || intervals.isEmpty() || intervals.size() > 4)
        return;
    int iv[4] {};
    for (int k = 0; k < intervals.size(); ++k)
        iv[k] = sc::wrapPc (intervals[k]);
    sc::ChordLock lock;
    lock.start = start;
    lock.end = end;
    lock.chord = sc::makeChordFromIntervals (root, iv, intervals.size(), sc::keyPrefersFlats (owned->score.scaleAt (start)));
    sc::addChordLock (locks, lock);
    harmonySettingsChanged();
}

void LearnSession::unlockChords (int64_t start, int64_t end)
{
    const auto before = locks;
    sc::removeChordLocks (locks, start, end);
    auto same = [] (const sc::ChordLock& a, const sc::ChordLock& b) { return a.start == b.start && a.end == b.end; };
    if (! std::equal (before.begin(), before.end(), locks.begin(), locks.end(), same))
        harmonySettingsChanged();
}

void LearnSession::unlockAllChords()
{
    if (locks.empty())
        return;
    locks.clear();
    harmonySettingsChanged();
}

//==============================================================================
File LearnSession::exportFolder()
{
    if (owned == nullptr)
        return {};
    if (exported.takeId != owned->takeId || ! exported.folder.isDirectory())
    {
        exported = {};
        exported.takeId = owned->takeId;
        const auto base = exportRoot != File() ? exportRoot
                                               : File::getSpecialLocation (File::userDocumentsDirectory)
                                                     .getChildFile ("Sanctuary Choir").getChildFile ("Exports");
        base.createDirectory();
        exported.folder = base.getNonexistentChildFile (Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S"), {}, false);
        exported.folder.createDirectory();
    }
    return exported.folder;
}

StringArray LearnSession::getExportFiles (ExportItem item)
{
    StringArray files;
    if (owned == nullptr || owned->analysis == nullptr || owned->analysis->isEmpty())
        return files;

    if (item == ExportItem::Midi)
    {
        // Cheap, and a DAW copies MIDI in when it is dropped: written fresh every time.
        const auto file = exportFolder().getChildFile ("Sanctuary Choir - Parts.mid");
        const bool words = getParams && getParams().vocalType == sc::VocalType::Words;
        const auto midi = makeTakeMidi (*owned->analysis, owned->score, owned->timing, words);
        file.deleteFile();
        FileOutputStream out (file);
        if (out.openedOk() && midi.writeTo (out))
        {
            out.flush();
            files.add (file.getFullPathName());
        }
        return files;
    }

    if (owned->stems == nullptr || owned->stale.load() || exportWorker != nullptr
        || exported.takeId != owned->takeId)
        return files;
    const String signature = String (owned->stems->id) + "|" + (getMixSignature ? getMixSignature() : String());
    const bool fullReady = exported.fullSignature == signature && exported.full.existsAsFile();
    const bool sectionsReady = exported.sectionsSignature == signature;
    switch (item)
    {
        case ExportItem::FullChoir:
            if (fullReady) files.add (exported.full.getFullPathName());
            break;
        case ExportItem::Sopranos:
        case ExportItem::Altos:
        case ExportItem::Tenors:
        case ExportItem::Basses:
        {
            const int s = (int) item - (int) ExportItem::Sopranos;
            if (sectionsReady && s < exported.sections.size() && File (exported.sections[s]).existsAsFile())
                files.add (exported.sections[s]);
            break;
        }
        case ExportItem::AllStems:
            if (! fullReady || ! sectionsReady)
                break;
            for (const auto& f : exported.sections)
                if (File (f).existsAsFile())
                    files.add (f);
            if (files.size() != sc::NumSections)
                files.clear();
            else
                files.add (exported.full.getFullPathName());
            break;
        case ExportItem::Midi:
        default:
            break;
    }
    return files;
}

void LearnSession::prepareExport (bool withRoom, std::function<void (File)> onDone, bool automatic)
{
    const bool fresh = owned != nullptr && owned->stems != nullptr && ! owned->stale.load();
    if (! fresh)
    {
        if (onDone) onDone ({});
        return;
    }
    if (exportWorker != nullptr)
    {
        // Busy: once this finishes, see what is still missing.
        auto previous = std::move (onExportDone);
        onExportDone = [this, previous, withRoom, onDone, automatic]
        {
            if (previous) previous();
            prepareExport (withRoom, onDone, automatic);
        };
        return;
    }

    const auto stems = owned->stems;
    const String signature = String (stems->id) + "|" + (getMixSignature ? getMixSignature() : String());
    if (automatic && signature == exportFailedSignature)
        return;
    const auto folder = exportFolder();
    // The sections stand on the stage and go through the mics: they follow the mix too.
    bool needSections = exported.sectionsSignature != signature || exported.sections.size() != sc::NumSections;
    for (const auto& f : exported.sections)
        needSections = needSections || ! File (f).existsAsFile();
    const bool needFull = exported.fullSignature != signature || ! exported.full.existsAsFile();
    const bool needRoom = withRoom && (exported.roomSignature != signature || ! exported.room.existsAsFile());
    if (! needSections && ! needFull && ! needRoom)
    {
        if (onDone) onDone (folder);
        return;
    }

    // New names for new sound: a DAW that plays the earlier files from here keeps them as they were.
    auto plan = std::make_shared<ExportFiles> (exported);
    if (needSections)
    {
        plan->sectionsSignature = signature;
        plan->sections.clear();
        for (auto* name : sectionFileNames)
            plan->sections.add (folder.getNonexistentChildFile (String ("Sanctuary Choir - ") + name, ".wav", true).getFullPathName());
    }
    if (needFull)
    {
        plan->fullSignature = signature;
        plan->full = folder.getNonexistentChildFile ("Sanctuary Choir - Full Choir", ".wav", true);
    }
    if (needRoom)
    {
        plan->roomSignature = signature;
        plan->room = folder.getNonexistentChildFile ("Sanctuary Choir - Room", ".wav", true);
    }

    const sc::EngineParams params = getParams ? getParams() : sc::EngineParams{};
    const double rate = owned->sampleRate;
    // Files start at the song's start when the take was learned in time with the song, so
    // they line up when dropped at the beginning of the song.
    const int64_t offset = std::max<int64_t> (0, owned->timelineStart);
    auto ok = std::make_shared<bool> (true);
    exportCancel = false;
    exportDone = false;
    exportWorker = std::make_unique<std::thread> ([this, plan, stems, params, rate, offset, ok, needSections, needFull, needRoom]
    {
        const StemSet& st = *stems;
        auto readStems = [&st] (int64_t pos, int len, float* const* channels)
        {
            for (size_t ch = 0; ch < st.data.size(); ++ch)
                for (int i = 0; i < len; ++i)
                    channels[ch][i] = pos + i < (int64_t) st.data[ch].size() ? (float) st.data[ch][(size_t) (pos + i)] * st.scale[ch] : 0.0f;
        };
        auto mix = [&] (const File& file, const sc::EngineParams& p, bool roomOnly, float tail)
        {
            StereoWavWriter w;
            *ok = w.open (file, rate) && w.writeSilence (offset)
                  && sc::mixdownStemsStreaming (rate, st.length, p, roomOnly, tail, readStems,
                                                [&] (const float* l, const float* r, int len) { return ! exportCancel.load() && w.write (l, r, len); })
                  && w.close();
        };
        // Each section where it stands on the stage, through the mics, without the hall.
        for (int s = 0; s < sc::NumSections && needSections && *ok; ++s)
        {
            sc::EngineParams one = params;
            one.reverb = 0.0f;
            for (int o = 0; o < sc::NumSections; ++o)
            {
                one.sections[(size_t) o].mute = o != s;
                one.sections[(size_t) o].solo = false;
            }
            mix (File (plan->sections[s]), one, false, 0.3f);
        }
        if (needFull && *ok)
            mix (plan->full, params, false, 4.0f);
        if (needRoom && *ok)
            mix (plan->room, params, true, 4.0f);
        exportDone = true;
    });

    auto previous = std::move (onExportDone);
    onExportDone = [this, plan, ok, folder, onDone, signature]
    {
        exportFailedSignature = *ok ? String() : signature;
        if (*ok && plan->takeId == exported.takeId)
            exported = *plan;
        if (onDone) onDone (*ok ? folder : File());
    };
    jassert (previous == nullptr);
    if (onStatusChanged) onStatusChanged();
}

void LearnSession::finishExport()
{
    if (exportWorker != nullptr && exportWorker->joinable())
        exportWorker->join();
    exportWorker.reset();
    auto fn = std::move (onExportDone);
    onExportDone = nullptr;
    if (fn) fn();
    if (onStatusChanged) onStatusChanged();
}

//==============================================================================
void LearnSession::timerCallback()
{
    // Keep capture memory allocated ahead of the audio thread.
    if (phase.load() == Phase::Recording)
    {
        const int64_t c = captured.load() / chunkSize;
        for (int64_t k = c; k < std::min<int64_t> (maxChunks, c + kChunksAhead); ++k)
            allocateChunk (k);

        // Armed but the host never played: fall back to free-running capture.
        if (stopRequested.load())
            finishRecording();
        else if (onStatusChanged)
            onStatusChanged();
    }

    if (workDone.exchange (false))
    {
        if (worker != nullptr && worker->joinable())
            worker->join();
        worker.reset();
        auto fn = std::move (onWorkDone);
        onWorkDone = nullptr;
        if (fn) fn();
    }
    else if ((phase.load() == Phase::Analysing || phase.load() == Phase::Rendering) && onStatusChanged)
    {
        onStatusChanged();
    }

    if (previewPlaying.load() && onStatusChanged)
        onStatusChanged();

    if (exportDone.exchange (false))
        finishExport();

    matchSessionRate();

    if (renderWhenReady)
    {
        const Phase p = phase.load();
        if (p == Phase::Learned || p == Phase::Rendered)
        {
            if (! (voicesBusy && voicesBusy()))
                render();
        }
        else if (p != Phase::Recording && p != Phase::Analysing && p != Phase::Rendering)
        {
            renderWhenReady = false;
        }
    }

    // Free old playback data and capture memory once the audio thread can no longer be using it.
    const auto now = Time::getMillisecondCounter();
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(),
                                     [now] (const auto& g) { return now - g.second > 2000; }),
                     graveyard.end());
    chunkGraveyard.erase (std::remove_if (chunkGraveyard.begin(), chunkGraveyard.end(),
                                          [now] (const auto& g) { return now - g.second > 2000; }),
                          chunkGraveyard.end());
}

void LearnSession::matchSessionRate()
{
    // A take learned at another sample rate (project reopened at 48 kHz after 44.1 kHz, or the
    // audio device changed): convert it, so it stays in tune and in time with the song.
    const Phase p = phase.load();
    if (! prepared || owned == nullptr || owned->take == nullptr || owned->take->empty()
        || std::abs (owned->sampleRate - sr) <= 1.0
        || p == Phase::Recording || p == Phase::Analysing || p == Phase::Rendering)
        return;

    const double ratio = owned->sampleRate / sr;
    const auto& take = *owned->take;
    const int outLen = (int) std::floor ((double) take.size() / ratio);
    std::vector<float> out ((size_t) std::max (0, outLen), 0.0f);
    LagrangeInterpolator interp;
    interp.process (ratio, take.data(), out.data(), outLen, (int) take.size(), 0);
    const int64_t start = owned->timelineStart >= 0 ? (int64_t) std::llround ((double) owned->timelineStart / ratio) : -1;
    const bool wasRendered = owned->stems != nullptr && ! owned->stale.load();
    std::vector<sc::BandChord> band = owned->analysis != nullptr ? owned->analysis->bandChords : std::vector<sc::BandChord>{};
    for (auto& b : band)
    {
        b.start = (int64_t) std::llround ((double) b.start / ratio);
        b.end = (int64_t) std::llround ((double) b.end / ratio);
    }
    for (auto& l : locks)
    {
        l.start = (int64_t) std::llround ((double) l.start / ratio);
        l.end = (int64_t) std::llround ((double) l.end / ratio);
    }
    startAnalysis (std::move (out), sr, start, wasRendered || renderWhenReady, {}, std::move (band));
}

//==============================================================================
String LearnSession::describe() const
{
    const Phase p = phase.load();
    const PlaybackData* d = owned.get();
    switch (p)
    {
        case Phase::Empty:
            return lastError.isNotEmpty() ? lastError : "Press LEARN and sing, or LOAD a vocal";
        case Phase::Recording:
        {
            const double secs = (double) captured.load() / captureSampleRate;
            if (! captureStarted.load())
                return "LEARN ARMED - press play in your DAW";
            return "LEARNING " + String ((int) secs / 60) + ":" + String ((int) secs % 60).paddedLeft ('0', 2) + "  - press LEARN to finish";
        }
        case Phase::Analysing:
            return "LISTENING TO YOUR TAKE...";
        case Phase::Rendering:
            return "RENDERING " + String (roundToInt (progress.load() * 100.0f)) + "%";
        case Phase::Failed:
            return lastError;
        case Phase::Learned:
        case Phase::Rendered:
        default:
            break;
    }
    if (d == nullptr)
        return {};
    String s (CharPointer_UTF8 (d->score.describe().c_str()));
    const String dot (CharPointer_UTF8 ("  \xc2\xb7  "));
    if (p == Phase::Rendered && d->stems != nullptr && ! d->stale.load())
        s << dot << renderedSingers << " SINGERS RENDERED IN " << String (lastRenderSeconds, 1) << " S";
    else if (d->stems != nullptr)
        s << dot << "RENDER TO UPDATE";
    return s;
}

String LearnSession::exportStateName() const
{
    if (owned == nullptr || owned->stems == nullptr || owned->stale.load())
        return "none";   // render first
    if (exportWorker != nullptr)
        return "preparing";
    const String signature = String (owned->stems->id) + "|" + (getMixSignature ? getMixSignature() : String());
    if (signature == exportFailedSignature)
        return "failed";
    bool ready = exported.takeId == owned->takeId && exported.sectionsSignature == signature
                 && exported.fullSignature == signature && exported.full.existsAsFile() && exported.sections.size() == sc::NumSections;
    for (const auto& f : exported.sections)
        ready = ready && File (f).existsAsFile();
    return ready ? "ready" : "stale";
}

var LearnSession::getStatus() const
{
    auto o = std::make_unique<DynamicObject>();
    static const char* phaseNames[] = { "empty", "recording", "analysing", "learned", "rendering", "rendered", "failed" };
    const Phase p = phase.load();
    o->setProperty ("phase", phaseNames[(int) p]);
    o->setProperty ("text", describe());
    o->setProperty ("progress", progress.load());
    o->setProperty ("armed", p == Phase::Recording && ! captureStarted.load());
    o->setProperty ("hasTake", hasTake());
    o->setProperty ("hasStems", owned != nullptr && owned->stems != nullptr);
    o->setProperty ("stale", owned != nullptr && owned->stems != nullptr && owned->stale.load());
    o->setProperty ("previewing", previewPlaying.load());
    o->setProperty ("tiedToTimeline", owned != nullptr && owned->timelineStart >= 0);
    o->setProperty ("laneVersion", laneVersion);
    o->setProperty ("locks", (int) locks.size());
    o->setProperty ("export", exportStateName());
    if (owned != nullptr)
    {
        o->setProperty ("seconds", owned->score.durationSeconds);
        o->setProperty ("key", String (CharPointer_UTF8 (owned->score.keyName().c_str())));
        o->setProperty ("phrases", owned->score.numPhrases);
        o->setProperty ("notes", owned->score.numNotes);
        if (previewPlaying.load() && owned->sampleRate > 0)
            o->setProperty ("previewSeconds", (double) previewPos.load() / owned->sampleRate);
    }
    return var (o.release());
}

//==============================================================================
static constexpr int kStateMagic = 0x4b544353;  // "SCTK"
static constexpr int kExtrasMagic = 0x58544353; // "SCTX": chosen chords and song position, after the take

void LearnSession::writeState (OutputStream& out) const
{
    if (! hasTake())
    {
        out.writeInt (0);
        return;
    }
    // Version 2 adds the band's chords; takes without a band stay version 1, so projects remain
    // readable by earlier versions of the plug-in.
    const auto* band = owned->analysis != nullptr ? &owned->analysis->bandChords : nullptr;
    const bool withBand = band != nullptr && ! band->empty();
    out.writeInt (kStateMagic);
    out.writeInt (withBand ? 2 : 1);
    out.writeDouble (owned->sampleRate);
    out.writeInt64 (owned->timelineStart);
    out.writeBool (owned->stems != nullptr && ! owned->stale.load());

    // Hosts ask for the state often (some on every parameter change, for undo), so the take is
    // encoded once and reused for as long as it does not change.
    const std::lock_guard<std::mutex> g (encodeLock);
    if (encodedTake.lock() != owned->take)
    {
        encodedFlac.reset();
        FlacAudioFormat flac;
        std::unique_ptr<OutputStream> stream = std::make_unique<MemoryOutputStream> (encodedFlac, false);
        auto writer = flac.createWriterFor (stream, AudioFormatWriterOptions{}.withSampleRate (owned->sampleRate).withNumChannels (1).withBitsPerSample (24));
        if (writer != nullptr)
        {
            const float* ch[] = { owned->take->data() };
            const int64_t len = (int64_t) owned->take->size();
            for (int64_t pos = 0; pos < len; pos += 65536)
            {
                const float* p[] = { ch[0] + pos };
                writer->writeFromFloatArrays (p, 1, (int) std::min<int64_t> (65536, len - pos));
            }
        }
        writer.reset();   // flushes into encodedFlac
        encodedTake = owned->take;
    }
    out.writeInt64 ((int64) encodedFlac.getSize());
    out.write (encodedFlac.getData(), encodedFlac.getSize());
    if (withBand)
    {
        out.writeInt ((int) band->size());
        for (const auto& b : *band)
        {
            out.writeInt64 (b.start);
            out.writeInt64 (b.end);
            out.writeInt (b.index);
            out.writeInt (b.bass);
        }
    }

    // Chords chosen by hand and where the take sits in the song follow in a block of their own,
    // which earlier versions of the plug-in never read.
    const auto& timing = owned->timing;
    if (! owned->locks.empty() || timing.bpm > 0.0 || timing.ppqStart >= 0.0)
    {
        auto o = std::make_unique<DynamicObject>();
        o->setProperty ("bpm", timing.bpm);
        o->setProperty ("ppq", timing.ppqStart);
        o->setProperty ("sigNum", timing.sigNum);
        o->setProperty ("sigDen", timing.sigDen);
        Array<var> lockList;
        for (const auto& l : owned->locks)
        {
            auto lo = std::make_unique<DynamicObject>();
            lo->setProperty ("s", (int64) l.start);
            lo->setProperty ("e", (int64) l.end);
            lo->setProperty ("root", l.chord.root);
            lo->setProperty ("iv", intervalsOf (l.chord));
            lo->setProperty ("name", String (CharPointer_UTF8 (l.chord.name.c_str())));
            lockList.add (var (lo.release()));
        }
        o->setProperty ("locks", lockList);
        out.writeInt (kExtrasMagic);
        out.writeString (JSON::toString (var (o.release()), true));
    }
}

void LearnSession::readState (InputStream& in)
{
    if (in.getNumBytesRemaining() < 4 || in.readInt() != kStateMagic)
    {
        if (hasTake())
            clear();
        return;
    }
    const int version = in.readInt();
    if (version != 1 && version != 2)
        return;
    const double rate = in.readDouble();
    const int64_t timelineStart = in.readInt64();
    const bool wasRendered = in.readBool();
    const int64_t size = in.readInt64();
    if (size <= 0 || size > in.getNumBytesRemaining() || ! (rate >= 8000.0 && rate <= 768000.0))
        return;

    MemoryBlock block;
    in.readIntoMemoryBlock (block, (ssize_t) size);
    FlacAudioFormat flac;
    std::unique_ptr<AudioFormatReader> reader (flac.createReaderFor (new MemoryInputStream (block, false), true));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->lengthInSamples > (int64) (rate * 60.0 * 15.0))
        return;
    std::vector<float> audio ((size_t) reader->lengthInSamples);
    float* dest[] = { audio.data() };
    reader->read (dest, 1, 0, (int) reader->lengthInSamples);

    // The band's chords (version 2).
    std::vector<sc::BandChord> band;
    if (version >= 2 && in.getNumBytesRemaining() >= 4)
    {
        const int count = in.readInt();
        for (int i = 0; i < count && i < 100000 && in.getNumBytesRemaining() >= 24; ++i)
        {
            sc::BandChord b;
            b.start = in.readInt64();
            b.end = in.readInt64();
            b.index = in.readInt();
            b.bass = in.readInt();
            if (b.index >= 0 && b.index < sc::kBandChords && b.end > b.start && b.bass >= -1 && b.bass < 12)
                band.push_back (b);
        }
    }

    // Chords chosen by hand and the song position.
    std::vector<sc::ChordLock> savedLocks;
    TakeTiming timing;
    if (in.getNumBytesRemaining() >= 5 && in.readInt() == kExtrasMagic)
    {
        const auto extras = JSON::parse (in.readString());
        timing.bpm = jlimit (0.0, 999.0, (double) extras.getProperty ("bpm", 0.0));
        timing.ppqStart = (double) extras.getProperty ("ppq", -1.0);
        timing.sigNum = jlimit (1, 32, (int) extras.getProperty ("sigNum", 4));
        timing.sigDen = jlimit (1, 32, (int) extras.getProperty ("sigDen", 4));
        if (auto* list = extras.getProperty ("locks", {}).getArray())
        {
            for (const auto& l : *list)
            {
                const auto* iv = l.getProperty ("iv", {}).getArray();
                if (iv == nullptr || iv->isEmpty() || iv->size() > 4)
                    continue;
                int intervals[4] {};
                for (int k = 0; k < iv->size(); ++k)
                    intervals[k] = sc::wrapPc ((int) (*iv)[k]);
                sc::ChordLock lock;
                lock.start = (int64) l.getProperty ("s", 0);
                lock.end = (int64) l.getProperty ("e", 0);
                lock.chord = sc::makeChordFromIntervals ((int) l.getProperty ("root", 0), intervals, iv->size());
                lock.chord.name = l.getProperty ("name", "").toString().toStdString();
                if (lock.end > lock.start && lock.chord.name.size() < 16)
                    savedLocks.push_back (std::move (lock));
            }
        }
    }

    // The same take we already have (undo, A/B compare, a host restoring what it just saved):
    // keep the analysis and stems; changed settings are picked up as usual.
    const Phase p = phase.load();
    if (hasTake() && p != Phase::Recording && p != Phase::Analysing && p != Phase::Rendering
        && std::abs (owned->sampleRate - rate) < 1.0 && owned->timelineStart == timelineStart
        && owned->take->size() == audio.size())
    {
        const auto& cur = *owned->take;
        bool same = true;
        for (size_t i = 0; i < audio.size() && same; ++i)
            same = std::abs (cur[i] - audio[i]) < 1.0e-5f;
        const auto& curBand = owned->analysis != nullptr ? owned->analysis->bandChords : std::vector<sc::BandChord>{};
        same = same && curBand.size() == band.size();
        for (size_t i = 0; i < band.size() && same; ++i)
            same = curBand[i].start == band[i].start && curBand[i].end == band[i].end && curBand[i].index == band[i].index && curBand[i].bass == band[i].bass;
        if (same)
        {
            // Only the chosen chords differ (undo of a chord change): harmonise again.
            auto sameLock = [] (const sc::ChordLock& a, const sc::ChordLock& b)
            { return a.start == b.start && a.end == b.end && a.chord.root == b.chord.root && a.chord.mask() == b.chord.mask(); };
            if (! std::equal (savedLocks.begin(), savedLocks.end(), locks.begin(), locks.end(), sameLock))
            {
                locks = std::move (savedLocks);
                harmonySettingsChanged();
            }
            return;
        }
    }
    locks = std::move (savedLocks);
    takeTiming = timing;
    startAnalysis (std::move (audio), rate, timelineStart, wasRendered, {}, std::move (band));
}
} // namespace sanctuary
