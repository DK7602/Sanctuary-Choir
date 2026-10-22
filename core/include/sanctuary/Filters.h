// Sanctuary Choir - small filter building blocks.
#pragma once

#include "Common.h"

namespace sc
{
/** Transposed direct form II biquad with RBJ cookbook designs. */
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }

    inline float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setNormalised (double B0, double B1, double B2, double A0, double A1, double A2)
    {
        b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0);
        a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
    }

    void lowPass (double sr, double f, double q = 0.70710678)
    {
        const double w = 2.0 * kPiD * clampv (f, 10.0, sr * 0.49) / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        setNormalised ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void highPass (double sr, double f, double q = 0.70710678)
    {
        const double w = 2.0 * kPiD * clampv (f, 10.0, sr * 0.49) / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        setNormalised ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    /** Band-pass with 0 dB peak gain. */
    void bandPass (double sr, double f, double q)
    {
        const double w = 2.0 * kPiD * clampv (f, 10.0, sr * 0.49) / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        setNormalised (al, 0, -al, 1 + al, -2 * c, 1 - al);
    }
    void peak (double sr, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * kPiD * clampv (f, 10.0, sr * 0.49) / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        setNormalised (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void lowShelf (double sr, double f, double gainDb, double slope = 1.0)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * kPiD * clampv (f, 10.0, sr * 0.49) / sr, c = std::cos (w), s = std::sin (w);
        const double al = s / 2.0 * std::sqrt ((A + 1 / A) * (1 / slope - 1) + 2), sa = 2 * std::sqrt (A) * al;
        setNormalised (A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sa),
                       (A + 1) + (A - 1) * c + sa, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa);
    }
    void highShelf (double sr, double f, double gainDb, double slope = 1.0)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * kPiD * clampv (f, 10.0, sr * 0.49) / sr, c = std::cos (w), s = std::sin (w);
        const double al = s / 2.0 * std::sqrt ((A + 1 / A) * (1 / slope - 1) + 2), sa = 2 * std::sqrt (A) * al;
        setNormalised (A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa),
                       (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
    }
};

/** Two-pole resonator with unity peak gain, cheap to retune per sample block (formants). */
struct Resonator
{
    float a1 = 0, a2 = 0, g = 0;
    float y1 = 0, y2 = 0;

    void reset() { y1 = y2 = 0; }

    void set (double sr, double freq, double bandwidth)
    {
        const double r = std::exp (-kPiD * bandwidth / sr);
        const double w = 2.0 * kPiD * clampv (freq, 20.0, sr * 0.45) / sr;
        a1 = (float) (2.0 * r * std::cos (w));
        a2 = (float) (-r * r);
        // Normalise so the response at the centre frequency is ~1.
        g = (float) ((1.0 - r) * std::sqrt (1.0 - 2.0 * r * std::cos (2.0 * w) + r * r));
    }

    inline float process (float x)
    {
        const float y = g * x + a1 * y1 + a2 * y2;
        y2 = y1;
        y1 = y;
        return y;
    }
};

struct OnePoleLP
{
    float a = 0, z = 0;
    void setCutoff (double sr, double f) { a = (float) std::exp (-2.0 * kPiD * clampv (f, 1.0, sr * 0.49) / sr); }
    inline float process (float x) { z = x + (z - x) * a; return z; }
    void reset (float v = 0) { z = v; }
};

struct DcBlocker
{
    float r = 0.995f, x1 = 0, y1 = 0;
    void prepare (double sr) { r = (float) std::exp (-2.0 * kPiD * 20.0 / sr); }
    inline float process (float x) { const float y = x - x1 + r * y1; x1 = x; y1 = y; return y; }
    void reset() { x1 = y1 = 0; }
};

/** Envelope follower with separate attack and release. */
struct EnvelopeFollower
{
    float att = 0, rel = 0, env = 0;
    void prepare (double sr, float attackS, float releaseS) { att = onePoleCoeff (attackS, sr); rel = onePoleCoeff (releaseS, sr); }
    inline float process (float x)
    {
        const float a = std::abs (x);
        env = a + (env - a) * (a > env ? att : rel);
        return env;
    }
};

} // namespace sc
