// Sanctuary Choir - tests for the plug-in's LEARN / RENDER session (message-thread logic).
//
// Drives a LearnSession the way the processor does: audio blocks in, button presses and
// setting changes on the message thread, with the session's timer running in between.
#include "../Source/ChoirVoices.h"
#include "../Source/LearnSession.h"
#include "../Source/MeasuredRooms.h"
#include "../Source/ai/AiSingers.h"
#include "TestSignals.h"

#include <sanctuary/FFT.h>
#include <sanctuary/PitchTracker.h>

#include <algorithm>
#include <complex>
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

    String mixSignature = "mix 1";
    File exports = File::getSpecialLocation (File::tempDirectory).getNonexistentChildFile ("SanctuarySessionTests", {}, false);

    ~Rig() { exports.deleteRecursively(); }

    explicit Rig (double rate)
    {
        params.vocalType = sc::VocalType::Ah;
        params.choirSize = 10;
        session.getParams = [this] { return params; };
        session.voicesBusy = [this] { return voicesBusy; };
        session.getMixSignature = [this] { return mixSignature; };
        session.exportRoot = exports;
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

    /** Sings the take in time with a host that plays from songStart (samples) at bpm. */
    void singInSong (const std::vector<float>& take, int64_t songStart, double bpm, double rate)
    {
        std::vector<float> block (512);
        for (size_t pos = 0; pos < take.size(); pos += block.size())
        {
            const size_t n = std::min (block.size(), take.size() - pos);
            std::copy (take.begin() + (long) pos, take.begin() + (long) (pos + n), block.begin());
            float* ch[] = { block.data() };
            LearnSession::BlockInfo info;
            info.hostPlaying = true;
            info.hasPosition = true;
            info.hostPosition = songStart + (int64_t) pos;
            info.bpm = bpm;
            info.ppq = (double) info.hostPosition / rate * bpm / 60.0;
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
    check (rig.session.isPreparingPlayback(), "a bounce would wait while the take is analysed and rendered");
    check (waitFor (rig.session, [&] { return phaseName (rig.session) == "rendered"; }), "rendered after the analysis");
    check (std::abs (rig.learnedSeconds() - firstSeconds) < 0.5, "the rendered take is the third one");
    pump (150);
    check (! rig.session.isPreparingPlayback(), "and goes on once it is rendered");
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
    check (rig.session.isPreparingPlayback(), "a bounce would wait while the take is converted");
    check (waitFor (rig.session, [&] {
        MemoryBlock now;
        MemoryOutputStream out (now, false);
        rig.session.writeState (out);
        out.flush();
        return phaseName (rig.session) == "learned" && storedRate (now) == 48000;
    }), "converted to 48 kHz");
    check (std::abs (rig.learnedSeconds() - seconds) < 0.1, "same length in seconds");
    pump (150);
    check (! rig.session.isPreparingPlayback(), "and goes on once it plays at 48 kHz");
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

static void learnPressedWhileTheSongPlays()
{
    std::printf ("LEARN pressed while the song plays keeps the phrase that had already begun\n");
    const double rate = 48000.0, bpm = 120.0;
    const int64_t songStart = (int64_t) (10.0 * rate);   // the host plays from 0:10
    // Silence, then a phrase from 2.5 s; LEARN is pressed at 4.0 s, in the middle of it.
    std::vector<float> stream ((size_t) (2.5 * rate), 0.0f);
    const auto phrase = melody (rate, 0.0f, 1);
    stream.insert (stream.end(), phrase.begin(), phrase.end());
    stream.resize ((size_t) (8.0 * rate), 0.0f);
    auto play = [&] (Rig& rig, size_t from, size_t to, bool playing)
    {
        std::vector<float> block (512);
        for (size_t pos = from; pos < to; pos += block.size())
        {
            const size_t n = std::min (block.size(), to - pos);
            std::copy_n (stream.begin() + (long) pos, n, block.begin());
            float* ch[] = { block.data() };
            LearnSession::BlockInfo info;
            info.hostPlaying = playing;
            info.hasPosition = true;
            info.hostPosition = songStart + (int64_t) pos;
            info.bpm = bpm;
            info.ppq = (double) info.hostPosition / rate * bpm / 60.0;
            rig.session.processInput (ch, 1, (int) n, info);
            if ((pos / block.size()) % 64 == 0)
                pump (1);
        }
    };
    {
        Rig rig (rate);
        rig.session.standalone = false;
        play (rig, 0, (size_t) (4.0 * rate), true);
        rig.session.startRecording();
        play (rig, (size_t) (4.0 * rate), stream.size(), true);
        play (rig, stream.size() - 512, stream.size(), false);   // the song stops: so does LEARN
        check (waitFor (rig.session, [&] { return phaseName (rig.session) == "learned"; }), "learned");
        const var lane = rig.session.getChordLane();
        const double start = (double) lane.getProperty ("songStart", -1.0);
        const auto* notes = lane.getProperty ("melody", var()).getArray();
        const double firstNote = notes != nullptr && ! notes->isEmpty() ? (double) (int64) (*notes)[0].getProperty ("s", 0) / rate : -1.0;
        std::printf ("  the take starts at %.2f s in the song (bar %d beat %d), its first note %.2f s in\n", start,
                     (int) lane.getProperty ("bar", 0), (int) lane.getProperty ("beat", 0), firstNote);
        check (std::abs (start - 11.0) < 0.02, "the take starts 3 s before LEARN was pressed");
        check ((int) lane.getProperty ("bar", 0) == 6 && (int) lane.getProperty ("beat", 0) == 3, "and its MIDI lines up with the song there");
        // The phrase starts 1.5 s into the take (its first vowel after a short consonant); LEARN was pressed at 3 s.
        check (firstNote > 1.5 && firstNote < 1.75, "the phrase that had begun is learned from its first note");
    }
    {
        // LEARN pressed with the song stopped waits for play and starts right there, as before.
        Rig rig (rate);
        rig.session.standalone = false;
        play (rig, 0, (size_t) (2.0 * rate), true);
        play (rig, (size_t) (2.0 * rate), (size_t) (2.0 * rate) + 512, false);
        rig.session.startRecording();
        play (rig, (size_t) (2.0 * rate), stream.size(), true);
        play (rig, stream.size() - 512, stream.size(), false);
        check (waitFor (rig.session, [&] { return phaseName (rig.session) == "learned"; }), "learned from play");
        const double start = (double) rig.session.getChordLane().getProperty ("songStart", -1.0);
        check (std::abs (start - 12.0) < 0.001, "a take started from play begins where play began");
    }
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


static int64_t wavLength (const File& f, bool& silentUntil, int64_t offset)
{
    WavAudioFormat wav;
    std::unique_ptr<AudioFormatReader> reader (wav.createReaderFor (new FileInputStream (f), true));
    if (reader == nullptr)
        return -1;
    AudioBuffer<float> head (2, (int) offset + 48000);
    reader->read (&head, 0, head.getNumSamples(), 0, true, true);
    silentUntil = head.getMagnitude (0, (int) offset) == 0.0f && head.getMagnitude ((int) offset, 48000) > 0.0f;
    return (int64_t) reader->lengthInSamples;
}

static void chordLaneAndDragOut()
{
    std::printf ("The chord lane: chosen chords stay with the project; MIDI and audio files line up with the song\n");
    const double rate = 48000.0;
    const int64_t songStart = 2 * 48000;   // the take starts 2 s (bar 2 at 120 bpm) into the song
    MemoryBlock state;
    String lockedName;
    int64_t lockedStart = 0;
    {
        Rig rig (rate);
        rig.session.standalone = false;
        rig.session.startRecording();
        rig.singInSong (melody (rate, 0.0f, 2), songStart, 120.0, rate);
        rig.session.stopRecording();
        check (waitFor (rig.session, [&] { return phaseName (rig.session) == "learned"; }), "learned in time with the song");

        const var lane = rig.session.getChordLane();
        const auto* spans = lane.getProperty ("spans", {}).getArray();
        check (spans != nullptr && spans->size() >= 3, "the lane shows the chords");
        check (lane.getProperty ("melody", {}).getArray() != nullptr && lane.getProperty ("melody", {}).getArray()->size() >= 10, "and the melody");
        // The four parts, each in order and in its range, sopranos above the basses.
        {
            const auto* parts = lane.getProperty ("parts", {}).getArray();
            check (parts != nullptr && parts->size() == 4, "and the four parts");
            bool ordered = parts != nullptr;
            double top = 0, bottom = 0;
            for (int s = 0; parts != nullptr && s < parts->size(); ++s)
            {
                const auto* notes = (*parts)[s].getArray();
                ordered = ordered && notes != nullptr && notes->size() >= 4;
                double sum = 0;
                for (int k = 0; notes != nullptr && k < notes->size(); ++k)
                {
                    const auto& n = (*notes)[k];
                    ordered = ordered && (int64) n.getProperty ("e", 0) > (int64) n.getProperty ("s", 0)
                              && (k == 0 || (int64) n.getProperty ("s", 0) >= (int64) (*notes)[k - 1].getProperty ("s", 0));
                    sum += (double) (int) n.getProperty ("p", 0);
                }
                const double mean = notes != nullptr && notes->size() > 0 ? sum / notes->size() : 0;
                if (s == 0) top = mean;
                if (s == 3) bottom = mean;
            }
            std::printf ("  parts: sopranos around %.1f, basses around %.1f\n", top, bottom);
            check (ordered && top > bottom + 12.0, "each part's notes in order, the sopranos above the basses");
        }
        check (std::abs ((double) lane.getProperty ("songStart", 0.0) - 2.0) < 0.02, "the take starts 2 s into the song");
        check ((int) lane.getProperty ("bar", 0) == 2 && (int) lane.getProperty ("beat", 0) == 1, "at bar 2");
        check ((bool) lane.getProperty ("editable", false), "chords can be chosen");
        if (spans == nullptr || spans->size() < 3)
            return;

        // Choose another chord for the second span.
        const var span = (*spans)[1];
        const int64_t s = (int64) span.getProperty ("s", 0), e = (int64) span.getProperty ("e", 0);
        const var options = rig.session.getChordOptions (s, e);
        check (options.size() >= 4, "ranked choices for the chord");
        var pick;
        for (const auto& o : *options.getArray())
            if (! (bool) o.getProperty ("current", false)) { pick = o; break; }
        check (! pick.isVoid(), "something else to choose");
        if (pick.isVoid())
            return;
        Array<int> iv;
        const var pickIv = pick.getProperty ("iv", {});
        for (const auto& v : *pickIv.getArray())
            iv.add ((int) v);
        const int before = rig.session.getLaneVersion();
        rig.session.lockChord (s, e, (int) pick.getProperty ("root", 0), iv);
        check (rig.session.getLaneVersion() != before, "the lane changes");
        lockedName = pick.getProperty ("name", "").toString();
        lockedStart = s;
        bool shown = false;
        const var laneNow = rig.session.getChordLane();
        for (const auto& sp : *laneNow.getProperty ("spans", {}).getArray())
            shown = shown || ((bool) sp.getProperty ("locked", false) && sp.getProperty ("name", "").toString() == lockedName
                              && (int64) sp.getProperty ("s", 0) == s);
        check (shown, "the chosen chord is in the lane, locked");
        std::printf ("  chose %s at %.2f s\n", lockedName.toRawUTF8(), (double) s / rate);

        // MIDI: tempo, the parts, and everything shifted to the take's place in the song.
        const auto midiFiles = rig.session.getExportFiles (LearnSession::ExportItem::Midi);
        check (midiFiles.size() == 1 && File (midiFiles[0]).existsAsFile(), "MIDI written at once (no render needed)");
        if (midiFiles.size() == 1)
        {
            MidiFile midi;
            FileInputStream in { File (midiFiles[0]) };
            check (midi.readFrom (in), "the MIDI file reads back");
            std::printf ("  MIDI: %d tracks\n", midi.getNumTracks());
            check (midi.getNumTracks() == 7, "conductor, S, A, T, B, melody and chords");
            double tempo = 0.0, firstNote = 1.0e9;
            bool chordName = false;
            int channels = 0;
            for (int t = 0; t < midi.getNumTracks(); ++t)
                for (const auto* ev : *midi.getTrack (t))
                {
                    const auto& m = ev->message;
                    if (m.isTempoMetaEvent()) tempo = 60.0 / m.getTempoSecondsPerQuarterNote();
                    if (m.isNoteOn()) { firstNote = std::min (firstNote, m.getTimeStamp()); channels |= 1 << m.getChannel(); }
                    if (m.isTextMetaEvent() && m.getMetaEventType() == 1 && m.getTextFromTextMetaEvent() == lockedName) chordName = true;
                }
            check (std::abs (tempo - 120.0) < 0.01, "at the song's tempo");
            check (firstNote >= 4 * 960 - 1, "starting where the take sits in the song (bar 2)");
            check (channels == (0x3e | 0x40), "parts on channels 1-4, melody 5, chords 6");
            check (chordName, "the chosen chord is named in the chords track");
        }

        // Audio needs a render, then the files are written in the background.
        check (rig.session.getExportFiles (LearnSession::ExportItem::FullChoir).isEmpty(), "no audio before RENDER");
        rig.session.render();
        check (waitFor (rig.session, [&] { return phaseName (rig.session) == "rendered"; }), "rendered");
        check (rig.session.getStatus().getProperty ("export", "").toString() == "stale", "audio files not written yet");
        rig.session.prepareExport();
        check (waitFor (rig.session, [&] { return rig.session.getStatus().getProperty ("export", "").toString() == "ready"; }), "audio files ready");
        const auto all = rig.session.getExportFiles (LearnSession::ExportItem::AllStems);
        check (all.size() == 7, "four sections, the room, the lead on the stage and the full choir");
        bool silent = false;
        const int64_t len = all.size() == 7 ? wavLength (File (all[0]), silent, songStart) : -1;
        std::printf ("  %s: %lld samples\n", all.isEmpty() ? "-" : File (all[0]).getFileName().toRawUTF8(), (long long) len);
        check (len > songStart + (int64_t) (rate * 4.0), "a section file covers the song up to the end of the take");
        check (silent, "silent before the take, so it lines up when dropped at the song's start");
        const auto room = rig.session.getExportFiles (LearnSession::ExportItem::Room);
        const auto lead = rig.session.getExportFiles (LearnSession::ExportItem::LeadOnStage);
        check (room.size() == 1 && lead.size() == 1, "the room and your lead on the stage, a file each");
        bool leadSilent = false, roomSilent = false;
        const int64_t leadLen = lead.size() == 1 ? wavLength (File (lead[0]), leadSilent, songStart) : -1;
        const int64_t roomLen = room.size() == 1 ? wavLength (File (room[0]), roomSilent, songStart) : -1;
        bool fullSilent = false;
        const int64_t fullLen = all.size() == 7 ? wavLength (File (all[6]), fullSilent, songStart) : -1;
        std::printf ("  lead %lld samples (silent before the take: %d), room %lld (%d), full choir %lld\n",
                     (long long) leadLen, (int) leadSilent, (long long) roomLen, (int) roomSilent, (long long) fullLen);
        check (leadLen == fullLen && leadSilent, "the lead lines up with the song like the choir");
        check (roomLen == fullLen && roomSilent, "so does the room");

        // A mix change: a new full choir file; the files a DAW may already play stay as they were.
        const File firstFull (all.size() == 7 ? all[6] : String());
        rig.mixSignature = "mix 2";
        check (rig.session.getStatus().getProperty ("export", "").toString() == "stale", "the full choir is out of date after a mix change");
        check (rig.session.getExportFiles (LearnSession::ExportItem::Sopranos).isEmpty(), "the sections follow the mix too (they stand on the stage)");
        rig.session.prepareExport();
        check (waitFor (rig.session, [&] { return rig.session.getStatus().getProperty ("export", "").toString() == "ready"; }), "mixed again");
        const auto full2 = rig.session.getExportFiles (LearnSession::ExportItem::FullChoir);
        check (full2.size() == 1 && File (full2[0]) != firstFull && firstFull.existsAsFile(), "a new file, the old one kept");
        const auto sop2 = rig.session.getExportFiles (LearnSession::ExportItem::Sopranos);
        check (sop2.size() == 1 && sop2[0] != all[0] && File (all[0]).existsAsFile(), "new section files too, the old ones kept");

        MemoryOutputStream out (state, false);
        rig.session.writeState (out);
    }
    {
        Rig rig (rate);
        MemoryInputStream in (state, false);
        rig.session.readState (in);
        check (waitFor (rig.session, [&] { return phaseName (rig.session) == "learned" || phaseName (rig.session) == "rendered"; }), "learned again from the project");
        bool kept = false;
        const var restored = rig.session.getChordLane();
        if (auto* spans = restored.getProperty ("spans", {}).getArray())
            for (const auto& sp : *spans)
                kept = kept || ((bool) sp.getProperty ("locked", false) && sp.getProperty ("name", "").toString() == lockedName
                                && (int64) sp.getProperty ("s", 0) == lockedStart);
        check (kept, "the chosen chord comes back with the project");
        check ((int) rig.session.getChordLane().getProperty ("bar", 0) == 2, "and so does the take's place in the song");
        rig.session.unlockAllChords();
        bool anyLocked = false;
        const var unlocked = rig.session.getChordLane();
        if (auto* spans = unlocked.getProperty ("spans", {}).getArray())
            for (const auto& sp : *spans)
                anyLocked = anyLocked || (bool) sp.getProperty ("locked", false);
        check (! anyLocked, "unlock all: the choir chooses again");
        rig.session.cancelWork();
    }
}

// The middle of a held note's recording, as floats.
static std::vector<float> middleOf (const sc::SampleZone& z)
{
    const auto n = (size_t) z.length();
    std::vector<float> x;
    for (size_t i = n / 5; i < n - n / 5; ++i)
        x.push_back ((float) z.pcm[i] * z.scale);
    return x;
}

// How far the odd harmonics of the note's named pitch (f, 3f, 5f) stand above the level between
// its harmonics (1.5f, 2.5f...), in dB: against the typical gap and against the quietest one.
static std::pair<double, double> oddHarmonicsAboveGaps (const sc::SampleZone& z)
{
    const auto x = middleOf (z);
    int order = 10;
    while ((1 << order) < (int) x.size() * 2 && order < 17)
        ++order;
    sc::FFT fft (order);
    const int size = fft.getSize();
    std::vector<std::complex<float>> buf ((size_t) size);
    const size_t len = std::min (x.size(), (size_t) size);
    for (size_t i = 0; i < len; ++i)
        buf[i] = x[i] * (float) (0.5 - 0.5 * std::cos (2.0 * MathConstants<double>::pi * (double) i / (double) (len - 1)));
    fft.forward (buf.data());
    const double f0 = 440.0 * std::pow (2.0, (z.rootMidi - 69.0) / 12.0), binHz = z.sampleRate / size;
    auto peakDb = [&] (double hz, double width)
    {
        float m = 0.0f;
        for (int k = (int) (hz * (1.0 - width) / binHz); k <= (int) (hz * (1.0 + width) / binHz) && k < size / 2; ++k)
            m = std::max (m, std::abs (buf[(size_t) k]));
        return 20.0 * std::log10 (m + 1.0e-12);
    };
    double odd = -1.0e9;
    for (int h : { 1, 3, 5 })
        odd = std::max (odd, peakDb (f0 * h, 0.02));
    std::vector<double> gaps;
    for (double h : { 1.5, 2.5, 3.5, 4.5 })
        gaps.push_back (peakDb (f0 * h, 0.01));
    std::sort (gaps.begin(), gaps.end());
    return { odd - 0.5 * (gaps[1] + gaps[2]), odd - gaps[0] };
}

// Correlation of the note's waveform with itself half a named period later.
static double halfPeriodRepeat (const sc::SampleZone& z)
{
    const auto x = middleOf (z);
    const int lag = (int) std::lround (0.5 * z.sampleRate / (440.0 * std::pow (2.0, (z.rootMidi - 69.0) / 12.0)));
    double ab = 0.0, aa = 0.0, bb = 0.0;
    for (size_t i = 0; i + (size_t) lag < x.size(); ++i)
    {
        ab += (double) x[i] * x[i + (size_t) lag];
        aa += (double) x[i] * x[i];
        bb += (double) x[i + (size_t) lag] * x[i + (size_t) lag];
    }
    return ab / std::sqrt (aa * bb + 1.0e-12);
}

static void voicePackLoads()
{
    // The voice pack that ships with the plug-in: held notes, onsets, legato moves, the
    // breathy and belted takes and the classical voice (soft and full), all loaded, and singing
    // a legato line from them.
    std::printf ("[ voicePackLoads ]\n");
    const File dir (String (SANCTUARY_SOURCE_DIR) + "/voices/SanctuaryVoices");
    if (! dir.isDirectory())
    {
        std::printf ("  (no voice pack in this checkout)\n");
        return;
    }
    sanctuary::ChoirVoices voices;
    const auto t0 = Time::getMillisecondCounterHiRes();
    voices.loadFolder (dir);
    for (int waited = 0; waited < 120000 && (voices.isBusy() || voices.getLive() == nullptr); waited += 20)
        pump (20);
    const auto* set = voices.getLive();
    check (set != nullptr, "voice pack loaded");
    if (set == nullptr)
        return;
    std::printf ("  %s in %.1f s\n", set->describe().c_str(), (Time::getMillisecondCounterHiRes() - t0) / 1000.0);
    check (set->numNotes() > 3000, "held notes in every take");
    check (set->has (sc::Take::Breathy) && set->has (sc::Take::Belt), "breathy and belted takes");
    check (set->has (sc::Take::Vibrato) && set->has (sc::Take::Full), "the classical voice, soft and full");
    size_t onsets = 0, moves = 0;
    for (int s = 0; s < sc::NumSections; ++s)
        for (int t = 0; t < sc::SampleSet::kTakes; ++t)
            if (const auto* l = set->layerFor (s, sc::VocalType::Ah, (sc::Take) t))
            {
                onsets += l->onsets.size();
                moves += l->moves.size();
                check (l != set->layerFor (s, sc::VocalType::Ah) || t == 0, "a layer of its own per take");
            }
    std::printf ("  Ah: %zu onsets, %zu moves\n", onsets, moves);
    check (onsets > 40 && moves > 200, "onsets and moves");

    // Every held note sounds at the pitch in its name, not an octave above it. Named an octave
    // low, a note's odd harmonics (f, 3f, 5f) are only the noise between its real ones, and its
    // waveform repeats every half period. 53 notes (mostly the basses' lowest) once were, and
    // sang an octave high; this catches 52 of them and none of the 3,365 named right.
    {
        int held = 0, octaveUp = 0;
        for (int s = 0; s < sc::NumSections; ++s)
            for (int v = 0; v < sc::SampleSet::kVowels; ++v)
                for (int t = 0; t < sc::SampleSet::kTakes; ++t)
                    if (const auto* l = set->exactLayer (s, (sc::VocalType) v, (sc::Take) t))
                        for (const auto& z : l->zones)
                        {
                            ++held;
                            const auto [typical, quietest] = oddHarmonicsAboveGaps (z);
                            const double half = halfPeriodRepeat (z);
                            if (typical < 12.0 || (typical <= 31.0 && half > 0.6 && quietest < 35.0))
                            {
                                ++octaveUp;
                                std::printf ("    an octave above its name? section %d vowel %d take %d, %.2f: %.1f / %.1f dB, %.2f\n", s, v, t, z.rootMidi, typical, quietest, half);
                            }
                        }
        std::printf ("  %d held notes checked against their names: %d an octave above\n", held, octaveUp);
        check (held > 3000 && octaveUp == 0, "every held note sings the pitch in its name");
    }

    // Sing a melody (learned, so every section moves note to note) in each way of singing.
    for (auto how : { sc::Singing::Natural, sc::Singing::Breathy, sc::Singing::Belt })
        for (auto style : { sc::Style::Sacred, sc::Style::Gospel })
    {
        if (how != sc::Singing::Natural && style != sc::Style::Sacred)
            continue;
        sc::ChoirEngine engine;
        engine.prepare (48000.0, 512);
        engine.setSampleSet (set);
        sc::EngineParams p;
        p.singing = how;
        p.style = style;   // Gospel: most of the choir in their classical voice
        p.reverb = 0.0f;
        p.leadDb = -60.0f;
        engine.setParams (p);
        const auto x = sc::test::singMelody (sc::test::amazingGrace(), 48000.0, sc::Tenor);
        std::vector<float> l (x.size()), r (x.size());
        double e = 0.0;
        bool finite = true;
        for (size_t i = 0; i < x.size(); i += 512)
        {
            const int n = (int) std::min<size_t> (512, x.size() - i);
            engine.process (x.data() + i, x.data() + i, l.data() + i, r.data() + i, n);
        }
        for (float v : l) { e += (double) v * v; finite = finite && std::isfinite (v); }
        const double rmsDb = 10.0 * std::log10 (e / (double) l.size() + 1e-20);
        std::printf ("  %s, %s: %.1f dB\n", sc::getSingingNames()[(int) how], sc::getStyleNames()[(int) style], rmsDb);
        check (finite && rmsDb > -40.0 && rmsDb < -6.0, "sings from the pack");
    }

    // Typed words over MIDI, from the recorded vowels: one syllable per chord.
    {
        sc::ChoirEngine engine;
        engine.prepare (48000.0, 512);
        engine.setSampleSet (set);
        sc::EngineParams p;
        p.reverb = 0.0f;
        engine.setParams (p);
        const auto script = sc::LyricScript::make (sc::Lyrics::parse ("Kyrie eleison"));
        engine.setLyrics (&script);
        std::vector<float> in (512, 0.0f), l (512), r (512);
        double e = 0.0;
        bool finite = true;
        std::vector<int> seen;
        for (int block = 0; block < 900; ++block)
        {
            const int k = block / 60, into = block % 60;
            sc::NoteSet chord;
            if (k < 7 && into < 56)
                for (int note : { 60, 64, 67 })
                    chord.add (note + (k % 2) * 2);
            engine.setMidi (chord, 0.8f, k < 7 && into == 0 ? 1 : 0);
            engine.process (in.data(), nullptr, l.data(), r.data(), 512);
            for (float v : l) { e += (double) v * v; finite = finite && std::isfinite (v); }
            const int shown = engine.takeSnapshot().lyricIndex;
            if (shown >= 0 && (seen.empty() || seen.back() != shown)) seen.push_back (shown);
        }
        std::printf ("  Kyrie eleison from the pack: %.1f dB, %zu syllables\n", 10.0 * std::log10 (e / (900.0 * 512.0) + 1e-20), seen.size());
        check (finite && e > 0.0, "sings typed words from the pack");
        check (seen == std::vector<int> ({ 0, 1, 2, 3, 4, 5, 6 }), "a syllable per chord");
    }
}

static int passCount (const LearnSession& s)
{
    const var status = s.getStatus();
    const auto* list = status.getProperty ("passes", {}).getArray();
    return list != nullptr ? list->size() : 0;
}

static void passesMakeAMultiTakeChoir()
{
    std::printf ("More passes of the take: recorded with the song or along with the take, loaded, saved, rendered\n");
    const double rate = 48000.0;
    const int64_t songStart = 2 * 48000;
    std::vector<sc::test::SungNote> notes;
    for (int r = 0; r < 2; ++r)
        for (const auto& n : sc::test::cMajorScale (60.0f, 0.35f))
            notes.push_back (n);
    const auto take = sc::test::singMelody (notes, rate, 1);
    // The same part sung again: the notes a little longer or shorter, another voice.
    auto again = [&] (int seed)
    {
        auto other = notes;
        for (size_t k = 0; k < other.size(); ++k)
            other[k].seconds *= 1.0f + 0.05f * (float) ((((int) k + seed) % 5) - 2);
        return sc::test::singMelody (other, rate, 1, 30.0f, (uint32_t) seed);
    };

    MemoryBlock beforePass, withPass;
    {
        Rig rig (rate);
        rig.params.vocalType = sc::VocalType::Words;
        rig.session.standalone = false;
        rig.session.startRecording();
        rig.singInSong (take, songStart, 120.0, rate);
        rig.session.stopRecording();
        check (waitFor (rig.session, [&] { return phaseName (rig.session) == "learned"; }), "the take is learned in time with the song");
        {
            MemoryOutputStream out (beforePass, false);
            rig.session.writeState (out);
        }

        // Another pass, from half a second before the take started in the song.
        rig.session.togglePass();
        check ((bool) rig.session.getStatus().getProperty ("recordingPass", false), "PASS records");
        std::vector<float> pass ((size_t) (rate * 0.5), 0.0f);
        const auto sung = again (21);
        pass.insert (pass.end(), sung.begin(), sung.end());
        rig.singInSong (pass, songStart - (int64_t) (rate * 0.5), 120.0, rate);
        check (rig.session.getStatus().getProperty ("text", "").toString().contains ("RECORDING PASS 2"), "shown as the second pass");
        rig.session.togglePass();
        check ((bool) rig.session.getStatus().getProperty ("aligningPass", false), "lined up with the take in the background");
        check (waitFor (rig.session, [&] { return phaseName (rig.session) == "learned" && passCount (rig.session) == 1; }), "the pass is added");
        const String text = rig.session.getStatus().getProperty ("text", "").toString();
        std::printf ("  %s\n", text.toRawUTF8());
        check (text.contains ("2 PASSES"), "the take now has two passes");
        const var status = rig.session.getStatus();
        const auto* list = status.getProperty ("passes", {}).getArray();
        check (list != nullptr && ! list->isEmpty() && (float) (*list)[0].getProperty ("coverage", 0.0f) > 0.8f, "the pass sings what the take sings");

        // Playing the song: the pass comes with the input, sample for sample.
        LearnSession::BlockInfo block;
        block.hostPlaying = true;
        block.hasPosition = true;
        block.hostPosition = songStart + (int64_t) rate;
        const sc::ScoreSource* score = nullptr;
        const float* const* passes = nullptr;
        int numPasses = 0;
        rig.session.preparePlayback (block, 512, score, passes, numPasses);
        float peak = 0.0f;
        for (int i = 0; i < 512 && numPasses == 1; ++i)
            peak = std::max (peak, std::abs (passes[0][i]));
        check (score != nullptr && numPasses == 1 && peak > 0.01f, "the pass plays with the take");

        {
            MemoryOutputStream out (withPass, false);
            rig.session.writeState (out);
        }
        // Undo and redo adding the pass: the same take, so nothing is learned again.
        MemoryInputStream undo (beforePass, false);
        rig.session.readState (undo);
        check (phaseName (rig.session) == "learned" && passCount (rig.session) == 0, "undo takes the pass away");
        MemoryInputStream redo (withPass, false);
        rig.session.readState (redo);
        check (phaseName (rig.session) == "learned" && passCount (rig.session) == 1, "redo brings it back");

        // Rendered with the pass; taking it away puts the Words choir's render out of date.
        rig.session.render();
        check (waitFor (rig.session, [&] { return phaseName (rig.session) == "rendered"; }), "rendered with two passes");
        rig.session.removePass (0);
        check (passCount (rig.session) == 0, "the pass is removed");
        check ((bool) rig.session.getStatus().getProperty ("stale", false) && phaseName (rig.session) == "learned", "the render is out of date");
    }
    {
        // The project reopened: the pass comes back with the take.
        Rig rig (rate);
        rig.params.vocalType = sc::VocalType::Words;
        MemoryInputStream in (withPass, false);
        rig.session.readState (in);
        check (waitFor (rig.session, [&] { return phaseName (rig.session) == "learned"; }), "learned again from the project");
        check (passCount (rig.session) == 1, "with its pass");
    }
    {
        // No song: the pass is sung along with the take, which plays through the choir.
        Rig rig (rate);
        rig.params.vocalType = sc::VocalType::Words;
        rig.session.startRecording();
        rig.sing (take);
        rig.session.stopRecording();
        check (waitFor (rig.session, [&] { return phaseName (rig.session) == "learned"; }), "learned");
        rig.session.togglePass();
        check ((bool) rig.session.getStatus().getProperty ("previewing", false), "the take plays to sing along with");
        rig.sing (again (33));
        rig.session.togglePass();
        check (waitFor (rig.session, [&] { return phaseName (rig.session) == "learned" && passCount (rig.session) == 1; }), "the pass is added");
        check (! (bool) rig.session.getStatus().getProperty ("previewing", false), "and the take stops playing");

        // One more from a file, 0.8 s late and quieter.
        std::vector<float> late ((size_t) (rate * 0.8), 0.0f);
        const auto sung = again (44);
        late.insert (late.end(), sung.begin(), sung.end());
        for (auto& v : late) v *= 0.5f;
        const File file = rig.exports.getChildFile ("pass3.wav");
        rig.exports.createDirectory();
        {
            WavAudioFormat wav;
            std::unique_ptr<OutputStream> stream = std::make_unique<FileOutputStream> (file);
            auto writer = wav.createWriterFor (stream, AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (1).withBitsPerSample (24));
            const float* ch[] = { late.data() };
            check (writer != nullptr && writer->writeFromFloatArrays (ch, 1, (int) late.size()), "pass written to a file");
        }
        rig.session.loadPassFile (file);
        check (waitFor (rig.session, [&] { return phaseName (rig.session) == "learned" && passCount (rig.session) == 2; }), "a pass loaded from a file");

        // Only the first few notes: not the same part.
        const int before = (int) rig.session.getStatus().getProperty ("noticeId", 0);
        std::vector<float> part (take.begin(), take.begin() + (long) (take.size() / 4));
        part.resize (take.size(), 0.0f);
        rig.session.togglePass();
        rig.sing (part);
        rig.session.togglePass();
        check (waitFor (rig.session, [&] { return phaseName (rig.session) == "learned" && (int) rig.session.getStatus().getProperty ("noticeId", 0) > before; }),
               "a pass that leaves out most of the part is turned down");
        std::printf ("  %s\n", rig.session.getStatus().getProperty ("notice", "").toString().toRawUTF8());
        check (passCount (rig.session) == 2, "and not added");
    }
}

static void measuredRoomsLoad()
{
    // Every venue's room is in the plug-in, decodes to unit energy on each side (Stone Abbey's
    // measurement leans 4.7 dB right) with its decay as built (SOURCES.json), and follows the
    // venue: a change builds the new room in the background.
    std::printf ("[ measuredRoomsLoad ]\n");
    const auto sources = JSON::parse (File (String (SANCTUARY_SOURCE_DIR) + "/rooms/SOURCES.json"));
    for (int v = 0; v < (int) sc::Venue::Count; ++v)
    {
        const auto t0 = Time::getMillisecondCounterHiRes();
        const auto ir = sanctuary::MeasuredRooms::getImpulse ((sc::Venue) v);
        const double ms = Time::getMillisecondCounterHiRes() - t0;
        check (ir != nullptr && ir->left.size() == ir->right.size() && ! ir->left.empty(), "room decodes");
        if (ir == nullptr)
            continue;
        double e = 0.0, el = 0.0, er = 0.0;
        for (size_t i = 0; i < ir->left.size(); ++i)
        {
            el += (double) ir->left[i] * ir->left[i];
            er += (double) ir->right[i] * ir->right[i];
        }
        e = 0.5 * (el + er);
        std::vector<float> both (ir->left.size());
        for (size_t i = 0; i < both.size(); ++i)
            both[i] = ir->left[i] + ir->right[i];
        const float t30 = sc::measureDecayTime (both.data(), (int64_t) both.size(), ir->sampleRate);
        double expected = 0.0;
        if (const auto* rows = sources.getArray())
            for (const auto& r : *rows)
                if (r.getProperty ("file", {}).toString() == String (sc::getVenueSlug ((sc::Venue) v)) + ".ogg")
                    expected = (double) r.getProperty ("t30_s", 0.0);
        std::printf ("  %-16s %.1f s, energy %.3f (right %+.2f dB), decay %.2f s (built %.2f s), decoded in %.0f ms\n", sc::getVenueNames()[v],
                     (double) ir->left.size() / ir->sampleRate, e, 10.0 * std::log10 (er / el), t30, expected, ms);
        check (std::abs (e - 1.0) < 0.01, "unit energy");
        check (std::abs (10.0 * std::log10 (er / el)) < 0.05, "both sides alike");
        check (std::abs (t30 - expected) < 0.12 * expected + 0.03, "decay as built");
    }

    sc::Venue venue = sc::Venue::GrandCathedral;
    sanctuary::MeasuredRooms rooms;
    rooms.getVenue = [&venue] { return venue; };
    const auto t0 = Time::getMillisecondCounterHiRes();
    rooms.prepare (44100.0);
    const double prepMs = Time::getMillisecondCounterHiRes() - t0;
    auto* first = rooms.getLive();
    check (first != nullptr && std::abs (first->getSampleRate() - 44100.0) < 0.5, "room ready after prepare, at the session's rate");
    venue = sc::Venue::StudioRoom;
    bool switched = false;
    for (int waited = 0; waited < 10000 && ! switched; waited += 20)
    {
        pump (20);
        switched = rooms.getLive() != first && rooms.getLive() != nullptr;
    }
    check (switched, "a venue change builds the new room");
    std::printf ("  cathedral built for 44.1 kHz in %.0f ms; studio room %s\n", prepMs, switched ? "followed" : "did not follow");

    // A bounce faster than real time builds the room at once, where the change is in the song.
    venue = sc::Venue::ConcertHall;
    auto* before = rooms.getLive();
    const auto t1 = Time::getMillisecondCounterHiRes();
    rooms.buildNow (venue);
    const double nowMs = Time::getMillisecondCounterHiRes() - t1;
    auto* built = rooms.getLive();
    check (built != nullptr && built != before && std::abs (built->getSampleRate() - 44100.0) < 0.5, "a bounce gets the new room before its next block");
    rooms.buildNow (venue);
    check (rooms.getLive() == built, "and keeps it (no rebuild for the room already playing)");
    // The background build may already be under way for that venue: it must not swap in a second
    // copy afterwards (the room's tail would start again from silence).
    venue = sc::Venue::WoodenChapel;
    pump (150);
    rooms.buildNow (venue);
    auto* chapel = rooms.getLive();
    pump (1500);
    check (chapel != nullptr && rooms.getLive() == chapel, "a background build of the same room doesn't replace it");
    std::printf ("  concert hall built at once for a bounce in %.0f ms\n", nowMs);
}

//==============================================================================
/** Energy above 1.5 kHz against all of it (dB): how much of a voice's sound is more than its note. */
static double topShareDb (const std::vector<float>& x, size_t from, size_t to)
{
    sc::FFT fft (12);
    const int size = fft.getSize();
    std::vector<std::complex<float>> buf ((size_t) size);
    double top = 0.0, all = 0.0;
    for (size_t start = from; start + (size_t) size <= to && start + (size_t) size <= x.size(); start += (size_t) size / 2)
    {
        for (int i = 0; i < size; ++i)
            buf[(size_t) i] = x[start + (size_t) i] * (0.5f - 0.5f * std::cos (sc::kTwoPi * (float) i / (float) size));
        fft.forward (buf.data());
        for (int k = 1; k < size / 2; ++k)
        {
            const double e = std::norm (buf[(size_t) k]);
            all += e;
            if (k * 48000.0 / size >= 1500.0) top += e;
        }
    }
    return 10.0 * std::log10 ((top + 1e-20) / (all + 1e-20));
}

/** The take's section stems as rendered (left channel of each section), read through playback. */
static std::array<std::vector<float>, sc::NumSections> renderedSections (LearnSession& session, size_t length)
{
    std::array<std::vector<float>, sc::NumSections> out;
    for (auto& o : out) o.assign (length, 0.0f);
    session.togglePreview();   // plays the take from its start
    for (size_t pos = 0; pos < length; pos += 512)
    {
        LearnSession::BlockInfo info;
        info.hostPlaying = true;
        info.hasPosition = true;
        info.hostPosition = (int64_t) pos;
        const sc::ScoreSource* score = nullptr;
        const float* const* passes = nullptr;
        int numPasses = 0;
        const int n = (int) std::min<size_t> (512, length - pos);
        if (const auto* stems = session.preparePlayback (info, n, score, passes, numPasses))
            for (int s = 0; s < sc::NumSections; ++s)
                if (stems->sections[(size_t) s * 2] != nullptr)
                    std::copy_n (stems->sections[(size_t) s * 2], n, out[(size_t) s].begin() + (long) pos);
    }
    session.togglePreview();
    return out;
}

/** Sets (or, with an empty value, clears) an environment variable for this process. */
static void setEnv (const char* name, const String& value)
{
   #if JUCE_WINDOWS
    _putenv_s (name, value.toRawUTF8());
   #else
    if (value.isEmpty()) unsetenv (name);
    else setenv (name, value.toRawUTF8(), 1);
   #endif
}

static void aiSingersSingLikePeople()
{
    std::printf ("AI singers' parts are sung like people: vibrato on held notes, scoops, each their own\n");
    const double rate = 48000.0;
    const auto take = sc::test::singMelody ({ { 57.0f, 1.6f, 0.6f }, { 60.0f, 1.6f, 0.6f }, { 59.0f, 1.6f, 0.3f } }, rate, 1, 0.0f);
    const auto analysis = sc::analyseTake (take.data(), (int64_t) take.size(), rate);
    const auto score = sc::harmoniseTake (analysis, sc::HarmonySettings {});
    auto singer = [] (const char* id) { sanctuary::ai::Singer s; s.id = id; s.name = id; s.low = 45; s.high = 67; s.median = 56.0f; return s; };
    const auto one = singer ("tenor-one"), two = singer ("tenor-two");
    const auto len = (int64_t) take.size();
    const auto a = sanctuary::ai::partPitch (score, analysis, len, rate, sc::Tenor, 0, one, 440.0f, sc::Style::Gospel);
    const auto again = sanctuary::ai::partPitch (score, analysis, len, rate, sc::Tenor, 0, one, 440.0f, sc::Style::Gospel);
    const auto b = sanctuary::ai::partPitch (score, analysis, len, rate, sc::Tenor, 0, two, 440.0f, sc::Style::Gospel);
    const auto sacred = sanctuary::ai::partPitch (score, analysis, len, rate, sc::Tenor, 0, one, 440.0f, sc::Style::Sacred);
    check (a == again, "the same singer sings the part the same way every time (a cached voice stays right)");
    bool written = false, lowWritten = true;
    auto low = singer ("low-bass");
    low.low = 28; low.high = 45; low.median = 36.0f;
    sanctuary::ai::partPitch (score, analysis, len, rate, sc::Tenor, 0, one, 440.0f, sc::Style::Gospel, &written);
    sanctuary::ai::partPitch (score, analysis, len, rate, sc::Tenor, 0, low, 440.0f, sc::Style::Gospel, &lowWritten);
    check (written && ! lowWritten, "a part sung at its own pitch says so; one moved an octave down to suit a low voice says it was moved");

    // Each held note: its first frames (the scoop) and its last 0.7 s (vibrato), in cents from the note.
    auto notesOf = [] (const std::vector<float>& f0)
    {
        std::vector<std::pair<int, int>> runs;
        for (int i = 0; i < (int) f0.size();)
        {
            if (f0[(size_t) i] <= 0.0f) { ++i; continue; }
            int j = i;
            while (j < (int) f0.size() && f0[(size_t) j] > 0.0f) ++j;
            if (j - i > 100) runs.push_back ({ i, j });
            i = j;
        }
        return runs;
    };
    auto centsFrom = [] (float hz, float note) { return 1200.0f * std::log2 (hz / (440.0f * std::exp2 ((note - 69.0f) / 12.0f))); };
    const auto runs = notesOf (a);
    check (runs.size() == 3, "three held notes");
    double worstMean = 0.0, minSpread = 1.0e9, firstA = 0.0, firstSacred = 0.0, apart = 0.0;
    int compared = 0;
    for (const auto& [s, e] : runs)
    {
        std::vector<float> tail;
        for (int i = e - 70; i < e; ++i)
            tail.push_back (a[(size_t) i]);
        std::vector<float> sorted = tail;
        std::sort (sorted.begin(), sorted.end());
        const float note = std::round (12.0f * std::log2 (sorted[sorted.size() / 2] / 440.0f) + 69.0f);
        double mean = 0.0, sq = 0.0;
        for (float hz : tail) { const double c = centsFrom (hz, note); mean += c; sq += c * c; }
        mean /= (double) tail.size();
        const double spread = std::sqrt (sq / (double) tail.size() - mean * mean);
        worstMean = std::max (worstMean, std::abs (mean));
        minSpread = std::min (minSpread, spread);
        firstA += centsFrom (a[(size_t) s], note) / 3.0;
        firstSacred += sacred[(size_t) s] > 0.0f ? centsFrom (sacred[(size_t) s], note) / 3.0 : 0.0;
        for (int i = s; i < e; ++i)
            if (b[(size_t) i] > 0.0f) { apart += std::abs (centsFrom (a[(size_t) i], note) - centsFrom (b[(size_t) i], note)); ++compared; }
    }
    apart /= std::max (1, compared);
    std::printf ("  held notes: vibrato %.0f cents rms, centred within %.1f cents; scoop in %.0f cents (Sacred %.0f); two singers %.0f cents apart\n",
                 minSpread, worstMean, firstA, firstSacred, apart);
    check (minSpread > 6.0 && minSpread < 35.0 && worstMean < 10.0, "held notes have vibrato, centred on the note");
    check (firstA < -15.0 && std::abs (firstSacred) < 8.0, "a Gospel singer scoops into a note after a rest, a Sacred one doesn't");
    check (apart > 3.0, "and no two singers sing it alike");
}

static void aiSingersSkipTheSilences()
{
    std::printf ("AI singers sing only where the part is sung, and the same as when the whole take is sung\n");
    const auto ortLib = SystemStats::getEnvironmentVariable ("SANCTUARY_TEST_ORT_LIB", {});
    const auto modelsDir = File (SystemStats::getEnvironmentVariable ("SANCTUARY_TEST_MODELS_DIR", "/nonexistent"));
    if (ortLib.isEmpty() || ! modelsDir.getChildFile ("generator_40k.onnx").existsAsFile())
    {
        std::printf ("  skipped: set SANCTUARY_TEST_ORT_LIB and SANCTUARY_TEST_MODELS_DIR\n");
        return;
    }
    const auto root = File::getSpecialLocation (File::tempDirectory).getNonexistentChildFile ("SanctuaryAiGaps", {}, false);
    const auto pack = root.getChildFile ("VoxaVersa").getChildFile ("Voices").getChildFile ("band-male-1");
    pack.createDirectory();
    root.getChildFile ("VoxaVersa").getChildFile ("Encoders").createDirectory();
    modelsDir.getChildFile ("encoder_768.onnx").copyFileTo (root.getChildFile ("VoxaVersa").getChildFile ("Encoders").getChildFile ("contentvec-768.onnx"));
    modelsDir.getChildFile ("generator_40k.onnx").copyFileTo (pack.getChildFile ("model_e100.onnx"));
    pack.getChildFile ("voice.json").replaceWithText ("{ \"id\": \"band-male-1\", \"name\": \"band-male-1\", \"gender\": \"male\", \"range\": [45, 66], "
                                                      "\"model\": { \"file\": \"model_e100.onnx\", \"encoder\": \"contentvec-768\", \"sampleRate\": 40000 } }");
    setEnv ("SANCTUARY_VOXAVERSA_DIR", root.getChildFile ("VoxaVersa").getFullPathName());
    setEnv ("SANCTUARY_ONNXRUNTIME_PATH", ortLib);
    setEnv ("SANCTUARY_AI_CACHE_DIR", root.getChildFile ("cache").getFullPathName());
    setEnv ("SANCTUARY_AI_DIR", root.getChildFile ("Sanctuary").getFullPathName());

    // Two lines with 3.5 s of silence between them; the second starts on an odd 10 ms frame.
    const double rate = 48000.0;
    auto take = melody (rate, 0.0f, 1);
    const size_t gapStart = take.size();
    take.resize (take.size() + (size_t) (3.5 * rate) + 480, 0.0f);
    const size_t gapEnd = take.size();
    const auto second = melody (rate, 2.0f, 1);
    take.insert (take.end(), second.begin(), second.end());
    const auto analysis = sc::analyseTake (take.data(), (int64_t) take.size(), rate);
    sc::HarmonySettings hs;
    hs.wordsMode = true;
    hs.style = sc::Style::Gospel;
    const auto score = sc::harmoniseTake (analysis, hs);
    const auto singers = sanctuary::ai::findSingers();
    check (singers.size() == 1, "the singer is found");
    if (singers.size() != 1)
        return;
    // Four parts, sung two at a time.
    sanctuary::ai::Cast cast;
    for (int s = 0; s < sc::NumSections; ++s)
        cast[(size_t) s].add ("band-male-1");
    sanctuary::ai::Renderer renderer;
    renderer.setSingersAtOnce (2);
    sanctuary::ai::Renderer::Result result;
    check (renderer.render (take, rate, analysis, score, cast, singers, 440.0f, result, {}, nullptr) && result.sung == 4,
           "four parts sung, two at a time");

    String error;
    const auto model = sanctuary::ai::NeuralVoiceModel::load (singers[0].folder, error);
    for (int s = 0; s < sc::NumSections && model != nullptr; ++s)
    {
        if (result.voices[(size_t) s].empty())
            continue;
        const auto& v = result.voices[(size_t) s][0];
        // The same part sung over the whole take in one go.
        const auto f0 = sanctuary::ai::partPitch (score, analysis, (int64_t) take.size(), rate, s, 0, singers[0]);
        AudioBuffer<float> input (1, (int) take.size()), whole;
        input.copyFrom (0, 0, take.data(), (int) take.size());
        check (model->convert (input, rate, f0, 100.0, whole, nullptr, error), "the whole take sung in one go");

        const int hop = (int) (0.04 * rate);
        double worst = 0.0;
        int compared = 0;
        for (size_t i = 0; i + (size_t) hop <= take.size(); i += (size_t) hop)
        {
            double a = 0.0, b = 0.0;
            for (int k = 0; k < hop; ++k)
            {
                a += std::pow ((double) (*v.pcm)[i + (size_t) k] * v.scale, 2.0);
                b += std::pow ((double) whole.getSample (0, (int) i + k), 2.0);
            }
            if (b / hop < 1.0e-5)
                continue;   // only where it sings
            worst = std::max (worst, std::abs (10.0 * std::log10 ((a + 1.0e-12) / (b + 1.0e-12))));
            ++compared;
        }
        float inGap = 0.0f;
        for (size_t i = gapStart + (size_t) rate; i < gapEnd - (size_t) rate; ++i)
            inGap = std::max (inGap, std::abs ((float) (*v.pcm)[i] * v.scale));
        std::printf ("  section %d: %d stretches of 40 ms compared, at most %.2f dB apart; the silence peaks at %g\n",
                     s, compared, worst, (double) inGap);
        check (compared > 50 && worst < 0.5, "where it sings, it sings as the whole take does");
        check (inGap == 0.0f, "and the silence between the lines is silent");
    }

    setEnv ("SANCTUARY_VOXAVERSA_DIR", {});
    setEnv ("SANCTUARY_AI_CACHE_DIR", {});
    setEnv ("SANCTUARY_AI_DIR", {});
    root.deleteRecursively();
}

static void aiSingersShareAndInstall()
{
    std::printf ("AI singers: a pack of the singers that may be shared, installed on a computer without VoxaVersa\n");
    const auto root = File::getSpecialLocation (File::tempDirectory).getNonexistentChildFile ("SanctuaryAiShare", {}, false);
    const auto voxa = root.getChildFile ("VoxaVersa");
    const auto models = File (SystemStats::getEnvironmentVariable ("SANCTUARY_TEST_MODELS_DIR", "/nonexistent"));
    const bool realModels = models.getChildFile ("generator_40k.onnx").existsAsFile()
                            && SystemStats::getEnvironmentVariable ("SANCTUARY_TEST_ORT_LIB", {}).isNotEmpty();
    struct Pack { const char* id; const char* gender; const char* licence; };
    for (const Pack& p : { Pack { "vocalset-female1", "female", "VocalSet: A Singing Voice Dataset, J. Wilkins et al., ISMIR 2018, CC BY 4.0" },
                           Pack { "studio-female-a", "female", "Trained from licence-free studio vocals in the owner's library" },
                           Pack { "band-male-1", "male", "Public domain recordings: works of US military band members made as part of their duties" },
                           Pack { "gtsinger-en-alto-1", "female", "CC BY-NC-SA 4.0 - GTSinger (Zhang et al., NeurIPS 2024): personal, non-commercial use only" },
                           Pack { "ccby-nd-tenor", "male", "CC BY-ND 4.0 a cappella" } })
    {
        const auto dir = voxa.getChildFile ("Voices").getChildFile (p.id);
        dir.createDirectory();
        if (realModels)
            models.getChildFile ("generator_40k.onnx").copyFileTo (dir.getChildFile ("model_e100.onnx"));
        else
            dir.getChildFile ("model_e100.onnx").replaceWithText ("a model");
        dir.getChildFile ("voice.json").replaceWithText (String ("{ \"id\": \"") + p.id + "\", \"name\": \"" + p.id + "\", \"gender\": \"" + p.gender
            + "\", \"license\": \"" + p.licence + "\", \"model\": { \"file\": \"model_e100.onnx\", \"encoder\": \"contentvec-768\", \"sampleRate\": 40000 } }");
    }
    voxa.getChildFile ("Voices").getChildFile ("voice_assignment.json").replaceWithText (
        "{ \"packs\": { \"vocalset-female1\": { \"name\": \"VocalSet Female 1\", \"range\": [60, 78], \"median\": 68 } } }");
    voxa.getChildFile ("Encoders").createDirectory();
    if (realModels)
        models.getChildFile ("encoder_768.onnx").copyFileTo (voxa.getChildFile ("Encoders").getChildFile ("contentvec-768.onnx"));
    else
        voxa.getChildFile ("Encoders").getChildFile ("contentvec-768.onnx").replaceWithText ("an encoder");

    // Your computer: VoxaVersa's singers.
    setEnv ("SANCTUARY_VOXAVERSA_DIR", voxa.getFullPathName());
    setEnv ("SANCTUARY_AI_DIR", root.getChildFile ("mine").getFullPathName());
    const auto mine = sanctuary::ai::findSingers();
    int shareable = 0;
    for (const auto& s : mine)
        shareable += s.shareable ? 1 : 0;
    check (mine.size() == 5 && shareable == 2, "of the five singers the VocalSet and the GTSinger ones may be shared");
    using sanctuary::ai::ShareTerms;
    using sanctuary::ai::shareTerms;
    check (shareTerms ("CC BY 4.0") == ShareTerms::free && shareTerms ("CC0 1.0") == ShareTerms::free
           && shareTerms ("CC BY-NC 4.0") == ShareTerms::nonCommercial && shareTerms ("cc-by-nc-sa") == ShareTerms::nonCommercial
           && shareTerms ("CC BY-NC-SA 4.0 - GTSinger (Zhang et al., NeurIPS 2024): personal, non-commercial use only") == ShareTerms::nonCommercial
           && shareTerms ("CC BY-ND 4.0") == ShareTerms::never && shareTerms ("personal use only") == ShareTerms::never
           && shareTerms ("non-commercial, testing only") == ShareTerms::never,
           "Creative Commons voices may be shared (NonCommercial ones for non-commercial use), NoDerivatives and private ones never");
    const auto zipFile = root.getChildFile ("Sanctuary AI Singers.zip");
    String error;
    const int packed = sanctuary::ai::makeSharePack (mine, zipFile, {}, nullptr, error);
    check (packed == 2, "the pack holds two singers");
    {
        ZipFile zip (zipFile);
        bool studio = false, band = false, noDerivatives = false;
        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            const auto name = zip.getEntry (i)->filename;
            studio = studio || name.contains ("studio");
            band = band || name.contains ("band");
            noDerivatives = noDerivatives || name.contains ("ccby-nd");
        }
        check (! studio && ! band && ! noDerivatives, "and never a sample-pack, band or no-derivatives singer");
        String credits;
        if (auto* entry = zip.getEntry ("CREDITS.txt"))
            if (std::unique_ptr<InputStream> in { zip.createStreamForEntry (*entry) })
                credits = in->readEntireStreamAsString();
        check (zip.getEntry ("Voices/gtsinger-en-alto-1/voice.json") != nullptr && credits.contains ("NON-COMMERCIAL USE ONLY: gtsinger-en-alto-1")
               && credits.contains ("GTSinger: A Global Multi-Technique Singing Corpus") && credits.contains ("CC BY-NC-SA 4.0"),
               "the GTSinger singer goes in, marked non-commercial, with GTSinger's credit and licence");
        check (zip.getEntry ("Voices/vocalset-female1/voice.json") != nullptr && zip.getEntry ("Encoders/contentvec-768.onnx") != nullptr
               && zip.getEntry ("CREDITS.txt") != nullptr && zip.getEntry ("Voices/voice_assignment.json") != nullptr,
               "with the content encoder, the singer's measured range and the credits");
    }

    // A friend's computer: no VoxaVersa.
    setEnv ("SANCTUARY_VOXAVERSA_DIR", root.getChildFile ("no VoxaVersa here").getFullPathName());
    setEnv ("SANCTUARY_AI_DIR", root.getChildFile ("friend").getFullPathName());
    check (sanctuary::ai::findSingers().empty(), "a friend without VoxaVersa has no singers at first");
    const auto installed = sanctuary::ai::installVoices (zipFile, error);
    const auto theirs = sanctuary::ai::findSingers();
    const sanctuary::ai::Singer* lyric = nullptr;
    const sanctuary::ai::Singer* gts = nullptr;
    for (const auto& s : theirs)
    {
        if (s.name == "Lyric Soprano") lyric = &s;
        if (s.id == "gtsinger-en-alto-1") gts = &s;
    }
    check (installed.size() == 2 && theirs.size() == 2 && lyric != nullptr && lyric->low == 60 && lyric->median == 68.0f,
           "INSTALL VOICES puts the singers in Sanctuary's own folder, with their names and ranges");
    check (gts != nullptr && gts->shareable && gts->nonCommercial && lyric != nullptr && ! lyric->nonCommercial,
           "and the friend's Sanctuary knows which one is for non-commercial use");
    check (sanctuary::ai::getSanctuaryEncodersDirectory().getChildFile ("contentvec-768.onnx").existsAsFile(), "and the content encoder");
    if (realModels)
    {
        setEnv ("SANCTUARY_ONNXRUNTIME_PATH", SystemStats::getEnvironmentVariable ("SANCTUARY_TEST_ORT_LIB", {}));
        String loadError;
        const auto model = sanctuary::ai::NeuralVoiceModel::load (lyric != nullptr ? lyric->folder : File(), loadError);
        check (model != nullptr, "and the singer loads from there, encoder and all");
        if (model == nullptr)
            std::printf ("  %s\n", loadError.toRawUTF8());
    }

    // A single VoxaVersa .vxpack installs too.
    const auto vxpack = root.getChildFile ("one.vxpack");
    {
        ZipFile::Builder b;
        b.addFile (voxa.getChildFile ("Voices").getChildFile ("band-male-1").getChildFile ("voice.json"), 0, "voice.json");
        b.addFile (voxa.getChildFile ("Voices").getChildFile ("band-male-1").getChildFile ("model_e100.onnx"), 0, "model_e100.onnx");
        FileOutputStream out (vxpack);
        b.writeToStream (out, nullptr);
    }
    const auto one = sanctuary::ai::installVoices (vxpack, error);
    check (one.size() == 1 && sanctuary::ai::findSingers().size() == 3, "a VoxaVersa .vxpack installs as one more singer");

    setEnv ("SANCTUARY_VOXAVERSA_DIR", {});
    setEnv ("SANCTUARY_AI_DIR", {});
    root.deleteRecursively();
}

static void aiSingersSingTheParts()
{
    std::printf ("AI singers: VoxaVersa's singers sing each section's part with the take's words\n");
    // Stand-in models (tools/ai/make_dummy_models.py): a singer sings a pure tone at the pitch it is given,
    // as loud as the take's sound, so the choir's pitch and purity show what the AI singers did.
    const auto ortLib = SystemStats::getEnvironmentVariable ("SANCTUARY_TEST_ORT_LIB", {});
    const auto modelsDir = File (SystemStats::getEnvironmentVariable ("SANCTUARY_TEST_MODELS_DIR", "/nonexistent"));
    if (ortLib.isEmpty() || ! modelsDir.getChildFile ("generator_40k.onnx").existsAsFile())
    {
        std::printf ("  skipped: set SANCTUARY_TEST_ORT_LIB and SANCTUARY_TEST_MODELS_DIR\n");
        return;
    }
    const auto root = File::getSpecialLocation (File::tempDirectory).getNonexistentChildFile ("SanctuaryAiTest", {}, false);
    const auto voicesDir = root.getChildFile ("VoxaVersa").getChildFile ("Voices");
    root.getChildFile ("VoxaVersa").getChildFile ("Encoders").createDirectory();
    modelsDir.getChildFile ("encoder_768.onnx").copyFileTo (root.getChildFile ("VoxaVersa").getChildFile ("Encoders").getChildFile ("contentvec-768.onnx"));
    struct Pack { const char* id; const char* gender; int low, high; };
    for (const Pack& p : { Pack { "band-female-1", "female", 57, 77 }, Pack { "band-female-2", "female", 53, 72 },
                           Pack { "band-male-1", "male", 45, 66 }, Pack { "vocalset-male8", "male", 40, 62 } })
    {
        const auto dir = voicesDir.getChildFile (p.id);
        dir.createDirectory();
        modelsDir.getChildFile ("generator_40k.onnx").copyFileTo (dir.getChildFile ("model_e100.onnx"));
        dir.getChildFile ("voice.json").replaceWithText (String ("{ \"id\": \"") + p.id + "\", \"name\": \"" + p.id + "\", \"gender\": \"" + p.gender
            + "\", \"range\": [" + String (p.low) + ", " + String (p.high) + "], \"model\": { \"file\": \"model_e100.onnx\", \"encoder\": \"contentvec-768\", \"sampleRate\": 40000 } }");
    }
    voicesDir.getChildFile ("voice_assignment.json").replaceWithText ("{ \"packs\": { \"vocalset-male8\": { \"name\": \"VocalSet Male 8\", \"median\": 50 } } }");
    const auto cacheDir = root.getChildFile ("cache");
    setEnv ("SANCTUARY_VOXAVERSA_DIR", root.getChildFile ("VoxaVersa").getFullPathName());
    setEnv ("SANCTUARY_ONNXRUNTIME_PATH", ortLib);
    setEnv ("SANCTUARY_AI_CACHE_DIR", cacheDir.getFullPathName());
    setEnv ("SANCTUARY_AI_DIR", root.getChildFile ("Sanctuary").getFullPathName());   // nothing installed into Sanctuary

    const auto singers = sanctuary::ai::findSingers();
    check (singers.size() == 4, "the four singers are found");
    bool named = false;
    for (const auto& s : singers)
        named = named || (s.name == "Basso Profundo" && s.median == 50.0f && s.classical);
    check (named, "with VoxaVersa's names and the assignment's ranges");
    const auto cast = sanctuary::ai::completeCast (singers, {}, 1, sc::Style::Gospel, sc::defaultSectionRanges());
    check (cast[0].size() == 1 && cast[0][0].startsWith ("band-female") && cast[2][0] == "band-male-1" && cast[3][0] == "vocalset-male8",
           "each section gets a singer of its own voice (women above, men below; the bass the deepest)");

    const double rate = 48000.0;
    const auto take = melody (rate, 0.0f, 2);
    Rig rig (rate);
    rig.params.vocalType = sc::VocalType::Words;
    rig.params.harmonyMode = sc::HarmonyMode::FullChoir;
    rig.params.style = sc::Style::Gospel;
    bool aiOn = false;
    rig.session.getAiPlan = [&] { sanctuary::ai::Plan p; p.enabled = aiOn; p.perSection = 1; return p; };
    rig.session.startRecording();
    rig.sing (take);
    rig.session.stopRecording();
    check (waitFor (rig.session, [&] { return phaseName (rig.session) == "learned"; }), "the take is learned");

    rig.session.render();
    check (waitFor (rig.session, [&] { return phaseName (rig.session) == "rendered"; }, 120000), "rendered with the take's own voice");
    const auto own = renderedSections (rig.session, take.size());

    aiOn = true;
    const auto t0 = Time::getMillisecondCounterHiRes();
    rig.session.render();
    check (waitFor (rig.session, [&] { return phaseName (rig.session) == "rendered"; }, 300000), "rendered with the AI singers");
    const double firstSeconds = (Time::getMillisecondCounterHiRes() - t0) / 1000.0;
    const String text = rig.session.getStatus().getProperty ("text", "").toString();
    std::printf ("  %s (%.1f s)\n", text.toRawUTF8(), firstSeconds);
    check (text.contains ("4 AI SINGERS"), "the status says four AI singers sang");
    const auto ai = renderedSections (rig.session, take.size());

    const size_t from = (size_t) (0.5 * rate), to = take.size() - (size_t) (0.5 * rate);
    float previous = 200.0f;
    bool ordered = true, purer = true;
    for (int s = 0; s < sc::NumSections; ++s)
    {
        const double ownTop = topShareDb (own[(size_t) s], from, to), aiTop = topShareDb (ai[(size_t) s], from, to);
        // The section's pitch: the median of a pitch track of its stem.
        auto frames = sc::PitchTracker::analyse (ai[(size_t) s].data() + from, (int64_t) (to - from), rate);
        std::vector<float> m;
        for (const auto& f : frames)
            if (f.voiced)
                m.push_back (f.midi);
        std::sort (m.begin(), m.end());
        const float median = m.empty() ? 0.0f : m[m.size() / 2];
        std::printf ("  section %d: above 1.5 kHz %.1f dB with the take's voice, %.1f dB with the AI singer; median pitch %.1f\n",
                     s, ownTop, aiTop, median);
        purer = purer && aiTop < ownTop - 10.0;
        ordered = ordered && median > 0.0f && median < previous;
        previous = median;
    }
    check (purer, "every section sings from its AI singer, not the take (the stand-in singers sing pure tones)");
    check (ordered, "and the sections stay in order, sopranos on top");

    // The same again: every voice comes from the cache.
    const auto cached = cacheDir.findChildFiles (File::findFiles, false, "*.wav").size();
    check (cached == 4, "the four voices are kept on disk");
    rig.mixSignature = "mix 2";
    rig.session.renderSettingsChanged();
    const auto t1 = Time::getMillisecondCounterHiRes();
    rig.session.render();
    check (waitFor (rig.session, [&] { return phaseName (rig.session) == "rendered"; }, 120000), "rendered again");
    const double againSeconds = (Time::getMillisecondCounterHiRes() - t1) / 1000.0;
    std::printf ("  again from the cache: %.1f s\n", againSeconds);
    check (cacheDir.findChildFiles (File::findFiles, false, "*.wav").size() == 4, "nothing sung twice");

    setEnv ("SANCTUARY_VOXAVERSA_DIR", {});
    setEnv ("SANCTUARY_AI_CACHE_DIR", {});
    setEnv ("SANCTUARY_AI_DIR", {});
    root.deleteRecursively();
}

#if JUCE_WINDOWS
// A crash prints where it happened (named when the build's .pdb is next to the program).
static void printCrash (void*)
{
    std::printf ("\n*** crashed:\n%s\n", SystemStats::getStackBacktrace().toRawUTF8());
}
#endif

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);   // a crash on CI still shows which test it was in
   #if JUCE_WINDOWS
    SystemStats::setApplicationCrashHandler (printCrash);
   #endif
    ScopedJuceInitialiser_GUI juce;
    // --repeat N: the rooms and the voice pack, N times over (a crash that only comes now and then).
    if (argc > 2 && String (argv[1]) == "--repeat")
    {
        for (int i = 0, n = String (argv[2]).getIntValue(); i < n; ++i)
        {
            std::printf ("--- round %d of %d\n", i + 1, n);
            measuredRoomsLoad();
            voicePackLoads();
        }
        std::printf ("\n%d checks, %d failures\n", checks, failures);
        return failures == 0 ? 0 : 1;
    }
    measuredRoomsLoad();
    voicePackLoads();
    renderPressedWhileLearning();
    settingsChangedWhileRendering();
    projectReopenedAtAnotherRate();
    reloadWaitsForVoicesAndSkipsSameTake();
    learnPressedWhileTheSongPlays();
    learnRecordsTheBand();
    chordLaneAndDragOut();
    passesMakeAMultiTakeChoir();
    aiSingersSingTheParts();
    aiSingersSingLikePeople();
    aiSingersSkipTheSilences();
    aiSingersShareAndInstall();
    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
