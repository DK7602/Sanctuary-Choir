// Sanctuary Choir - voice prints of real singers.
//
// How each VocalSet singer differs from the average singer of their sex: the length of their
// vocal tract (a formant scale), the colour of their voice once that is allowed for (six EQ
// bands), and their own vibrato. In Words mode every choir member is the lead's own voice; a
// voice print makes each one sound like a different person. Measured by
// tools/voiceprints/build_voiceprints.py.
#pragma once

#include <array>

namespace sc
{
struct VoicePrint
{
    const char* singer;
    bool female;
    float centre;                 // median pitch they sang (MIDI)
    float formantScale;           // their formants against the average voice (>1: a shorter tract)
    float vibratoHz, vibratoCents;
    std::array<float, 6> eqDb;    // their colour, on the bands below
};

struct VoicePrintBand
{
    enum Kind { LowShelf, Peak, HighShelf };
    Kind kind;
    float hz, q;
};

/** All the prints (women first). */
const VoicePrint* getVoicePrints (int& count) noexcept;
const std::array<VoicePrintBand, 6>& getVoicePrintBands() noexcept;
/** Average vibrato of the women's (or men's) prints. */
float meanVibratoHz (bool female) noexcept;
float meanVibratoCents (bool female) noexcept;

/** The print for a section's member (index from 0): women's prints for sopranos and altos,
    men's for tenors and basses, the women's for every part of a children's choir. Sopranos and
    tenors take the shortest vocal tracts first, altos and basses the longest, so each section
    starts with its own singers. Past the number of prints they come round again; `round` says
    how many times (0 the first), so a repeat can be made a little different. */
int voicePrintFor (int section, int index, bool children, int* round = nullptr) noexcept;
} // namespace sc
