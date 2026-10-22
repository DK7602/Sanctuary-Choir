// Sanctuary Choir - sung vowel synthesis (glottal source + parallel formants).
#pragma once

#include "Filters.h"
#include "Theory.h"

namespace sc
{
enum class Vowel : int { A = 0, O, U, I, E, M, Count };

Vowel vowelForVocalType (VocalType t);

struct FormantSpec
{
    std::array<float, 5> freq {}, gainDb {}, bandwidth {};
};

/** Formant data for a vowel sung by a section (Csound/Rodet singing-voice tables). */
FormantSpec getFormants (Vowel v, int section);

/** Band-limited glottal pulse wavetables (two spectral tilts, one table per octave). */
class GlottalTables
{
public:
    static constexpr int tableSize = 2048;

    void prepare (double sampleRate);
    bool isReady() const noexcept { return ! bands.empty(); }

    int bandFor (float f0) const
    {
        const int b = (int) std::floor (std::log2 (std::max (f0, 20.0f) / baseHz));
        return clampv (b, 0, (int) bands.size() - 1);
    }
    /** tableSize + 1 samples (the last repeats the first, for interpolation). */
    const float* bright (int band) const { return bands[(size_t) band].bright.data(); }
    const float* soft (int band) const { return bands[(size_t) band].soft.data(); }

    /** softness 0 = bright/pressed, 1 = soft/breathy tilt. */
    inline float read (float phase, float f0, float softness) const
    {
        const int b = bandFor (f0);
        return readBand (bright (b), soft (b), phase, softness);
    }

    static inline float readBand (const float* br, const float* so, float phase, float softness)
    {
        const float p = phase * (float) tableSize;
        const int i0 = std::min ((int) p, tableSize - 1);
        const float fr = p - (float) i0;
        const float a = br[i0] + (br[i0 + 1] - br[i0]) * fr;
        const float s = so[i0] + (so[i0 + 1] - so[i0]) * fr;
        return a + (s - a) * softness;
    }

    /** Harmonic amplitude of the bright/soft source (for loudness normalisation). */
    float harmonicAmplitude (int k, float softness) const;

private:
    struct Band { std::vector<float> bright, soft; };
    std::vector<Band> bands;
    double sr = 48000.0;
    float baseHz = 27.5f;
};

/** One synthetic singer: glottal oscillator with breath noise through a parallel formant bank. */
class VowelSinger
{
public:
    void prepare (double sampleRate);
    void reset();
    void setPhase (float p) { phase = p - std::floor (p); }

    /** Configure formants (call when vowel/section/formant scale changes). */
    void setFormants (const FormantSpec& spec, float formantScale);

    /** Renders n samples into out, scaled per sample by amp. */
    void render (const GlottalTables& tables, float f0, float softness, float breath, Rng& rng,
                 const float* amp, float* out, int n)
    {
        if (! (f0 > 1.0f && f0 < 20000.0f))
            f0 = 100.0f;   // NaN or nonsense: never index the tables with it
        const int band = tables.bandFor (f0);
        const float* br = tables.bright (band);
        const float* so = tables.soft (band);
        const float inc = f0 * invSr;
        float ph = phase;
        float y1[5], y2[5];
        for (int k = 0; k < 5; ++k) { y1[k] = s1[(size_t) k]; y2[k] = s2[(size_t) k]; }
        for (int i = 0; i < n; ++i)
        {
            ph += inc;
            if (ph >= 1.0f) ph -= 1.0f;
            float src = GlottalTables::readBand (br, so, ph, softness);
            src += rng.bi() * breath * (ph < 0.55f ? 1.0f : 0.3f);
            float y = 0.0f;
            for (int k = 0; k < 5; ++k)
            {
                const float v = g[(size_t) k] * src + a1[(size_t) k] * y1[k] + a2[(size_t) k] * y2[k];
                y2[k] = y1[k];
                y1[k] = v;
                y += v;
            }
            out[i] = y * amp[i];
        }
        phase = ph;
        for (int k = 0; k < 5; ++k) { s1[(size_t) k] = y1[k]; s2[(size_t) k] = y2[k]; }
    }

    /** Single-sample convenience (used by the test signal generator). */
    float process (const GlottalTables& tables, float f0, float softness, float breath, Rng& rng)
    {
        const float one = 1.0f;
        float out = 0.0f;
        render (tables, f0, softness, breath, rng, &one, &out, 1);
        return out;
    }

    /** Recompute the loudness normalisation for a typical pitch. */
    void normalise (const GlottalTables& tables, float f0, float softness);

private:
    void updateGains();

    double sr = 48000.0;
    float invSr = 1.0f / 48000.0f;
    float phase = 0.0f;
    std::array<Resonator, 5> res;          // coefficient source
    std::array<float, 5> gains {};
    std::array<float, 5> a1 {}, a2 {}, g {}, s1 {}, s2 {};
    float norm = 1.0f;
};
} // namespace sc
