#include "ChoirVoices.h"

#include <map>

namespace sanctuary
{
using namespace juce;

namespace
{
    // One copy of each loaded set per process, shared by every instance of the plug-in.
    // Deliberately never destroyed: a loading thread may still finish while the host shuts
    // down, after ordinary statics are gone.
    struct Cache
    {
        std::mutex lock;
        std::map<String, std::weak_ptr<sc::SampleSet>> sets;
        std::map<String, std::shared_ptr<std::mutex>> loading;   // one load per folder at a time
    };
    Cache& cache()
    {
        static Cache* c = new Cache();
        return *c;
    }

    String displayName (const File& dir)
    {
        return dir == ChoirVoices::findBundledVoices() ? String ("Sanctuary Voices") : dir.getFileName();
    }
}

File ChoirVoices::findBundledVoices()
{
    const String rel = "Sanctuary Choir/Voices/SanctuaryVoices";
    Array<File> places;
   #if JUCE_MAC
    places.add (File ("/Library/Application Support").getChildFile (rel));
    places.add (File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("Application Support").getChildFile (rel));
   #elif JUCE_WINDOWS
    places.add (File::getSpecialLocation (File::commonApplicationDataDirectory).getChildFile (rel));
    places.add (File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile (rel));
   #else
    places.add (File::getSpecialLocation (File::userHomeDirectory).getChildFile (".local/share").getChildFile (rel));
    places.add (File ("/usr/share").getChildFile (rel));
    places.add (File ("/usr/local/share").getChildFile (rel));
   #endif
    for (const auto& f : places)
        if (f.isDirectory())
            return f;
    return {};
}

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
    choseBuiltIn = false;
    {
        std::lock_guard<std::mutex> g (lock);
        folder = dir;
        error.clear();
        pending.reset();
    }

    // Already loaded by another instance? Share it.
    {
        std::lock_guard<std::mutex> g (cache().lock);
        if (auto shared = cache().sets[dir.getFullPathName()].lock())
        {
            publish (shared);
            return;
        }
    }

    progress = 0.0f;
    loading = true;
    if (onStatusChanged)
        onStatusChanged();

    worker = std::make_unique<std::thread> ([this, dir]
    {
        // Several instances opening with a project would otherwise each decode (and keep) their
        // own copy: wait for whichever started first, then share its set.
        const String key = dir.getFullPathName();
        std::shared_ptr<std::mutex> folderLock;
        {
            std::lock_guard<std::mutex> g (cache().lock);
            auto& m = cache().loading[key];
            if (m == nullptr)
                m = std::make_shared<std::mutex>();
            folderLock = m;
        }
        std::unique_lock<std::mutex> busy (*folderLock, std::defer_lock);
        while (! busy.try_lock())
        {
            if (cancel)
            {
                loading = false;
                return;
            }
            Thread::sleep (20);
        }
        {
            std::lock_guard<std::mutex> g (cache().lock);
            if (auto shared = cache().sets[key].lock())
            {
                std::lock_guard<std::mutex> g2 (lock);
                pending = shared;
                loading = false;
                return;
            }
        }

        AudioFormatManager formats;
        formats.registerBasicFormats();
        auto files = dir.findChildFiles (File::findFiles, true, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        files.sort();

        auto set = std::make_shared<sc::SampleSet>();
        set->name = displayName (dir).toStdString();
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
            // Files named with their pitch ("..._m6312.ogg") are single notes, already cut.
            if (const float root = sc::SampleSet::rootFromPath (path); root > 0.0f)
                set->addNote (section, vowel, mono.data(), (int64_t) len, reader->sampleRate, root);
            else
                set->addRecording (section, vowel, mono.data(), (int64_t) len, reader->sampleRate);
            progress = (float) (i + 1) / (float) files.size();
        }

        if (! cancel)
        {
            set->finalise();
            if (! set->isEmpty())
            {
                std::lock_guard<std::mutex> g (cache().lock);
                cache().sets[key] = set;
            }
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
    choseBuiltIn = true;
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

bool ChoirVoices::isBusy() const
{
    if (loading.load())
        return true;
    std::lock_guard<std::mutex> g (lock);
    return pending != nullptr;
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
    o->setProperty ("bundled", findBundledVoices().isDirectory());
    o->setProperty ("usingBundled", current != nullptr && usesBundledVoices());
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
