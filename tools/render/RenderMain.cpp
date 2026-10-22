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
//     --synth-demo         ignore the input and sing a demo melody (writes it next to the output)
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
        std::fprintf (stderr, "usage: %s input.wav output.wav [--learn] [--voice ah|words|...] [--harmony full|...] [--style sacred|...] [--key auto|C|...] [--size 70] [--reverb 65] [--lead 0] [--stems DIR] [--voices DIR] [--synth-demo]\n", argv[0]);
        return 2;
    }

    const std::string inPath = argv[1], outPath = argv[2];
    EngineParams p;
    bool learn = false, demo = false;
    std::string stemsDir, voicesDir;

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
        else if (a == "--scale") p.scale = (ScaleType) indexOf (scales, next(), 0);
        else if (a == "--size") p.choirSize = std::stof (next());
        else if (a == "--reverb") p.reverb = std::stof (next());
        else if (a == "--lead") p.leadDb = std::stof (next());
        else if (a == "--stems") stemsDir = next();
        else if (a == "--voices") voicesDir = next();
        else { std::fprintf (stderr, "unknown option %s\n", a.c_str()); return 2; }
    }

    test::Audio in;
    std::string err;
    if (demo)
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
            recorded.addRecording (section, std::max (0, SampleSet::vowelFromPath (rel)), m.data(), (int64_t) m.size(), rec.sampleRate);
        }
        recorded.finalise();
        std::printf ("recorded voices: %s\n", recorded.describe().c_str());
    }
    const SampleSet* voicesPtr = recorded.isEmpty() ? nullptr : &recorded;

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
        hs.mode = p.harmonyMode;
        hs.style = p.style;
        hs.wordsMode = p.vocalType == VocalType::Words;
        const auto score = harmoniseTake (take, hs);
        std::printf ("%s\n", score.describe().c_str());

        RenderedStems stems;
        if (! renderTake (mono.data(), n, sr, p, score, stems, [] (float f) { std::printf ("\rrendering %3d%%", (int) (f * 100)); std::fflush (stdout); }, nullptr, voicesPtr))
        {
            std::fprintf (stderr, "render failed\n");
            return 1;
        }
        std::printf ("\n");
        auto mix = mixdownStems (stems, p, false);
        const float lead = p.leadDb <= -59.5f ? 0.0f : dbToGain (p.leadDb);
        for (int c = 0; c < 2; ++c)
            for (int64_t i = 0; i < n && i < (int64_t) mix[(size_t) c].size(); ++i)
                mix[(size_t) c][(size_t) i] += lead * in.channels[(size_t) std::min<int> (c, (int) in.channels.size() - 1)][(size_t) i];
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
        const int lat = eng.getLatencySamples();
        const int64_t total = n + lat + (int64_t) (sr * 4.0);
        std::vector<float> l ((size_t) total), r ((size_t) total), x ((size_t) total, 0.0f), xr ((size_t) total, 0.0f);
        for (int64_t i = 0; i < n; ++i)
        {
            x[(size_t) i] = in.channels[0][(size_t) i];
            xr[(size_t) i] = in.channels[std::min<size_t> (1, in.channels.size() - 1)][(size_t) i];
        }
        for (int64_t pos = 0; pos < total; pos += 512)
        {
            const int len = (int) std::min<int64_t> (512, total - pos);
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
