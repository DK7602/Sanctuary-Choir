// Sanctuary Choir DSP tests (no framework dependency: a tiny CHECK harness).
#include "TestSignals.h"

#include <sanctuary/ChoirEngine.h>
#include <sanctuary/ChordDetector.h>
#include <sanctuary/FFT.h>
#include <sanctuary/Learn.h>

#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <complex>
#include <cstdio>
#include <functional>
#include <limits>
#include <map>

using namespace sc;

static int failures = 0, checks = 0;
#define CHECK(cond) do { ++checks; if (! (cond)) { ++failures; std::printf ("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_NEAR(a, b, tol) do { ++checks; const double va = (a), vb = (b); if (std::abs (va - vb) > (tol)) { ++failures; std::printf ("  FAIL %s:%d: %s = %g, expected %g (+/- %g)\n", __FILE__, __LINE__, #a, va, vb, (double) (tol)); } } while (0)

static std::vector<std::pair<const char*, std::function<void()>>>& registry()
{
    static std::vector<std::pair<const char*, std::function<void()>>> r;
    return r;
}
struct Reg { Reg (const char* n, std::function<void()> f) { registry().push_back ({ n, std::move (f) }); } };
#define TEST(name) static void name(); static Reg reg_##name (#name, name); static void name()

static const char* outDir()
{
    const char* d = std::getenv ("SANCTUARY_TEST_OUT");
    return d != nullptr ? d : nullptr;
}

static void maybeWrite (const std::string& name, const std::vector<float>& l, const std::vector<float>& r, double sr)
{
    if (outDir() == nullptr) return;
    test::Audio a;
    a.sampleRate = sr;
    a.channels = { l, r };
    test::writeWav (std::string (outDir()) + "/" + name + ".wav", a);
}

/** Median pitch (MIDI) of the voiced frames between two times. */
static float medianPitch (const std::vector<float>& x, double sr, double t0, double t1)
{
    const auto frames = PitchTracker::analyse (x.data(), (int64_t) x.size(), sr);
    std::vector<float> v;
    for (const auto& f : frames)
        if (f.voiced && f.centre >= t0 * sr && f.centre <= t1 * sr)
            v.push_back (f.midi);
    if (v.empty()) return -1.0f;
    std::sort (v.begin(), v.end());
    return v[v.size() / 2];
}

//==============================================================================
TEST (fftRoundTrip)
{
    FFT fft (10);
    std::vector<std::complex<float>> x (1024);
    for (int i = 0; i < 1024; ++i) x[(size_t) i] = { std::sin (0.1f * (float) i) + 0.3f * std::cos (0.37f * (float) i), 0.0f };
    auto y = x;
    fft.forward (y.data());
    fft.inverse (y.data());
    float err = 0;
    for (int i = 0; i < 1024; ++i) err = std::max (err, std::abs (y[(size_t) i] - x[(size_t) i]));
    CHECK (err < 1e-4f);
}

TEST (pitchTrackerAccuracy)
{
    const double sr = 48000.0;
    for (float midi : { 40.0f, 45.0f, 52.0f, 57.0f, 64.0f, 69.0f, 76.0f, 81.0f })
    {
        const int section = midi < 50 ? Bass : midi < 60 ? Tenor : midi < 70 ? Alto : Soprano;
        auto x = test::singMelody ({ { midi, 1.0f, 0.2f } }, sr, section, 0.0f);
        const float m = medianPitch (x, sr, 0.3, 1.0);
        CHECK_NEAR (m, midi, 0.12);
    }
}

TEST (theoryBasics)
{
    Scale c { 0, ScaleType::Major };
    CHECK (c.quantise (61.4f) == 62 || c.quantise (61.4f) == 60);
    CHECK (c.quantise (64.3f) == 64);
    CHECK (c.stepDiatonic (64, -2) == 60);   // E4 down a third = C4
    CHECK (c.stepDiatonic (60, 2) == 64);
    CHECK (c.stepDiatonic (72, -5) == 64);   // C5 down a sixth = E4
    const auto vocab = buildChordVocabulary (c, Style::Sacred);
    std::map<std::string, bool> names;
    for (const auto& ch : vocab) names[ch.name] = true;
    CHECK (names.count ("C") && names.count ("Dm") && names.count ("Em") && names.count ("F") && names.count ("G") && names.count ("Am"));

    VoicingRequest req;
    req.scale = c;
    req.chord = vocab[0]; // C
    req.melody = 64;      // E4
    req.sopranoOctaveShift = chooseOctaveShift (64, req.ranges[Soprano]);
    const Voicing v = voiceChord (req);
    CHECK (v.valid);
    for (int s = 0; s < NumSections; ++s)
    {
        CHECK (v.primary (s) >= req.ranges[(size_t) s].low - 2 && v.primary (s) <= req.ranges[(size_t) s].high + 2);
        CHECK (req.chord.contains (v.primary (s)));
    }
    CHECK (v.primary (Soprano) >= v.primary (Alto));
    CHECK (v.primary (Alto) >= v.primary (Tenor));
    CHECK (v.primary (Tenor) > v.primary (Bass));
    CHECK (wrapPc (v.primary (Bass)) == 0); // root position
}

TEST (keyEstimation)
{
    std::array<float, 12> h {};
    for (int pc : { 0, 2, 4, 5, 7, 9, 11 }) h[(size_t) pc] = 1.0f;
    h[0] = 3.0f; h[7] = 2.0f; h[4] = 1.5f;
    auto k = estimateKey (h, false, true);
    CHECK (k.tonic == 0 && ! k.minor);

    std::array<float, 12> m {};
    for (int pc : { 9, 11, 0, 2, 4, 5, 7 }) m[(size_t) pc] = 1.0f;
    m[9] = 3.0f; m[4] = 2.0f; m[0] = 1.5f;
    k = estimateKey (m, true, false);
    CHECK (k.tonic == 9 && k.minor);
}

TEST (keyNeverLeavesOutSungNotes)
{
    // A melody that sits on G and F with some Eb and D (it was heard in a real session):
    // G major or C major would snap the F or the Eb to a neighbour. The key must contain all three.
    std::array<float, 12> h {};
    h[7] = 0.37f; h[5] = 0.28f; h[3] = 0.12f; h[2] = 0.10f; h[6] = 0.05f; h[8] = 0.04f;
    const auto k = estimateKey (h, false, true);
    const Scale s { k.tonic, ScaleType::Major };
    std::printf ("  estimated %s major\n", getPitchClassName (k.tonic, true));
    CHECK (s.contains (7) && s.contains (5) && s.contains (3));
}

static PitchFrame sung (float midi)
{
    PitchFrame f;
    f.voiced = true;
    f.midi = midi;
    f.hz = midiToHz (midi);
    f.clarity = 0.95f;
    f.rmsDb = -20.0f;
    return f;
}

TEST (harmonyHoldsThroughPassingNotes)
{
    // Eb major, Full Choir: G (long) - F (quick) - G should stay on the Eb chord with only the
    // melody moving; a held F changes the chord.
    HarmonyContext ctx;
    ctx.scale = { 3, ScaleType::Major };
    LiveHarmonizer h;
    h.reset();
    const float dt = 0.005f;
    auto sing = [&] (float midi, float seconds) { for (float t = 0; t < seconds; t += dt) h.process (sung (midi), ctx, dt); };
    sing (55.0f, 0.5f);
    const std::string first = h.getChord().name;
    CHECK (first == "Eb");
    int changes = 0;
    std::string last = first;
    for (int rep = 0; rep < 4; ++rep)
    {
        for (float t = 0; t < 0.12f; t += dt) { h.process (sung (53.0f), ctx, dt); if (h.getChord().name != last) { ++changes; last = h.getChord().name; } }
        CHECK (h.getVoicing().primary (Soprano) % 12 == 5);   // the melody voice moves to F
        for (float t = 0; t < 0.4f; t += dt) { h.process (sung (55.0f), ctx, dt); if (h.getChord().name != last) { ++changes; last = h.getChord().name; } }
    }
    std::printf ("  chord changes over four quick passing notes: %d\n", changes);
    CHECK (changes == 0);
    sing (53.0f, 0.6f);
    CHECK (h.getChord().contains (5));
    CHECK (h.getChord().name != "Eb");
}

TEST (chordSpeedControlsHarmonicRhythm)
{
    // The same melody (G, quick F, G, quick Bb-ish leap) changes chord more often as Chord Speed rises.
    auto changesAt = [] (float speed)
    {
        HarmonyContext ctx;
        ctx.scale = { 3, ScaleType::Major };
        ctx.speed = speed;
        LiveHarmonizer h;
        h.reset();
        const float dt = 0.005f;
        int changes = 0;
        std::string last;
        const float melody[] = { 55, 53, 55, 53, 51, 53, 55, 58, 55, 53 };
        for (int rep = 0; rep < 3; ++rep)
            for (float m : melody)
                for (float t = 0; t < 0.16f; t += dt)
                {
                    h.process (sung (m), ctx, dt);
                    if (h.getChord().name != last) { ++changes; last = h.getChord().name; }
                }
        return changes;
    };
    const int slow = changesAt (0.0f), mid = changesAt (0.5f), fast = changesAt (1.0f);
    std::printf ("  chord changes: slow %d, default %d, fast %d\n", slow, mid, fast);
    CHECK (slow <= mid && mid < fast);
    CHECK (passingNoteTime (0.0f) > passingNoteTime (0.5f) && passingNoteTime (0.5f) > passingNoteTime (1.0f));
}

TEST (harmonyIgnoresWobblesBetweenScaleNotes)
{
    // An out-of-key F sung with vibrato in G major sits between E and F#: the melody must not
    // flip between them every few frames.
    HarmonyContext ctx;
    ctx.scale = { 7, ScaleType::Major };
    LiveHarmonizer h;
    h.reset();
    const float dt = 0.005f;
    int flips = 0, lastLead = -1;
    for (float t = 0; t < 2.0f; t += dt)
    {
        h.process (sung (53.0f + 0.25f * std::sin (kTwoPi * 5.5f * t)), ctx, dt);
        if (h.getLeadNote() != lastLead) { ++flips; lastLead = h.getLeadNote(); }
    }
    std::printf ("  lead note changes over 2 s of a wobbling in-between pitch: %d\n", flips);
    CHECK (flips <= 2);

    // A few frames an octave off (a tracking slip) must not move the melody.
    h.reset();
    for (float t = 0; t < 0.3f; t += dt) h.process (sung (55.0f), ctx, dt);
    for (int i = 0; i < 4; ++i) h.process (sung (43.0f), ctx, dt);
    CHECK (h.getLeadNote() == 55);
}

TEST (sopranoDoublesAMaleMelodyOneOctaveUp)
{
    // A baritone melody around F3 that starts on a low pickup: in Words mode the sopranos must
    // double it one octave up, not two (a 4x pitch shift sounds like a cartoon).
    for (bool words : { true, false })
    {
        HarmonyContext ctx;
        ctx.scale = { 3, ScaleType::Major };
        ctx.wordsMode = words;
        LiveHarmonizer h;
        h.reset();
        const float dt = 0.005f;
        const float melody[] = { 50, 55, 53, 55, 53, 51, 53, 55 };
        int maxShift = 0;
        for (int phrase = 0; phrase < 3; ++phrase)
        {
            for (float m : melody)
                for (float t = 0; t < 0.3f; t += dt)
                {
                    h.process (sung (m), ctx, dt);
                    maxShift = std::max (maxShift, h.getSopranoShift());
                }
            for (float t = 0; t < 0.5f; t += dt) h.process (PitchFrame{}, ctx, dt);
        }
        std::printf ("  %s: largest soprano shift %d semitones\n", words ? "words" : "vowels", maxShift);
        CHECK (maxShift == 12);
    }
}

TEST (learnFoldsOctaveSlips)
{
    // A short blip an octave below the surrounding notes is a tracking slip, not a leap.
    const double sr = 48000.0;
    auto x = test::singMelody ({ { 51.0f, 0.6f, 0.0f }, { 39.0f, 0.13f, 0.0f }, { 52.0f, 0.6f, 0.0f } }, sr, Bass, 0.0f);
    const auto take = analyseTake (x.data(), (int64_t) x.size(), sr);
    float lowest = 1000.0f;
    for (const auto& n : take.notes) lowest = std::min (lowest, n.pitch);
    std::printf ("  notes %zu, lowest %.1f\n", take.notes.size(), lowest);
    CHECK (lowest > 45.0f);
}

TEST (psolaShift)
{
    const double sr = 48000.0;
    auto x = test::singMelody ({ { 57.0f, 1.5f, 0.2f } }, sr, Tenor, 0.0f);
    PitchTracker tracker;
    PitchTracker::Settings ts;
    ts.minHz = 75.0f;
    tracker.prepare (sr, ts);
    PsolaAnalyzer an;
    const int maxPeriod = (int) (sr / 75.0) + 2;
    an.prepare (sr, maxPeriod, 8192);
    PsolaVoice voice;
    voice.prepare (sr, 64, maxPeriod);
    voice.reset (0.0);
    Rng rng (3);
    const int latency = 2200;
    std::vector<float> y (x.size(), 0.0f);
    for (size_t pos = 0; pos + 64 <= x.size(); pos += 64)
    {
        tracker.push (x.data() + pos, 64);
        for (int k = 0; k < tracker.getNumNewFrames(); ++k) an.addPitchFrame (tracker.getNewFrame (k));
        an.pushInput (x.data() + pos, 64);
        an.updateMarks();
        const int64_t t0 = (int64_t) pos + 64 - 64 - latency;
        PsolaControl ctl;
        ctl.targetHz = midiToHz (57.0f + 7.0f); // up a fifth
        ctl.formant = 1.0f;
        if (t0 >= 0)
            voice.render (an, t0, 64, y.data() + pos, ctl, rng);
    }
    const float m = medianPitch (y, sr, 0.5, 1.4);
    CHECK_NEAR (m, 64.0, 0.15);
    maybeWrite ("psola_fifth", y, y, sr);
}

//==============================================================================
static std::vector<float> runEngine (ChoirEngine& eng, const std::vector<float>& x, double sr, std::vector<float>* outR = nullptr,
                                     const std::function<void (int64_t)>& perBlock = {})
{
    const int block = 256;
    const int lat = eng.getReportedLatency();   // what a host would compensate
    std::vector<float> in (x);
    in.resize (x.size() + (size_t) lat + block, 0.0f);
    std::vector<float> l (in.size(), 0.0f), r (in.size(), 0.0f);
    for (size_t pos = 0; pos + block <= in.size(); pos += block)
    {
        if (perBlock) perBlock ((int64_t) pos);
        eng.process (in.data() + pos, nullptr, l.data() + pos, r.data() + pos, block);
    }
    // compensate latency
    std::vector<float> outL (x.size()), oR (x.size());
    for (size_t i = 0; i < x.size(); ++i) { outL[i] = l[i + (size_t) lat]; oR[i] = r[i + (size_t) lat]; }
    if (outR) *outR = oR;
    return outL;
}

static EngineParams dryParams (VocalType vt, HarmonyMode mode)
{
    EngineParams p;
    p.vocalType = vt;
    p.harmonyMode = mode;
    p.reverb = 0.0f;
    p.leadDb = -60.0f;
    p.choirSize = 50.0f;
    p.humanize = 0.0f;   // keep the pitch measurements clean
    p.vibrato = 0.0f;
    p.key = 1;           // C
    p.glue = 0.0f;
    return p;
}

static void checkSectionPitches (VocalType vt, const char* label)
{
    const double sr = 48000.0;
    // Sing C4 for a while (tenor-ish voice) in C major, Unison mode: each section sings C in its octave.
    auto x = test::singMelody ({ { 60.0f, 2.0f, 0.3f } }, sr, Tenor, 0.0f);
    const std::array<float, NumSections> expected = { 72.0f, 60.0f, 60.0f, 48.0f };
    for (int s = 0; s < NumSections; ++s)
    {
        ChoirEngine eng;
        eng.prepare (sr, 256);
        auto p = dryParams (vt, HarmonyMode::Unison);
        p.sections[(size_t) s].solo = true;
        eng.setParams (p);
        std::vector<float> r;
        auto l = runEngine (eng, x, sr, &r);
        std::vector<float> m (l.size());
        for (size_t i = 0; i < l.size(); ++i) m[i] = 0.5f * (l[i] + r[i]);
        const float pitch = medianPitch (m, sr, 0.6, 1.9);
        std::printf ("  %s section %d: pitch %.2f (expected %.0f)\n", label, s, pitch, expected[(size_t) s]);
        CHECK_NEAR (pitch, expected[(size_t) s], 0.3);
    }
}

TEST (engineUnisonWords) { checkSectionPitches (VocalType::Words, "words"); }
TEST (engineUnisonVowels) { checkSectionPitches (VocalType::Ah, "vowels"); }

TEST (engineFullChoirHarmony)
{
    const double sr = 48000.0;
    auto x = test::singMelody (test::cMajorScale (60.0f, 0.5f), sr, Tenor, 20.0f);
    for (VocalType vt : { VocalType::Words, VocalType::Ah })
    {
        ChoirEngine eng;
        eng.prepare (sr, 256);
        auto p = dryParams (vt, HarmonyMode::FullChoir);
        p.leadDb = 0.0f;
        p.reverb = 40.0f;
        p.humanize = 100.0f;
        p.vibrato = 100.0f;
        eng.setParams (p);
        int snapshots = 0, harmonised = 0;
        std::vector<float> r;
        auto l = runEngine (eng, x, sr, &r, [&] (int64_t)
        {
            const auto snap = eng.takeSnapshot();
            ++snapshots;
            bool any = false;
            for (int s = 0; s < NumSections; ++s)
            {
                const int nn = snap.sectionNotes[(size_t) s][0];
                if (nn >= 0)
                {
                    any = true;
                    Scale c { 0, ScaleType::Major };
                    CHECK (c.contains (nn));
                }
            }
            if (any) ++harmonised;
        });
        CHECK (harmonised > snapshots / 3);
        float rms = 0;
        for (float v : l) rms += v * v;
        rms = std::sqrt (rms / (float) l.size());
        std::printf ("  full choir (%s): output rms %.4f, harmonised blocks %d/%d\n", vt == VocalType::Words ? "words" : "ah", rms, harmonised, snapshots);
        CHECK (rms > 0.01f && rms < 0.5f);
        maybeWrite (vt == VocalType::Words ? "full_choir_words" : "full_choir_ah", l, r, sr);
    }
    maybeWrite ("input_scale", x, x, sr);
}

TEST (midiPlaysTheChoir)
{
    // With no voice coming in, held MIDI notes make the choir sing (in every vocal type).
    const double sr = 48000.0;
    for (VocalType vt : { VocalType::Ah, VocalType::Words })
    {
        ChoirEngine eng;
        eng.prepare (sr, 256);
        auto p = dryParams (vt, HarmonyMode::FullChoir);
        eng.setParams (p);
        NoteSet chord;
        chord.add (48); chord.add (60); chord.add (64); chord.add (67);
        eng.setMidi (chord, 0.8f);
        std::vector<float> silence ((size_t) sr * 2, 0.0f);
        auto out = runEngine (eng, silence, sr);
        float rms = 0;
        for (size_t i = (size_t) sr; i < out.size(); ++i) rms += out[i] * out[i];
        rms = std::sqrt (rms / (float) (out.size() - (size_t) sr));
        std::printf ("  midi chord (%s): rms %.4f\n", vt == VocalType::Words ? "words" : "ah", rms);
        CHECK (rms > 0.01f);
        const auto snap = eng.takeSnapshot();
        for (int s = 0; s < NumSections; ++s)
        {
            const int n = snap.sectionNotes[(size_t) s][0];
            CHECK (n >= 0 && (wrapPc (n) == 0 || wrapPc (n) == 4 || wrapPc (n) == 7));
        }
    }
}

TEST (echoAnswersAfterThePhrase)
{
    // Response = Echo: the choir stays quiet while you sing, then sings the phrase back.
    const double sr = 48000.0;
    auto x = test::singMelody ({ { 60.0f, 1.2f, 0.0f }, { 64.0f, 0.8f, 3.0f } }, sr, Tenor, 15.0f);
    for (int run = 0; run < 3; ++run)
    {
        const VocalType vt = run == 0 ? VocalType::Words : VocalType::Ah;
        ChoirEngine eng;
        eng.prepare (sr, 256);
        auto p = dryParams (vt, HarmonyMode::FullChoir);
        p.response = 1;
        p.lowLatency = run == 2;
        eng.setParams (p);
        auto y = runEngine (eng, x, sr);
        auto rms = [&] (double t0, double t1)
        {
            double e = 0; size_t n = 0;
            for (size_t i = (size_t) (t0 * sr); i < (size_t) (t1 * sr) && i < y.size(); ++i, ++n) e += (double) y[i] * y[i];
            return n ? std::sqrt (e / (double) n) : 0.0;
        };
        const double during = rms (0.3, 2.0), after = rms (2.7, 4.2);
        std::printf ("  echo (%s%s): during phrase %.4f, after %.4f\n", vt == VocalType::Words ? "words" : "ah", p.lowLatency ? ", live" : "", during, after);
        CHECK (during < 0.01);
        CHECK (after > 0.03);
        // The echo carries the melody: its pitch follows the phrase (C then E in some octave).
        const float pitch = medianPitch (y, sr, 2.8, 3.6);
        CHECK (pitch > 0.0f);
    }
}

static double bandRatioDb (const std::vector<float>& x, double sr, double t0, double t1)
{
    // Energy 2.2-4.5 kHz relative to 250-1200 Hz (the male "ring" vs the body of the voice).
    const int N = 4096;
    std::vector<double> acc (N / 2 + 1, 0.0);
    for (size_t i = (size_t) (t0 * sr); i + N < (size_t) (t1 * sr) && i + N < x.size(); i += N / 2)
    {
        std::vector<std::complex<float>> buf (N);
        for (int k = 0; k < N; ++k) buf[(size_t) k] = x[i + (size_t) k] * (0.5f - 0.5f * std::cos (kTwoPi * (float) k / (float) N));
        FFT fft (12);
        fft.forward (buf.data());
        for (int k = 0; k <= N / 2; ++k) acc[(size_t) k] += std::norm (buf[(size_t) k]);
    }
    double hi = 0, body = 0;
    for (int k = 0; k <= N / 2; ++k)
    {
        const double f = k * sr / N;
        if (f >= 2200 && f < 4500) hi += acc[(size_t) k];
        if (f >= 250 && f < 1200) body += acc[(size_t) k];
    }
    return 10.0 * std::log10 ((hi + 1e-20) / (body + 1e-20));
}

TEST (wordsSectionsTakeTheOtherVoiceType)
{
    // Words mode re-uses the lead's own voice. With a male lead the sopranos must lose the male
    // ring around 3 kHz; with a female lead the tenors gain it.
    const double sr = 48000.0;
    struct Case { float leadMidi; int leadSection, target; bool moreRing; };
    for (const Case cs : { Case { 55.0f, Tenor, Soprano, false }, Case { 67.0f, Soprano, Tenor, true } })
    {
        auto lead = test::singMelody ({ { cs.leadMidi, 2.5f, 0.2f } }, sr, cs.leadSection, 0.0f);
        ChoirEngine eng;
        eng.prepare (sr, 256);
        auto p = dryParams (VocalType::Words, HarmonyMode::Unison);
        p.sections[(size_t) cs.target].solo = true;
        eng.setParams (p);
        auto y = runEngine (eng, lead, sr);
        const double src = bandRatioDb (lead, sr, 0.8, 2.4), out = bandRatioDb (y, sr, 0.8, 2.4);
        std::printf ("  %s lead -> %s: ring/body %.1f dB -> %.1f dB\n", cs.moreRing ? "female" : "male", cs.target == Soprano ? "sopranos" : "tenors", src, out);
        if (cs.moreRing) CHECK (out > src + 2.0);
        else CHECK (out < src - 6.0);
    }
}

static SampleSet makeRecordedChoir (double sr, int* keptOut = nullptr)
{
    // A pretend recording session: each section sings five held notes with breaths between.
    SampleSet set;
    const std::array<std::array<float, 5>, NumSections> notes { { { 62, 65, 69, 72, 76 }, { 55, 58, 62, 65, 69 },
                                                                  { 50, 53, 57, 60, 64 }, { 41, 45, 48, 52, 55 } } };
    int kept = 0;
    for (int s = 0; s < NumSections; ++s)
    {
        std::vector<test::SungNote> take;
        for (float m : notes[(size_t) s]) take.push_back ({ m, 2.0f, 0.5f });
        const auto x = test::singMelody (take, sr, s, 18.0f, (uint32_t) (11 + s));
        kept += set.addRecording (s, 0, x.data(), (int64_t) x.size(), sr);
    }
    set.finalise();
    if (keptOut != nullptr) *keptOut = kept;
    return set;
}

TEST (recordedChoirSlicesTakes)
{
    const double sr = 48000.0;
    int kept = 0;
    const auto set = makeRecordedChoir (sr, &kept);
    std::printf ("  %s\n", set.describe().c_str());
    CHECK (kept == 20);
    const auto* sop = set.layerFor (Soprano, VocalType::Oo);   // falls back to Ah
    CHECK (sop != nullptr && sop->zones.size() == 5);
    if (sop != nullptr && sop->zones.size() == 5)
    {
        const float expected[] = { 62, 65, 69, 72, 76 };
        for (int i = 0; i < 5; ++i)
            CHECK_NEAR (sop->zones[(size_t) i].rootMidi, expected[i], 0.25);
    }
    CHECK (SampleSet::sectionFromPath ("My Choir/Altos/alto_ah_take2.wav") == Alto);
    CHECK (SampleSet::vowelFromPath ("My Choir/Altos/alto_ah_take2.wav") == 0);
    CHECK (SampleSet::vowelFromPath ("bass hum.wav") == 5);
    CHECK (SampleSet::sectionFromPath ("bassoon.wav") == -1);
}

TEST (recordedChoirSingsInTuneAndSustains)
{
    // The same Unison check as the synthesised voices, sung from the recordings, on a note held
    // far longer than any recorded note.
    const double sr = 48000.0;
    const auto set = makeRecordedChoir (sr);
    auto x = test::singMelody ({ { 60.0f, 6.0f, 0.3f } }, sr, Tenor, 0.0f);
    const std::array<float, NumSections> expected = { 72.0f, 60.0f, 60.0f, 48.0f };
    auto levelRangeDb = [&] (const std::vector<float>& y)
    {
        float lo = 1e9f, hi = 0.0f;
        const size_t w = (size_t) (0.05 * sr);
        for (size_t i = (size_t) (1.2 * sr); i + w < (size_t) (5.8 * sr); i += w)
        {
            double e = 0; for (size_t k = i; k < i + w; ++k) e += (double) y[k] * y[k];
            const float r = (float) std::sqrt (e / (double) w);
            lo = std::min (lo, r); hi = std::max (hi, r);
        }
        return 20.0f * std::log10 (hi / std::max (lo, 1e-9f));
    };
    for (int s = 0; s < NumSections; ++s)
    {
        ChoirEngine eng;
        eng.prepare (sr, 256);
        eng.setSampleSet (&set);
        auto p = dryParams (VocalType::Ah, HarmonyMode::Unison);
        p.sections[(size_t) s].solo = true;
        eng.setParams (p);
        const float pitch = medianPitch (runEngine (eng, x, sr), sr, 0.8, 5.8);

        // One singer per section: the loop must carry the note with no dropouts (the recording's
        // own vibrato moves the level by a few dB).
        ChoirEngine one;
        one.prepare (sr, 256);
        one.setSampleSet (&set);
        p.choirSize = 10.0f;
        one.setParams (p);
        const float range = levelRangeDb (runEngine (one, x, sr));
        std::printf ("  recorded section %d: pitch %.2f (expected %.0f), solo singer level range %.1f dB\n", s, pitch, expected[(size_t) s], range);
        CHECK_NEAR (pitch, expected[(size_t) s], 0.3);
        CHECK (range > 0.0f && range < 6.0f);
    }
}

/** A test recording: a sine-rich tone gliding from `from` to `to` (MIDI) around `cross`
    seconds, with an attack over `attack` seconds. */
static std::vector<float> testTone (double sr, double seconds, float from, float to, double cross, double attack)
{
    std::vector<float> x ((size_t) (seconds * sr));
    double ph = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = (double) i / sr;
        const double k = 0.5 + 0.5 * std::tanh ((t - cross) / 0.02);   // the slide, halfway at `cross`
        const double m = from + (to - from) * k;
        ph += 440.0 * std::exp2 ((m - 69.0) / 12.0) / sr;
        const double env = attack > 0.0 ? std::min (1.0, t / attack) : 1.0;
        x[i] = (float) (0.3 * env * (std::sin (2.0 * kPi * ph) + 0.4 * std::sin (4.0 * kPi * ph) + 0.2 * std::sin (6.0 * kPi * ph)));
    }
    return x;
}

static float rmsAt (const std::vector<float>& y, double sr, double t0, double t1)
{
    double e = 0.0;
    const size_t a = (size_t) (t0 * sr), b = std::min (y.size(), (size_t) (t1 * sr));
    for (size_t i = a; i < b; ++i) e += (double) y[i] * y[i];
    return b > a ? (float) std::sqrt (e / (double) (b - a)) : 0.0f;
}

TEST (recordedNamesDescribeTheirTakes)
{
    const auto move = SampleSet::infoFromPath ("Sopranos/ah/breathy/legato/f2_ah_007_m6561_to6342_x152.wav");
    CHECK (move.singing == Singing::Breathy);
    CHECK (move.kind == SampleKind::Move);
    CHECK_NEAR (move.toMidi, 63.42, 0.001);
    CHECK_NEAR (move.crossingSeconds, 0.152, 0.0001);
    CHECK (move.singer == 102);
    CHECK_NEAR (SampleSet::rootFromPath ("Sopranos/ah/breathy/legato/f2_ah_007_m6561_to6342_x152.wav"), 65.61, 0.001);
    const auto onset = SampleSet::infoFromPath ("Basses/oh/belt/short/m11_oh_003_m4512.wav");
    CHECK (onset.singing == Singing::Belt);
    CHECK (onset.kind == SampleKind::Onset);
    CHECK (onset.singer == 211);
    const auto plain = SampleSet::infoFromPath ("My Choir/Altos/alto_ah_take2.wav");
    CHECK (plain.singing == Singing::Natural && plain.kind == SampleKind::Held && plain.singer == -1);
}

TEST (recordedSingingStylesFallBack)
{
    const double sr = 48000.0;
    SampleSet set;
    const auto x = testTone (sr, 2.0, 60.0f, 60.0f, 99.0, 0.0);
    set.addNote (Tenor, 0, x.data(), (int64_t) x.size(), sr, 60.0f);
    SampleNoteInfo breathy;
    breathy.singing = Singing::Breathy;
    set.addNote (Tenor, 0, x.data(), (int64_t) x.size(), sr, 62.0f, breathy);
    set.finalise();
    CHECK (set.numNotes() == 2);
    CHECK (set.has (Singing::Breathy) && ! set.has (Singing::Belt));
    const auto* natural = set.layerFor (Tenor, VocalType::Ah);
    const auto* airy = set.layerFor (Tenor, VocalType::Ah, Singing::Breathy);
    const auto* belt = set.layerFor (Tenor, VocalType::Ah, Singing::Belt);
    CHECK (natural != nullptr && airy != nullptr && natural != airy);
    CHECK (belt == natural);   // nothing belted: natural singing
    if (airy != nullptr) CHECK_NEAR (airy->zones[0].rootMidi, 62.0, 0.01);
}

TEST (recordedLegatoSingsTheRecordedMove)
{
    // Held notes on 60 and 62 and a recorded move between them. Stepping 60 -> 62 while the
    // engine's own glide is still slow, the voice must take the recording's slide: past halfway
    // within ~0.1 s, at the new note's pitch, then settle on the held 62.
    const double sr = 48000.0;
    SampleSet set;
    for (float m : { 60.0f, 62.0f })
    {
        const auto x = testTone (sr, 2.0, m, m, 99.0, 0.0);
        set.addNote (Tenor, 0, x.data(), (int64_t) x.size(), sr, m);
    }
    SampleNoteInfo move;
    move.kind = SampleKind::Move;
    move.toMidi = 62.0f;
    move.crossingSeconds = 0.15f;
    const auto mx = testTone (sr, 0.4, 60.0f, 62.0f, 0.15, 0.0);
    set.addNote (Tenor, 0, mx.data(), (int64_t) mx.size(), sr, 60.0f, move);
    set.finalise();
    const auto* layer = set.layerFor (Tenor, VocalType::Ah);
    CHECK (layer != nullptr && layer->moves.size() == 1);
    if (layer == nullptr) return;

    auto sing = [&] (bool withMove)
    {
        SampleLayer l = *layer;
        if (! withMove) l.moves.clear();
        SamplePlayer player;
        std::vector<float> y ((size_t) (1.2 * sr), 0.0f);
        std::vector<float> amp (64, 0.5f);
        SampleNote note;
        float glide = 60.0f;
        for (size_t i = 0; i + 64 <= y.size(); i += 64)
        {
            const double t = (double) i / sr;
            const float target = t < 0.5 ? 60.0f : 62.0f;
            if (t >= 0.5) glide += (target - glide) * (1.0f - std::exp (-64.0f / (float) sr / 0.3f));   // a slow glide
            if (target != note.noteMidi) { note.noteMidi = target; ++note.serial; }
            note.hz = midiToHz (glide);
            note.noteHz = midiToHz (target);
            player.render (l, note, amp.data(), y.data() + i, 64, sr, 0, 0.0f);
        }
        return y;
    };
    const auto legato = sing (true), plain = sing (false);
    const float pLegato = medianPitch (legato, sr, 0.58, 0.64), pPlain = medianPitch (plain, sr, 0.58, 0.64);
    const float settled = medianPitch (legato, sr, 0.95, 1.15);
    std::printf ("  0.1 s after the step: %.2f with the recorded move, %.2f gliding; settled %.2f\n", pLegato, pPlain, settled);
    CHECK (pLegato > 61.5f);
    CHECK (pPlain < 61.2f);
    CHECK_NEAR (settled, 62.0, 0.1);
    // No dropouts across the move and the hand-over to the held note.
    float lo = 1e9f, hi = 0.0f;
    for (double t = 0.45; t < 1.15; t += 0.02)
    {
        const float r = rmsAt (legato, sr, t, t + 0.02);
        lo = std::min (lo, r); hi = std::max (hi, r);
    }
    std::printf ("  level through the move: %.1f dB range\n", 20.0f * std::log10 (hi / lo));
    CHECK (20.0f * std::log10 (hi / lo) < 3.0f);
}

TEST (recordedMoveStretchesToANearbyStep)
{
    // Only a tone up (60 -> 62) was recorded; stepping a minor third (60 -> 63) the voice still
    // takes that slide, widened: it starts on 60, slides when the recording does, lands on 63.
    const double sr = 48000.0;
    SampleSet set;
    for (float m : { 60.0f, 63.0f })
    {
        const auto x = testTone (sr, 2.0, m, m, 99.0, 0.0);
        set.addNote (Tenor, 0, x.data(), (int64_t) x.size(), sr, m);
    }
    SampleNoteInfo move;
    move.kind = SampleKind::Move;
    move.toMidi = 62.0f;
    move.crossingSeconds = 0.15f;
    const auto mx = testTone (sr, 0.4, 60.0f, 62.0f, 0.15, 0.0);
    set.addNote (Tenor, 0, mx.data(), (int64_t) mx.size(), sr, 60.0f, move);
    set.finalise();
    const auto* layer = set.layerFor (Tenor, VocalType::Ah);
    if (layer == nullptr) { CHECK (false); return; }
    CHECK (layer->nearestMove (60.0f, 63.0f, 0) == 0);
    CHECK (layer->nearestMove (60.0f, 58.0f, 0) == -1);   // the other way: never
    CHECK (layer->nearestMove (60.0f, 67.0f, 0) == -1);   // a fifth from a tone: too far

    SamplePlayer player;
    std::vector<float> y ((size_t) (1.2 * sr), 0.0f);
    std::vector<float> amp (64, 0.5f);
    SampleNote note;
    for (size_t i = 0; i + 64 <= y.size(); i += 64)
    {
        const float target = (double) i / sr < 0.5 ? 60.0f : 63.0f;
        if (target != note.noteMidi) { note.noteMidi = target; ++note.serial; }
        note.hz = note.noteHz = midiToHz (target);
        player.render (*layer, note, amp.data(), y.data() + i, 64, sr, 0, 0.0f);
    }
    const float before = medianPitch (y, sr, 0.5, 0.54), after = medianPitch (y, sr, 0.62, 0.7), settled = medianPitch (y, sr, 0.9, 1.1);
    std::printf ("  60 -> 63 on a recorded tone: %.2f before the slide, %.2f after, settled %.2f\n", before, after, settled);
    CHECK (before < 60.5f);   // unstretched, the recording would start on 61
    CHECK_NEAR (after, 63.0, 0.25);
    CHECK_NEAR (settled, 63.0, 0.1);
}

TEST (recordedNotesStartWithAnOnset)
{
    // A held note that starts at full level and a recorded onset that speaks over 80 ms: a
    // note from silence starts with the onset, then carries on as the held note; with the gap
    // before a note sung again, it is sung afresh.
    const double sr = 48000.0;
    SampleSet set;
    const auto x = testTone (sr, 2.0, 60.0f, 60.0f, 99.0, 0.0);
    set.addNote (Alto, 0, x.data(), (int64_t) x.size(), sr, 60.0f);
    SampleNoteInfo onset;
    onset.kind = SampleKind::Onset;
    const auto ox = testTone (sr, 0.3, 60.0f, 60.0f, 99.0, 0.08);
    set.addNote (Alto, 0, ox.data(), (int64_t) ox.size(), sr, 60.0f, onset);
    set.finalise();
    const auto* layer = set.layerFor (Alto, VocalType::Ah);
    if (layer == nullptr) { CHECK (false); return; }

    auto sing = [&] (bool withOnset)
    {
        SampleLayer l = *layer;
        if (! withOnset) l.onsets.clear();
        SamplePlayer player;
        std::vector<float> y ((size_t) (1.0 * sr), 0.0f);
        std::vector<float> amp (64, 0.5f);
        SampleNote note;
        note.hz = note.noteHz = midiToHz (60.0f);
        note.noteMidi = 60.0f;
        for (size_t i = 0; i + 64 <= y.size(); i += 64)
            player.render (l, note, amp.data(), y.data() + i, 64, sr, 0, 0.0f);
        return y;
    };
    const auto withOnset = sing (true), without = sing (false);
    const float early = rmsAt (withOnset, sr, 0.0, 0.03), later = rmsAt (withOnset, sr, 0.15, 0.25), held = rmsAt (withOnset, sr, 0.6, 0.9);
    std::printf ("  onset: first 30 ms %.1f dB below the note (%.1f dB without an onset); held after it %.1f dB\n",
                 20.0f * std::log10 (later / early), 20.0f * std::log10 (rmsAt (without, sr, 0.15, 0.25) / rmsAt (without, sr, 0.0, 0.03)),
                 20.0f * std::log10 (held / later));
    CHECK (20.0f * std::log10 (later / early) > 6.0f);
    CHECK (std::abs (20.0f * std::log10 (held / later)) < 1.5f);
    CHECK (std::abs (20.0f * std::log10 (rmsAt (without, sr, 0.15, 0.25) / rmsAt (without, sr, 0.0, 0.03))) < 1.5f);

    // Sung again after a short gap (the release had not finished): afresh, from the onset,
    // where a note carried on legato just comes back up.
    auto again = [&] (bool detached)
    {
        SamplePlayer player;
        std::vector<float> y ((size_t) (1.0 * sr), 0.0f);
        std::vector<float> amp (64);
        SampleNote note;
        note.hz = note.noteHz = midiToHz (60.0f);
        note.noteMidi = 60.0f;
        float a = 0.0f;
        for (size_t i = 0; i + 64 <= y.size(); i += 64)
        {
            const double t = (double) i / sr;
            const bool on = t < 0.4 || t >= 0.45;
            if (t >= 0.45 && note.serial == 0) { note.serial = 1; note.detached = detached; }
            for (auto& v : amp) { a += ((on ? 0.5f : 0.0f) - a) * 0.002f; v = a; }
            player.render (*layer, note, amp.data(), y.data() + i, 64, sr, 0, 0.0f);
        }
        return rmsAt (y, sr, 0.455, 0.48);
    };
    const float afresh = again (true), carried = again (false);
    std::printf ("  sung again after a gap: %.1f dB softer at the restart than carried on\n", 20.0f * std::log10 (carried / afresh));
    CHECK (20.0f * std::log10 (carried / afresh) > 4.0f);
}

TEST (leadLatencyAlignment)
{
    const double sr = 48000.0;
    ChoirEngine eng;
    eng.prepare (sr, 256);
    auto p = dryParams (VocalType::Ah, HarmonyMode::FullChoir);
    p.leadDb = 0.0f;
    p.choirDb = -60.0f;
    eng.setParams (p);
    std::vector<float> x (48000, 0.0f);
    x[10000] = 0.5f;
    auto y = runEngine (eng, x, sr);
    size_t peak = 0;
    for (size_t i = 0; i < y.size(); ++i) if (std::abs (y[i]) > std::abs (y[peak])) peak = i;
    CHECK (peak == 10000);
}

TEST (liveMonitoringLowLatency)
{
    // Live mode: the lead is not delayed at all, the vowel choir answers after the pitch analysis
    // only, Words keep the full latency, and the plugin reports zero latency.
    const double sr = 48000.0;
    {
        ChoirEngine eng;
        eng.prepare (sr, 256);
        auto p = dryParams (VocalType::Ah, HarmonyMode::FullChoir);
        p.leadDb = 0.0f;
        p.choirDb = -60.0f;
        p.lowLatency = true;
        eng.setParams (p);
        CHECK (eng.getReportedLatency() == 0);
        CHECK (eng.getLatencySamples() > 0);
        std::vector<float> x (48000, 0.0f);
        x[10000] = 0.5f;
        auto y = runEngine (eng, x, sr);
        size_t peak = 0;
        for (size_t i = 0; i < y.size(); ++i) if (std::abs (y[i]) > std::abs (y[peak])) peak = i;
        CHECK (peak == 10000);
    }

    // How quickly the choir follows a note change, measured on the raw (uncompensated) output.
    const auto x = test::singMelody ({ { 60.0f, 1.0f, 0.0f }, { 64.0f, 1.0f, 0.3f } }, sr, Tenor, 0.0f);
    const double stepMs = (0.15 + 1.0) * 1000.0;    // singMelody starts after 0.15 s of silence
    auto follow = [&] (VocalType vt, bool live)
    {
        ChoirEngine eng;
        eng.prepare (sr, 256);
        auto p = dryParams (vt, HarmonyMode::Unison);
        p.lowLatency = live;
        p.glide = 0.0f;
        p.sections[Tenor].solo = true;
        eng.setParams (p);
        std::vector<float> in (x);
        in.resize (x.size() + 4096, 0.0f);
        std::vector<float> y (in.size(), 0.0f);
        for (size_t pos = 0; pos + 256 <= in.size(); pos += 256)
            eng.process (in.data() + pos, nullptr, y.data() + pos, nullptr, 256);
        // When the choir starts to move off the old note (a level the tracker can't fake with an
        // octave slip in the middle of the glide, as a midpoint can).
        for (const auto& f : PitchTracker::analyse (y.data(), (int64_t) y.size(), sr))
            if (f.voiced && f.midi > 60.25f && (double) f.centre / sr * 1000.0 > stepMs - 50.0)
                return (double) f.centre / sr * 1000.0 - stepMs;
        return 1.0e9;
    };
    const double studioAh = follow (VocalType::Ah, false), liveAh = follow (VocalType::Ah, true);
    const double studioWords = follow (VocalType::Words, false), liveWords = follow (VocalType::Words, true);
    std::printf ("  choir follows a note change after: vowels %.1f ms studio / %.1f ms live, words %.1f / %.1f ms\n",
                 studioAh, liveAh, studioWords, liveWords);
    CHECK (studioAh - liveAh > 18.0 && studioAh - liveAh < 30.0);   // vowels run ~24 ms closer to the voice
    CHECK (liveAh < 40.0);
    CHECK_NEAR (liveWords, studioWords, 6.0); // Words always need the full lookahead
}

TEST (learnAndRender)
{
    const double sr = 48000.0;
    auto x = test::singMelody (test::amazingGrace(), sr, Tenor, 25.0f);
    const auto take = analyseTake (x.data(), (int64_t) x.size(), sr);
    std::printf ("  learned %zu notes, %zu phrases\n", take.notes.size(), take.phrases.size());
    CHECK (take.phrases.size() == 2);
    CHECK (take.notes.size() >= 14 && take.notes.size() <= 18);

    HarmonySettings hs;
    hs.scale = ScaleType::Major;
    const auto score = harmoniseTake (take, hs);
    std::printf ("  %s\n", score.describe().c_str());
    CHECK (score.scale.tonic == 7); // G major
    CHECK ((int) score.events.size() == (int) take.notes.size());
    for (const auto& e : score.events)
        for (int s = 0; s < NumSections; ++s)
            if (e.voicing.primary (s) >= 0)
                CHECK (score.scale.contains (e.voicing.primary (s)) || e.chord.contains (e.voicing.primary (s)));
    // Final chord of each phrase should be the tonic or dominant.
    for (const auto& ph : take.phrases)
    {
        const auto& ch = score.events[(size_t) ph.lastNote].chord;
        CHECK (ch.degree == 0 || ch.degree == 4);
    }

    EngineParams p;
    p.vocalType = VocalType::Words;
    p.choirSize = 60.0f;
    RenderedStems stems;
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = renderTake (x.data(), (int64_t) x.size(), sr, p, score, stems);
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("  rendered %.1f s of audio in %.2f s\n", (double) x.size() / sr, secs);
    CHECK (ok);
    CHECK (stems.length == (int64_t) x.size());
    for (int ch = 0; ch < NumSections * 2; ++ch)
    {
        float e = 0;
        for (float v : stems.channels[(size_t) ch]) e += v * v;
        CHECK (e > 1.0f);
    }
    // Onset alignment: the first stem energy should start near the first sung note.
    const auto& sop = stems.channels[0];
    size_t firstStem = 0;
    while (firstStem < sop.size() && std::abs (sop[firstStem]) < 0.01f) ++firstStem;
    size_t firstIn = 0;
    while (firstIn < x.size() && std::abs (x[firstIn]) < 0.01f) ++firstIn;
    std::printf ("  onset input %.3f s, stems %.3f s\n", (double) firstIn / sr, (double) firstStem / sr);
    CHECK (std::abs ((double) firstStem - (double) firstIn) < 0.08 * sr);

    auto mix = mixdownStems (stems, p, false);
    maybeWrite ("amazing_grace_input", x, x, sr);
    maybeWrite ("amazing_grace_choir", mix[0], mix[1], sr);
}

TEST (badInputAndParametersNeverStick)
{
    // One NaN or infinite input sample (a glitching host or plug-in upstream) and nonsense
    // parameter values must never leave the choir silent or the output non-finite.
    const double sr = 48000.0;
    auto x = test::singMelody (test::cMajorScale (57.0f, 0.4f), sr, Tenor, 20.0f);
    for (int run = 0; run < 3; ++run)
    {
        const VocalType vt = run == 1 ? VocalType::Words : VocalType::Ah;
        ChoirEngine eng;
        eng.prepare (sr, 256);
        auto p = dryParams (vt, HarmonyMode::FullChoir);
        eng.setParams (p);
        std::vector<float> in (x);
        in[(size_t) (0.5 * sr)] = std::numeric_limits<float>::quiet_NaN();
        in[(size_t) (0.9 * sr)] = std::numeric_limits<float>::infinity();
        if (run == 2)
        {
            p.vibrato = std::numeric_limits<float>::quiet_NaN();
            p.volumeDb = std::numeric_limits<float>::infinity();
            p.formant = -std::numeric_limits<float>::infinity();
            p.sections[0].level = std::numeric_limits<float>::quiet_NaN();
            eng.setParams (p);
        }
        auto y = runEngine (eng, in, sr);
        bool finite = true;
        for (float v : y) finite = finite && std::isfinite (v);
        double e = 0;
        const size_t a = (size_t) (3.5 * sr), b = std::min (y.size(), (size_t) (5.5 * sr));
        for (size_t i = a; i < b; ++i) e += (double) y[i] * y[i];
        const double late = std::sqrt (e / (double) std::max<size_t> (1, b - a));
        std::printf ("  run %d: output finite %d, choir level long after the bad sample %.4f\n", run, (int) finite, late);
        CHECK (finite);
        CHECK (late > 0.01);
    }
}

TEST (echoPhrasesBackToBackWithoutClicks)
{
    // Two phrases with a short breath between: the second answer starts on time and the
    // hand-over between answers is smooth (it used to restart every singer mid-note).
    const double sr = 48000.0;
    auto x = test::singMelody ({ { 55, 0.8f, 0.05f }, { 57, 0.8f, 0.05f }, { 59, 0.9f, 0.6f },
                                 { 62, 0.8f, 0.05f }, { 60, 0.8f, 0.05f }, { 59, 0.9f, 4.0f } }, sr, Tenor);
    for (VocalType vt : { VocalType::Words, VocalType::Ah })
    {
        ChoirEngine eng;
        eng.prepare (sr, 64);
        EngineParams p;   // the default choir, answering each phrase
        p.vocalType = vt;
        p.response = 1;
        eng.setParams (p);
        std::array<std::vector<float>, NumSections * 2> stems;
        float* taps[NumSections * 2];
        for (size_t i = 0; i < stems.size(); ++i) { stems[i].assign (x.size() + 64, 0.0f); taps[i] = stems[i].data(); }
        eng.setStemTap (taps);
        std::vector<float> l (64), r (64);
        for (size_t pos = 0; pos + 64 <= x.size(); pos += 64)
            eng.process (x.data() + pos, nullptr, l.data(), r.data(), 64);
        std::vector<float> bus (x.size(), 0.0f);
        for (size_t i = 0; i < x.size(); ++i)
            for (int s = 0; s < NumSections; ++s) bus[i] += stems[(size_t) s * 2][i];
        float maxStep = 0.0f, peak = 0.0f;
        for (size_t i = 1; i < bus.size(); ++i)
        {
            maxStep = std::max (maxStep, std::abs (bus[i] - bus[i - 1]));
            peak = std::max (peak, std::abs (bus[i]));
        }
        // The second answer is sung from about 6.3 s (stem timeline, i.e. after the latency).
        const float pitch = medianPitch (bus, sr, 6.6 + eng.getLatencySamples() / sr, 7.2 + eng.getLatencySamples() / sr);
        std::printf ("  %s: peak %.3f, largest sample step %.3f, second answer pitch %.1f\n", vt == VocalType::Words ? "words" : "ah", peak, maxStep, pitch);
        CHECK (maxStep < 0.4f * std::max (peak, 0.1f));
        CHECK (pitch > 0.0f);
    }
}

TEST (wideRangesKeepChordSearchFast)
{
    // Drone / MIDI-only chords search every voicing: with very wide section ranges that used to
    // take milliseconds per chord change on the audio thread.
    VoicingRequest req;
    req.scale = Scale { 0, ScaleType::Major };
    req.mode = HarmonyMode::Drone;
    req.style = Style::Gospel;
    for (auto& r : req.ranges) r = SectionRange { 24, 96 };
    const auto vocab = buildChordVocabulary (req.scale, Style::Gospel);
    const auto t0 = std::chrono::steady_clock::now();
    int voiced = 0;
    for (int i = 0; i < 50; ++i)
    {
        req.chord = vocab[(size_t) i % vocab.size()];
        const Voicing v = voiceChord (req);
        voiced += v.count[Bass] > 0 ? 1 : 0;
    }
    const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count() / 50.0;
    std::printf ("  %.3f ms per chord\n", ms);
    CHECK (voiced == 50);
    CHECK (ms < 1.5);
}

TEST (learnFollowsKeyChanges)
{
    // A song that moves up a whole step for its last verses is harmonised in both keys; a song
    // in one key, or with a single odd phrase, keeps its key.
    const double sr = 48000.0;
    auto song = [] (std::initializer_list<float> transpositions, bool oddPhrase)
    {
        std::vector<test::SungNote> m;
        int k = 0;
        for (float t : transpositions)
        {
            auto v = test::amazingGrace (t);
            if (oddPhrase && k == 2)
                v.resize (9);   // just the first phrase
            m.insert (m.end(), v.begin(), v.end());
            ++k;
        }
        return m;
    };
    HarmonySettings hs;
    hs.scale = ScaleType::Major;

    {
        auto x = test::singMelody (song ({ 0, 0, 2, 2 }, false), sr, Tenor, 20.0f);
        const auto take = analyseTake (x.data(), (int64_t) x.size(), sr);
        const auto score = harmoniseTake (take, hs);
        std::printf ("  modulating song: %s\n", score.describe().c_str());
        CHECK (score.keys.size() == 2);
        if (score.keys.size() == 2)
        {
            CHECK (score.keys[0].scale.tonic == 7);   // G
            CHECK (score.keys[1].scale.tonic == 9);   // A
            // The change lands where the third verse starts (the 5th phrase).
            CHECK (take.phrases.size() == 8);
            if (take.phrases.size() == 8)
                CHECK (score.keys[1].start == take.phrases[4].start);
            // Every voice after the change sings in A major (or a chord tone).
            int outside = 0;
            for (const auto& e : score.events)
                if (e.start >= score.keys[1].start)
                    for (int s = 0; s < NumSections; ++s)
                        if (e.voicing.primary (s) >= 0 && ! score.keys[1].scale.contains (e.voicing.primary (s)) && ! e.chord.contains (e.voicing.primary (s)))
                            ++outside;
            CHECK (outside == 0);
        }
        HarmonySettings fixed = hs;
        fixed.key = 8;   // G chosen by hand: no detection, one key
        CHECK (harmoniseTake (take, fixed).keys.size() == 1);
    }
    {
        auto x = test::singMelody (song ({ 0, 0, 0 }, false), sr, Tenor, 20.0f);
        const auto take = analyseTake (x.data(), (int64_t) x.size(), sr);
        const auto score = harmoniseTake (take, hs);
        std::printf ("  one key: %s\n", score.describe().c_str());
        CHECK (score.keys.size() == 1);
        CHECK (score.scale.tonic == 7);
    }
    {
        auto x = test::singMelody (song ({ 0, 0, 2, 0, 0 }, true), sr, Tenor, 20.0f);
        const auto take = analyseTake (x.data(), (int64_t) x.size(), sr);
        const auto score = harmoniseTake (take, hs);
        std::printf ("  one odd phrase: %s\n", score.describe().c_str());
        CHECK (score.keys.size() == 1);
        CHECK (score.scale.tonic == 7);
    }
}

static std::array<std::vector<float>, NumSections * 2> runStems (ChoirEngine& eng, const std::vector<float>& x,
                                                              const std::function<void (int64_t)>& perBlock = {})
{
    std::array<std::vector<float>, NumSections * 2> stems;
    float* taps[NumSections * 2];
    for (size_t i = 0; i < stems.size(); ++i) { stems[i].assign (x.size() + 256, 0.0f); taps[i] = stems[i].data(); }
    eng.setStemTap (taps);
    std::vector<float> l (256), r (256);
    for (size_t pos = 0; pos + 256 <= x.size(); pos += 256)
    {
        if (perBlock) perBlock ((int64_t) pos);
        eng.process (x.data() + pos, nullptr, l.data(), r.data(), 256);
    }
    eng.setStemTap (nullptr);
    return stems;
}

TEST (childrenSingInTrebleRange)
{
    // A children's choir sings every part as trebles; the adult basses sit far below.
    const double sr = 48000.0;
    auto x = test::singMelody ({ { 60, 1.5f, 0.1f }, { 64, 1.5f, 0.1f }, { 67, 1.5f, 0.5f } }, sr, Soprano, 20.0f);
    float adultBass = 0, kidsBass = 0, kidsTenor = 0;
    for (Ensemble ens : { Ensemble::Choir, Ensemble::Children })
    {
        ChoirEngine eng;
        eng.prepare (sr, 256);
        auto p = dryParams (VocalType::Ah, HarmonyMode::FullChoir);
        p.ensemble = ens;
        eng.setParams (p);
        auto st = runStems (eng, x);
        const double lat = eng.getLatencySamples() / sr;
        const float bass = medianPitch (st[Bass * 2], sr, 0.4 + lat, 4.0 + lat);
        const float tenor = medianPitch (st[Tenor * 2], sr, 0.4 + lat, 4.0 + lat);
        std::printf ("  %s: tenor %.1f, bass %.1f\n", ens == Ensemble::Children ? "children" : "choir", tenor, bass);
        if (ens == Ensemble::Choir) adultBass = bass;
        else { kidsBass = bass; kidsTenor = tenor; }
    }
    CHECK (adultBass > 0.0f && adultBass < 55.0f);
    CHECK (kidsBass >= 54.5f);
    CHECK (kidsTenor >= 54.5f);
    CHECK (ensembleRange (Ensemble::Children, Bass, SectionRange { 40, 64 }).low >= 55);
    CHECK (ensembleRange (Ensemble::Choir, Bass, SectionRange { 40, 64 }).low == 40);
}

TEST (congregationEntersLooser)
{
    // A congregation comes in late and scattered: the choir takes longer to reach full voice.
    const double sr = 48000.0;
    auto x = test::singMelody ({ { 57, 2.0f, 0.5f } }, sr, Tenor, 15.0f);
    double rise[2] = {};
    int k = 0;
    for (Ensemble ens : { Ensemble::Choir, Ensemble::Congregation })
    {
        ChoirEngine eng;
        eng.prepare (sr, 256);
        auto p = dryParams (VocalType::Ah, HarmonyMode::FullChoir);
        p.ensemble = ens;
        p.humanize = 100.0f;
        eng.setParams (p);
        auto y = runEngine (eng, x, sr);
        auto level = [&] (double t0) { double e = 0; for (int i = 0; i < 480; ++i) e += (double) y[(size_t) (t0 * sr) + (size_t) i] * y[(size_t) (t0 * sr) + (size_t) i]; return std::sqrt (e / 480.0); };
        const double full = level (1.2);
        double t = 0.0;
        while (t < 1.0 && level (t) < 0.7 * full) t += 0.005;
        rise[k++] = t;
        std::printf ("  %s: reaches 70%% after %.3f s\n", ens == Ensemble::Choir ? "choir" : "congregation", t);
    }
    CHECK (rise[1] > rise[0] + 0.02);
}

TEST (rhythmGateFollowsTheSong)
{
    // Pulse 1/4 at 50 %: the choir sounds on the first half of every beat of the song and is
    // silent on the second half, lined up with the host's beat after latency compensation.
    const double sr = 48000.0, bpm = 120.0;
    auto x = test::singMelody ({ { 57, 5.0f, 0.3f } }, sr, Tenor, 10.0f);
    for (int pattern : { 1, 11 })
    {
        ChoirEngine eng;
        eng.prepare (sr, 256);
        auto p = dryParams (VocalType::Ah, HarmonyMode::FullChoir);
        p.rhythmPattern = pattern;   // Pulse 1/4, then Arp Up
        p.rhythmLength = 50.0f;
        if (pattern == 11)
            p.sections[Soprano].solo = true;   // the arpeggio gives sopranos every 4th sixteenth
        eng.setParams (p);
        auto y = runEngine (eng, x, sr, nullptr, [&] (int64_t pos) { eng.setTransport (true, bpm, (double) pos / sr * bpm / 60.0); });
        double on = 0, off = 0;
        size_t nOn = 0, nOff = 0;
        for (size_t i = (size_t) (1.0 * sr); i < (size_t) (4.5 * sr); ++i)
        {
            const double beat = (double) i / sr * bpm / 60.0;
            const double step = std::fmod (beat * 4.0, 16.0);
            const double into = pattern == 1 ? std::fmod (beat, 1.0) : std::fmod (step, 4.0) - 3.0;   // sopranos: steps 3, 7, 11, 15
            const bool open = pattern == 1 ? into > 0.05 && into < 0.45 : into > 0.1 && into < 0.4;
            const bool closed = pattern == 1 ? into > 0.6 && into < 0.95 : (std::fmod (step, 4.0) > 0.7 && std::fmod (step, 4.0) < 2.9);
            if (open) { on += (double) y[i] * y[i]; ++nOn; }
            if (closed) { off += (double) y[i] * y[i]; ++nOff; }
        }
        const double ratioDb = 10.0 * std::log10 ((on / std::max<size_t> (1, nOn) + 1e-12) / (off / std::max<size_t> (1, nOff) + 1e-12));
        std::printf ("  pattern %d: on/off %.1f dB\n", pattern, ratioDb);
        CHECK (ratioDb > 20.0);
    }
}

TEST (midiPartsSingTheirChannels)
{
    // Parts mode: notes on channels 1-4 are sung by sopranos, altos, tenors and basses as given.
    const double sr = 48000.0;
    std::vector<float> silence ((size_t) (3.0 * sr), 0.0f);
    ChoirEngine eng;
    eng.prepare (sr, 256);
    auto p = dryParams (VocalType::Ah, HarmonyMode::FullChoir);
    p.midiParts = true;
    eng.setParams (p);
    std::array<NoteSet, NumSections> parts;
    const int want[] = { 76, 67, 60, 48 };
    for (int s = 0; s < NumSections; ++s) parts[(size_t) s].add (want[s]);
    eng.setMidiParts (parts, 0.8f);
    auto st = runStems (eng, silence);
    for (int s = 0; s < NumSections; ++s)
    {
        const float m = medianPitch (st[(size_t) s * 2], sr, 1.0, 2.5);
        std::printf ("  section %d: %.1f (wanted %d)\n", s, m, want[s]);
        CHECK_NEAR (m, want[s], 0.6);
    }
}

static std::vector<test::BandChordSpec> bandProgression()
{
    // C  G/B  Am  F  Dm7  G7  C  Em  Fmaj7  Gsus4  G  Am7
    return { { { 60, 64, 67 }, 36, 2.0f }, { { 59, 62, 67 }, 35, 2.0f }, { { 57, 60, 64 }, 33, 2.0f }, { { 57, 60, 65 }, 41, 2.0f },
             { { 57, 60, 62, 65 }, 38, 2.0f }, { { 59, 62, 65, 67 }, 43, 2.0f }, { { 60, 64, 67 }, 36, 2.0f }, { { 59, 64, 67 }, 40, 2.0f },
             { { 57, 60, 64, 65 }, 41, 2.0f }, { { 60, 62, 67 }, 43, 1.0f }, { { 59, 62, 67 }, 43, 1.0f }, { { 57, 60, 64, 67 }, 33, 2.0f } };
}

TEST (bandChordsAreHeard)
{
    // The side-chain detector names the band's chords (triads, sevenths, sus, inversions) and
    // its bass note, live and offline, at the common sample rates.
    const char* want[] = { "C", "G", "Am", "F", "Dm7", "G7", "C", "Em", "Fmaj7", "Gsus4", "G", "Am7" };
    const int wantBass[] = { 0, 11, 9, 5, 2, 7, 0, 4, 5, 7, 7, 9 };
    const auto prog = bandProgression();
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        auto x = test::playBand (prog, sr, 0.5f, 0.5f);
        ChordDetector det;
        det.prepare (sr);
        int chords = 0, basses = 0;
        int64_t pos = 0;
        double t = 0.5;
        for (size_t k = 0; k < prog.size(); ++k)
        {
            while ((double) pos / sr < t + prog[k].seconds * 0.85) { det.push (x.data() + pos, 256); pos += 256; }
            chords += det.chord() != nullptr && det.chord()->name == want[k];
            basses += det.bass() == wantBass[k];
            t += prog[k].seconds;
        }
        const auto lane = analyseBandChords (x.data(), (int64_t) x.size(), sr);
        int laneOk = 0;
        t = 0.5;
        for (size_t k = 0; k < prog.size(); ++k)
        {
            const double mid = t + 0.5 * prog[k].seconds;
            for (const auto& bc : lane)
                if (bc.start <= mid * sr && bc.end > mid * sr && bc.chord().name == want[k] && bc.bass == wantBass[k])
                    ++laneOk;
            // Chord changes are found within a quarter of a second of where they happen.
            if (k > 0)
            {
                double nearest = 1.0e9;
                for (const auto& bc : lane) nearest = std::min (nearest, std::abs ((double) bc.start / sr - t));
                CHECK (nearest < 0.25);
            }
            t += prog[k].seconds;
        }
        std::printf ("  %.0f Hz: live %d/12 chords, %d/12 basses; offline %d/12 (%zu segments)\n", sr, chords, basses, laneOk, lane.size());
        CHECK (chords == 12);
        CHECK (basses == 12);
        CHECK (laneOk == 12);
        CHECK (lane.size() == 12);
    }
}

TEST (choirFollowsTheBand)
{
    // A held E fits both C (third) and A minor (fifth): the melody alone can't tell, the band
    // can. With the band on the side-chain the basses follow it from C to A; without, they
    // don't move.
    const double sr = 48000.0;
    auto voice = test::singMelody ({ { 64, 8.0f, 0.5f } }, sr, Soprano, 10.0f);
    auto band = test::playBand ({ { { 60, 64, 67 }, 36, 4.0f }, { { 57, 60, 64 }, 33, 4.0f } }, sr);
    band.resize (voice.size(), 0.0f);
    float bassFirst[2] = {}, bassSecond[2] = {};
    for (int follow = 0; follow < 2; ++follow)
    {
        ChoirEngine eng;
        eng.prepare (sr, 256);
        auto p = dryParams (VocalType::Ah, HarmonyMode::FullChoir);
        p.followBand = follow == 1;
        eng.setParams (p);
        auto st = runStems (eng, voice, [&] (int64_t pos) { eng.setSidechain (band.data() + pos, nullptr); });
        const double lat = eng.getLatencySamples() / sr;
        bassFirst[follow] = medianPitch (st[Bass * 2], sr, 1.5 + lat, 3.5 + lat);
        bassSecond[follow] = medianPitch (st[Bass * 2], sr, 5.5 + lat, 7.5 + lat);
        std::printf ("  follow band %d: basses sing %.1f then %.1f\n", follow, bassFirst[follow], bassSecond[follow]);
    }
    CHECK (wrapPc ((int) std::lround (bassFirst[1])) == 0);    // C
    CHECK (wrapPc ((int) std::lround (bassSecond[1])) == 9);   // A
    CHECK (std::abs (bassFirst[0] - bassSecond[0]) < 0.5f);    // the melody alone keeps one chord
}

TEST (learnHarmonisesWithTheBand)
{
    // LEARN with the band recorded too: every note is harmonised with the chord the band plays
    // under it, inversions included, and the band helps find the key.
    const double sr = 48000.0;
    const auto prog = bandProgression();
    // The melody sits on notes common to neighbouring chords, so it can't give the chords away.
    std::vector<test::SungNote> melody;
    const int line[] = { 67, 67, 64, 65, 65, 67, 67, 64, 64, 67, 67, 64 };
    for (size_t k = 0; k < prog.size(); ++k)
        melody.push_back ({ (float) line[k], prog[k].seconds - 0.1f, 0.1f });
    auto voice = test::singMelody (melody, sr, Soprano, 15.0f);
    std::vector<test::SungNote> lead = { { 0.0f, 0.5f, 0.0f } };
    auto band = test::playBand (prog, sr);
    auto take = analyseTake (voice.data(), (int64_t) voice.size(), sr);
    take.bandChords = analyseBandChords (band.data(), (int64_t) band.size(), sr);
    HarmonySettings hs;
    const auto score = harmoniseTake (take, hs);
    std::printf ("  %s, %zu band chords\n", score.describe().c_str(), take.bandChords.size());
    CHECK (score.scale.tonic == 0);   // C major
    const char* want[] = { "C", "G", "Am", "F", "Dm7", "G7", "C", "Em", "Fmaj7", "Gsus4", "G", "Am7" };
    const int wantBass[] = { 0, 11, 9, 5, 2, 7, 0, 4, 5, 7, 7, 9 };
    int ok = 0, bassOk = 0, n = 0;
    double t = 0.0;
    for (size_t k = 0; k < prog.size(); ++k)
    {
        const double mid = t + 0.5 * prog[k].seconds;
        for (const auto& e : score.events)
            if (e.start <= mid * sr && e.end > mid * sr)
            {
                ++n;
                ok += e.chord.name == want[k];
                bassOk += e.voicing.primary (Bass) >= 0 && wrapPc (e.voicing.primary (Bass)) == wantBass[k];
                if (! (e.voicing.primary (Bass) >= 0 && wrapPc (e.voicing.primary (Bass)) == wantBass[k]))
                    std::printf ("    %s: choir S %d A %d T %d B %d, band bass %d\n", want[k], e.voicing.primary (Soprano), e.voicing.primary (Alto),
                                 e.voicing.primary (Tenor), e.voicing.primary (Bass), wantBass[k]);
            }
        t += prog[k].seconds;
    }
    std::printf ("  %d/%d chords and %d/%d bass notes as the band plays them\n", ok, n, bassOk, n);
    CHECK (n == 12);
    CHECK (ok == 12);
    CHECK (bassOk >= 11);

    // Without the band the harmony is the choir's own guess (some chords differ).
    HarmonySettings own = hs;
    own.followBand = false;
    const auto guess = harmoniseTake (take, own);
    int same = 0;
    t = 0.0;
    for (size_t k = 0; k < prog.size(); ++k)
    {
        const double mid = t + 0.5 * prog[k].seconds;
        for (const auto& e : guess.events)
            if (e.start <= mid * sr && e.end > mid * sr) same += e.chord.name == want[k];
        t += prog[k].seconds;
    }
    CHECK (same < 12);
}

TEST (chordLocksSplitAndJoin)
{
    // A new lock replaces whatever it overlaps; unlocking a range cuts locks that reach past it.
    auto chord = [] (int root) { const int iv[] = { 0, 4, 7 }; return makeChordFromIntervals (root, iv, 3); };
    std::vector<ChordLock> locks;
    addChordLock (locks, { 0, 100, chord (0) });
    addChordLock (locks, { 50, 150, chord (7) });
    CHECK (locks.size() == 2);
    CHECK (locks[0].start == 0 && locks[0].end == 50 && locks[0].chord.name == "C");
    CHECK (locks[1].start == 50 && locks[1].end == 150 && locks[1].chord.name == "G");
    addChordLock (locks, { 20, 30, chord (5) });
    CHECK (locks.size() == 4);
    CHECK (locks[1].start == 20 && locks[1].end == 30 && locks[1].chord.name == "F");
    CHECK (locks[2].start == 30 && locks[2].end == 50 && locks[2].chord.name == "C");
    removeChordLocks (locks, 25, 60);
    CHECK (locks.size() == 3);
    CHECK (locks[0].end == 20 && locks[1].start == 20 && locks[1].end == 25 && locks[2].start == 60 && locks[2].chord.name == "G");
    addChordLock (locks, { 10, 10, chord (2) });   // empty: ignored
    CHECK (locks.size() == 3);
    const int sev[] = { 0, 4, 7, 11 };
    CHECK (makeChordFromIntervals (8, sev, 4, true).name == "Abmaj7");
}

/** A hymn-like line in C major: four phrases of quarter and half notes. */
static std::vector<test::SungNote> hymnLine()
{
    const float q = 0.42f, h = 0.84f;
    return { { 64, q, 0 }, { 64, q, 0 }, { 65, q, 0 }, { 67, h, 0.4f },
             { 67, q, 0 }, { 65, q, 0 }, { 64, q, 0 }, { 62, h, 0.4f },
             { 60, q, 0 }, { 60, q, 0 }, { 62, q, 0 }, { 64, h, 0.4f },
             { 64, q, 0 }, { 62, q, 0 }, { 62, q, 0 }, { 60, h, 0.4f } };
}

TEST (chordLaneLocksAndAlternatives)
{
    const double sr = 48000.0;
    const auto voice = test::singMelody (hymnLine(), sr, Soprano, 15.0f);
    const auto take = analyseTake (voice.data(), (int64_t) voice.size(), sr);
    HarmonySettings hs;
    const auto score = harmoniseTake (take, hs);
    CHECK (score.scale.tonic == 0);
    const auto lane = score.chordLane();
    std::string line;
    for (const auto& s : lane) line += s.chord.name + " ";
    std::printf ("  lane: %s(%zu spans for %zu notes)\n", line.c_str(), lane.size(), score.events.size());
    CHECK (lane.size() >= 4 && lane.size() < score.events.size());
    bool ordered = true, anyLocked = false;
    for (size_t k = 0; k < lane.size(); ++k)
    {
        ordered = ordered && lane[k].end > lane[k].start && (k == 0 || lane[k].start >= lane[k - 1].end);
        anyLocked = anyLocked || lane[k].locked;
    }
    CHECK (ordered);
    CHECK (! anyLocked);

    // Alternatives for a span in the middle: ranked, with the current chord marked.
    const auto& span = lane[lane.size() / 2];
    const auto options = chordAlternatives (take, score, hs, span.start, span.end);
    std::string opts;
    for (const auto& o : options) opts += o.chord.name + (o.current ? "* " : " ");
    std::printf ("  options for %s: %s\n", span.chord.name.c_str(), opts.c_str());
    CHECK (options.size() >= 4 && options.size() <= 8);
    int current = 0;
    bool distinct = true;
    for (size_t a = 0; a < options.size(); ++a)
    {
        current += options[a].current;
        if (options[a].current)
            CHECK (options[a].chord.name == span.chord.name);
        for (size_t b = a + 1; b < options.size(); ++b)
            distinct = distinct && options[a].chord.name != options[b].chord.name;
    }
    CHECK (current == 1);
    CHECK (distinct);
    CHECK (options.front().fit >= 0.5f);

    // Lock the best other choice: those notes sing it, and the harmony leads in and out of it.
    const ChordOption* pick = nullptr;
    for (const auto& o : options)
        if (! o.current) { pick = &o; break; }
    CHECK (pick != nullptr);
    if (pick == nullptr) return;
    HarmonySettings locked = hs;
    addChordLock (locked.locks, { span.start, span.end, pick->chord });
    const auto score2 = harmoniseTake (take, locked);
    int inSpan = 0, singIt = 0, bassOk = 0;
    for (const auto& e : score2.events)
    {
        if (e.start < span.start || e.start >= span.end) { CHECK (! e.locked); continue; }
        ++inSpan;
        singIt += e.locked && e.chord.name == pick->chord.name;
        bassOk += e.voicing.primary (Bass) >= 0 && pick->chord.contains (e.voicing.primary (Bass));
    }
    std::printf ("  locked %s: %d/%d notes sing it\n", pick->chord.name.c_str(), singIt, inSpan);
    CHECK (inSpan > 0 && singIt == inSpan && bassOk == inSpan);
    bool laneShowsIt = false;
    for (const auto& s : score2.chordLane())
        laneShowsIt = laneShowsIt || (s.locked && s.chord.name == pick->chord.name && s.start == span.start);
    CHECK (laneShowsIt);

    // A chord from outside the key (Ab, the flat sixth) can be chosen too.
    const auto& last = lane.back();
    HarmonySettings borrowed = hs;
    addChordLock (borrowed.locks, { last.start, last.end, makeChordOfQuality (8, ChordQuality::Major, true) });
    const auto score3 = harmoniseTake (take, borrowed);
    int ab = 0, lastNotes = 0;
    for (const auto& e : score3.events)
        if (e.start >= last.start && e.start < last.end) { ++lastNotes; ab += e.chord.name == "Ab"; }
    CHECK (lastNotes > 0 && ab == lastNotes);

    // Unlocking brings the choir's own choice back exactly.
    removeChordLocks (locked.locks, span.start, span.end);
    CHECK (locked.locks.empty());
    const auto score4 = harmoniseTake (take, locked);
    bool same = score4.events.size() == score.events.size();
    for (size_t i = 0; same && i < score.events.size(); ++i)
        same = score4.events[i].chord.name == score.events[i].chord.name;
    CHECK (same);
}

TEST (chosenChordsWinOverTheBand)
{
    const double sr = 48000.0;
    const auto prog = bandProgression();
    std::vector<test::SungNote> melody;
    const int line[] = { 67, 67, 64, 65, 65, 67, 67, 64, 64, 67, 67, 64 };
    for (size_t k = 0; k < prog.size(); ++k)
        melody.push_back ({ (float) line[k], prog[k].seconds - 0.1f, 0.1f });
    auto voice = test::singMelody (melody, sr, Soprano, 15.0f);
    auto band = test::playBand (prog, sr);
    auto take = analyseTake (voice.data(), (int64_t) voice.size(), sr);
    take.bandChords = analyseBandChords (band.data(), (int64_t) band.size(), sr);
    HarmonySettings hs;
    const auto score = harmoniseTake (take, hs);
    const auto lane = score.chordLane();
    CHECK (! lane.empty() && lane.front().fromBand);
    // The band plays Am third; the singer wants C there.
    const ChordSpan* am = nullptr;
    for (const auto& s : lane)
        if (s.chord.name == "Am") { am = &s; break; }
    CHECK (am != nullptr);
    if (am == nullptr) return;
    const auto options = chordAlternatives (take, score, hs, am->start, am->end);
    CHECK (! options.empty() && options.front().chord.name == "Am" && options.front().current);
    addChordLock (hs.locks, { am->start, am->end, makeChordOfQuality (0, ChordQuality::Major) });
    const auto score2 = harmoniseTake (take, hs);
    int c = 0, n = 0, band2 = 0;
    for (const auto& e : score2.events)
    {
        if (e.start >= am->start && e.start < am->end) { ++n; c += e.chord.name == "C" && e.locked && ! e.fromBand; }
        else band2 += e.fromBand;
    }
    CHECK (n > 0 && c == n);
    CHECK (band2 == (int) score2.events.size() - n);
}

TEST (bandHelpsFindTheKey)
{
    // C, D and E fit C, F and G major alike; a band playing F, Bb and C settles it.
    const double sr = 48000.0;
    std::vector<test::SungNote> m;
    for (int r = 0; r < 4; ++r)
        for (int n : { 60, 62, 64, 62 })
            m.push_back ({ (float) n, 0.45f, 0.05f });
    auto voice = test::singMelody (m, sr, Soprano, 15.0f);
    auto band = test::playBand ({ { { 53, 57, 60 }, 41, 2.0f }, { { 58, 62, 65 }, 46, 2.0f }, { { 55, 60, 64 }, 48, 2.0f }, { { 53, 57, 60 }, 41, 2.0f } }, sr);
    band.resize (voice.size(), 0.0f);
    ChoirEngine eng;
    eng.prepare (sr, 256);
    auto p = dryParams (VocalType::Ah, HarmonyMode::FullChoir);
    p.key = 0;   // auto
    eng.setParams (p);
    runEngine (eng, voice, sr, nullptr, [&] (int64_t pos) { eng.setSidechain (band.data() + pos, nullptr); });
    const auto snap = eng.takeSnapshot();
    std::printf ("  key with the band: %d (F = 5)\n", snap.keyTonic);
    CHECK (snap.keyTonic == 5);
}

//==============================================================================
// The stage and its microphones.

/** A section of independent "singers": noise on each edge of the bus, plus a shared middle. */
static std::array<std::vector<float>, 2> sectionNoise (int n, uint32_t seed)
{
    Rng rng;
    rng.reseed (seed);
    std::array<std::vector<float>, 2> bus { std::vector<float> ((size_t) n), std::vector<float> ((size_t) n) };
    for (int i = 0; i < n; ++i)
    {
        const float mid = 0.1f * rng.bi();
        bus[0][(size_t) i] = 0.1f * rng.bi() + mid;
        bus[1][(size_t) i] = 0.1f * rng.bi() + mid;
    }
    return bus;
}

struct StageResult { std::vector<float> l, r; };

/** One section through the stage (the others silent). */
static StageResult runStage (const ChoirStage::Settings& settings, int section, const std::array<std::vector<float>, 2>& bus, double sr = 48000.0)
{
    ChoirStage stage;
    stage.setSettings (settings);
    stage.prepare (sr, 64);
    StageResult out { std::vector<float> (bus[0].size()), std::vector<float> (bus[0].size()) };
    std::vector<float> silence (64, 0.0f);
    for (size_t pos = 0; pos < bus[0].size(); pos += 64)
    {
        const int n = (int) std::min<size_t> (64, bus[0].size() - pos);
        stage.begin (n);
        for (int s = 0; s < NumSections; ++s)
            stage.addSection (s, s == section ? bus[0].data() + pos : silence.data(), s == section ? bus[1].data() + pos : silence.data(), n);
        stage.mix (out.l.data() + pos, out.r.data() + pos, n);
    }
    return out;
}

static double energy (const std::vector<float>& x, size_t from = 0)
{
    double e = 0.0;
    for (size_t i = from; i < x.size(); ++i) e += (double) x[i] * x[i];
    return e;
}

/** Where a sound sits between the speakers: -1 left .. 1 right (from the energy balance). */
static double stereoPosition (const StageResult& r)
{
    const double el = energy (r.l, 4800), er = energy (r.r, 4800);
    return std::atan2 (std::sqrt (er), std::sqrt (el)) / (0.25 * kPiD) - 1.0;
}

/** How alike the two channels are (1 = one point in the middle, 0 = spread wide). */
static double correlation (const StageResult& r)
{
    double lr = 0, ll = 0, rr = 0;
    for (size_t i = 4800; i < r.l.size(); ++i) { lr += r.l[i] * r.r[i]; ll += r.l[i] * r.l[i]; rr += r.r[i] * r.r[i]; }
    return lr / std::sqrt (ll * rr + 1.0e-20);
}

TEST (stagePlacesTheSections)
{
    const auto bus = sectionNoise (48000, 7);
    ChoirStage::Settings st;
    for (int s = 0; s < NumSections; ++s)
    {
        const auto d = defaultSectionPlacement (s);
        st.sections[(size_t) s] = { d.pan, d.width, d.depth };
    }
    st.spread = 0.7f;
    const double sop = stereoPosition (runStage (st, Soprano, bus)), alt = stereoPosition (runStage (st, Alto, bus));
    std::printf ("  default stage: sopranos at %.2f, altos at %.2f\n", sop, alt);
    CHECK (sop < -0.2 && sop > -0.6);
    CHECK_NEAR (alt, -sop, 0.02);

    // Sopranos moved to the right of the stage.
    auto moved = st;
    moved.sections[Soprano].x = 0.8f;
    const double right = stereoPosition (runStage (moved, Soprano, bus));
    std::printf ("  sopranos moved right: %.2f\n", right);
    CHECK (right > 0.4);

    // A narrow block sounds like one place; a wide one spreads out. Narrowing keeps the level.
    auto narrow = st, wide = st;
    narrow.sections[Soprano] = { 0.0f, 0.03f, 0.25f };
    wide.sections[Soprano] = { 0.0f, 1.0f, 0.25f };
    wide.spread = narrow.spread = 1.0f;
    const auto n = runStage (narrow, Soprano, bus), w = runStage (wide, Soprano, bus);
    std::printf ("  correlation narrow %.2f, wide %.2f; level narrow/wide %.2f dB\n", correlation (n), correlation (w),
                 10.0 * std::log10 ((energy (n.l) + energy (n.r)) / (energy (w.l) + energy (w.r))));
    CHECK (correlation (n) > 0.95);
    CHECK (correlation (w) < 0.6);
    CHECK_NEAR (10.0 * std::log10 ((energy (n.l) + energy (n.r)) / (energy (w.l) + energy (w.r))), 0.0, 1.0);

    // Space shrinks the whole stage.
    auto small = st;
    small.spread = 0.25f;
    CHECK (std::abs (stereoPosition (runStage (small, Soprano, bus))) < std::abs (sop) * 0.6);
}

/** Delay of y against x in samples (cross-correlation peak up to maxLag). */
static int lagOf (const std::vector<float>& x, const std::vector<float>& y, int maxLag)
{
    int best = 0;
    double bestV = -1.0e30;
    for (int lag = 0; lag <= maxLag; ++lag)
    {
        double v = 0.0;
        for (size_t i = 4800; i + (size_t) lag < y.size(); ++i) v += (double) x[i] * y[i + (size_t) lag];
        if (v > bestV) { bestV = v; best = lag; }
    }
    return best;
}

/** Share of the energy above 6 kHz (one-pole split, enough to compare). */
static double brightness (const std::vector<float>& x, double sr)
{
    OnePoleLP lp;
    lp.setCutoff (sr, 6000.0);
    double hi = 0.0, all = 0.0;
    for (size_t i = 4800; i < x.size(); ++i) { const float low = lp.process (x[i]); hi += (double) (x[i] - low) * (x[i] - low); all += (double) x[i] * x[i]; }
    return hi / (all + 1.0e-20);
}

TEST (backRowsSoundFurtherAway)
{
    const double sr = 48000.0;
    const auto bus = sectionNoise (48000, 11);
    ChoirStage::Settings st;
    st.spread = 1.0f;
    for (auto& p : st.sections) p = { 0.0f, 0.4f, 0.0f };
    auto back = st;
    back.sections[Tenor].depth = 1.0f;   // the tenors at the back wall, everyone else at the front
    const auto front = runStage (st, Tenor, bus), far = runStage (back, Tenor, bus);
    const double levelDb = 10.0 * std::log10 ((energy (far.l) + energy (far.r)) / (energy (front.l) + energy (front.r)));
    const int lag = lagOf (front.l, far.l, 400);
    std::printf ("  back wall against front: %.1f dB, %.2f ms later, brightness %.3f vs %.3f\n", levelDb, lag / sr * 1000.0,
                 brightness (far.l, sr), brightness (front.l, sr));
    CHECK (levelDb < -2.0 && levelDb > -6.5);   // further away, and (on this white noise) the lost highs
    CHECK_NEAR (lag / sr * 1000.0, 4.5, 0.2);
    CHECK (brightness (far.l, sr) < brightness (front.l, sr) * 0.85);
    ChoirStage probe;
    probe.setSettings (back);
    probe.prepare (sr, 64);
    CHECK_NEAR (probe.sectionDelaySeconds (Tenor) * 1000.0, 4.5, 0.01);
    CHECK (probe.sectionDelaySeconds (Soprano) == 0.0f);

    // The spot mics stand close to every row: the back row is as loud and bright as the front.
    auto closeOnly = [] (ChoirStage::Settings s) { s.close = 1.0f; s.tree = 0.0f; return s; };
    const auto cf = runStage (closeOnly (st), Tenor, bus), cb = runStage (closeOnly (back), Tenor, bus);
    const double closeDb = 10.0 * std::log10 ((energy (cb.l) + energy (cb.r)) / (energy (cf.l) + energy (cf.r)));
    std::printf ("  close mics, back wall against front: %.1f dB\n", closeDb);
    CHECK (std::abs (closeDb) < 0.3);
}

TEST (micsPickUpTheChoirDifferently)
{
    const double sr = 48000.0;
    const auto bus = sectionNoise (48000, 3);
    ChoirStage::Settings st;
    for (int s = 0; s < NumSections; ++s)
    {
        const auto d = defaultSectionPlacement (s);
        st.sections[(size_t) s] = { d.pan, d.width, d.depth };
    }
    auto only = [&st] (float c, float t, float r) { auto s = st; s.close = c; s.tree = t; s.room = r; return s; };
    const auto tree = runStage (only (0, 1, 0), Soprano, bus), close = runStage (only (1, 0, 0), Soprano, bus), room = runStage (only (0, 0, 1), Soprano, bus);
    // The room pair is far out in the hall: the choir reaches it ~20 ms later, darker and nearer the middle.
    const int roomLag = lagOf (tree.l, room.l, 1500);
    std::printf ("  room mics: %.1f ms later, brightness %.3f (tree %.3f, close %.3f), position %.2f (tree %.2f)\n",
                 roomLag / sr * 1000.0, brightness (room.l, sr), brightness (tree.l, sr), brightness (close.l, sr),
                 stereoPosition (room), stereoPosition (tree));
    CHECK (roomLag / sr * 1000.0 > 15.0 && roomLag / sr * 1000.0 < 20.0);
    CHECK (brightness (room.l, sr) < brightness (tree.l, sr) * 0.7);
    CHECK (brightness (close.l, sr) > brightness (tree.l, sr));   // presence on the spot mics
    CHECK (std::abs (stereoPosition (room)) < std::abs (stereoPosition (tree)));

    // How much of the hall each pair hears, and the hall's input stays at the tree's level.
    ChoirStage hall;
    hall.setSettings (only (0, 1, 0));
    hall.prepare (sr, 64);
    hall.begin (64);
    CHECK_NEAR (hall.reverbAmount(), 1.0, 1.0e-4);
    CHECK_NEAR (hall.reverbInputScale(), 1.0, 1.0e-4);
    hall.setSettings (only (1, 0, 0));
    for (int i = 0; i < 400; ++i) hall.begin (64);
    CHECK (hall.reverbAmount() < 0.4f);
    hall.setSettings (only (0, 0, 1));
    for (int i = 0; i < 400; ++i) hall.begin (64);
    CHECK (hall.reverbAmount() > 1.7f);
    hall.setSettings (only (0, 0, 0));
    for (int i = 0; i < 400; ++i) hall.begin (64);
    CHECK (hall.reverbInputScale() == 0.0f);
}

TEST (renderedTakeCanBeMovedOnStage)
{
    // The stage comes after the rendered stems, so a rendered take can be rearranged and the
    // mics changed without rendering again.
    const double sr = 48000.0;
    auto x = test::singMelody (test::amazingGrace(), sr, Tenor, 25.0f);
    x.resize ((size_t) (sr * 6.0));
    const auto take = analyseTake (x.data(), (int64_t) x.size(), sr);
    auto p = dryParams (VocalType::Ah, HarmonyMode::FullChoir);
    p.key = 0;
    const auto score = harmoniseTake (take, HarmonySettings {});
    RenderedStems stems;
    CHECK (renderTake (x.data(), (int64_t) x.size(), sr, p, score, stems));
    auto balance = [] (const std::array<std::vector<float>, 2>& m)
    {
        double l = 0, r = 0;
        for (size_t i = 0; i < m[0].size(); ++i) { l += (double) m[0][i] * m[0][i]; r += (double) m[1][i] * m[1][i]; }
        return 10.0 * std::log10 ((r + 1.0e-12) / (l + 1.0e-12));   // dB right of left
    };
    auto sopOnly = p;
    for (int s = 1; s < NumSections; ++s) sopOnly.sections[(size_t) s].mute = true;
    const double home = balance (mixdownStems (stems, sopOnly, false, 0.2f));
    sopOnly.sections[Soprano].pan = 0.8f;
    const double moved = balance (mixdownStems (stems, sopOnly, false, 0.2f));
    std::printf ("  sopranos: %.1f dB right of left at home, %.1f dB once moved right\n", home, moved);
    CHECK (home < -3.0);
    CHECK (moved > 3.0);
}

TEST (performanceBudget)
{
    const double sr = 48000.0;
    auto x = test::singMelody (test::cMajorScale (57.0f, 0.5f), sr, Tenor, 25.0f);
    for (VocalType vt : { VocalType::Words, VocalType::Ah })
    {
        ChoirEngine eng;
        eng.prepare (sr, 256);
        EngineParams p;
        p.vocalType = vt;
        p.choirSize = 100.0f;
        eng.setParams (p);
        const auto t0 = std::chrono::steady_clock::now();
        auto y = runEngine (eng, x, sr);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        const double audio = (double) x.size() / sr;
        std::printf ("  %d singers (%s): %.1fx real time\n", kMaxSingers, vt == VocalType::Words ? "words" : "ah", audio / secs);
        CHECK (audio / secs > 3.0);
    }
}

int main (int argc, char** argv)
{
    const char* only = argc > 1 ? argv[1] : nullptr;
    for (auto& [name, fn] : registry())
    {
        if (only != nullptr && std::string (name).find (only) == std::string::npos)
            continue;
        std::printf ("[ %s ]\n", name);
        const int before = failures;
        fn();
        std::printf ("  %s\n", failures == before ? "ok" : "FAILED");
    }
    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
