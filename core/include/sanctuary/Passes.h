// Sanctuary Choir - more passes of the same part (the multi-take choir).
//
// Sing the part two to four times and the extra passes become real doubles: in Words mode
// some of the choir sing from each pass, with that pass's own breaths, pitch and colour,
// instead of everyone being copies of one take. Each pass is first lined up with the main
// take, note by note, so the choir still sings together.
#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

namespace sc
{
constexpr int kMaxExtraPasses = 3;   // up to four passes with the main take

struct PassAlignment
{
    int64_t offset = 0;      // where the pass starts against the take (samples; > 0: later)
    float coverage = 0.0f;   // share of the take's singing the pass also sings (0..1)
    float drift = 0.0f;      // the most the pass had to be moved from that start (seconds)
    float gainDb = 0.0f;     // level change to match the take
};

/** Lines `pass` up with `take` and returns it on the take's timeline (n samples, levelled to
    the take). Where the pass starts is `hint` (samples, the pass's start minus the take's,
    known when both were recorded against the song), else it is searched for. Then dynamic
    time warping on the two voices' sound follows the singer's timing note by note, and the
    pass is time-stretched to it (WSOLA, the pitch untouched). Returns an empty vector if
    cancelled. */
std::vector<float> alignPass (const float* take, int64_t n, const float* pass, int64_t m, double sampleRate,
                              const int64_t* hint = nullptr, PassAlignment* info = nullptr,
                              const std::atomic<bool>* cancel = nullptr);
} // namespace sc
