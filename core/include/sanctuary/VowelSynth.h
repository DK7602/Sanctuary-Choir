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

    /** softness 0 = bright/pressed, 1 = soft/breathy tilt. */
    inline float read (float phase, float f0, float softness) const
    {
        const int b = bandFor (f0);
        const auto& bright = bands[(size_t) b].bright;
        const auto& soft = bands[(size_t) b].soft;
        const float p = phase * (float) tableSize;
        const int i0 = (int) p & (tableSize - 1);
        const int i1 = (i0 + 1) & (tableSize - 1);
        const float fr = p - std::floor (p);
        const float a = bright[(size_t) i0] + (bright[(size_t) i1] - bright[(size_t) i0]) * fr;
        const float s = soft[(size_t) i0] + (soft[(size_t) i1] - soft[(size_t) i0]) * fr;
        return a + (s - a) * softness;
    }

    /** Harmonic amplitude of the bright/soft source (for loudness normalisation). */
    float harmonicAmplitude (int k, float softness) const;

private:
    int bandFor (float f0) const
    {
        const int b = (int) std::floor (std::log2 (std::max (f0, 20.0f) / baseHz));
        return clampv (b, 0, (int) bands.size() - 1);
    }

    struct Band { std::vector<float> bright, soft; };
    std::vector<Band> bands;
    double sr = 48000.0;
    float baseHz = 27.5f;
    float brightScale = 1.0f, softScale = 1.0f;
};

/** One synthetic singer: glottal oscillator with breath noise through a formant bank. */
class VowelSinger
{
public:
    void prepare (double sampleRate);
    void reset();

    /** Configure formants (call when vowel/section/formant scale changes). */
    void setFormants (const FormantSpec& spec, float formantScale);

    inline float process (const GlottalTables& tables, float f0, float softness, float breath, Rng& rng)
    {
        phase += f0 * invSr;
        if (phase >= 1.0f)
            phase -= std::floor (phase);
        float src = tables.read (phase, f0, softness);
        const float openPhase = phase < 0.55f ? 1.0f : 0.3f;
        src += rng.bi() * breath * openPhase;
        float y = 0.0f;
        for (int i = 0; i < 5; ++i)
            y += gains[(size_t) i] * res[(size_t) i].process (src);
        return y * norm;
    }

    /** Recompute the loudness normalisation for a typical pitch. */
    void normalise (const GlottalTables& tables, float f0, float softness);

private:
    double sr = 48000.0;
    float invSr = 1.0f / 48000.0f;
    float phase = 0.0f;
    std::array<Resonator, 5> res;
    std::array<float, 5> gains {};
    std::array<float, 5> freqs {}, bws {};
    float norm = 1.0f;
};
} // namespace sc
