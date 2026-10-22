#include "TestSignals.h"

#include <sanctuary/Filters.h>
#include <sanctuary/VowelSynth.h>

#include <cstdio>
#include <cstring>

namespace sc::test
{
//==============================================================================
static uint32_t rd32 (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
static uint16_t rd16 (const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }

bool readWav (const std::string& path, Audio& out, std::string& error)
{
    FILE* f = std::fopen (path.c_str(), "rb");
    if (f == nullptr) { error = "cannot open " + path; return false; }
    std::vector<uint8_t> data;
    uint8_t buf[65536];
    size_t got;
    while ((got = std::fread (buf, 1, sizeof (buf), f)) > 0)
        data.insert (data.end(), buf, buf + got);
    std::fclose (f);

    if (data.size() < 12 || std::memcmp (data.data(), "RIFF", 4) != 0 || std::memcmp (data.data() + 8, "WAVE", 4) != 0)
    {
        error = "not a RIFF/WAVE file";
        return false;
    }

    int format = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    const uint8_t* pcm = nullptr;
    size_t pcmBytes = 0;
    size_t pos = 12;
    while (pos + 8 <= data.size())
    {
        const uint8_t* ck = data.data() + pos;
        const uint32_t size = rd32 (ck + 4);
        const uint8_t* body = ck + 8;
        if (std::memcmp (ck, "fmt ", 4) == 0 && size >= 16)
        {
            format = rd16 (body);
            channels = rd16 (body + 2);
            rate = rd32 (body + 4);
            bits = rd16 (body + 14);
            if (format == 0xFFFE && size >= 40)
                format = rd16 (body + 24);
        }
        else if (std::memcmp (ck, "data", 4) == 0)
        {
            pcm = body;
            pcmBytes = std::min<size_t> (size, data.size() - (pos + 8));
        }
        pos += 8 + size + (size & 1);
    }
    if (pcm == nullptr || channels <= 0 || (format != 1 && format != 3))
    {
        error = "unsupported WAV encoding";
        return false;
    }

    const int bytes = bits / 8;
    const size_t frames = pcmBytes / (size_t) (bytes * channels);
    out.sampleRate = rate;
    out.channels.assign ((size_t) channels, std::vector<float> (frames, 0.0f));
    for (size_t i = 0; i < frames; ++i)
    {
        for (int c = 0; c < channels; ++c)
        {
            const uint8_t* s = pcm + (i * (size_t) channels + (size_t) c) * (size_t) bytes;
            float v = 0.0f;
            if (format == 3 && bits == 32) { float x; std::memcpy (&x, s, 4); v = x; }
            else if (bits == 16) v = (float) (int16_t) rd16 (s) / 32768.0f;
            else if (bits == 24) v = (float) ((int32_t) (((uint32_t) s[0] << 8) | ((uint32_t) s[1] << 16) | ((uint32_t) s[2] << 24)) >> 8) / 8388608.0f;
            else if (bits == 32) v = (float) (int32_t) rd32 (s) / 2147483648.0f;
            else if (bits == 8) v = ((float) s[0] - 128.0f) / 128.0f;
            out.channels[(size_t) c][i] = v;
        }
    }
    return true;
}

bool writeWav (const std::string& path, const Audio& a, int bits)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (f == nullptr) return false;
    const int channels = (int) a.channels.size();
    const uint32_t frames = (uint32_t) a.length();
    const int bytes = bits / 8;
    const uint32_t dataBytes = frames * (uint32_t) (channels * bytes);
    auto w32 = [f] (uint32_t v) { uint8_t b[4] = { (uint8_t) v, (uint8_t) (v >> 8), (uint8_t) (v >> 16), (uint8_t) (v >> 24) }; std::fwrite (b, 1, 4, f); };
    auto w16 = [f] (uint16_t v) { uint8_t b[2] = { (uint8_t) v, (uint8_t) (v >> 8) }; std::fwrite (b, 1, 2, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + dataBytes); std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f); w32 (16); w16 (1); w16 ((uint16_t) channels); w32 ((uint32_t) a.sampleRate);
    w32 ((uint32_t) (a.sampleRate * channels * bytes)); w16 ((uint16_t) (channels * bytes)); w16 ((uint16_t) bits);
    std::fwrite ("data", 1, 4, f); w32 (dataBytes);
    std::vector<uint8_t> out ((size_t) dataBytes);
    size_t o = 0;
    for (uint32_t i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c)
        {
            const float v = clampv (a.channels[(size_t) c][i], -1.0f, 1.0f);
            if (bits == 16) { const int16_t s = (int16_t) std::lround (v * 32767.0f); out[o++] = (uint8_t) s; out[o++] = (uint8_t) (s >> 8); }
            else { const int32_t s = (int32_t) std::lround (v * 8388607.0f); out[o++] = (uint8_t) s; out[o++] = (uint8_t) (s >> 8); out[o++] = (uint8_t) (s >> 16); }
        }
    std::fwrite (out.data(), 1, out.size(), f);
    std::fclose (f);
    return true;
}

//==============================================================================
std::vector<float> singMelody (const std::vector<SungNote>& notes, double sr, int section, float vibratoCents, uint32_t seed)
{
    GlottalTables tables;
    tables.prepare (sr);
    VowelSinger voice;
    voice.prepare (sr);
    Rng rng (seed);
    Biquad hiss;
    hiss.bandPass (sr, 6000.0, 1.2);

    double total = 0.3;
    for (const auto& n : notes) total += n.seconds + n.gapAfter;
    std::vector<float> out ((size_t) (total * sr), 0.0f);

    const Vowel vowels[] = { Vowel::A, Vowel::E, Vowel::O, Vowel::I, Vowel::A, Vowel::U };
    size_t pos = (size_t) (0.15 * sr);
    float prevMidi = notes.empty() ? 60.0f : notes.front().midi;
    float vibPhase = 0.0f;
    Wander drift;
    for (size_t k = 0; k < notes.size(); ++k)
    {
        const auto& n = notes[k];
        voice.setFormants (getFormants (vowels[k % 6], section), 1.0f);
        voice.normalise (tables, midiToHz (n.midi), 0.4f);
        const size_t len = (size_t) (n.seconds * sr);
        const bool legato = k > 0 && notes[k - 1].gapAfter < 0.05f;
        for (size_t i = 0; i < len && pos + i < out.size(); ++i)
        {
            const float t = (float) i / (float) sr;
            const float glide = legato ? std::exp (-t / 0.025f) : 0.0f;
            const float vibAmt = clampv ((t - 0.18f) / 0.25f, 0.0f, 1.0f);
            vibPhase += 5.4f / (float) sr;
            const float cents = vibratoCents * vibAmt * std::sin (kTwoPi * vibPhase) + drift.tick (rng, 1.0f / (float) sr) * 4.0f;
            const float midi = n.midi + (prevMidi - n.midi) * glide + cents / 100.0f;
            const float env = std::min (1.0f, t / 0.04f) * std::min (1.0f, (n.seconds - t) / 0.06f);
            out[pos + i] += voice.process (tables, midiToHz (midi), 0.4f, 0.05f, rng) * env * 2.0f;
        }
        pos += len;
        prevMidi = n.midi;

        // Consonant in the gap ("s"/"t"-like noise burst).
        const size_t gap = (size_t) (n.gapAfter * sr);
        const size_t burst = std::min<size_t> (gap, (size_t) (0.035 * sr));
        for (size_t i = 0; i < burst && pos + i < out.size(); ++i)
        {
            const float e = std::sin (kPi * (float) i / (float) burst);
            out[pos + i] += hiss.process (rng.bi()) * 0.05f * e;
        }
        pos += gap;
    }
    return out;
}

std::vector<SungNote> amazingGrace (float tr)
{
    const float b = 0.5f;
    std::vector<SungNote> m = {
        { 62, b * 1, 0.04f }, { 67, b * 2, 0.0f }, { 71, b * 0.5f, 0.0f }, { 67, b * 0.5f, 0.0f }, { 71, b * 2, 0.04f },
        { 69, b * 1, 0.04f }, { 67, b * 2, 0.04f }, { 64, b * 1, 0.04f }, { 62, b * 2, 0.5f },
        { 62, b * 1, 0.04f }, { 67, b * 2, 0.0f }, { 71, b * 0.5f, 0.0f }, { 67, b * 0.5f, 0.0f }, { 71, b * 2, 0.04f },
        { 69, b * 1, 0.04f }, { 74, b * 3, 0.5f },
    };
    for (auto& n : m) n.midi += tr;
    return m;
}

std::vector<float> playBand (const std::vector<BandChordSpec>& chords, double sr, float beat, float leadIn, uint32_t seed)
{
    float total = leadIn;
    for (const auto& c : chords) total += c.seconds;
    std::vector<float> out ((size_t) ((total + 1.0f) * sr), 0.0f);
    Rng rng (seed);
    auto addNote = [&] (int midi, double t0, double dur, float gain, int harmonics, float decay)
    {
        const double f0 = 440.0 * std::pow (2.0, (midi - 69) / 12.0);
        const int64_t a = (int64_t) (t0 * sr), len = (int64_t) (dur * sr);
        for (int h = 1; h <= harmonics; ++h)
        {
            const double f = f0 * h * (1.0 + 0.0004 * h * h);   // a little piano-like inharmonicity
            if (f > 0.45 * sr)
                break;
            const float ah = gain / std::pow ((float) h, 1.3f);
            const double w = 2.0 * kPiD * f / sr;
            const double ph = rng.uni() * 6.2831853;
            for (int64_t i = 0; i < len && a + i < (int64_t) out.size(); ++i)
            {
                const float env = std::exp (-(float) i / (float) sr / (decay / (0.6f + 0.4f * (float) h))) * std::min (1.0f, (float) i / (0.004f * (float) sr));
                out[(size_t) (a + i)] += ah * env * (float) std::sin (w * (double) i + ph);
            }
        }
    };
    double t = leadIn;
    for (const auto& c : chords)
    {
        for (double b = 0.0; b < c.seconds - 1.0e-6; b += beat)
        {
            const double dur = std::min ((double) beat, c.seconds - b) + 0.05;
            for (int n : c.notes)
                addNote (n, t + b, dur, 0.06f, 10, 0.9f);
            if (c.bass >= 0)
                addNote (c.bass, t + b, dur, 0.12f, 4, 1.2f);
        }
        t += c.seconds;
    }
    for (auto& v : out)
        v += 0.0015f * rng.bi();
    return out;
}

std::vector<SungNote> cMajorScale (float base, float secs)
{
    const int steps[] = { 0, 2, 4, 5, 7, 9, 11, 12, 11, 9, 7, 5, 4, 2, 0 };
    std::vector<SungNote> m;
    for (int s : steps) m.push_back ({ base + (float) s, secs, 0.03f });
    m.back().gapAfter = 0.4f;
    return m;
}
} // namespace sc::test
