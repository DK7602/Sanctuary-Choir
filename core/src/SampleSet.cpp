#include "sanctuary/SampleSet.h"
#include "sanctuary/Learn.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>

namespace sc
{
namespace
{
    constexpr float kTargetRms = 0.1f;        // every note is levelled to this in its sustain
    constexpr double kMinNoteSeconds = 0.6;
    constexpr double kMaxNoteSeconds = 8.0;

    std::vector<std::string> tokens (const std::string& path)
    {
        std::vector<std::string> out;
        std::string cur;
        for (char ch : path)
        {
            const unsigned char c = (unsigned char) ch;
            if (std::isalnum (c)) cur += (char) std::tolower (c);
            else if (! cur.empty()) { out.push_back (cur); cur.clear(); }
        }
        if (! cur.empty()) out.push_back (cur);
        return out;
    }

    inline float at (const SampleZone& z, int64_t i) noexcept
    {
        return i >= 0 && i < (int64_t) z.pcm.size() ? (float) z.pcm[(size_t) i] * z.scale : 0.0f;
    }

    inline float interp (const SampleZone& z, double pos) noexcept
    {
        const int64_t i = (int64_t) pos;
        const float fr = (float) (pos - (double) i);
        const float a = at (z, i), b = at (z, i + 1);
        return a + (b - a) * fr;
    }

    /** Sample at pos with the sustain loop crossfaded in (equal power: sustains are uncorrelated).
        Each singer loops back to its own point, so copies of one recording drift apart in time
        instead of stacking into a phasey comb. */
    inline float readLooped (const SampleZone& z, double& pos, int64_t loopStart) noexcept
    {
        const double loopLen = (double) (z.loopEnd - loopStart);
        if (loopLen > 0.0 && pos >= (double) z.loopEnd)
            pos -= loopLen;
        float v = interp (z, pos);
        const double xfStart = (double) (z.loopEnd - z.crossfade);
        if (z.crossfade > 0 && pos > xfStart)
        {
            const float t = (float) ((pos - xfStart) / (double) z.crossfade);
            const float w = clampv (t, 0.0f, 1.0f) * 1.5707963f;
            v = v * std::cos (w) + interp (z, pos - loopLen) * std::sin (w);
        }
        return v;
    }
}

//==============================================================================
namespace
{
    /** Every zone costing no more than the best plus `slack` is a candidate (none above
        `limit`); each choir member (variant) takes a different one, among the given singer's
        if there are any. -1 if nothing qualifies. */
    template <typename Cost>
    int pickZone (const std::vector<SampleZone>& zones, Cost cost, float slack, float limit, int variant, int singer) noexcept
    {
        float best = 1.0e9f;
        for (const auto& z : zones)
            best = std::min (best, cost (z));
        if (! (best <= limit))
            return -1;
        int candidates[64], own[64];
        int count = 0, ownCount = 0;
        for (int i = 0; i < (int) zones.size() && count < 64; ++i)
        {
            if (cost (zones[(size_t) i]) > best + slack)
                continue;
            candidates[count++] = i;
            if (singer >= 0 && zones[(size_t) i].singer == singer)
                own[ownCount++] = i;
        }
        if (ownCount > 0)
            return own[(unsigned) variant % (unsigned) ownCount];
        return count > 0 ? candidates[(unsigned) variant % (unsigned) count] : -1;
    }
}

int SampleLayer::nearest (float midi, int variant, int singer) const noexcept
{
    if (zones.empty())
        return -1;
    // Cost: how far the recording must be re-pitched, plus a little for short notes (their
    // loops are shorter). Every recording within a semitone of the best is a candidate, and
    // each choir member takes a different one: real different singers, not copies.
    auto cost = [midi] (const SampleZone& z) { return std::abs (z.rootMidi - midi) + (z.seconds() < 1.2f ? 0.6f : 0.0f); };
    const int i = pickZone (zones, cost, 1.0f, 1.0e8f, variant, singer);
    return i >= 0 ? i : 0;   // -1 only when midi is not a number
}

int SampleLayer::nearestOnset (float midi, int variant, int singer) const noexcept
{
    auto cost = [midi] (const SampleZone& z) { return std::abs (z.rootMidi - midi); };
    return pickZone (onsets, cost, 1.0f, 2.5f, variant, singer);
}

int SampleLayer::nearestSyllable (uint32_t phones, float midi, int variant, int singer) const noexcept
{
    if (phones == 0)
        return -1;
    auto cost = [phones, midi] (const SampleZone& z) { return z.phones == phones ? std::abs (z.rootMidi - midi) : 1.0e9f; };
    return pickZone (syllables, cost, 1.0f, 5.0f, variant, singer);
}

int SampleLayer::nearestMove (float from, float to, int variant, int singer) const noexcept
{
    // A step the same way, as close in size as possible (a recorded third for a third; a step
    // of a tone can stretch to a third), re-pitched as little as possible.
    const float step = to - from;
    auto cost = [to, step] (const SampleZone& z)
    {
        const float zStep = z.toMidi - z.rootMidi;
        const float stretch = std::abs (zStep - step);
        if ((zStep > 0.0f) != (step > 0.0f) || stretch > std::max (1.25f, 0.4f * std::abs (step)))
            return 1.0e9f;
        return 0.6f * std::abs (z.toMidi - to) + stretch;
    };
    return pickZone (moves, cost, 0.75f, 2.6f, variant, singer);
}

//==============================================================================
uint64_t SampleSet::nextId() noexcept
{
    static std::atomic<uint64_t> counter { 0 };
    return ++counter;
}

int SampleSet::addRecording (int section, int vowel, const float* x, int64_t n, double sampleRate, float a4)
{
    if (section < 0 || section >= NumSections || vowel < 0 || vowel >= kVowels || n <= 0)
        return 0;
    const auto take = analyseTake (x, n, sampleRate, a4);
    int kept = 0;
    for (const auto& note : take.notes)
    {
        const double secs = (double) (note.end - note.start) / sampleRate;
        if (secs < kMinNoteSeconds)
            continue;
        // Keep the singer's attack (start a touch early) and stop before the release.
        const int64_t start = std::max<int64_t> (0, note.start - (int64_t) (0.03 * sampleRate));
        const int64_t end = std::min<int64_t> (n, std::min (note.end, start + (int64_t) (kMaxNoteSeconds * sampleRate)));
        addNote (section, vowel, x + start, end - start, sampleRate, note.pitch);
        ++kept;
    }
    return kept;
}

void SampleSet::addNote (int section, int vowel, const float* x, int64_t n, double sampleRate, float rootMidi, const NoteInfo& info)
{
    const int take = (int) info.take;
    if (section < 0 || section >= NumSections || vowel < 0 || vowel >= kVowels || n <= 0 || take < 0 || take >= kTakes)
        return;
    SampleZone z;
    z.sampleRate = sampleRate;
    z.rootMidi = rootMidi;
    z.singer = info.singer;
    z.phones = info.phones;
    Kind kind = info.kind;
    if (kind == Kind::Syllable && z.phones == 0)
        kind = Kind::Onset;   // no consonants named: a plain onset
    if (kind == Kind::Move)
    {
        z.toMidi = info.toMidi;
        z.crossing = (int64_t) (info.crossingSeconds * sampleRate);
        if (! (z.toMidi > 0.0f) || z.crossing <= 0 || z.crossing >= n)
            kind = Kind::Held;   // not a usable move: keep it as a note
    }
    float peak = 1.0e-6f;
    for (int64_t i = 0; i < n; ++i) peak = std::max (peak, std::abs (x[i]));
    z.scale = peak / 32767.0f;
    z.pcm.resize ((size_t) n);
    for (int64_t i = 0; i < n; ++i)
        z.pcm[(size_t) i] = (int16_t) std::lround (x[i] / peak * 32767.0f);
    auto& layer = layers[(size_t) section][(size_t) vowel][(size_t) take];
    (kind == Kind::Onset ? layer.onsets : kind == Kind::Move ? layer.moves : kind == Kind::Syllable ? layer.syllables : layer.zones).push_back (std::move (z));
}

namespace
{
    /** Short fades so the edges never click. */
    void fadeEdges (SampleZone& z, double inSeconds, double outSeconds)
    {
        const int64_t len = z.length();
        const int64_t fadeIn = std::min<int64_t> (len / 4, (int64_t) (inSeconds * z.sampleRate));
        const int64_t fadeOut = std::min<int64_t> (len / 4, (int64_t) (outSeconds * z.sampleRate));
        for (int64_t i = 0; i < fadeIn; ++i) z.pcm[(size_t) i] = (int16_t) (z.pcm[(size_t) i] * i / fadeIn);
        for (int64_t i = 0; i < fadeOut; ++i) z.pcm[(size_t) (len - 1 - i)] = (int16_t) (z.pcm[(size_t) (len - 1 - i)] * i / fadeOut);
    }

    double rmsOf (const SampleZone& z, int64_t a, int64_t b)
    {
        a = std::max<int64_t> (0, a);
        b = std::min (z.length(), b);
        double e = 0.0;
        for (int64_t i = a; i < b; ++i) e += (double) z.pcm[(size_t) i] * z.pcm[(size_t) i];
        return b > a ? std::sqrt (e / (double) (b - a)) * z.scale : 0.0;
    }

    /** Scales z so that `rms` (measured on it) becomes the common level. */
    void level (SampleZone& z, double rms)
    {
        if (rms > 1.0e-6)
            z.scale *= (float) (kTargetRms / rms);
    }
}

void SampleSet::finalise()
{
    auto byPitch = [] (const SampleZone& a, const SampleZone& b) { return a.rootMidi < b.rootMidi; };
    for (auto& section : layers)
    for (auto& vowel : section)
    {
        for (size_t take = 0; take < vowel.size(); ++take)
        {
            auto& layer = vowel[take];
            // Onsets: levelled by where they settle, so they meet the held note they hand over to.
            std::sort (layer.onsets.begin(), layer.onsets.end(), byPitch);
            for (auto& z : layer.onsets)
            {
                fadeEdges (z, 0.002, 0.03);
                level (z, rmsOf (z, z.length() * 55 / 100, z.length() - (int64_t) (0.03 * z.sampleRate)));
            }
            // Syllables: levelled by the vowel they open into (the consonant keeps its own level
            // against it).
            std::sort (layer.syllables.begin(), layer.syllables.end(), byPitch);
            for (auto& z : layer.syllables)
            {
                fadeEdges (z, 0.002, 0.025);
                level (z, rmsOf (z, z.length() * 60 / 100, z.length() - (int64_t) (0.025 * z.sampleRate)));
            }
            // Moves: levelled by the notes either side of the move.
            std::sort (layer.moves.begin(), layer.moves.end(), byPitch);
            for (auto& z : layer.moves)
            {
                fadeEdges (z, 0.01, 0.03);
                const auto w = (int64_t) (0.06 * z.sampleRate);
                level (z, 0.5 * (rmsOf (z, 0, w) + rmsOf (z, z.length() - w - (int64_t) (0.03 * z.sampleRate), z.length())));
            }

            std::sort (layer.zones.begin(), layer.zones.end(), byPitch);
            for (auto& z : layer.zones)
            {
                const int64_t len = z.length();
                const double sr = z.sampleRate;

                // Short fades so the edges never click.
                const int64_t fadeIn = std::min<int64_t> (len / 4, (int64_t) (0.004 * sr));
                const int64_t fadeOut = std::min<int64_t> (len / 4, (int64_t) (0.03 * sr));
                for (int64_t i = 0; i < fadeIn; ++i) z.pcm[(size_t) i] = (int16_t) (z.pcm[(size_t) i] * i / fadeIn);
                for (int64_t i = 0; i < fadeOut; ++i) z.pcm[(size_t) (len - 1 - i)] = (int16_t) (z.pcm[(size_t) (len - 1 - i)] * i / fadeOut);

                // Sustain loop: after the attack, ending before the release.
                z.loopStart = std::max<int64_t> ((int64_t) (0.3 * sr), len * 35 / 100);
                z.loopEnd = len - std::max<int64_t> (fadeOut, (int64_t) (0.08 * sr));
                if (z.loopEnd - z.loopStart < (int64_t) (0.15 * sr))
                {
                    z.loopStart = len * 3 / 10;
                    z.loopEnd = len * 9 / 10;
                }
                // With vibrato the two ends of the loop are rarely at the same point of its cycle: a
                // longer crossfade (more than a cycle) blends them instead of jumping.
                const double fade = takeHasVibrato ((Take) take) ? 0.26 : 0.15;
                z.crossfade = std::min<int64_t> ((z.loopEnd - z.loopStart) / 3, (int64_t) (fade * sr));

                // Level: the sustain at a common loudness, so notes and singers blend evenly.
                level (z, rmsOf (z, z.loopStart, z.loopEnd));
            }
        }
    }
}

const SampleLayer* SampleSet::layerFor (int section, VocalType vowel, Take take) const noexcept
{
    if (section < 0 || section >= NumSections)
        return nullptr;
    const auto& s = layers[(size_t) section];
    // Another take only of the same vowel (an "oo" never turns into a breathy "ah"); the full
    // voice falls back to the soft classical voice before the straight tone.
    if (take != Take::Natural)
    {
        if (const auto* l = exactLayer (section, vowel, take))
            return l;
        if (take == Take::Full)
            if (const auto* l = exactLayer (section, vowel, Take::Vibrato))
                return l;
    }
    const int v = (int) vowel;
    if (v >= 0 && v < kVowels && ! s[(size_t) v][0].empty())
        return &s[(size_t) v][0];
    if (vowel == VocalType::Mm)
        return nullptr;   // no recorded humming: the synthesised hum is closer than an open vowel
    if (! s[0][0].empty())
        return &s[0][0];
    for (const auto& layer : s)
        if (! layer[0].empty())
            return &layer[0];
    return nullptr;
}

const SampleLayer* SampleSet::exactLayer (int section, VocalType vowel, Take take) const noexcept
{
    const int v = (int) vowel, how = (int) take;
    if (section < 0 || section >= NumSections || v < 0 || v >= kVowels || how < 0 || how >= kTakes)
        return nullptr;
    const auto& l = layers[(size_t) section][(size_t) v][(size_t) how];
    return l.empty() ? nullptr : &l;
}

bool SampleSet::has (Take take) const noexcept
{
    const int how = (int) take;
    if (how < 0 || how >= kTakes)
        return false;
    for (const auto& s : layers)
        for (const auto& v : s)
            if (! v[(size_t) how].empty())
                return true;
    return false;
}

int SampleSet::numNotes() const noexcept
{
    int n = 0;
    for (const auto& s : layers)
        for (const auto& v : s)
            for (const auto& l : v)
                n += (int) l.zones.size();
    return n;
}

int SampleSet::numSections() const noexcept
{
    int n = 0;
    for (const auto& s : layers)
        n += std::any_of (s.begin(), s.end(), [] (const auto& v) { return ! v[0].empty(); }) ? 1 : 0;
    return n;
}

int SampleSet::numVowels() const noexcept
{
    int n = 0;
    for (int v = 0; v < kVowels; ++v)
        n += std::any_of (layers.begin(), layers.end(), [v] (const auto& s) { return ! s[(size_t) v][0].empty(); }) ? 1 : 0;
    return n;
}

std::string SampleSet::describe() const
{
    char buf[96];
    const int s = numSections(), v = numVowels(), n = numNotes();
    std::snprintf (buf, sizeof (buf), "%d SECTION%s \xC2\xB7 %d VOWEL%s \xC2\xB7 %d NOTES", s, s == 1 ? "" : "S", v, v == 1 ? "" : "S", n);
    return buf;
}

int SampleSet::sectionFromPath (const std::string& path)
{
    int found = -1;
    for (const auto& t : tokens (path))
    {
        if (t == "soprano" || t == "sopranos" || t == "sop" || t == "s1" || t == "s2") found = Soprano;
        else if (t == "alto" || t == "altos" || t == "alt" || t == "a1" || t == "a2") found = Alto;
        else if (t == "tenor" || t == "tenors" || t == "ten" || t == "t1" || t == "t2") found = Tenor;
        else if (t == "bass" || t == "basses" || t == "bas" || t == "b1" || t == "b2" || t == "baritone") found = Bass;
    }
    return found;   // the last match wins (file name over folder name)
}

float SampleSet::rootFromPath (const std::string& path)
{
    float root = -1.0f;
    for (const auto& t : tokens (path))
        if ((t.size() == 5 || t.size() == 6) && t[0] == 'm' && std::all_of (t.begin() + 1, t.end(), [] (char c) { return c >= '0' && c <= '9'; }))
            root = (float) std::atoi (t.c_str() + 1) / 100.0f;
    return root;
}

SampleSet::NoteInfo SampleSet::infoFromPath (const std::string& path)
{
    auto digits = [] (const std::string& t, size_t from) { return t.size() > from && std::all_of (t.begin() + (long) from, t.end(), [] (char c) { return c >= '0' && c <= '9'; }); };
    NoteInfo info;
    for (const auto& t : tokens (path))
    {
        if (t == "breathy") info.take = Take::Breathy;
        else if (t == "belt" || t == "belted") info.take = Take::Belt;
        else if (t == "vibrato") info.take = Take::Vibrato;
        else if (t == "syllables") info.kind = Kind::Syllable;
        else if (t == "fullvoice") info.take = Take::Full;
        else if (t == "short" || t == "onset" || t == "onsets") info.kind = Kind::Onset;
        else if (t == "legato" || t == "moves") info.kind = Kind::Move;
        else if ((t.size() == 6 || t.size() == 7) && t[0] == 't' && t[1] == 'o' && digits (t, 2)) info.toMidi = (float) std::atoi (t.c_str() + 2) / 100.0f;
        else if (t.size() >= 2 && t.size() <= 5 && t[0] == 'x' && digits (t, 1)) info.crossingSeconds = (float) std::atoi (t.c_str() + 1) / 1000.0f;
        else if (t.size() >= 2 && t.size() <= 3 && (t[0] == 'f' || t[0] == 'm') && digits (t, 1)) info.singer = (t[0] == 'f' ? 100 : 200) + std::atoi (t.c_str() + 1);
        else if (t.size() > 2 && t[0] == 'o' && t[1] == 'n')
        {
            // A syllable's consonants: "onkxr" = k, r.
            Phone ph[3];
            int count = 0;
            bool ok = true;
            size_t from = 2;
            while (ok && from <= t.size())
            {
                const size_t to = std::min (t.find ('x', from), t.size());
                const std::string name = t.substr (from, to - from);
                int found = -1;
                for (int k = 0; k < (int) Phone::Count; ++k)
                    if (name == phoneName ((Phone) k))
                        found = k;
                ok = found >= 0 && count < 3;
                if (ok)
                    ph[count++] = (Phone) found;
                from = to + 1;
            }
            if (ok && count > 0)
                info.phones = phonesKey (ph, count);
        }
    }
    return info;
}

uint32_t SampleSet::phonesKey (const Phone* phones, int count) noexcept
{
    uint32_t key = 0;
    for (int i = 0; i < std::min (count, 3); ++i)
        key |= (uint32_t) ((int) phones[i] + 1) << (8 * i);
    return key;
}

int SampleSet::vowelFromPath (const std::string& path)
{
    int found = -1;
    for (const auto& t : tokens (path))
    {
        if (t == "ah" || t == "aah" || t == "aa") found = 0;
        else if (t == "oh") found = 1;
        else if (t == "oo" || t == "ooh" || t == "uu") found = 2;
        else if (t == "ee" || t == "eee") found = 3;
        else if (t == "eh") found = 4;
        else if (t == "mm" || t == "mmm" || t == "hum" || t == "humming") found = 5;
    }
    return found;
}

//==============================================================================
int64_t SamplePlayer::loopPointFor (const SampleZone& z, float jitter) noexcept
{
    // Somewhere in the first 45% of the sustain, always leaving room for the crossfade.
    const int64_t span = z.loopEnd - z.loopStart;
    const int64_t start = z.loopStart + (int64_t) (jitter * 0.45f * (float) span);
    return std::min (start, z.loopEnd - 2 * z.crossfade);
}

void SamplePlayer::reset() noexcept
{
    for (auto& h : heads)
        h = Head{};
    cur = -1;
    next = nullptr;
    lastSerial = 0;
    lastNote = -1.0f;
    lastLayer = nullptr;
}

void SamplePlayer::start (const Head& h, float fadeSeconds, double outRate, bool freeze) noexcept
{
    const float step = fadeSeconds > 0.0f ? (float) (1.0 / ((double) fadeSeconds * outRate)) : 1.0f;
    // A free slot, else the quietest head (by then nearly faded out).
    int slot = -1;
    for (int i = 0; i < kHeads && slot < 0; ++i)
        if (heads[i].zone == nullptr)
            slot = i;
    if (slot < 0)
    {
        slot = 0;
        for (int i = 1; i < kHeads; ++i)
            if (i != cur && (slot == cur || heads[i].level < heads[slot].level))
                slot = i;
    }
    for (int i = 0; i < kHeads; ++i)
        if (heads[i].zone != nullptr)
        {
            heads[i].step = -step;
            heads[i].frozen = heads[i].frozen || (freeze && heads[i].rate > 0.0);
        }
    heads[slot] = h;
    heads[slot].level = fadeSeconds > 0.0f ? 0.0f : 1.0f;
    heads[slot].step = step;
    if (fadeSeconds <= 0.0f)
        for (int i = 0; i < kHeads; ++i)
            if (i != slot)
                heads[i] = Head{};
    cur = slot;
}

void SamplePlayer::render (const SampleLayer& layer, float hz, const float* amp, float* out, int n,
                           double outRate, int variant, float jitter, float zoneOffset) noexcept
{
    SampleNote note;
    note.hz = note.noteHz = hz;
    render (layer, note, amp, out, n, outRate, variant, jitter, zoneOffset);
}

void SamplePlayer::render (const SampleLayer& layer, const SampleNote& note, const float* amp, float* out, int n,
                           double outRate, int variant, float jitter, float zoneOffset) noexcept
{
    const bool silent = amp[0] < 1.0e-5f && amp[n - 1] < 1.0e-5f;
    if (silent || layer.empty())
    {
        // Between notes: forget the position so the next note starts with its attack.
        if (silent)
        {
            for (auto& h : heads)
                h = Head{};
            cur = -1;
            next = nullptr;
        }
        std::fill (out, out + n, 0.0f);
        return;
    }
    float hz = note.hz, noteHz = note.noteHz;
    if (! (hz > 20.0f && hz < 20000.0f))
        hz = 220.0f;
    if (! (noteHz > 20.0f && noteHz < 20000.0f))
        noteHz = hz;
    const float midi = hzToMidi (hz) + zoneOffset;
    const bool notes = note.noteMidi >= 0.0f;
    const float target = notes ? note.noteMidi + zoneOffset : midi;

    auto held = [&] (const SampleZone* z, bool fromStart) -> Head
    {
        Head h;
        h.zone = z;
        h.mode = Mode::Held;
        h.loopStart = loopPointFor (*z, jitter);
        h.pos = fromStart ? jitter * 0.05 * z->sampleRate   // staggered a little per singer
                          : (double) z->loopStart + jitter * 0.5 * (double) (z->loopEnd - z->loopStart);
        return h;
    };
    // A note from silence (or sung afresh after a gap): its recorded onset if there is one,
    // handing over to the held note; else the held note from its start.
    auto sing = [&] (float fadeSeconds)
    {
        const SampleZone* h = &layer.zones[(size_t) layer.nearest (target, variant)];
        const int o = layer.nearestOnset (target, variant, h->singer);
        if (o >= 0)
        {
            const SampleZone& z = layer.onsets[(size_t) o];
            Head head;
            head.zone = &z;
            head.mode = Mode::Onset;
            head.pos = jitter * 0.006 * z.sampleRate;
            start (head, fadeSeconds, outRate, true);
            next = &layer.zones[(size_t) layer.nearest (target, variant, z.singer)];
            handover = (double) z.length() - 0.09 * z.sampleRate;
        }
        else
        {
            start (held (h, true), fadeSeconds, outRate, true);
            next = nullptr;
        }
    };

    if (&layer != lastLayer)
    {
        // Another vowel (typed words) or way of singing mid-note: crossfade into it. The heads
        // still sounding keep pointing into the same set, which outlives this player's use.
        const bool sounding = current() != nullptr && lastLayer != nullptr;
        lastLayer = &layer;
        if (sounding)
        {
            start (held (&layer.zones[(size_t) layer.nearest (target, variant)], false), 0.04f, outRate);
            next = nullptr;
            lastSerial = note.serial;
            lastNote = target;
        }
        else
        {
            for (auto& h : heads)
                h = Head{};
            cur = -1;
            next = nullptr;
        }
    }

    // Typed words: a syllable that opens with a recorded consonant is sung from the recording
    // (the consonant, then the start of its vowel) and hands over to the held note.
    if (note.syllableSerial != lastSyllableSerial)
    {
        lastSyllableSerial = note.syllableSerial;
        if (note.syllable >= 0 && note.syllable < (int) layer.syllables.size())
        {
            const SampleZone& z = layer.syllables[(size_t) note.syllable];
            Head head;
            head.zone = &z;
            head.mode = Mode::Onset;
            start (head, current() != nullptr ? 0.012f : 0.0f, outRate, true);
            next = &layer.zones[(size_t) layer.nearest (target, variant, z.singer)];
            handover = (double) z.length() - 0.05 * z.sampleRate;
            lastSerial = note.serial;
            lastNote = target;
        }
    }

    Head* c = current();
    if (c == nullptr)
    {
        sing (0.0f);
        lastSerial = note.serial;
        lastNote = target;
    }
    else if (note.serial != lastSerial)
    {
        const float from = lastNote;
        lastSerial = note.serial;
        lastNote = target;
        const int m = notes && from >= 0.0f && std::abs (target - from) >= 0.5f
                          ? layer.nearestMove (from, target, variant, c->zone->singer) : -1;
        if (note.detached)
        {
            sing (0.015f);
        }
        else if (m >= 0)
        {
            // Legato: the recorded move, from just before its pitch starts moving.
            const SampleZone& z = layer.moves[(size_t) m];
            Head head;
            head.zone = &z;
            head.mode = Mode::Move;
            head.pos = std::max (0.0, (double) z.crossing - 0.07 * z.sampleRate);
            head.stretch = (from - target) - (z.rootMidi - z.toMidi);
            start (head, 0.025f, outRate, true);
            next = &layer.zones[(size_t) layer.nearest (target, variant, z.singer)];
            handover = std::min ((double) z.crossing + 0.12 * z.sampleRate, (double) z.length() - 0.09 * z.sampleRate);
        }
        else if (c->mode != Mode::Held)
        {
            // No recorded move for this step: straight on to the new held note.
            start (held (&layer.zones[(size_t) layer.nearest (target, variant, c->zone->singer)], false), 0.07f, outRate);
            next = nullptr;
        }
        else
        {
            c->onNote = false;   // follows the glide to the new note
        }
    }

    c = current();
    if (c->mode != Mode::Held && (next == nullptr || c->pos >= handover))
    {
        // The onset or move has arrived: on into the held note (at the note: a move has already
        // sung the slide, which the gliding pitch may still be catching up with).
        Head h = held (next != nullptr ? next : &layer.zones[(size_t) layer.nearest (target, variant)], false);
        h.onNote = c->mode == Mode::Move;
        start (h, 0.06f, outRate);
        next = nullptr;
    }
    else if (c->mode == Mode::Held && c->level >= 1.0f)
    {
        // The pitch moved on (a glide, or a step with no recorded move): crossfade into the
        // nearer recorded note, mid-sustain (after any crossfade still running, so a fast
        // glide never cuts one off halfway).
        const float at = c->onNote ? hzToMidi (noteHz) + zoneOffset : midi;
        const SampleZone* want = &layer.zones[(size_t) layer.nearest (at, variant, c->zone->singer)];
        if (want != c->zone && std::abs (at - c->zone->rootMidi) > 1.6f)
        {
            Head h = held (want, false);
            h.onNote = c->onNote;
            start (h, 0.07f, outRate);
        }
    }

    std::fill (out, out + n, 0.0f);
    for (auto& h : heads)
    {
        if (h.zone == nullptr)
            continue;
        const SampleZone& z = *h.zone;
        // Held notes and onsets follow the pitch; a move already holds its own slide, so it
        // plays at the new note's pitch (stretched before the slide to start on the old one).
        const double rate = h.frozen ? h.rate
                          : (h.mode == Mode::Move ? (double) noteHz / (double) midiToHz (z.toMidi)
                                                  : (double) (h.onNote ? noteHz : hz) / (double) midiToHz (z.rootMidi))
                            * z.sampleRate / outRate;
        h.rate = rate;
        const double end = (double) (z.length() - 1);
        const bool stretched = h.mode == Mode::Move && h.stretch != 0.0f;
        const double slideWidth = 0.025 * z.sampleRate;
        for (int i = 0; i < n; ++i)
        {
            h.level = clampv (h.level + h.step, 0.0f, 1.0f);
            const float w = std::sin (h.level * 1.5707963f);
            float v;
            if (h.mode == Mode::Held)
                v = readLooped (z, h.pos, h.loopStart);
            else
                v = h.pos < end ? interp (z, h.pos) : 0.0f;
            out[i] += v * w;
            if (stretched)
            {
                const double before = 0.5 - 0.5 * std::tanh ((h.pos - (double) z.crossing) / slideWidth);
                h.pos += rate * std::exp2 ((double) h.stretch * before / 12.0);
            }
            else
            {
                h.pos += rate;
            }
        }
    }
    for (int i = 0; i < n; ++i)
        out[i] *= amp[i];
    for (int i = 0; i < kHeads; ++i)
    {
        Head& h = heads[i];
        if (h.zone == nullptr)
            continue;
        const bool gone = (h.step < 0.0f && h.level <= 0.0f) || (h.mode != Mode::Held && h.pos >= (double) (h.zone->length() - 1));
        if (gone && i != cur)
            h = Head{};
    }
}
} // namespace sc
