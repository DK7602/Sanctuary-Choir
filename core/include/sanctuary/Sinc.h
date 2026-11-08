// Sanctuary Choir - windowed-sinc interpolation, shared by everything that reads a recording
// between its samples (the recorded voices, and the Words singers' grains of your voice).
#pragma once

#include "Common.h"

#include <cmath>
#include <vector>

namespace sc
{
/** Windowed-sinc interpolation: 8 taps (Kaiser, beta 6), Phases positions between two samples
    (the nearest is used), each normalised to unit gain. Each table is built on first use, so
    call its function off the audio thread first. The recorded voices read with 2048 positions:
    on the pack's 32 kHz recordings that is flat to 8 kHz and 0.4 dB down at 10 kHz, where
    reading between two samples in a straight line lost 2.8 dB, and up to 8 dB at 12 kHz that
    came and went as the pitch moved. */
template <int Phases>
struct SincTableOf
{
    static constexpr int taps = 8, phases = Phases;
    std::vector<float> w;

    SincTableOf() : w ((size_t) ((phases + 1) * taps))
    {
        auto bessel0 = [] (double x)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 32; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
            }
            return sum;
        };
        const double beta = 6.0, cut = 0.95;
        for (int p = 0; p <= phases; ++p)
        {
            const double frac = (double) p / phases;
            double sum = 0.0;
            float* row = w.data() + p * taps;
            for (int k = 0; k < taps; ++k)
            {
                // Tap k is the sample k - 3 places from the one before the read position.
                const double t = (double) (k - (taps / 2 - 1)) - frac;
                const double u = 2.0 * t / taps;
                const double window = std::abs (u) < 1.0 ? bessel0 (beta * std::sqrt (1.0 - u * u)) / bessel0 (beta) : 0.0;
                const double x = cut * t;
                const double sinc = std::abs (x) < 1.0e-9 ? 1.0 : std::sin (kPi * x) / (kPi * x);
                row[k] = (float) (sinc * window);
                sum += row[k];
            }
            for (int k = 0; k < taps; ++k)
                row[k] = (float) (row[k] / sum);
        }
    }
};

using SincTable = SincTableOf<2048>;

inline const SincTable& sincTable()
{
    static const SincTable table;
    return table;
}

/** The same with 256 positions between two samples: 8 KB, so it stays in the fastest cache when
    every read lands somewhere new (the Words singers' grains). At 48 kHz the nearest position is
    out by at most 1/512 of a sample: errors below -47 dB up to 16 kHz. */
using SmallSincTable = SincTableOf<256>;

inline const SmallSincTable& smallSincTable()
{
    static const SmallSincTable table;
    return table;
}

/** Reads a source (read (i) returns sample i) at a fractional position with one of the tables. */
template <typename Table, typename Read>
inline float sincRead (const Table& table, double pos, Read&& read) noexcept
{
    constexpr int taps = Table::taps;
    const double floorPos = std::floor (pos);
    const int64_t first = (int64_t) floorPos - (taps / 2 - 1);
    const int p = (int) ((pos - floorPos) * Table::phases + 0.5);
    const float* w = table.w.data() + p * taps;
    float acc = 0.0f;
    for (int k = 0; k < taps; ++k)
        acc += w[k] * read (first + k);
    return acc;
}
} // namespace sc
