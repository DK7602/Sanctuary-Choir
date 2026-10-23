// sanctuary_render: run the Sanctuary Choir engine over a WAV file from the command line.
//
//   sanctuary_render input.wav output.wav [options]
//     --learn              learn the whole take first (smarter harmony), then render
//     --voice NAME         ah | oh | oo | ee | eh | mm | words      (default ah)
//     --harmony NAME       full | gospel | open | unison | thirds | organum | upper | lower | drone
//     --style NAME         sacred | gospel | gregorian | cinematic | worship | chamber | spiritual
//     --key NAME           auto | C | C# | D | Eb | E | F | F# | G | Ab | A | Bb | B
//     --scale NAME         major | minor | harmonic | dorian | mixolydian | lydian | phrygian | gospel | chromatic
//     --size PCT           choir size 0..100 (default 70)
//     --reverb PCT         reverb 0..100 (default 65)
//     --lead DB            lead vocal level in dB (default 0, -60 = off)
//     --stems DIR          also write the dry section stems (learn mode)
//     --mics C,T,R         close, tree and room microphone levels in dB (-60 = off; default -60,0,-60)
//     --formation NAME     blocks | across | mencentre | mixed
//     --singing NAME       natural | breathy | belt  (recorded voices: their breathy or belted takes)
//     --words TEXT         ignore the input: play chords over MIDI, one per syllable, and sing TEXT
//     --real-voices PCT    Words mode: how far each singer takes on a real singer's voice (default 100)
//     --pass FILE          learn mode: another pass of the same part (up to 3); lined up with the
//                          input, and the Words choir sings from every pass ("demo" with
//                          --synth-demo: the demo melody sung again)
//     --synth-demo         ignore the input and sing a demo melody (writes it next to the output)
//     --venue NAME         cathedral | abbey | chapel | concert | gospel | studio  (default cathedral)
//     --rooms DIR          the venues' measured rooms as WAV (<slug>.wav, from tools/rooms/build_rooms.py
//                          --wav); without it the algorithmic reverb plays the venue
#include "TestSignals.h"

#include <sanctuary/Learn.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>

using namespace sc;

static int indexOf (const std::map<std::string, int>& m, const std::string& v, int fallback)
{
    std::string k = v;
    for (auto& c : k) c = (char) std::tolower ((unsigned char) c);
    auto it = m.find (k);
    return it == m.end() ? fallback : it->second;
}

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: %s input.wav output.wav [--learn] [--voice ah|words|...] [--harmony full|...] [--style sacred|...] [--key auto|C|...] [--size 70] [--reverb 65] [--lead 0] [--stems DIR] [--voices DIR] [--ensemble choir|children|gang|congregation] [--rhythm 0-14] [--mics C,T,R] [--formation blocks|across|mencentre|mixed] [--singing natural|breathy|belt] [--words TEXT] [--real-voices 100] [--energy 50] [--sub 0] [--lead-stage X,D] [--pass FILE] [--venue cathedral|abbey|chapel|concert|gospel|studio] [--rooms DIR] [--synth-demo]\n"
                              "  --scale also takes auto (major or minor, heard); --lead-stage puts your voice on the stage (X -1..1, D 0..1)\n", argv[0]);
        return 2;
    }

    const std::string inPath = argv[1], outPath = argv[2];
    EngineParams p;
    bool learn = false, demo = false;
    std::string words;
    std::string stemsDir, voicesDir, roomsDir;
    std::vector<std::string> passPaths;

    const std::map<std::string, int> voices { { "ah", 0 }, { "oh", 1 }, { "oo", 2 }, { "ee", 3 }, { "eh", 4 }, { "mm", 5 }, { "words", 6 } };
    const std::map<std::string, int> modes { { "full", 0 }, { "gospel", 1 }, { "open", 2 }, { "unison", 3 }, { "thirds", 4 }, { "organum", 5 }, { "upper", 6 }, { "lower", 7 }, { "drone", 8 } };
    const std::map<std::string, int> styles { { "sacred", 0 }, { "gospel", 1 }, { "gregorian", 2 }, { "cinematic", 3 }, { "worship", 4 }, { "chamber", 5 }, { "spiritual", 6 } };
    const std::map<std::string, int> keys { { "auto", 0 }, { "c", 1 }, { "c#", 2 }, { "db", 2 }, { "d", 3 }, { "eb", 4 }, { "d#", 4 }, { "e", 5 }, { "f", 6 }, { "f#", 7 }, { "gb", 7 }, { "g", 8 }, { "ab", 9 }, { "g#", 9 }, { "a", 10 }, { "bb", 11 }, { "a#", 11 }, { "b", 12 } };
    const std::map<std::string, int> scales { { "major", 0 }, { "minor", 1 }, { "harmonic", 2 }, { "dorian", 3 }, { "mixolydian", 4 }, { "lydian", 5 }, { "phrygian", 6 }, { "gospel", 7 }, { "chromatic", 8 } };

    for (int i = 3; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&] { return i + 1 < argc ? std::string (argv[++i]) : std::string(); };
        if (a == "--learn") learn = true;
        else if (a == "--synth-demo") demo = true;
        else if (a == "--voice") p.vocalType = (VocalType) indexOf (voices, next(), 0);
        else if (a == "--harmony") p.harmonyMode = (HarmonyMode) indexOf (modes, next(), 0);
        else if (a == "--style") p.style = (Style) indexOf (styles, next(), 0);
        else if (a == "--key") p.key = indexOf (keys, next(), 0);
        else if (a == "--scale")
        {
            const std::string v = next();
            p.scaleAuto = v == "auto";
            p.scale = (ScaleType) indexOf (scales, v, 0);
        }
        else if (a == "--energy") p.energy = std::stof (next());
        else if (a == "--sub") p.subOctave = std::stof (next());
        else if (a == "--lead-stage")
        {
            float x = 0.0f, d = 0.0f;
            if (std::sscanf (next().c_str(), "%f,%f", &x, &d) == 2) { p.leadOnStage = true; p.leadPan = x; p.leadDepth = d; }
        }
        else if (a == "--size") p.choirSize = std::stof (next());
        else if (a == "--reverb") p.reverb = std::stof (next());
        else if (a == "--lead") p.leadDb = std::stof (next());
        else if (a == "--stems") stemsDir = next();
        else if (a == "--voices") voicesDir = next();
        else if (a == "--rooms") roomsDir = next();
        else if (a == "--venue") p.venue = (Venue) indexOf ({ { "cathedral", 0 }, { "abbey", 1 }, { "chapel", 2 }, { "concert", 3 }, { "gospel", 4 }, { "studio", 5 } }, next(), 0);
        else if (a == "--ensemble") p.ensemble = (Ensemble) indexOf ({ { "choir", 0 }, { "children", 1 }, { "gang", 2 }, { "congregation", 3 } }, next(), 0);
        else if (a == "--words") words = next();
        else if (a == "--real-voices") p.realVoices = std::stof (next());
        else if (a == "--pass") passPaths.push_back (next());
        else if (a == "--singing") p.singing = (Singing) indexOf ({ { "natural", 0 }, { "breathy", 1 }, { "belt", 2 } }, next(), 0);
        else if (a == "--rhythm") p.rhythmPattern = std::clamp (std::stoi (next()), 0, kNumRhythmPatterns - 1);
        else if (a == "--mics")
        {
            float c = -60.0f, t = 0.0f, r = -60.0f;
            if (std::sscanf (next().c_str(), "%f,%f,%f", &c, &t, &r) == 3) { p.micCloseDb = c; p.micTreeDb = t; p.micRoomDb = r; }
        }
        else if (a == "--formation")
        {
            // x, half-width, depth for S, A, T, B (as the Stage page's formation buttons).
            static const float shapes[4][4][3] = {
                { { -0.55f, 0.42f, 0.25f }, { 0.55f, 0.42f, 0.25f }, { -0.42f, 0.40f, 0.75f }, { 0.42f, 0.40f, 0.75f } },
                { { -0.75f, 0.24f, 0.45f }, { -0.25f, 0.24f, 0.45f }, { 0.25f, 0.24f, 0.55f }, { 0.75f, 0.24f, 0.55f } },
                { { -0.62f, 0.36f, 0.30f }, { 0.62f, 0.36f, 0.30f }, { -0.2f, 0.20f, 0.70f }, { 0.2f, 0.20f, 0.70f } },
                { { 0.0f, 0.95f, 0.30f }, { 0.0f, 0.95f, 0.45f }, { 0.0f, 0.95f, 0.60f }, { 0.0f, 0.95f, 0.75f } } };
            const int f = indexOf ({ { "blocks", 0 }, { "across", 1 }, { "mencentre", 2 }, { "mixed", 3 } }, next(), 0);
            for (int s = 0; s < NumSections; ++s)
            {
                p.sections[(size_t) s].pan = shapes[f][s][0];
                p.sections[(size_t) s].width = shapes[f][s][1];
                p.sections[(size_t) s].depth = shapes[f][s][2];
            }
        }
        else { std::fprintf (stderr, "unknown option %s\n", a.c_str()); return 2; }
    }

    test::Audio in;
    std::string err;
    const auto script = LyricScript::make (Lyrics::parse (words));
    const double sylSeconds = 0.8;
    if (! words.empty())
    {
        std::printf ("words: %s\n", script.text.c_str());
        in.sampleRate = 48000.0;
        in.channels = { std::vector<float> ((size_t) ((double) script.entries.size() * sylSeconds * in.sampleRate + in.sampleRate), 0.0f) };
    }
    else if (demo)
    {
        in.sampleRate = 48000.0;
        in.channels = { test::singMelody (test::amazingGrace(), in.sampleRate, Tenor) };
        test::writeWav (outPath + ".input.wav", in);
    }
    else if (! test::readWav (inPath, in, err))
    {
        std::fprintf (stderr, "error: %s\n", err.c_str());
        return 1;
    }

    // Recorded choir voices: every audio file under the folder, sorted by its name.
    SampleSet recorded;
    if (! voicesDir.empty())
    {
        namespace fs = std::filesystem;
        for (const auto& entry : fs::recursive_directory_iterator (voicesDir))
        {
            if (! entry.is_regular_file() || entry.path().extension() != ".wav")
                continue;
            const std::string rel = fs::relative (entry.path(), voicesDir).string();
            const int section = SampleSet::sectionFromPath (rel);
            if (section < 0)
                continue;
            test::Audio rec;
            std::string e;
            if (! test::readWav (entry.path().string(), rec, e))
                continue;
            std::vector<float> m (rec.channels[0].size(), 0.0f);
            for (const auto& ch : rec.channels)
                for (size_t i = 0; i < m.size(); ++i)
                    m[i] += ch[i] / (float) rec.channels.size();
            const int vowel = std::max (0, SampleSet::vowelFromPath (rel));
            if (const float root = SampleSet::rootFromPath (rel); root > 0.0f)
                recorded.addNote (section, vowel, m.data(), (int64_t) m.size(), rec.sampleRate, root, SampleSet::infoFromPath (rel));
            else
                recorded.addRecording (section, vowel, m.data(), (int64_t) m.size(), rec.sampleRate);
        }
        recorded.finalise();
        std::printf ("recorded voices: %s\n", recorded.describe().c_str());
    }
    const SampleSet* voicesPtr = recorded.isEmpty() ? nullptr : &recorded;

    // The venue's measured room.
    RoomImpulse roomImpulse;
    if (! roomsDir.empty())
    {
        test::Audio ra;
        std::string e;
        const std::string path = roomsDir + "/" + getVenueSlug (p.venue) + ".wav";
        if (! test::readWav (path, ra, e) || ra.channels.empty())
        {
            std::fprintf (stderr, "error: %s: %s\n", path.c_str(), e.c_str());
            return 1;
        }
        roomImpulse.sampleRate = ra.sampleRate;
        roomImpulse.left = ra.channels[0];
        roomImpulse.right = ra.channels[std::min<size_t> (1, ra.channels.size() - 1)];
        normaliseRoom (roomImpulse);   // as the plug-in plays it
        std::printf ("room: %s (%.1f s)\n", path.c_str(), (double) ra.length() / ra.sampleRate);
    }
    const RoomImpulse* roomPtr = roomImpulse.left.empty() ? nullptr : &roomImpulse;

    const double sr = in.sampleRate;
    const int64_t n = in.length();
    std::vector<float> mono ((size_t) n, 0.0f);
    for (const auto& ch : in.channels)
        for (int64_t i = 0; i < n; ++i)
            mono[(size_t) i] += ch[(size_t) i] / (float) in.channels.size();

    test::Audio out;
    out.sampleRate = sr;

    if (learn)
    {
        const auto take = analyseTake (mono.data(), n, sr, p.a4);
        HarmonySettings hs;
        hs.key = p.key;
        hs.scale = p.scale;
        hs.scaleAuto = p.scaleAuto;
        hs.mode = p.harmonyMode;
        hs.style = p.style;
        hs.wordsMode = p.vocalType == VocalType::Words;
        const auto score = harmoniseTake (take, hs);
        std::printf ("%s\n", score.describe().c_str());

        // More passes of the part, each lined up with the input.
        std::vector<std::vector<float>> passes;
        for (const auto& path : passPaths)
        {
            if ((int) passes.size() >= kMaxExtraPasses)
                break;
            std::vector<float> raw;
            if (path == "demo" && demo)
            {
                auto notes = test::amazingGrace();
                for (size_t k = 0; k < notes.size(); ++k)
                    notes[k].seconds *= 1.0f + 0.04f * (float) ((((int) k + (int) passes.size()) % 5) - 2);
                raw = test::singMelody (notes, sr, Tenor, 30.0f, 21 + 12 * (uint32_t) passes.size());
            }
            else
            {
                test::Audio pa;
                std::string e;
                if (! test::readWav (path, pa, e) || std::abs (pa.sampleRate - sr) > 1.0)
                {
                    std::fprintf (stderr, "error: %s: %s\n", path.c_str(), e.empty() ? "not at the input's sample rate" : e.c_str());
                    return 1;
                }
                raw.assign ((size_t) pa.length(), 0.0f);
                for (const auto& ch : pa.channels)
                    for (size_t i = 0; i < raw.size(); ++i)
                        raw[i] += ch[i] / (float) pa.channels.size();
            }
            PassAlignment info;
            passes.push_back (alignPass (mono.data(), n, raw.data(), (int64_t) raw.size(), sr, nullptr, &info));
            std::printf ("pass %d: starts at %+.2f s, sings %.0f%% of the take, moved up to %.2f s, %+.1f dB\n", (int) passes.size() + 1,
                         (double) info.offset / sr, info.coverage * 100.0, info.drift, info.gainDb);
        }
        std::vector<const float*> passAudio;
        for (const auto& pa : passes)
            passAudio.push_back (pa.data());

        RenderedStems stems;
        if (! renderTake (mono.data(), n, sr, p, score, stems, [] (float f) { std::printf ("\rrendering %3d%%", (int) (f * 100)); std::fflush (stdout); }, nullptr, voicesPtr, passAudio))
        {
            std::fprintf (stderr, "render failed\n");
            return 1;
        }
        std::printf ("\n");
        // On the stage your voice goes through the engine with the choir, in its place;
        // otherwise it is added dry, as it was recorded.
        auto mix = mixdownStems (stems, p, false, 4.0f, p.leadOnStage ? mono.data() : nullptr, p.leadOnStage ? n : 0, roomPtr);
        const float lead = p.leadOnStage || p.leadDb <= -59.5f ? 0.0f : dbToGain (p.leadDb);
        for (int c = 0; c < 2; ++c)
            for (int64_t i = 0; i < n && i < (int64_t) mix[(size_t) c].size(); ++i)
                mix[(size_t) c][(size_t) i] += lead * in.channels[(size_t) std::min<int> (c, (int) in.channels.size() - 1)][(size_t) i];
        // The lead is added after the choir's limiter here: keep the sum from clipping the file.
        float peak = 0.0f;
        for (const auto& ch : mix)
            for (float v : ch) peak = std::max (peak, std::abs (v));
        if (peak > 0.97f)
            for (auto& ch : mix)
                for (float& v : ch) v *= 0.97f / peak;
        out.channels = { mix[0], mix[1] };

        if (! stemsDir.empty())
        {
            static const char* names[] = { "soprano", "alto", "tenor", "bass" };
            for (int s = 0; s < NumSections; ++s)
            {
                test::Audio st;
                st.sampleRate = sr;
                st.channels = { stems.channels[(size_t) s * 2], stems.channels[(size_t) s * 2 + 1] };
                test::writeWav (stemsDir + "/" + names[s] + ".wav", st);
            }
        }
    }
    else
    {
        ChoirEngine eng;
        eng.prepare (sr, 512);
        eng.setParams (p);
        eng.setSampleSet (voicesPtr);
        const auto room = roomPtr != nullptr ? ConvolutionRoom::create (*roomPtr, sr) : nullptr;
        eng.setRoom (room.get());
        const int lat = eng.getLatencySamples() + eng.getOutputDelay();
        const int64_t total = n + lat + (int64_t) (sr * 4.0);
        if (! words.empty())
            eng.setLyrics (&script);
        // --words: a hymn-like progression in C, one chord per syllable, re-sung each time.
        static const int chords[][3] = { { 60, 64, 67 }, { 60, 65, 69 }, { 59, 62, 67 }, { 60, 64, 67 }, { 57, 60, 64 }, { 57, 62, 65 }, { 55, 59, 62 }, { 55, 60, 64 } };
        const int64_t sylLen = (int64_t) (sylSeconds * sr), gap = (int64_t) (0.05 * sr);
        std::vector<float> l ((size_t) total), r ((size_t) total), x ((size_t) total, 0.0f), xr ((size_t) total, 0.0f);
        for (int64_t i = 0; i < n; ++i)
        {
            x[(size_t) i] = in.channels[0][(size_t) i];
            xr[(size_t) i] = in.channels[std::min<size_t> (1, in.channels.size() - 1)][(size_t) i];
        }
        for (int64_t pos = 0; pos < total; pos += 512)
        {
            const int len = (int) std::min<int64_t> (512, total - pos);
            if (! words.empty())
            {
                // Each chord is a new note; before a held syllable ("_") they join up.
                const int64_t k = pos / sylLen, into = pos % sylLen;
                const auto count = (int64_t) script.entries.size();
                const bool tied = k + 1 < count && script.entries[(size_t) (k + 1)].hold;
                const bool on = k < count && (tied || into < sylLen - gap);
                NoteSet held;
                if (on)
                    for (int note : chords[k % 8])
                        held.add (note);
                eng.setMidi (held, 0.75f, on && into < 512 ? 1 : 0);
            }
            eng.process (x.data() + pos, xr.data() + pos, l.data() + pos, r.data() + pos, len);
        }
        out.channels = { std::vector<float> (l.begin() + lat, l.end()), std::vector<float> (r.begin() + lat, r.end()) };
    }

    if (! test::writeWav (outPath, out))
    {
        std::fprintf (stderr, "cannot write %s\n", outPath.c_str());
        return 1;
    }
    std::printf ("wrote %s\n", outPath.c_str());
    return 0;
}
