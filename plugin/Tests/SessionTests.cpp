// Sanctuary Choir - tests for the plug-in's LEARN / RENDER session (message-thread logic).
//
// Drives a LearnSession the way the processor does: audio blocks in, button presses and
// setting changes on the message thread, with the session's timer running in between.
#include "../Source/LearnSession.h"
#include "TestSignals.h"

#include <cstdio>

using namespace juce;
using sanctuary::LearnSession;

static int checks = 0, failures = 0;

static void check (bool ok, const char* what)
{
    ++checks;
    if (! ok)
    {
        ++failures;
        std::printf ("  FAILED: %s\n", what);
    }
}

static void pump (int ms)
{
    MessageManager::getInstance()->runDispatchLoopUntil (ms);
}

static bool waitFor (LearnSession& s, std::function<bool()> done, int timeoutMs = 60000)
{
    for (int waited = 0; waited < timeoutMs; waited += 20)
    {
        if (done())
            return true;
        pump (20);
    }
    return done();
}

static String phaseName (const LearnSession& s) { return s.getStatus().getProperty ("phase", {}).toString(); }

struct Rig
{
    LearnSession session;
    sc::EngineParams params;
    bool voicesBusy = false;

    explicit Rig (double rate)
    {
        params.vocalType = sc::VocalType::Ah;
        params.choirSize = 10;
        session.getParams = [this] { return params; };
        session.voicesBusy = [this] { return voicesBusy; };
        session.standalone = true;   // capture starts at once, no host transport needed
        session.prepare (rate);
    }

    /** Sings the take into the session in host-sized blocks (with a band on the side-chain). */
    void sing (const std::vector<float>& take, const std::vector<float>* band = nullptr)
    {
        std::vector<float> block (512);
        for (size_t pos = 0; pos < take.size(); pos += block.size())
        {
            const size_t n = std::min (block.size(), take.size() - pos);
            std::copy (take.begin() + (long) pos, take.begin() + (long) (pos + n), block.begin());
            float* ch[] = { block.data() };
            LearnSession::BlockInfo info;
            if (band != nullptr && pos + n <= band->size())
                info.sidechainL = band->data() + pos;
            session.processInput (ch, 1, (int) n, info);
            if ((pos / block.size()) % 64 == 0)
                pump (1);
        }
    }

    double learnedSeconds() const { return (double) session.getStatus().getProperty ("seconds", 0.0); }
};

static std::vector<float> melody (double rate, float transpose, int repeats)
{
    std::vector<sc::test::SungNote> notes;
    for (int r = 0; r < repeats; ++r)
        for (const auto& n : sc::test::cMajorScale (60.0f + transpose, 0.35f))
            notes.push_back (n);
    return sc::test::singMelody (notes, rate, 1);
}

static void renderPressedWhileLearning()
{
    std::printf ("RENDER pressed while a new take is still being learned\n");
    Rig rig (48000.0);
    rig.session.startRecording();
    rig.sing (melody (48000.0, 0.0f, 1));
    rig.session.stopRecording();
    check (waitFor (rig.session, [&] { return phaseName (rig.session) == "learned"; }), "first take learned");
    const double firstSeconds = rig.learnedSeconds();

    // Second, longer take; RENDER pressed while it is still recording.
    rig.session.startRecording();
    rig.sing (melody (48000.0, 2.0f, 2));
    rig.session.render();
    check (waitFor (rig.session, [&] { return phaseName (rig.session) == "rendered"; }), "the new take is rendered");
    check (rig.learnedSeconds() > firstSeconds * 1.5, "the rendered take is the new one, not the old one");

    // Third take; RENDER pressed while it is being analysed.
    rig.session.startRecording();
    rig.sing (melody (48000.0, -2.0f, 1));
    rig.session.stopRecording();
    check (phaseName (rig.session) == "analysing", "analysing after LEARN is pressed again");
    rig.session.render();
    check (waitFor (rig.session, [&] { return phaseName (rig.session) == "rendered"; }), "rendered after the analysis");
    check (std::abs (rig.learnedSeconds() - firstSeconds) < 0.5, "the rendered take is the third one");
}

static void settingsChangedWhileRendering()
{
    std::printf ("Settings changed while rendering are not lost\n");
    Rig rig (48000.0);
    rig.session.startRecording();
    rig.sing (melody (48000.0, 0.0f, 2));
    rig.session.stopRecording();
    check (waitFor (rig.session, [&] { return phaseName (rig.session) == "learned"; }), "learned");

    rig.session.render();
    check (phaseName (rig.session) == "rendering", "rendering");
    rig.params.vocalType = sc::VocalType::Oh;   // changed mid-render
    rig.session.renderSettingsChanged();
    check (waitFor (rig.session, [&] { return phaseName (rig.session) != "rendering"; }), "render finished");
    check ((bool) rig.session.getStatus().getProperty ("stale", false), "the stems are marked out of date");
    check (phaseName (rig.session) == "learned", "not shown as rendered");

    rig.session.render();
    check (waitFor (rig.session, [&] { return phaseName (rig.session) == "rendered"; }), "rendered again");
    rig.params.key = 3;   // D; then changed to G while the new take is analysed
    rig.session.startRecording();
    rig.sing (melody (48000.0, 0.0f, 1));
    rig.session.stopRecording();
    rig.params.key = 8;
    rig.session.harmonySettingsChanged();
    check (waitFor (rig.session, [&] { return phaseName (rig.session) == "learned"; }), "learned");
    pump (100);
    check (rig.session.getStatus().getProperty ("key", "").toString().startsWith ("G"), "harmony uses the key chosen during the analysis");
}

static int64_t storedRate (const MemoryBlock& state)
{
    MemoryInputStream in (state, false);
    if (in.readInt() != 0x4b544353) return 0;
    in.readInt();
    return (int64_t) std::llround (in.readDouble());
}

static void projectReopenedAtAnotherRate()
{
    std::printf ("A take learned at 44.1 kHz follows a project reopened at 48 kHz\n");
    MemoryBlock state;
    double seconds = 0.0;
    {
        Rig rig (44100.0);
        rig.session.startRecording();
        rig.sing (melody (44100.0, 0.0f, 1));
        rig.session.stopRecording();
        check (waitFor (rig.session, [&] { return phaseName (rig.session) == "learned"; }), "learned at 44.1 kHz");
        seconds = rig.learnedSeconds();
        MemoryOutputStream out (state, false);
        rig.session.writeState (out);
    }
    check (storedRate (state) == 44100, "stored at 44.1 kHz");

    Rig rig (48000.0);
    MemoryInputStream in (state, false);
    rig.session.readState (in);
    check (waitFor (rig.session, [&] {
        MemoryBlock now;
        MemoryOutputStream out (now, false);
        rig.session.writeState (out);
        out.flush();
        return phaseName (rig.session) == "learned" && storedRate (now) == 48000;
    }), "converted to 48 kHz");
    check (std::abs (rig.learnedSeconds() - seconds) < 0.1, "same length in seconds");
}

static void reloadWaitsForVoicesAndSkipsSameTake()
{
    std::printf ("A rendered project waits for the recorded voices, and restoring the same take is free\n");
    MemoryBlock state;
    {
        Rig rig (48000.0);
        rig.session.startRecording();
        rig.sing (melody (48000.0, 0.0f, 1));
        rig.session.stopRecording();
        rig.session.render();
        check (waitFor (rig.session, [&] { return phaseName (rig.session) == "rendered"; }), "rendered");
        MemoryOutputStream out (state, false);
        rig.session.writeState (out);
    }

    Rig rig (48000.0);
    rig.voicesBusy = true;
    MemoryInputStream in (state, false);
    rig.session.readState (in);
    check (waitFor (rig.session, [&] { return phaseName (rig.session) == "learned"; }), "learned again");
    pump (300);
    check (phaseName (rig.session) == "learned", "no render while the voices are loading");
    rig.voicesBusy = false;
    check (waitFor (rig.session, [&] { return phaseName (rig.session) == "rendered"; }), "rendered once they are loaded");

    MemoryBlock again;
    {
        MemoryOutputStream out (again, false);
        rig.session.writeState (out);
    }
    check (again == state, "the saved take is identical");
    MemoryInputStream in2 (again, false);
    rig.session.readState (in2);
    check (phaseName (rig.session) == "rendered", "restoring the same take keeps the render");
}

static void learnRecordsTheBand()
{
    std::printf ("LEARN records the band on the side-chain, harmonises with it and keeps it with the project\n");
    MemoryBlock state;
    const double rate = 48000.0;
    auto take = melody (rate, 0.0f, 2);
    std::vector<sc::test::BandChordSpec> prog;
    for (int i = 0; i < 6; ++i)
        prog.push_back (i % 2 == 0 ? sc::test::BandChordSpec { { 60, 64, 67 }, 36, 2.0f } : sc::test::BandChordSpec { { 57, 60, 65 }, 41, 2.0f });
    auto band = sc::test::playBand (prog, rate);
    band.resize (take.size(), 0.0f);
    {
        Rig rig (rate);
        rig.session.bandConnected = true;
        rig.session.startRecording();
        rig.sing (take, &band);
        rig.session.stopRecording();
        check (waitFor (rig.session, [&] { return phaseName (rig.session) == "learned"; }), "learned");
        const String text = rig.session.getStatus().getProperty ("text", "").toString();
        std::printf ("  %s\n", text.toRawUTF8());
        check (text.contains ("WITH THE BAND"), "harmonised with the band");
        MemoryOutputStream out (state, false);
        rig.session.writeState (out);
    }
    {
        Rig rig (rate);
        MemoryInputStream in (state, false);
        rig.session.readState (in);
        check (waitFor (rig.session, [&] { return phaseName (rig.session) == "learned"; }), "learned again from the project");
        check (rig.session.getStatus().getProperty ("text", "").toString().contains ("WITH THE BAND"), "the band's chords come back with the project");
        rig.params.followBand = false;
        rig.session.harmonySettingsChanged();
        check (! rig.session.getStatus().getProperty ("text", "").toString().contains ("WITH THE BAND"), "Follow the Band off: the choir's own harmony");
    }
    {
        // No band connected: nothing extra is recorded, the take is saved as before (version 1).
        Rig rig (rate);
        rig.session.startRecording();
        rig.sing (take);
        rig.session.stopRecording();
        check (waitFor (rig.session, [&] { return phaseName (rig.session) == "learned"; }), "learned without a band");
        MemoryBlock plain;
        {
            MemoryOutputStream out (plain, false);
            rig.session.writeState (out);
        }
        MemoryInputStream in (plain, false);
        in.readInt();
        check (in.readInt() == 1, "saved in the original format");
    }
}

int main()
{
    ScopedJuceInitialiser_GUI juce;
    renderPressedWhileLearning();
    settingsChangedWhileRendering();
    projectReopenedAtAnotherRate();
    reloadWaitsForVoicesAndSkipsSameTake();
    learnRecordsTheBand();
    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
