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
int SampleLayer::nearest (float midi, int variant) const noexcept
{
    if (zones.empty())
        return -1;
    // Cost: how far the recording must be re-pitched, plus a little for short notes (their
    // loops are shorter). Every recording within a semitone of the best is a candidate, and
    // each choir member takes a different one: real different singers, not copies.
    auto cost = [midi] (const SampleZone& z) { return std::abs (z.rootMidi - midi) + (z.seconds() < 1.2f ? 0.6f : 0.0f); };
    float best = 1.0e9f;
    for (const auto& z : zones)
        best = std::min (best, cost (z));
    int candidates[64];
    int count = 0;
    for (int i = 0; i < (int) zones.size() && count < 64; ++i)
        if (cost (zones[(size_t) i]) <= best + 1.0f)
            candidates[count++] = i;
    if (count == 0)
        return 0;   // only when midi is not a number
    return candidates[(unsigned) variant % (unsigned) count];
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

void SampleSet::addNote (int section, int vowel, const float* x, int64_t n, double sampleRate, float rootMidi)
{
    if (section < 0 || section >= NumSections || vowel < 0 || vowel >= kVowels || n <= 0)
        return;
    SampleZone z;
    z.sampleRate = sampleRate;
    z.rootMidi = rootMidi;
    float peak = 1.0e-6f;
    for (int64_t i = 0; i < n; ++i) peak = std::max (peak, std::abs (x[i]));
    z.scale = peak / 32767.0f;
    z.pcm.resize ((size_t) n);
    for (int64_t i = 0; i < n; ++i)
        z.pcm[(size_t) i] = (int16_t) std::lround (x[i] / peak * 32767.0f);
    layers[(size_t) section][(size_t) vowel].zones.push_back (std::move (z));
}

void SampleSet::finalise()
{
    for (auto& section : layers)
    {
        for (auto& layer : section)
        {
            std::sort (layer.zones.begin(), layer.zones.end(), [] (const SampleZone& a, const SampleZone& b) { return a.rootMidi < b.rootMidi; });
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
                z.crossfade = std::min<int64_t> ((z.loopEnd - z.loopStart) / 3, (int64_t) (0.15 * sr));

                // Level: the sustain at a common loudness, so notes and singers blend evenly.
                double e = 0.0;
                for (int64_t i = z.loopStart; i < z.loopEnd; ++i) e += (double) z.pcm[(size_t) i] * z.pcm[(size_t) i];
                const double rms = std::sqrt (e / (double) std::max<int64_t> (1, z.loopEnd - z.loopStart)) * z.scale;
                if (rms > 1.0e-6)
                    z.scale *= (float) (kTargetRms / rms);
            }
        }
    }
}

const SampleLayer* SampleSet::layerFor (int section, VocalType vowel) const noexcept
{
    if (section < 0 || section >= NumSections)
        return nullptr;
    const auto& s = layers[(size_t) section];
    const int v = (int) vowel;
    if (v >= 0 && v < kVowels && ! s[(size_t) v].empty())
        return &s[(size_t) v];
    if (vowel == VocalType::Mm)
        return nullptr;   // no recorded humming: the synthesised hum is closer than an open vowel
    if (! s[0].empty())
        return &s[0];
    for (const auto& layer : s)
        if (! layer.empty())
            return &layer;
    return nullptr;
}

int SampleSet::numNotes() const noexcept
{
    int n = 0;
    for (const auto& s : layers)
        for (const auto& l : s)
            n += (int) l.zones.size();
    return n;
}

int SampleSet::numSections() const noexcept
{
    int n = 0;
    for (const auto& s : layers)
        n += std::any_of (s.begin(), s.end(), [] (const SampleLayer& l) { return ! l.empty(); }) ? 1 : 0;
    return n;
}

int SampleSet::numVowels() const noexcept
{
    int n = 0;
    for (int v = 0; v < kVowels; ++v)
        n += std::any_of (layers.begin(), layers.end(), [v] (const auto& s) { return ! s[(size_t) v].empty(); }) ? 1 : 0;
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
    cur = old = Head{};
    fade = 1.0f;
    lastLayer = nullptr;
}

void SamplePlayer::render (const SampleLayer& layer, float hz, const float* amp, float* out, int n,
                           double outRate, int variant, float jitter) noexcept
{
    const bool silent = amp[0] < 1.0e-5f && amp[n - 1] < 1.0e-5f;
    if (silent || layer.empty())
    {
        // Between notes: forget the position so the next note starts with its attack.
        if (silent) cur.zone = old.zone = nullptr;
        std::fill (out, out + n, 0.0f);
        return;
    }

    if (! (hz > 20.0f && hz < 20000.0f))
        hz = 220.0f;
    const float midi = hzToMidi (hz);
    const int zi = layer.nearest (midi, variant);
    const SampleZone* want = &layer.zones[(size_t) zi];
    if (&layer != lastLayer)
    {
        lastLayer = &layer;
        cur.zone = old.zone = nullptr;
    }

    if (cur.zone == nullptr)
    {
        // New note: start at the attack, staggered a little per singer.
        cur = { want, jitter * 0.05 * want->sampleRate, 1.0f, loopPointFor (*want, jitter) };
        old.zone = nullptr;
        fade = 1.0f;
    }
    else if (fade >= 1.0f && want != cur.zone && std::abs (midi - cur.zone->rootMidi) > 1.6f)
    {
        // The melody moved on: crossfade into the nearer recorded note, mid-sustain (after any
        // crossfade still running, so a fast glide never cuts one off halfway).
        old = cur;
        const double span = (double) (want->loopEnd - want->loopStart);
        cur = { want, (double) want->loopStart + jitter * 0.5 * span, 0.0f, loopPointFor (*want, jitter) };
        fade = 0.0f;
    }

    const float fadeStep = (float) (1.0 / (0.07 * outRate));
    const double baseHz = (double) hz;
    for (int i = 0; i < n; ++i)
    {
        if (fade < 1.0f)
            fade = std::min (1.0f, fade + fadeStep);
        const float wNew = std::sin (fade * 1.5707963f), wOld = std::cos (fade * 1.5707963f);

        float v = 0.0f;
        {
            const SampleZone& z = *cur.zone;
            v += readLooped (z, cur.pos, cur.loopStart) * wNew;
            cur.pos += baseHz / (double) midiToHz (z.rootMidi) * z.sampleRate / outRate;
        }
        if (old.zone != nullptr)
        {
            const SampleZone& z = *old.zone;
            v += readLooped (z, old.pos, old.loopStart) * wOld;
            old.pos += baseHz / (double) midiToHz (z.rootMidi) * z.sampleRate / outRate;
        }
        out[i] = v * amp[i];
    }
    if (fade >= 1.0f)
        old.zone = nullptr;
}
} // namespace sc
