#include "MeasuredRooms.h"

#include <SanctuaryRoomsData.h>

#include <map>

namespace sanctuary
{
using namespace juce;

namespace
{
    const char* findResource (const String& file, int& size)
    {
        for (int i = 0; i < SanctuaryRoomsData::namedResourceListSize; ++i)
            if (file == SanctuaryRoomsData::originalFilenames[i])
                return SanctuaryRoomsData::getNamedResource (SanctuaryRoomsData::namedResourceList[i], size);
        size = 0;
        return nullptr;
    }

    /** How steeply the file was stored rising (SOURCES.json), in dB a second. */
    double emphasisFor (const String& file)
    {
        int size = 0;
        const char* data = findResource ("SOURCES.json", size);
        if (data == nullptr)
            return 0.0;
        const var sources = JSON::parse (String::fromUTF8 (data, size));
        if (const auto* rows = sources.getArray())
            for (const auto& r : *rows)
                if (r.getProperty ("file", {}).toString() == file)
                    return (double) r.getProperty ("emphasisDbPerSecond", 0.0);
        return 0.0;
    }

    std::shared_ptr<const sc::RoomImpulse> decode (sc::Venue venue)
    {
        const String file = String (sc::getVenueSlug (venue)) + ".ogg";
        int size = 0;
        const char* data = findResource (file, size);
        if (data == nullptr)
            return {};
        OggVorbisAudioFormat ogg;
        std::unique_ptr<AudioFormatReader> reader (ogg.createReaderFor (new MemoryInputStream (data, (size_t) size, false), true));
        if (reader == nullptr || reader->lengthInSamples <= 0 || reader->numChannels == 0)
            return {};
        const int n = (int) reader->lengthInSamples;
        AudioBuffer<float> buf ((int) reader->numChannels, n);
        reader->read (&buf, 0, n, 0, true, true);

        // The decay was stored flattened: take that back out, then scale to unit energy.
        auto ir = std::make_shared<sc::RoomImpulse>();
        ir->sampleRate = reader->sampleRate;
        ir->left.resize ((size_t) n);
        ir->right.resize ((size_t) n);
        const double step = std::pow (10.0, -emphasisFor (file) / reader->sampleRate / 20.0);
        const int rightCh = std::min (1, buf.getNumChannels() - 1);
        double g = 1.0, energy = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const float l = (float) (buf.getSample (0, i) * g), r = (float) (buf.getSample (rightCh, i) * g);
            ir->left[(size_t) i] = l;
            ir->right[(size_t) i] = r;
            energy += 0.5 * ((double) l * l + (double) r * r);
            g *= step;
        }
        if (energy <= 0.0)
            return {};
        const float scale = (float) (1.0 / std::sqrt (energy));
        for (auto* ch : { &ir->left, &ir->right })
            for (auto& v : *ch)
                v *= scale;
        return ir;
    }

    // One decoded copy of each room per process, shared by every instance of the plug-in.
    // Deliberately never destroyed (a build may still finish while the host shuts down).
    struct Cache
    {
        std::mutex lock;
        std::map<int, std::shared_ptr<const sc::RoomImpulse>> rooms;
    };
    Cache& cache()
    {
        static Cache* c = new Cache();
        return *c;
    }
}

std::shared_ptr<const sc::RoomImpulse> MeasuredRooms::getImpulse (sc::Venue venue)
{
    std::lock_guard<std::mutex> g (cache().lock);
    auto& slot = cache().rooms[(int) venue];
    if (slot == nullptr)
        slot = decode (venue);
    return slot;
}

MeasuredRooms::MeasuredRooms()
{
    startTimerHz (10);
}

MeasuredRooms::~MeasuredRooms()
{
    stopTimer();
    joinWorker();
    live.store (nullptr);
}

void MeasuredRooms::joinWorker()
{
    std::lock_guard<std::mutex> g (workerLock);
    if (worker != nullptr && worker->joinable())
        worker->join();
    worker.reset();
}

void MeasuredRooms::prepare (double rate)
{
    joinWorker();
    const auto venue = getVenue ? getVenue() : sc::Venue::GrandCathedral;
    auto ir = getImpulse (venue);
    auto room = ir != nullptr ? sc::ConvolutionRoom::create (*ir, rate) : nullptr;
    std::lock_guard<std::mutex> g (lock);
    sampleRate = rate;
    pending.reset();
    pendingVenue = buildingVenue = -1;
    publish (std::move (room), (int) venue, rate);
}

void MeasuredRooms::publish (std::unique_ptr<sc::ConvolutionRoom> room, int venue, double rate)
{
    // A room the engine played stays alive a while: its tail fades out after the switch.
    if (current != nullptr)
        retired.push_back ({ std::move (current), Time::getMillisecondCounter() });
    current = std::move (room);
    currentVenue = venue;
    currentRate = rate;
    live.store (current.get(), std::memory_order_release);
}

void MeasuredRooms::timerCallback()
{
    const auto venue = getVenue ? (int) getVenue() : -1;
    bool startBuild = false;
    double rate = 0.0;
    {
        std::lock_guard<std::mutex> g (lock);
        const auto now = Time::getMillisecondCounter();
        retired.erase (std::remove_if (retired.begin(), retired.end(),
                                       [now] (const auto& r) { return now - r.second > 3000; }),
                       retired.end());

        if (pendingVenue >= 0)
        {
            if (pendingRate == sampleRate)
                publish (std::move (pending), pendingVenue, pendingRate);
            pending.reset();
            pendingVenue = -1;
        }
        if (venue >= 0 && sampleRate > 0.0 && buildingVenue < 0 && (venue != currentVenue || currentRate != sampleRate))
        {
            buildingVenue = venue;
            rate = sampleRate;
            startBuild = true;
        }
    }
    if (! startBuild)
        return;
    joinWorker();
    std::lock_guard<std::mutex> wg (workerLock);
    worker = std::make_unique<std::thread> ([this, venue, rate]
    {
        auto ir = getImpulse ((sc::Venue) venue);
        auto room = ir != nullptr ? sc::ConvolutionRoom::create (*ir, rate) : nullptr;
        std::lock_guard<std::mutex> g (lock);
        if (buildingVenue == venue)
        {
            pending = std::move (room);
            pendingVenue = venue;
            pendingRate = rate;
            buildingVenue = -1;
        }
    });
}
} // namespace sanctuary
