// Sanctuary Choir - tests for the plug-in's LEARN / RENDER session (message-thread logic).
//
// Drives a LearnSession the way the processor does: audio blocks in, button presses and
// setting changes on the message thread, with the session's timer running in between.
#include "../Source/ChoirVoices.h"
#include "../Source/LearnSession.h"
#include "../Source/MeasuredRooms.h"
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
    learnRecordsTheBand();
    chordLaneAndDragOut();
    passesMakeAMultiTakeChoir();
    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
