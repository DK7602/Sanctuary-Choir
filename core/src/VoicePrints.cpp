#include "sanctuary/VoicePrints.h"
#include "sanctuary/Theory.h"

#include <algorithm>
#include <vector>

namespace sc
{
namespace
{
    const VoicePrint prints[] = {
#include "VoicePrints.inc"
    };
    constexpr int numPrints = (int) (sizeof (prints) / sizeof (prints[0]));

    /** Indices of one sex's prints, shortest vocal tract (highest formants) first. */
    const std::vector<int>& bySize (bool female)
    {
        static const auto make = [] (bool f)
        {
            std::vector<int> v;
            for (int i = 0; i < numPrints; ++i)
                if (prints[i].female == f)
                    v.push_back (i);
            std::stable_sort (v.begin(), v.end(), [] (int a, int b) { return prints[a].formantScale > prints[b].formantScale; });
            return v;
        };
        static const std::vector<int> women = make (true), men = make (false);
        return female ? women : men;
    }

    float mean (bool female, float VoicePrint::*field)
    {
        float sum = 0.0f;
        int n = 0;
        for (const auto& p : prints)
            if (p.female == female) { sum += p.*field; ++n; }
        return n > 0 ? sum / (float) n : 0.0f;
    }
}

const VoicePrint* getVoicePrints (int& count) noexcept
{
    count = numPrints;
    return prints;
}

const std::array<VoicePrintBand, 6>& getVoicePrintBands() noexcept
{
    // The same bands build_voiceprints.py fits.
    static const std::array<VoicePrintBand, 6> bands { { { VoicePrintBand::LowShelf, 180.0f, 0.707f }, { VoicePrintBand::Peak, 420.0f, 1.2f },
                                                         { VoicePrintBand::Peak, 850.0f, 1.2f }, { VoicePrintBand::Peak, 1700.0f, 1.2f },
                                                         { VoicePrintBand::Peak, 2900.0f, 1.8f }, { VoicePrintBand::HighShelf, 5000.0f, 0.707f } } };
    return bands;
}

float meanVibratoHz (bool female) noexcept { return mean (female, &VoicePrint::vibratoHz); }
float meanVibratoCents (bool female) noexcept { return mean (female, &VoicePrint::vibratoCents); }

int voicePrintFor (int section, int index, bool children, int* round) noexcept
{
    const bool female = children || section == Soprano || section == Alto;
    const auto& order = bySize (female);
    if (order.empty() || index < 0)
        return -1;
    const int n = (int) order.size();
    if (round != nullptr)
        *round = index / n;
    const int k = index % n;
    // Sopranos and tenors from the shortest tract down, altos and basses from the longest up.
    const bool high = section == Soprano || section == Tenor;
    return order[(size_t) (high ? k : n - 1 - k)];
}
} // namespace sc
