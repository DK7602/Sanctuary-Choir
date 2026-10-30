#include "ChoirVoices.h"

#include <condition_variable>
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

        auto files = dir.findChildFiles (File::findFiles, true, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        files.sort();
        auto set = std::make_shared<sc::SampleSet>();
        set->name = displayName (dir).toStdString();

        // Each file's place in the folder, worked out here before the decoding threads start.
        // On Windows a File converts its path in place each time it reaches the disk, so no File
        // is used by two threads at once: below, a decoding thread touches only its own file.
        std::vector<std::string> paths;
        paths.reserve ((size_t) files.size());
        for (const auto& f : files)
            paths.push_back (f.getRelativePathFrom (dir).toStdString());

        // Decode on a few threads (a full voice pack is thousands of files), adding the notes
        // to the set in file order as they arrive. Only a window of files runs ahead, so the
        // decoded audio waiting to be added stays small.
        struct Decoded
        {
            std::vector<float> mono;
            double sampleRate = 0.0;
            bool ready = false;
        };
        const int count = files.size();
        std::vector<Decoded> slots ((size_t) count);
        std::mutex slotLock;
        std::condition_variable changed;
        int nextFile = 0, consumed = 0;
        constexpr int kWindow = 64;
        auto decode = [&]
        {
            AudioFormatManager formats;
            formats.registerBasicFormats();
            for (;;)
            {
                int i = 0;
                {
                    std::unique_lock<std::mutex> g (slotLock);
                    while (! cancel && nextFile < count && nextFile >= consumed + kWindow)
                        changed.wait_for (g, std::chrono::milliseconds (50));
                    if (cancel || nextFile >= count)
                        return;
                    i = nextFile++;
                }
                Decoded d;
                if (sc::SampleSet::sectionFromPath (paths[(size_t) i]) >= 0)
                {
                    std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (files.getReference (i)));
                    const int64 len = reader != nullptr && reader->sampleRate > 0.0
                                          ? jmin (reader->lengthInSamples, (int64) (reader->sampleRate * 600.0))   // up to 10 minutes per file
                                          : 0;
                    if (len > 0)
                    {
                        AudioBuffer<float> buffer ((int) reader->numChannels, (int) len);
                        reader->read (&buffer, 0, (int) len, 0, true, true);
                        d.mono.assign ((size_t) len, 0.0f);
                        const float scale = 1.0f / (float) jmax (1, buffer.getNumChannels());
                        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                        {
                            const float* src = buffer.getReadPointer (ch);
                            for (int64 k = 0; k < len; ++k)
                                d.mono[(size_t) k] += src[k] * scale;
                        }
                        d.sampleRate = reader->sampleRate;
                    }
                }
                d.ready = true;
                {
                    std::lock_guard<std::mutex> g (slotLock);
                    slots[(size_t) i] = std::move (d);
                }
                changed.notify_all();
            }
        };
        std::vector<std::thread> pool;
        const int threads = jlimit (1, 6, (int) std::thread::hardware_concurrency() - 1);
        for (int t = 0; t < jmin (threads, count); ++t)
            pool.emplace_back (decode);

        for (int i = 0; i < count && ! cancel; ++i)
        {
            Decoded d;
            {
                std::unique_lock<std::mutex> g (slotLock);
                while (! cancel && ! slots[(size_t) i].ready)
                    changed.wait_for (g, std::chrono::milliseconds (50));
                if (cancel)
                    break;
                d = std::move (slots[(size_t) i]);
                slots[(size_t) i] = {};
                consumed = i + 1;
            }
            changed.notify_all();
            progress = (float) (i + 1) / (float) count;
            if (d.mono.empty())
                continue;
            const auto& path = paths[(size_t) i];
            const int section = sc::SampleSet::sectionFromPath (path);
            const int vowel = std::max (0, sc::SampleSet::vowelFromPath (path));
            const auto len = (int64_t) d.mono.size();
            // Files named with their pitch ("..._m6312.ogg") are already cut: held notes, or the
            // onsets and moves their folders and names say they are.
            if (const float root = sc::SampleSet::rootFromPath (path); root > 0.0f)
                set->addNote (section, vowel, d.mono.data(), len, d.sampleRate, root, sc::SampleSet::infoFromPath (path));
            else
                set->addRecording (section, vowel, d.mono.data(), len, d.sampleRate);
        }
        changed.notify_all();
        for (auto& t : pool)
            t.join();

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
