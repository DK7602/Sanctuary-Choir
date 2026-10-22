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
    auto s = std::make_shared<StemSet>();
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

static sc::RenderedStems fromStemSet (const StemSet& s, double sr)
{
    sc::RenderedStems r;
    r.sampleRate = sr;
    r.length = s.length;
    for (size_t ch = 0; ch < s.data.size(); ++ch)
    {
        r.channels[ch].resize (s.data[ch].size());
        for (size_t i = 0; i < s.data[ch].size(); ++i)
            r.channels[ch][i] = (float) s.data[ch][i] * s.scale[ch];
    }
    return r;
}

static sc::HarmonySettings harmonySettingsFor (const sc::EngineParams& p)
{
    sc::HarmonySettings hs;
    hs.key = p.key;
    hs.scale = p.scale;
    hs.mode = p.harmonyMode;
    hs.style = p.style;
    for (int s = 0; s < sc::NumSections; ++s)
        hs.ranges[(size_t) s] = p.sections[(size_t) s].range;
    hs.wordsMode = p.vocalType == sc::VocalType::Words;

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
    startTimerHz (20);
}

LearnSession::~LearnSession()
{
    stopTimer();
    cancel = true;
    if (worker != nullptr && worker->joinable())
        worker->join();
    live.store (nullptr);
}

void LearnSession::prepare (double sampleRate)
{
    sr = sampleRate;
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

    // Pre-allocate the first few chunks; the timer keeps allocating ahead.
    for (int i = 0; i < 8; ++i)
    {
        if (chunks[(size_t) i].load() == nullptr)
        {
            chunkStorage.push_back (std::make_unique<float[]> ((size_t) chunkSize));
            chunks[(size_t) i].store (chunkStorage.back().get(), std::memory_order_release);
        }
    }
    captured = 0;
    captureStarted = false;
    stopRequested = false;
    captureSampleRate = sr;
    armedLoudSamples = 0;
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

void LearnSession::finishRecording()
{
    const int64_t n = captured.load();
    std::vector<float> audio ((size_t) std::max<int64_t> (0, n));
    for (int64_t i = 0; i < n; i += chunkSize)
    {
        const int64_t c = i / chunkSize;
        const int64_t len = std::min<int64_t> (chunkSize, n - i);
        if (float* src = chunks[(size_t) c].load())
            std::memcpy (audio.data() + i, src, sizeof (float) * (size_t) len);
    }
    // Release capture memory.
    for (auto& c : chunks) c.store (nullptr);
    chunkStorage.clear();
    captured = 0;

    if (n < (int64_t) (captureSampleRate * 0.5))
    {
        lastError = captureStarted.load() ? "Take too short - sing for a little longer" : "Nothing was captured - press play in your DAW while LEARN is lit";
        setPhase (live.load() != nullptr ? Phase::Learned : Phase::Empty);
        return;
    }
    const int64_t start = captureStart.load();
    startAnalysis (std::move (audio), captureSampleRate, start >= 0 ? start : -1, false);
}

void LearnSession::startAnalysis (std::vector<float> audio, double sampleRate, int64_t timelineStart, bool renderAfter)
{
    cancelWork();
    setPhase (Phase::Analysing);
    progress = 0.0f;
    const sc::EngineParams params = getParams ? getParams() : sc::EngineParams{};

    auto result = std::make_shared<std::unique_ptr<PlaybackData>>();
    cancel = false;
    workDone = false;
    worker = std::make_unique<std::thread> ([this, result, params, sampleRate, timelineStart, audio = std::move (audio)]() mutable
    {
        auto take = std::make_shared<std::vector<float>> (std::move (audio));
        auto analysis = std::make_shared<sc::TakeAnalysis> (sc::analyseTake (take->data(), (int64_t) take->size(), sampleRate, params.a4, &cancel));
        if (! cancel.load())
        {
            auto d = std::make_unique<PlaybackData>();
            d->sampleRate = sampleRate;
            d->timelineStart = timelineStart;
            d->take = take;
            d->analysis = analysis;
            d->score = sc::harmoniseTake (*analysis, harmonySettingsFor (params));
            *result = std::move (d);
        }
        progress = 1.0f;
        workDone = true;
    });

    onWorkDone = [this, result, renderAfter]
    {
        if (*result == nullptr)
        {
            setPhase (live.load() != nullptr ? Phase::Learned : Phase::Empty);
            return;
        }
        const bool empty = (*result)->analysis->isEmpty();
        publish (std::move (*result));
        renderStale = false;
        if (empty)
        {
            lastError = "No singing found in the take";
            setPhase (Phase::Failed);
            return;
        }
        setPhase (Phase::Learned);
        if (renderAfter)
            render();
    };
}

void LearnSession::publish (std::unique_ptr<PlaybackData> data)
{
    PlaybackData* raw = data.get();
    if (owned != nullptr)
        graveyard.push_back ({ std::move (owned), Time::getMillisecondCounter() });
    owned = std::move (data);
    live.store (raw, std::memory_order_release);
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
    if (reader == nullptr)
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
    startAnalysis (std::move (mono), sr, -1, false);
}

void LearnSession::clear()
{
    cancelWork();
    previewPlaying = false;
    if (phase.load() == Phase::Recording)
    {
        stopRequested = true;
        for (auto& c : chunks) c.store (nullptr);
        chunkStorage.clear();
    }
    live.store (nullptr, std::memory_order_release);
    if (owned != nullptr)
        graveyard.push_back ({ std::move (owned), Time::getMillisecondCounter() });
    lastError.clear();
    renderStale = false;
    setPhase (Phase::Empty);
}

void LearnSession::render()
{
    if (owned == nullptr || owned->analysis == nullptr || owned->analysis->isEmpty())
    {
        lastError = "Learn or load a vocal take first";
        if (onStatusChanged) onStatusChanged();
        return;
    }
    cancelWork();
    setPhase (Phase::Rendering);
    progress = 0.0f;

    sc::EngineParams params = getParams ? getParams() : sc::EngineParams{};
    auto take = owned->take;
    auto analysis = owned->analysis;
    const double rate = owned->sampleRate;
    const int64_t timelineStart = owned->timelineStart;
    auto result = std::make_shared<std::unique_ptr<PlaybackData>>();
    const auto t0 = Time::getMillisecondCounterHiRes();

    cancel = false;
    workDone = false;
    worker = std::make_unique<std::thread> ([this, result, params, take, analysis, rate, timelineStart]
    {
        auto d = std::make_unique<PlaybackData>();
        d->sampleRate = rate;
        d->timelineStart = timelineStart;
        d->take = take;
        d->analysis = analysis;
        d->score = sc::harmoniseTake (*analysis, harmonySettingsFor (params));
        sc::RenderedStems stems;
        if (sc::renderTake (take->data(), (int64_t) take->size(), rate, params, d->score, stems,
                            [this] (float f) { progress = f; }, &cancel))
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
    if (p == Phase::Recording || p == Phase::Analysing || p == Phase::Rendering)
        return;

    const sc::EngineParams params = getParams ? getParams() : sc::EngineParams{};
    auto d = std::make_unique<PlaybackData>();
    d->sampleRate = owned->sampleRate;
    d->timelineStart = owned->timelineStart;
    d->take = owned->take;
    d->analysis = owned->analysis;
    d->score = sc::harmoniseTake (*owned->analysis, harmonySettingsFor (params));
    d->stems = owned->stems;
    d->stale = true;
    publish (std::move (d));
    renderSettingsChanged();
}

void LearnSession::renderSettingsChanged()
{
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

void LearnSession::exportStems (std::function<void (File)> onDone)
{
    if (owned == nullptr || owned->stems == nullptr)
        return;
    const sc::EngineParams params = getParams ? getParams() : sc::EngineParams{};
    auto stems = owned->stems;
    const double rate = owned->sampleRate;

    const auto folder = File::getSpecialLocation (File::userDocumentsDirectory)
                            .getChildFile ("Sanctuary Choir").getChildFile ("Exports")
                            .getChildFile (Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S"));

    std::thread ([stems, rate, params, folder, onDone]
    {
        folder.createDirectory();
        const auto rendered = fromStemSet (*stems, rate);
        auto write = [&] (const String& name, const float* l, const float* r, int64_t len)
        {
            WavAudioFormat wav;
            std::unique_ptr<OutputStream> stream = std::make_unique<FileOutputStream> (folder.getChildFile (name + ".wav"));
            auto writer = wav.createWriterFor (stream, AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (2).withBitsPerSample (24));
            if (writer == nullptr) return;
            for (int64_t pos = 0; pos < len; pos += 65536)
            {
                const float* chans[] = { l + pos, r + pos };
                writer->writeFromFloatArrays (chans, 2, (int) std::min<int64_t> (65536, len - pos));
            }
        };
        static const char* names[] = { "Sopranos", "Altos", "Tenors", "Basses" };
        for (int s = 0; s < sc::NumSections; ++s)
            write (String ("Sanctuary Choir - ") + names[s], rendered.channels[(size_t) s * 2].data(), rendered.channels[(size_t) s * 2 + 1].data(), rendered.length);
        const auto full = sc::mixdownStems (rendered, params, false);
        write ("Sanctuary Choir - Full Choir", full[0].data(), full[1].data(), (int64_t) full[0].size());
        const auto room = sc::mixdownStems (rendered, params, true);
        write ("Sanctuary Choir - Room", room[0].data(), room[1].data(), (int64_t) room[0].size());
        MessageManager::callAsync ([folder, onDone] { if (onDone) onDone (folder); });
    }).detach();
}

//==============================================================================
void LearnSession::timerCallback()
{
    // Keep capture memory allocated ahead of the audio thread.
    if (phase.load() == Phase::Recording)
    {
        const int64_t c = captured.load() / chunkSize;
        for (int64_t k = c; k < std::min<int64_t> (maxChunks, c + 8); ++k)
        {
            if (chunks[(size_t) k].load() == nullptr)
            {
                chunkStorage.push_back (std::make_unique<float[]> ((size_t) chunkSize));
                chunks[(size_t) k].store (chunkStorage.back().get(), std::memory_order_release);
            }
        }

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

    // Free old playback data once the audio thread can no longer be using it.
    const auto now = Time::getMillisecondCounter();
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(),
                                     [now] (const auto& g) { return now - g.second > 2000; }),
                     graveyard.end());
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
    if (owned != nullptr)
    {
        o->setProperty ("seconds", owned->score.durationSeconds);
        o->setProperty ("key", String (owned->score.keyName()));
        o->setProperty ("phrases", owned->score.numPhrases);
        o->setProperty ("notes", owned->score.numNotes);
        if (previewPlaying.load() && owned->sampleRate > 0)
            o->setProperty ("previewSeconds", (double) previewPos.load() / owned->sampleRate);
    }
    return var (o.release());
}

//==============================================================================
static constexpr int kStateMagic = 0x4b544353; // "SCTK"

void LearnSession::writeState (OutputStream& out) const
{
    if (! hasTake())
    {
        out.writeInt (0);
        return;
    }
    out.writeInt (kStateMagic);
    out.writeInt (1);
    out.writeDouble (owned->sampleRate);
    out.writeInt64 (owned->timelineStart);
    out.writeBool (owned->stems != nullptr && ! owned->stale.load());

    MemoryBlock flacData;
    {
        FlacAudioFormat flac;
        std::unique_ptr<OutputStream> stream = std::make_unique<MemoryOutputStream> (flacData, false);
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
    }
    out.writeInt64 ((int64) flacData.getSize());
    out.write (flacData.getData(), flacData.getSize());
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
    if (version != 1)
        return;
    const double rate = in.readDouble();
    const int64_t timelineStart = in.readInt64();
    const bool wasRendered = in.readBool();
    const int64_t size = in.readInt64();
    if (size <= 0 || size > in.getNumBytesRemaining())
        return;

    MemoryBlock block;
    in.readIntoMemoryBlock (block, (ssize_t) size);
    FlacAudioFormat flac;
    std::unique_ptr<AudioFormatReader> reader (flac.createReaderFor (new MemoryInputStream (block, false), true));
    if (reader == nullptr)
        return;
    std::vector<float> audio ((size_t) reader->lengthInSamples);
    float* dest[] = { audio.data() };
    reader->read (dest, 1, 0, (int) reader->lengthInSamples);
    startAnalysis (std::move (audio), rate, timelineStart, wasRendered);
}
} // namespace sanctuary
