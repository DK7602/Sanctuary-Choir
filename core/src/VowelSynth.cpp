#include "sanctuary/VowelSynth.h"
#include "sanctuary/FFT.h"

#include <complex>

namespace sc
{
Vowel vowelForVocalType (VocalType t)
{
    switch (t)
    {
        case VocalType::Oh: return Vowel::O;
        case VocalType::Oo: return Vowel::U;
        case VocalType::Ee: return Vowel::I;
        case VocalType::Eh: return Vowel::E;
        case VocalType::Mm: return Vowel::M;
        default:            return Vowel::A;
    }
}

FormantSpec getFormants (Vowel v, int section)
{
    // { freqs }, { gains dB }, { bandwidths } — after the classic Csound FOF tables.
    struct Row { float f[5], g[5], b[5]; };
    static const Row soprano[] = {
        { { 800, 1150, 2900, 3900, 4950 }, { 0, -6, -32, -20, -50 }, { 80, 90, 120, 130, 140 } },   // a
        { { 450, 800, 2830, 3800, 4950 },  { 0, -11, -22, -22, -50 }, { 70, 80, 100, 130, 135 } },  // o
        { { 325, 700, 2700, 3800, 4950 },  { 0, -16, -35, -40, -60 }, { 50, 60, 170, 180, 200 } },  // u
        { { 270, 2140, 2950, 3900, 4950 }, { 0, -12, -26, -26, -44 }, { 60, 90, 100, 120, 120 } },  // i
        { { 350, 2000, 2800, 3600, 4950 }, { 0, -20, -15, -40, -56 }, { 60, 100, 120, 150, 200 } }, // e
        { { 300, 1200, 2500, 3500, 4500 }, { 0, -26, -32, -40, -52 }, { 70, 250, 250, 300, 300 } }, // m
    };
    static const Row alto[] = {
        { { 800, 1150, 2800, 3500, 4950 }, { 0, -4, -20, -36, -60 }, { 80, 90, 120, 130, 140 } },
        { { 450, 800, 2830, 3500, 4950 },  { 0, -9, -16, -28, -55 }, { 70, 80, 100, 130, 135 } },
        { { 325, 700, 2530, 3500, 4950 },  { 0, -12, -30, -40, -64 }, { 50, 60, 170, 180, 200 } },
        { { 350, 1700, 2700, 3700, 4950 }, { 0, -20, -30, -36, -60 }, { 50, 100, 120, 150, 200 } },
        { { 400, 1600, 2700, 3300, 4950 }, { 0, -24, -30, -35, -60 }, { 60, 80, 120, 150, 200 } },
        { { 280, 1150, 2400, 3300, 4300 }, { 0, -26, -32, -40, -52 }, { 70, 250, 250, 300, 300 } },
    };
    static const Row tenor[] = {
        { { 650, 1080, 2650, 2900, 3250 }, { 0, -6, -7, -8, -22 }, { 80, 90, 120, 130, 140 } },
        { { 400, 800, 2600, 2800, 3000 },  { 0, -10, -12, -12, -26 }, { 40, 80, 100, 120, 120 } },
        { { 350, 600, 2700, 2900, 3300 },  { 0, -20, -17, -14, -26 }, { 40, 60, 100, 120, 120 } },
        { { 290, 1870, 2800, 3250, 3540 }, { 0, -15, -18, -20, -30 }, { 40, 90, 100, 120, 120 } },
        { { 400, 1700, 2600, 3200, 3580 }, { 0, -14, -12, -14, -20 }, { 70, 80, 100, 120, 120 } },
        { { 260, 1050, 2300, 2900, 3400 }, { 0, -26, -30, -38, -48 }, { 60, 220, 220, 260, 260 } },
    };
    static const Row bass[] = {
        { { 600, 1040, 2250, 2450, 2750 }, { 0, -7, -9, -9, -20 }, { 60, 70, 110, 120, 130 } },
        { { 400, 750, 2400, 2600, 2900 },  { 0, -11, -21, -20, -40 }, { 40, 80, 100, 120, 120 } },
        { { 350, 600, 2400, 2675, 2950 },  { 0, -20, -32, -28, -36 }, { 40, 80, 100, 120, 120 } },
        { { 250, 1750, 2600, 3050, 3340 }, { 0, -30, -16, -22, -28 }, { 60, 90, 100, 120, 120 } },
        { { 400, 1620, 2400, 2800, 3100 }, { 0, -12, -9, -12, -18 }, { 40, 80, 100, 120, 120 } },
        { { 240, 950, 2200, 2700, 3200 },  { 0, -26, -30, -36, -46 }, { 60, 200, 220, 250, 250 } },
    };

    const Row* table = section == Soprano ? soprano : section == Alto ? alto : section == Tenor ? tenor : bass;
    const Row& r = table[clampv ((int) v, 0, (int) Vowel::Count - 1)];
    FormantSpec s;
    for (int i = 0; i < 5; ++i)
    {
        s.freq[(size_t) i] = r.f[i];
        s.gainDb[(size_t) i] = r.g[i];
        s.bandwidth[(size_t) i] = r.b[i];
    }
    return s;
}

//==============================================================================
static constexpr float kBrightTilt = 0.45f;
static constexpr float kSoftTilt = 1.15f;

static float harmonicTaper (int k, int maxH)
{
    const float x = (float) k / (float) std::max (1, maxH);
    if (x < 0.7f) return 1.0f;
    if (x >= 1.0f) return 0.0f;
    const float c = std::cos ((x - 0.7f) / 0.3f * kPi * 0.5f);
    return c * c;
}

void GlottalTables::prepare (double sampleRate)
{
    sr = sampleRate;
    const int N = tableSize;

    // Rosenberg glottal flow derivative: gives the phase structure of a natural pulse.
    std::vector<std::complex<float>> spec ((size_t) N);
    const float tp = 0.40f, tn = 0.16f;
    for (int i = 0; i < N; ++i)
    {
        const float t = (float) i / (float) N;
        float d = 0.0f;
        if (t < tp)
            d = 0.5f * kPi / tp * std::sin (kPi * t / tp);
        else if (t < tp + tn)
            d = -0.5f * kPi / tn * std::sin (0.5f * kPi * (t - tp) / tn);
        spec[(size_t) i] = { d, 0.0f };
    }
    FFT fft (11);
    fft.forward (spec.data());

    std::vector<float> phases ((size_t) N / 2);
    for (int k = 1; k < N / 2; ++k)
        phases[(size_t) k] = std::arg (spec[(size_t) k]);

    bands.clear();
    for (int b = 0; baseHz * std::exp2 ((float) b) < 4200.0f; ++b)
    {
        const float topF0 = baseHz * std::exp2 ((float) (b + 1));
        const int maxH = std::max (1, std::min (N / 2 - 1, (int) std::floor (0.46 * sr / topF0)));
        Band band;
        for (int variant = 0; variant < 2; ++variant)
        {
            const float tilt = variant == 0 ? kBrightTilt : kSoftTilt;
            std::vector<std::complex<float>> x ((size_t) N, { 0.0f, 0.0f });
            for (int k = 1; k <= maxH; ++k)
            {
                const float a = std::pow ((float) k, -tilt) * harmonicTaper (k, maxH) * (float) N * 0.5f;
                x[(size_t) k] = std::polar (a, phases[(size_t) k]);
                x[(size_t) (N - k)] = std::conj (x[(size_t) k]);
            }
            fft.inverse (x.data());
            std::vector<float> table ((size_t) N);
            for (int i = 0; i < N; ++i)
                table[(size_t) i] = x[(size_t) i].real();
            (variant == 0 ? band.bright : band.soft) = std::move (table);
        }
        bands.push_back (std::move (band));
    }
}

float GlottalTables::harmonicAmplitude (int k, float softness) const
{
    const float a = std::pow ((float) k, -kBrightTilt);
    const float s = std::pow ((float) k, -kSoftTilt);
    return a + (s - a) * softness;
}

//==============================================================================
void VowelSinger::prepare (double sampleRate)
{
    sr = sampleRate;
    invSr = (float) (1.0 / sampleRate);
    reset();
}

void VowelSinger::reset()
{
    phase = 0.0f;
    for (auto& r : res)
        r.reset();
}

void VowelSinger::setFormants (const FormantSpec& spec, float formantScale)
{
    for (int i = 0; i < 5; ++i)
    {
        const float f = std::min (spec.freq[(size_t) i] * formantScale, (float) sr * 0.45f);
        const float bw = spec.bandwidth[(size_t) i] * std::sqrt (formantScale);
        freqs[(size_t) i] = f;
        bws[(size_t) i] = bw;
        gains[(size_t) i] = dbToGain (spec.gainDb[(size_t) i]);
        res[(size_t) i].set (sr, f, bw);
    }
}

void VowelSinger::normalise (const GlottalTables& tables, float f0, float softness)
{
    double power = 0.0;
    for (int k = 1; (double) k * f0 < 0.46 * sr; ++k)
    {
        const double w = 2.0 * kPiD * k * f0 / sr;
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
        std::complex<double> h { 0.0, 0.0 };
        for (int i = 0; i < 5; ++i)
        {
            const auto& r = res[(size_t) i];
            h += (double) gains[(size_t) i] * (double) r.g / (1.0 - (double) r.a1 * z1 - (double) r.a2 * z2);
        }
        const double a = tables.harmonicAmplitude (k, softness) * std::abs (h);
        power += 0.5 * a * a;
    }
    norm = power > 1.0e-12 ? (float) (0.12 / std::sqrt (power)) : 1.0f;
}
} // namespace sc
