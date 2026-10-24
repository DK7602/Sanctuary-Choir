// Sanctuary Choir - the venues' measured rooms.
//
// The rooms ship inside the plug-in (rooms/*.ogg, see tools/rooms). Each is decoded once per
// process when first needed, then built into a ConvolutionRoom for the session's sample rate:
// in prepareToPlay for the venue in use (so a bounce has it from the first sample), and on a
// background thread when the venue changes, handed to the audio thread without a lock.
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <sanctuary/Convolution.h>
#include <sanctuary/Effects.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace sanctuary
{
class MeasuredRooms : private juce::Timer
{
public:
    MeasuredRooms();
    ~MeasuredRooms() override;

    /** The venue to play (message thread, polled). */
    std::function<sc::Venue()> getVenue;

    /** Builds the current venue's room for this sample rate before returning (prepareToPlay). */
    void prepare (double sampleRate);

    /** The room the audio thread plays, or nullptr (lock-free). */
    sc::ConvolutionRoom* getLive() const noexcept { return live.load (std::memory_order_acquire); }
    /** The audio thread, after each block: the rooms the engine still holds (ChoirEngine::
        getRoomsInUse). A replaced room is freed only once it is neither, so a host that stops
        processing for a while (a silent track) never resumes into a freed room. */
    void setInUse (const sc::ConvolutionRoom* current, const sc::ConvolutionRoom* fading) noexcept
    {
        // Fading first: a room moving from current to fading is always named in one of them.
        inUseFading.store (fading);
        inUseCurrent.store (current);
    }

    /** A venue's measured response, decoded (any thread; decodes on first use). nullptr if the
        plug-in was built without it. */
    static std::shared_ptr<const sc::RoomImpulse> getImpulse (sc::Venue venue);

private:
    void timerCallback() override;
    void publish (std::unique_ptr<sc::ConvolutionRoom> room, int venue, double rate);
    void joinWorker();

    std::atomic<sc::ConvolutionRoom*> live { nullptr };
    std::atomic<const sc::ConvolutionRoom*> inUseCurrent { nullptr }, inUseFading { nullptr };
    std::unique_ptr<sc::ConvolutionRoom> current;
    int currentVenue = -1;
    double currentRate = 0.0;
    std::vector<std::pair<std::unique_ptr<sc::ConvolutionRoom>, juce::uint32>> retired;   // freed once the engine let go

    std::mutex lock, workerLock;
    std::unique_ptr<std::thread> worker;
    std::unique_ptr<sc::ConvolutionRoom> pending;   // built on the worker, published on the timer
    int pendingVenue = -1, buildingVenue = -1;
    double pendingRate = 0.0, sampleRate = 0.0;
};
} // namespace sanctuary
