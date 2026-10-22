#include "ChoirVoices.h"

namespace sanctuary
{
using namespace juce;

ChoirVoices::ChoirVoices()
{
    startTimerHz (5);
}

ChoirVoices::~ChoirVoices()
{
    stopTimer();
    cancel = true;
    if (worker != nullptr && worker->joinable())
        worker->join();
    live.store (nullptr);
}

void ChoirVoices::loadFolder (const File& dir)
{
    cancel = true;
    if (worker != nullptr && worker->joinable())
        worker->join();
    cancel = false;
    {
        std::lock_guard<std::mutex> g (lock);
        folder = dir;
        error.clear();
        pending.reset();
    }
    progress = 0.0f;
    loading = true;
    if (onStatusChanged)
        onStatusChanged();

    worker = std::make_unique<std::thread> ([this, dir]
    {
        AudioFormatManager formats;
        formats.registerBasicFormats();
        auto files = dir.findChildFiles (File::findFiles, true, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        files.sort();

        auto set = std::make_shared<sc::SampleSet>();
        set->name = dir.getFileName().toStdString();
        for (int i = 0; i < files.size() && ! cancel; ++i)
        {
            const auto& f = files.getReference (i);
            const auto path = f.getRelativePathFrom (dir).toStdString();
            const int section = sc::SampleSet::sectionFromPath (path);
            if (section < 0)
                continue;
            const int vowel = std::max (0, sc::SampleSet::vowelFromPath (path));

            std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (f));
            if (reader == nullptr || reader->sampleRate <= 0.0)
                continue;
            const int64 len = jmin (reader->lengthInSamples, (int64) (reader->sampleRate * 600.0));   // up to 10 minutes per file
            if (len <= 0)
                continue;
            AudioBuffer<float> buffer ((int) reader->numChannels, (int) len);
            reader->read (&buffer, 0, (int) len, 0, true, true);
            std::vector<float> mono ((size_t) len, 0.0f);
            const float scale = 1.0f / (float) jmax (1, buffer.getNumChannels());
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                const float* src = buffer.getReadPointer (ch);
                for (int64 k = 0; k < len; ++k)
                    mono[(size_t) k] += src[k] * scale;
            }
            set->addRecording (section, vowel, mono.data(), (int64_t) len, reader->sampleRate);
            progress = (float) (i + 1) / (float) files.size();
        }

        if (! cancel)
        {
            set->finalise();
            std::lock_guard<std::mutex> g (lock);
            if (set->isEmpty())
                error = "No usable recordings found. Name files or folders by section (soprano, alto, tenor, bass) "
                        "and vowel (ah, oh, oo, ee, eh, mm), with held notes of at least a second.";
            else
                pending = set;
        }
        loading = false;
    });
}

void ChoirVoices::clear()
{
    cancel = true;
    if (worker != nullptr && worker->joinable())
        worker->join();
    cancel = false;
    loading = false;
    {
        std::lock_guard<std::mutex> g (lock);
        folder = File();
        error.clear();
        pending.reset();
    }
    publish (nullptr);
}

void ChoirVoices::publish (std::shared_ptr<sc::SampleSet> set)
{
    if (current != nullptr)
        retired.push_back ({ current, Time::getMillisecondCounter() });
    current = std::move (set);
    live.store (current.get(), std::memory_order_release);
    if (onSetChanged)
        onSetChanged();
    if (onStatusChanged)
        onStatusChanged();
}

std::shared_ptr<const sc::SampleSet> ChoirVoices::getCurrent() const
{
    return current;
}

void ChoirVoices::timerCallback()
{
    std::shared_ptr<sc::SampleSet> ready;
    bool failed = false;
    if (! loading)
    {
        std::lock_guard<std::mutex> g (lock);
        ready = std::move (pending);
        pending.reset();
        failed = error.isNotEmpty() && wasLoading;
    }
    if (ready != nullptr)
        publish (std::move (ready));
    else if (failed && onStatusChanged)
        onStatusChanged();
    if (wasLoading != loading.load() && onStatusChanged)
        onStatusChanged();
    wasLoading = loading.load();

    // The audio thread picks up a new set at its next block; free old ones well after that.
    const auto now = Time::getMillisecondCounter();
    retired.erase (std::remove_if (retired.begin(), retired.end(), [now] (const auto& r) { return now - r.second > 2000; }), retired.end());
}

File ChoirVoices::getFolder() const
{
    std::lock_guard<std::mutex> g (lock);
    return folder;
}

var ChoirVoices::getStatus() const
{
    auto o = std::make_unique<DynamicObject>();
    o->setProperty ("loading", loading.load());
    o->setProperty ("progress", progress.load());
    {
        std::lock_guard<std::mutex> g (lock);
        o->setProperty ("folder", folder.getFullPathName());
        o->setProperty ("error", error);
    }
    if (current != nullptr)
    {
        o->setProperty ("name", String (CharPointer_UTF8 (current->name.c_str())));
        o->setProperty ("text", String (CharPointer_UTF8 (current->describe().c_str())));
    }
    return var (o.release());
}
} // namespace sanctuary
