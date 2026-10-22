// Sanctuary Choir DSP tests (no framework dependency: a tiny CHECK harness).
#include "TestSignals.h"

#include <sanctuary/ChoirEngine.h>
#include <sanctuary/FFT.h>
#include <sanctuary/Learn.h>

#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <complex>
#include <cstdio>
#include <functional>
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
    const int lat = eng.getLatencySamples();
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
    for (VocalType vt : { VocalType::Words, VocalType::Ah })
    {
        ChoirEngine eng;
        eng.prepare (sr, 256);
        auto p = dryParams (vt, HarmonyMode::FullChoir);
        p.response = 1;
        eng.setParams (p);
        auto y = runEngine (eng, x, sr);
        auto rms = [&] (double t0, double t1)
        {
            double e = 0; size_t n = 0;
            for (size_t i = (size_t) (t0 * sr); i < (size_t) (t1 * sr) && i < y.size(); ++i, ++n) e += (double) y[i] * y[i];
            return n ? std::sqrt (e / (double) n) : 0.0;
        };
        const double during = rms (0.3, 2.0), after = rms (2.7, 4.2);
        std::printf ("  echo (%s): during phrase %.4f, after %.4f\n", vt == VocalType::Words ? "words" : "ah", during, after);
        CHECK (during < 0.01);
        CHECK (after > 0.03);
        // The echo carries the melody: its pitch follows the phrase (C then E in some octave).
        const float pitch = medianPitch (y, sr, 2.8, 3.6);
        CHECK (pitch > 0.0f);
    }
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
