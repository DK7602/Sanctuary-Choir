// Sanctuary Choir - shared helpers for the DSP core.
// The core has no framework dependencies so it can be unit tested, used by the
// offline render tool, and wrapped by the JUCE plugin.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sc
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;
constexpr double kPiD = 3.14159265358979323846;

enum Section : int { Soprano = 0, Alto = 1, Tenor = 2, Bass = 3, NumSections = 4 };

inline float midiToHz (float midi, float a4 = 440.0f) { return a4 * std::exp2 ((midi - 69.0f) / 12.0f); }
inline float hzToMidi (float hz, float a4 = 440.0f) { return 69.0f + 12.0f * std::log2 (std::max (hz, 1.0e-3f) / a4); }
inline float dbToGain (float db) { return db <= -96.0f ? 0.0f : std::pow (10.0f, db / 20.0f); }
inline float gainToDb (float g) { return g <= 1.0e-6f ? -120.0f : 20.0f * std::log10 (g); }
inline float centsToRatio (float cents) { return std::exp2 (cents / 1200.0f); }

template <typename T>
inline T clampv (T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline int wrapPc (int pc) { return ((pc % 12) + 12) % 12; }

/** One-pole coefficient for a time constant in seconds. */
inline float onePoleCoeff (float seconds, double sampleRate)
{
    if (seconds <= 0.0f)
        return 0.0f;
    return (float) std::exp (-1.0 / (seconds * sampleRate));
}

/** Small, fast, deterministic random generator (xorshift32). */
struct Rng
{
    uint32_t state = 0x9E3779B9u;

    explicit Rng (uint32_t seed = 1u) { reseed (seed); }
    void reseed (uint32_t seed) { state = seed * 2654435761u + 0x6C8E9CF5u; if (state == 0) state = 1; next(); next(); }

    uint32_t next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    /** Uniform in [0, 1). */
    float uni() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
    /** Uniform in [-1, 1). */
    float bi() { return uni() * 2.0f - 1.0f; }
    /** Approximately normal(0,1) (sum of uniforms). */
    float gauss() { return (uni() + uni() + uni() + uni() - 2.0f) * 1.7320508f; }
};

/** Exponential parameter smoother. */
struct Smoothed
{
    float current = 0.0f, target = 0.0f, coeff = 0.0f;

    void prepare (double sampleRate, float seconds, float initial)
    {
        coeff = onePoleCoeff (seconds, sampleRate);
        current = target = initial;
    }
    void setTarget (float t) { target = t; }
    void snap (float v) { current = target = v; }
    float next() { current = target + (current - target) * coeff; return current; }
    /** Advance by n samples at once (for control-rate use). */
    float advance (int n) { current = target + (current - target) * std::pow (coeff, (float) n); return current; }
};

/** Smoothly varying random value with roughly unit variance, used for pitch drift and
    humanisation. An Ornstein-Uhlenbeck process followed by a one-pole smoother. */
struct Wander
{
    float raw = 0.0f, value = 0.0f;
    float timeConstant = 1.5f; // seconds; larger = slower wandering

    float tick (Rng& rng, float dt)
    {
        const float a = clampv (dt / timeConstant, 0.0f, 1.0f);
        raw += -raw * a + std::sqrt (2.0f * a) * rng.gauss();
        const float s = clampv (dt / (0.25f * timeConstant), 0.0f, 1.0f);
        value += (raw - value) * s;
        return value;
    }
};

} // namespace sc
