#include "AiSingers.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <set>
#include <thread>

namespace sanctuary::ai
{
namespace
{
    /** Letters and digits only, lower case: "VocalSet Male 3", "vocalset_male3" and "vocalset-male3" agree. */
    juce::String loose (const juce::String& s)
    {
        return s.toLowerCase().retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789");
    }

    /** The titles VoxaVersa's singer cards give its classically trained singers. */
    juce::String cardTitle (const juce::String& looseId)
    {
        static const std::map<juce::String, juce::String> titles {
            { "vocalsetfemale1", "Lyric Soprano" },   { "vocalsetfemale2", "Crystal Soprano" },
            { "vocalsetfemale3", "Silver Soprano" },  { "vocalsetfemale4", "Velvet Mezzo" },
            { "vocalsetfemale5", "Pearl Soprano" },   { "vocalsetfemale6", "Halo Soprano" },
            { "vocalsetfemale7", "Cathedral Soprano" }, { "vocalsetfemale8", "Aria Diva" },
            { "vocalsetfemale9", "Stage Soprano" },   { "vocalsetmale1", "Opera Baritone" },
            { "vocalsetmale2", "Concert Baritone" },  { "vocalsetmale3", "Lyric Tenor" },
            { "vocalsetmale4", "Broadway Baritone" }, { "vocalsetmale5", "Chamber Tenor" },
            { "vocalsetmale6", "Velvet Baritone" },   { "vocalsetmale7", "Falsetto Tenor" },
            { "vocalsetmale8", "Basso Profundo" },    { "vocalsetmale9", "Countertenor" },
            { "vocalsetmale10", "Hushed Tenor" },     { "vocalsetmale11", "Clarion Tenor" }
        };
        const auto it = titles.find (looseId);
        return it != titles.end() ? it->second : juce::String();
    }

    /** The voice assignment entry for a pack (by folder name or id, then loosely), or a void var. */
    juce::var assignmentFor (const juce::var& packs, const juce::String& folder, const juce::String& id, const juce::String& name)
    {
        auto* obj = packs.getDynamicObject();
        if (obj == nullptr)
            return {};
        for (const auto& entry : obj->getProperties())
            if (entry.name.toString() == folder || entry.name.toString() == id)
                return entry.value;
        const auto f = loose (folder), i = loose (id), n = loose (name);
        for (const auto& entry : obj->getProperties())
        {
            const auto key = loose (entry.name.toString());
            if (key == f || key == i || (n.isNotEmpty() && loose (entry.value.getProperty ("name", {}).toString()) == n))
                return entry.value;
        }
        return {};
    }

    /** 64-bit FNV-1a, for the cache's keys. */
    struct Hash
    {
        uint64_t h = 1469598103934665603ull;
        void bytes (const void* data, size_t n)
        {
            auto* p = static_cast<const uint8_t*> (data);
            for (size_t i = 0; i < n; ++i)
                h = (h ^ p[i]) * 1099511628211ull;
        }
        template <typename T> void add (const T& v) { bytes (&v, sizeof (v)); }
        void add (const juce::String& s) { bytes (s.toRawUTF8(), s.getNumBytesAsUTF8()); }
    };

    bool isModernStyle (sc::Style s)
    {
        return s == sc::Style::Gospel || s == sc::Style::Worship || s == sc::Style::Spiritual || s == sc::Style::Cinematic;
    }
}

//==============================================================================
/** May this singer be given to someone else? Only voices trained on recordings whose licence allows it:
    VocalSet's singers (CC BY 4.0), or a pack whose licence says CC BY or CC0. Never singers from sample packs
    in someone's own library, or from recordings whose singers never agreed (the band singers). */
bool isShareable (const juce::String& licenceAndDescription)
{
    const auto t = licenceAndDescription.toLowerCase();
    for (auto* no : { "owner's library", "owners library", "personal use", "testing only", "do not share", "military",
                      "band member", "not for sale", "private" })
        if (t.contains (no))
            return false;
    return t.contains ("vocalset") || t.contains ("cc by") || t.contains ("cc-by") || t.contains ("cc0");
}

static void findSingersIn (const juce::File& voicesDir, bool fromVoxaVersa, std::vector<Singer>& out)
{
    if (! voicesDir.isDirectory())
        return;

    const auto assignment = juce::JSON::parse (voicesDir.getChildFile ("voice_assignment.json"));
    const auto packs = assignment.getProperty ("packs", {});

    for (const auto& dir : voicesDir.findChildFiles (juce::File::findDirectories, false))
    {
        const auto json = juce::JSON::parse (dir.getChildFile ("voice.json"));
        auto* obj = json.getDynamicObject();
        auto* model = json.getProperty ("model", {}).getDynamicObject();
        if (obj == nullptr || model == nullptr || ! dir.getChildFile (model->getProperty ("file").toString()).existsAsFile()
            || model->getProperty ("file").toString().isEmpty())
            continue;

        Singer s;
        s.folder = dir;
        s.id = dir.getFileName();
        const auto packId = json.getProperty ("id", s.id).toString();
        const auto packName = json.getProperty ("name", s.id).toString();
        const auto entry = assignmentFor (packs, s.id, packId, packName);

        s.name = entry.getProperty ("name", packName).toString();
        if (const auto title = cardTitle (loose (packId)); title.isNotEmpty())
            s.name = title;
        else if (loose (s.name).contains ("studiofemaleblend"))
            s.name = "Studio Diva";
        s.female = json.getProperty ("gender", "").toString().equalsIgnoreCase ("female");
        s.licence = json.getProperty ("license", "").toString();
        s.fromVoxaVersa = fromVoxaVersa;
        s.shareable = isShareable (s.licence + " " + json.getProperty ("description", "").toString() + " "
                                   + (loose (packId).startsWith ("vocalset") ? "VocalSet" : ""));
        bool already = false;
        for (const auto& o : out)
            already = already || o.id == s.id;
        if (already)
            continue;   // the same singer in VoxaVersa and in Sanctuary's folder: VoxaVersa's (newer) one

        const auto style = json.getProperty ("style", "").toString();
        s.classical = style.equalsIgnoreCase ("classical") || loose (packId).startsWith ("vocalset");

        const juce::var range = entry.getProperty ("range", json.getProperty ("range", {}));
        if (auto* r = range.getArray(); r != nullptr && r->size() == 2)
        {
            s.low = (int) (*r)[0];
            s.high = (int) (*r)[1];
        }
        else
        {
            s.low = s.female ? 55 : 45;
            s.high = s.female ? 77 : 67;
        }
        if (s.high < s.low)
            std::swap (s.low, s.high);
        const juce::var median = entry.getProperty ("median", json.getProperty ("median", {}));
        s.median = median.isVoid() ? 0.5f * (float) (s.low + s.high) : (float) (double) median;
        s.assignment = entry;
        out.push_back (s);
    }
}

std::vector<Singer> findSingers()
{
    std::vector<Singer> out;
    findSingersIn (getVoicesDirectory(), true, out);
    findSingersIn (getSanctuaryVoicesDirectory(), false, out);
    std::sort (out.begin(), out.end(), [] (const Singer& a, const Singer& b) { return a.name.compareNatural (b.name) < 0; });
    return out;
}

//==============================================================================
static const char* sharePackCredits()
{
    return "Sanctuary Choir - AI singers\n"
           "============================\n\n"
           "Singers that may be shared: voice models trained for VoxaVersa on recordings whose licence allows it.\n\n"
           "VocalSet singers: the models were trained on recordings from\n"
           "  VocalSet: A Singing Voice Dataset, J. Wilkins, P. Seetharaman, A. Wahl, B. Pardo, ISMIR 2018,\n"
           "  https://zenodo.org/records/1442513, licensed CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/).\n"
           "  Changed: the recordings were cleaned and used to train a voice conversion model (RVC) of each singer.\n\n"
           "Content encoder (Encoders/contentvec-768.onnx): RVC's HuBERT/ContentVec base model\n"
           "  (lj1995/VoiceConversionWebUI, MIT licence), exported to ONNX.\n\n"
           "To install: in Sanctuary Choir, Settings > AI SINGERS > INSTALL VOICES, and choose this file.\n";
}

int makeSharePack (const std::vector<Singer>& singers, const juce::File& zipFile,
                   const std::function<void (float)>& progress, const std::atomic<bool>* cancel, juce::String& error)
{
    juce::ZipFile::Builder zip;
    int count = 0;
    auto assignments = std::make_unique<juce::DynamicObject>();
    for (const auto& s : singers)
    {
        if (! s.shareable)
            continue;
        for (const auto& f : s.folder.findChildFiles (juce::File::findFiles, true))
            zip.addFile (f, 0, "Voices/" + s.id + "/" + f.getRelativePathFrom (s.folder).replaceCharacter ('\\', '/'));
        if (! s.assignment.isVoid())
            assignments->setProperty (s.id, s.assignment);   // the measured range and middle note go along
        ++count;
    }
    if (count == 0)
    {
        error = "none of your singers may be shared (only those trained on VocalSet or other CC BY recordings)";
        return 0;
    }
    juce::File encoder;
    for (const auto& dir : { getEncodersDirectory(), getSanctuaryEncodersDirectory() })
        for (auto* name : { "contentvec-768.onnx", "vec-768-layer-12.onnx" })
            if (! encoder.existsAsFile() && dir.getChildFile (name).existsAsFile())
                encoder = dir.getChildFile (name);
    if (! encoder.existsAsFile())
    {
        error = "the content encoder (contentvec-768.onnx) was not found: import your voices in VoxaVersa first";
        return 0;
    }
    zip.addFile (encoder, 0, "Encoders/contentvec-768.onnx");

    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty ("packs", juce::var (assignments.release()));
    const auto json = juce::JSON::toString (juce::var (root.release()));
    zip.addEntry (std::make_unique<juce::MemoryInputStream> (json.toRawUTF8(), json.getNumBytesAsUTF8(), true), 0,
                  "Voices/voice_assignment.json", juce::Time::getCurrentTime());
    const juce::String credits (sharePackCredits());
    zip.addEntry (std::make_unique<juce::MemoryInputStream> (credits.toRawUTF8(), credits.getNumBytesAsUTF8(), true), 0,
                  "CREDITS.txt", juce::Time::getCurrentTime());

    const auto tmp = zipFile.getSiblingFile (zipFile.getFileNameWithoutExtension() + ".part");
    tmp.deleteFile();
    zipFile.getParentDirectory().createDirectory();
    bool ok = false;
    {
        juce::FileOutputStream out (tmp);
        if (out.openedOk())
        {
            double p = 0.0;
            // The builder reports its progress through a double it updates; a thread passes it on.
            std::atomic<bool> done { false };
            std::thread reporter ([&] { while (! done.load()) { if (progress) progress ((float) p); std::this_thread::sleep_for (std::chrono::milliseconds (200)); } });
            ok = zip.writeToStream (out, &p) && ! (cancel != nullptr && cancel->load());
            done = true;
            reporter.join();
            out.flush();
            ok = ok && ! out.getStatus().failed();
        }
    }
    if (! ok)
    {
        tmp.deleteFile();
        error = cancel != nullptr && cancel->load() ? juce::String ("Cancelled") : "could not write " + zipFile.getFullPathName();
        return 0;
    }
    zipFile.deleteFile();
    tmp.moveFileTo (zipFile);
    if (progress) progress (1.0f);
    return count;
}

juce::StringArray installVoices (const juce::File& source, juce::String& error)
{
    juce::StringArray installed;
    const auto voicesDir = getSanctuaryVoicesDirectory();
    const auto encodersDir = getSanctuaryEncodersDirectory();
    voicesDir.createDirectory();
    encodersDir.createDirectory();

    // Merges a pack's voice assignment entries (names, ranges) into Sanctuary's.
    auto mergeAssignment = [&] (const juce::var& packs)
    {
        auto* incoming = packs.getDynamicObject();
        if (incoming == nullptr)
            return;
        const auto file = voicesDir.getChildFile ("voice_assignment.json");
        auto current = juce::JSON::parse (file);
        if (current.getDynamicObject() == nullptr)
            current = juce::var (new juce::DynamicObject());
        auto existing = current.getProperty ("packs", {});
        if (existing.getDynamicObject() == nullptr)
        {
            existing = juce::var (new juce::DynamicObject());
            current.getDynamicObject()->setProperty ("packs", existing);
        }
        for (const auto& e : incoming->getProperties())
            existing.getDynamicObject()->setProperty (e.name, e.value);
        file.replaceWithText (juce::JSON::toString (current));
    };

    if (source.isDirectory())
    {
        // A pack folder (voice.json inside) or a folder of them.
        auto copyPack = [&] (const juce::File& dir)
        {
            const auto dest = voicesDir.getChildFile (dir.getFileName());
            dest.deleteRecursively();
            if (dir.copyDirectoryTo (dest))
                installed.add (dir.getFileName());
        };
        if (source.getChildFile ("voice.json").existsAsFile())
            copyPack (source);
        else
            for (const auto& d : source.findChildFiles (juce::File::findDirectories, false))
                if (d.getChildFile ("voice.json").existsAsFile())
                    copyPack (d);
    }
    else
    {
        juce::ZipFile zip (source);
        if (zip.getNumEntries() == 0)
        {
            error = source.getFileName() + " is not a singers pack (a .zip or VoxaVersa .vxpack)";
            return {};
        }
        if (zip.getEntry ("voice.json") != nullptr)
        {
            // One VoxaVersa .vxpack: voice.json and the model at its root.
            const auto json = juce::JSON::parse (zip.createStreamForEntry (*zip.getEntry ("voice.json"))->readEntireStreamAsString());
            const auto id = json.getProperty ("id", source.getFileNameWithoutExtension()).toString()
                                .retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_ ");
            const auto dest = voicesDir.getChildFile (id.isNotEmpty() ? id : source.getFileNameWithoutExtension());
            dest.deleteRecursively();
            if (zip.uncompressTo (dest, true).wasOk())
                installed.add (dest.getFileName());
        }
        else
        {
            // A Sanctuary singers pack: Voices/<pack>/..., Encoders/..., CREDITS.txt.
            const auto staging = voicesDir.getParentDirectory().getChildFile ("Installing");
            staging.deleteRecursively();
            if (! zip.uncompressTo (staging, true).wasOk())
            {
                staging.deleteRecursively();
                error = "could not unpack " + source.getFileName();
                return {};
            }
            for (const auto& d : staging.getChildFile ("Voices").findChildFiles (juce::File::findDirectories, false))
            {
                if (! d.getChildFile ("voice.json").existsAsFile())
                    continue;
                const auto dest = voicesDir.getChildFile (d.getFileName());
                dest.deleteRecursively();
                if (d.moveFileTo (dest))
                    installed.add (d.getFileName());
            }
            for (const auto& f : staging.getChildFile ("Encoders").findChildFiles (juce::File::findFiles, false, "*.onnx"))
                f.moveFileTo (encodersDir.getChildFile (f.getFileName()));
            mergeAssignment (juce::JSON::parse (staging.getChildFile ("Voices").getChildFile ("voice_assignment.json")).getProperty ("packs", {}));
            if (staging.getChildFile ("CREDITS.txt").existsAsFile())
                staging.getChildFile ("CREDITS.txt").moveFileTo (getSanctuaryAiDirectory().getChildFile ("CREDITS.txt"));
            staging.deleteRecursively();
        }
    }
    if (installed.isEmpty() && error.isEmpty())
        error = "no singers found in " + source.getFileName();
    return installed;
}

//==============================================================================
Cast completeCast (const std::vector<Singer>& singers, const Cast& chosen, int perSection, sc::Style style,
                   const std::array<sc::SectionRange, sc::NumSections>& ranges)
{
    Cast cast;
    perSection = juce::jlimit (1, kMaxPerSection, perSection);
    std::set<juce::String> used;
    std::map<juce::String, const Singer*> byId;
    for (const auto& s : singers)
        byId[s.id] = &s;

    // What was chosen by hand stays (singers that are no longer installed drop out).
    for (int sec = 0; sec < sc::NumSections; ++sec)
        for (const auto& id : chosen[(size_t) sec])
            if (byId.count (id) != 0 && cast[(size_t) sec].size() < kMaxPerSection)
            {
                cast[(size_t) sec].add (id);
                used.insert (id);
            }

    const bool modern = isModernStyle (style);
    auto cost = [&] (const Singer& s, int sec)
    {
        const bool high = sec == sc::Soprano || sec == sc::Alto;
        float c = std::abs (s.median - ranges[(size_t) sec].centre());
        // A man in a women's section (or the other way round) only when no one else fits: a high man
        // (countertenor) for the altos, a low woman for the tenors.
        if (s.female != high)
            c += 9.0f;
        if (s.classical == modern)
            c += 5.0f;
        if (used.count (s.id) != 0)
            c += 40.0f;   // everyone once before anyone twice
        return c;
    };

    // One place at a time, in every section in turn, so the best singers spread over the sections.
    for (int round = 0; round < perSection; ++round)
    {
        for (int sec = 0; sec < sc::NumSections; ++sec)
        {
            auto& here = cast[(size_t) sec];
            if (! chosen[(size_t) sec].isEmpty() || here.size() > round)
                continue;
            const Singer* best = nullptr;
            float bestCost = 1.0e9f;
            for (const auto& s : singers)
            {
                if (here.contains (s.id))
                    continue;
                if (const float c = cost (s, sec); c < bestCost)
                {
                    bestCost = c;
                    best = &s;
                }
            }
            if (best != nullptr)
            {
                here.add (best->id);
                used.insert (best->id);
            }
        }
    }
    return cast;
}

//==============================================================================
std::vector<float> partPitch (const sc::LearnedScore& score, const sc::TakeAnalysis& analysis, int64_t length,
                              double sampleRate, int section, int slot, const Singer& singer, float a4)
{
    constexpr double fps = 100.0;
    const int numFrames = (int) std::ceil ((double) length / sampleRate * fps);
    std::vector<float> f0 ((size_t) std::max (0, numFrames), 0.0f);
    if (numFrames <= 0 || section < 0 || section >= sc::NumSections)
        return f0;

    // The section's note in every frame (-1: none). A short gap between two notes is held over (legato).
    std::vector<int> notes ((size_t) numFrames, -1);
    const auto& events = score.events;
    for (size_t e = 0; e < events.size(); ++e)
    {
        const auto& ev = events[e];
        const int count = ev.voicing.count[(size_t) section];
        if (count <= 0)
            continue;
        const int note = ev.voicing.notes[(size_t) section][(size_t) (slot % count)];
        if (note < 0)
            continue;
        int64_t end = ev.end;
        if (e + 1 < events.size() && events[e + 1].start > end && events[e + 1].start - end < (int64_t) (0.15 * sampleRate))
            end = events[e + 1].start;
        const int a = std::max (0, (int) std::floor ((double) ev.start / sampleRate * fps));
        const int b = std::min (numFrames, (int) std::ceil ((double) end / sampleRate * fps));
        for (int f = a; f < b; ++f)
            notes[(size_t) f] = note;
    }

    // One move by whole octaves for the whole part keeps its shape: the one that puts most of it inside the
    // singer's range, nearest their middle. A note still outside then moves on its own.
    int bestShift = 0;
    double bestScore = -1.0e18;
    for (int shift = -24; shift <= 24; shift += 12)
    {
        double inside = 0.0, distance = 0.0;
        for (int n : notes)
            if (n >= 0)
            {
                const int m = n + shift;
                inside += (m >= singer.low && m <= singer.high) ? 1.0 : 0.0;
                distance += std::abs ((double) m - singer.median);
            }
        const double fit = inside * 1000.0 - distance * 0.01;
        if (fit > bestScore)
        {
            bestScore = fit;
            bestShift = shift;
        }
    }
    for (auto& n : notes)
    {
        if (n < 0)
            continue;
        n += bestShift;
        while (n > singer.high + 2 && n - 12 >= singer.low - 2)
            n -= 12;
        while (n < singer.low - 2 && n + 12 <= singer.high + 2)
            n += 12;
    }

    // Where the lead sings: its voiced frames, the pitched ones the tracker was unsure of (a run, a
    // slide) and the start of a note sung before its pitch was clear (the singer comes in with the
    // lead); its consonants and breaths stay unpitched, so the singer says them as the lead does.
    std::vector<uint8_t> sung ((size_t) numFrames, 0);
    const auto& frames = analysis.frames;
    size_t k = 0;
    for (int f = 0; f < numFrames; ++f)
    {
        const auto at = (int64_t) (((double) f + 0.5) / fps * sampleRate);
        while (k + 1 < frames.size() && std::abs (frames[k + 1].centre - at) <= std::abs (frames[k].centre - at))
            ++k;
        if (frames.empty())
            break;
        const auto& fr = frames[k];
        sung[(size_t) f] = (fr.voiced || (fr.clarity > 0.45f && fr.rmsDb > -45.0f) || score.enteringAt (at)) ? 1 : 0;
    }

    // A new note is reached in 40 ms (a sung step, not a jump).
    float previous = 0.0f;
    int glideLeft = 0;
    float glideFrom = 0.0f;
    for (int f = 0; f < numFrames; ++f)
    {
        const int n = notes[(size_t) f];
        if (n < 0 || sung[(size_t) f] == 0)
        {
            f0[(size_t) f] = 0.0f;
            if (n < 0)
                previous = 0.0f;
            continue;
        }
        const float target = (float) n;
        if (previous > 0.0f && std::abs (previous - target) > 0.01f && glideLeft == 0)
        {
            glideFrom = previous;
            glideLeft = 4;
        }
        float midi = target;
        if (glideLeft > 0)
        {
            const float t = 1.0f - (float) glideLeft / 4.0f;
            midi = glideFrom + (target - glideFrom) * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * t));
            --glideLeft;
        }
        previous = target;
        f0[(size_t) f] = a4 * std::exp2 ((midi - 69.0f) / 12.0f);
    }
    return f0;
}

//==============================================================================
juce::File Renderer::getCacheDirectory()
{
    const auto custom = juce::SystemStats::getEnvironmentVariable ("SANCTUARY_AI_CACHE_DIR", {});
    if (custom.isNotEmpty() && juce::File::isAbsolutePath (custom))
        return juce::File (custom);
    return getSanctuaryAiDirectory().getChildFile ("Cache");
}

void Renderer::trimCache (int64_t maxBytes)
{
    auto files = getCacheDirectory().findChildFiles (juce::File::findFiles, false, "*.wav");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getLastAccessTime() > b.getLastAccessTime(); });
    int64_t total = 0;
    for (const auto& f : files)
    {
        total += f.getSize();
        if (total > maxBytes)
            f.deleteFile();
    }
}

bool Renderer::render (const std::vector<float>& take, double sampleRate, const sc::TakeAnalysis& analysis,
                       const sc::LearnedScore& score, const Cast& cast, const std::vector<Singer>& singers, float a4,
                       Result& result, const std::function<void (float, const juce::String&)>& progress,
                       const std::atomic<bool>* cancel)
{
    const std::lock_guard<std::mutex> sl (lock);
    result = Result{};
    auto cancelled = [cancel] { return cancel != nullptr && cancel->load(); };
    static const char* sectionNames[] = { "sopranos", "altos", "tenors", "basses" };

    std::map<juce::String, const Singer*> byId;
    for (const auto& s : singers)
        byId[s.id] = &s;

    // The take's content, hashed once: a cached voice is only reused for the same take.
    Hash takeHash;
    takeHash.add (sampleRate);
    takeHash.bytes (take.data(), take.size() * sizeof (float));

    struct Job { int section; int slot; const Singer* singer; std::vector<float> f0; juce::File file; };
    std::vector<Job> jobs;
    for (int sec = 0; sec < sc::NumSections; ++sec)
    {
        int slot = 0;
        for (const auto& id : cast[(size_t) sec])
        {
            if (slot >= kMaxPerSection)
                break;
            const auto it = byId.find (id);
            if (it == byId.end())
            {
                result.problems.add (id + " is not installed in VoxaVersa any more");
                continue;
            }
            Job job { sec, slot, it->second, partPitch (score, analysis, (int64_t) take.size(), sampleRate, sec, slot, *it->second, a4), {} };
            Hash h = takeHash;
            h.bytes (job.f0.data(), job.f0.size() * sizeof (float));
            h.add (job.singer->id);
            // The pack as it is now: a retrained singer (new model, new voice.json) sings again.
            auto files = job.singer->folder.findChildFiles (juce::File::findFiles, false, "*.json;*.onnx");
            std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName() < b.getFileName(); });
            for (const auto& f : files)
            {
                h.add (f.getFileName());
                h.add (f.getLastModificationTime().toMilliseconds());
                h.add (f.getSize());
            }
            job.file = getCacheDirectory().getChildFile (juce::String::toHexString ((juce::int64) h.h) + ".wav");
            jobs.push_back (std::move (job));
            ++slot;
        }
    }

    juce::WavAudioFormat wav;
    auto readCached = [&] (const Job& job, Voice& v)
    {
        if (! job.file.existsAsFile())
            return false;
        std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (job.file.createInputStream().release(), true));
        if (reader == nullptr || (int64_t) reader->lengthInSamples != (int64_t) take.size())
            return false;
        juce::AudioBuffer<float> buf (1, (int) reader->lengthInSamples);
        if (! reader->read (&buf, 0, buf.getNumSamples(), 0, true, false))
            return false;
        auto pcm = std::make_shared<std::vector<int16_t>> ((size_t) buf.getNumSamples());
        const float* src = buf.getReadPointer (0);
        for (size_t i = 0; i < pcm->size(); ++i)
            (*pcm)[i] = (int16_t) juce::jlimit (-32767.0f, 32767.0f, std::round (src[i] * 32767.0f));
        v.pcm = pcm;
        v.scale = 1.0f / 32767.0f;
        job.file.setLastAccessTime (juce::Time::getCurrentTime());
        return true;
    };

    // The cached ones first: the rest are what takes time.
    std::vector<size_t> toSing;
    std::array<std::vector<std::pair<int, Voice>>, sc::NumSections> placed;
    for (size_t j = 0; j < jobs.size(); ++j)
    {
        Voice v;
        v.singerName = jobs[j].singer->name;
        if (readCached (jobs[j], v))
        {
            placed[(size_t) jobs[j].section].push_back ({ jobs[j].slot, v });
            ++result.fromCache;
        }
        else
        {
            toSing.push_back (j);
        }
    }

    std::shared_ptr<NeuralVoiceModel> previous;   // kept until the next loads: they share the content encoder
    juce::AudioBuffer<float> input (1, (int) take.size());
    input.copyFrom (0, 0, take.data(), (int) take.size());
    for (size_t n = 0; n < toSing.size(); ++n)
    {
        if (cancelled())
            return false;
        const auto& job = jobs[toSing[n]];
        const juce::String what = job.singer->name + " (" + sectionNames[job.section] + ")";
        const float base = (float) n / (float) toSing.size();
        const float span = 1.0f / (float) toSing.size();
        if (progress) progress (base, what);

        juce::String error;
        auto model = NeuralVoiceModel::load (job.singer->folder, error, cancel);
        if (model == nullptr)
        {
            if (cancelled())
                return false;
            result.problems.add (job.singer->name + ": " + error);
            continue;
        }
        juce::AudioBuffer<float> sung;
        const bool ok = model->convert (input, sampleRate, job.f0, 100.0, sung, cancel, error,
                                        [&] (float f) { if (progress) progress (base + span * juce::jlimit (0.0f, 1.0f, f), what); });
        previous = model;
        if (! ok)
        {
            if (cancelled())
                return false;
            result.problems.add (job.singer->name + ": " + error);
            continue;
        }

        Voice v;
        v.singerName = job.singer->name;
        auto pcm = std::make_shared<std::vector<int16_t>> (take.size(), (int16_t) 0);
        const float* src = sung.getReadPointer (0);
        const int len = std::min (sung.getNumSamples(), (int) take.size());
        float peak = 1.0e-6f;
        for (int i = 0; i < len; ++i)
            peak = std::max (peak, std::abs (src[i]));
        const float toInt = peak > 1.0f ? 32767.0f / peak : 32767.0f;   // never clips; the scale undoes it
        for (int i = 0; i < len; ++i)
            (*pcm)[(size_t) i] = (int16_t) std::round (src[i] * toInt);
        v.pcm = pcm;
        v.scale = 1.0f / toInt;
        placed[(size_t) job.section].push_back ({ job.slot, v });
        ++result.sung;

        // Kept for the next render (the file holds the voice at its true level).
        getCacheDirectory().createDirectory();
        juce::AudioBuffer<float> out (1, (int) pcm->size());
        for (size_t i = 0; i < pcm->size(); ++i)
            out.setSample (0, (int) i, (float) (*pcm)[i] * v.scale);
        const auto tmp = job.file.getSiblingFile (job.file.getFileNameWithoutExtension() + ".part");
        if (std::unique_ptr<juce::OutputStream> stream = tmp.createOutputStream())
        {
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate)
                                                           .withNumChannels (1).withBitsPerSample (24));
            if (writer != nullptr && writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples()))
            {
                writer.reset();
                tmp.moveFileTo (job.file);
            }
        }
        tmp.deleteFile();
    }

    for (int sec = 0; sec < sc::NumSections; ++sec)
    {
        auto& p = placed[(size_t) sec];
        std::sort (p.begin(), p.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
        for (auto& [slot, v] : p)
            result.voices[(size_t) sec].push_back (std::move (v));
    }
    trimCache();
    if (progress) progress (1.0f, {});
    return true;
}

//==============================================================================
juce::var describeAvailability (const std::vector<Singer>& singers)
{
    auto o = std::make_unique<juce::DynamicObject>();
    auto& runtime = OnnxRuntime::getInstance();
    const bool engine = runtime.ensureLoaded();
    o->setProperty ("engine", engine);
    o->setProperty ("engineText", engine ? juce::String ("AI engine ready") : runtime.getStatusMessage());
    o->setProperty ("folder", getVoicesDirectory().getFullPathName());
    bool encoder = false;
    for (const auto& dir : { getEncodersDirectory(), getSanctuaryEncodersDirectory() })
        encoder = encoder || dir.getChildFile ("contentvec-768.onnx").existsAsFile() || dir.getChildFile ("vec-768-layer-12.onnx").existsAsFile();
    o->setProperty ("encoder", encoder);
    o->setProperty ("voxaversa", getVoicesDirectory().isDirectory());
    o->setProperty ("sanctuaryFolder", getSanctuaryVoicesDirectory().getFullPathName());
    int shareable = 0;
    for (const auto& s : singers)
        shareable += s.shareable ? 1 : 0;
    o->setProperty ("shareable", shareable);
    juce::Array<juce::var> list;
    for (const auto& s : singers)
    {
        auto e = std::make_unique<juce::DynamicObject>();
        e->setProperty ("id", s.id);
        e->setProperty ("name", s.name);
        e->setProperty ("female", s.female);
        e->setProperty ("low", s.low);
        e->setProperty ("high", s.high);
        e->setProperty ("classical", s.classical);
        e->setProperty ("shareable", s.shareable);
        list.add (juce::var (e.release()));
    }
    o->setProperty ("singers", list);
    return juce::var (o.release());
}
} // namespace sanctuary::ai
