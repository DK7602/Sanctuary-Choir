#include "sanctuary/FFT.h"
#include "sanctuary/Common.h"

namespace sc
{
FFT::FFT (int order) : size (1 << order), bitRev ((size_t) size), twiddles ((size_t) size / 2)
{
    for (int i = 0; i < size; ++i)
    {
        int r = 0;
        for (int b = 0; b < order; ++b)
            if (i & (1 << b))
                r |= 1 << (order - 1 - b);
        bitRev[(size_t) i] = r;
    }

    for (int i = 0; i < size / 2; ++i)
    {
        const double a = -2.0 * kPiD * i / size;
        twiddles[(size_t) i] = { (float) std::cos (a), (float) std::sin (a) };
    }
}

void FFT::transform (std::complex<float>* data, bool inv) const
{
    for (int i = 0; i < size; ++i)
        if (i < bitRev[(size_t) i])
            std::swap (data[i], data[bitRev[(size_t) i]]);

    for (int len = 2; len <= size; len <<= 1)
    {
        const int half = len >> 1, step = size / len;
        for (int i = 0; i < size; i += len)
        {
            for (int j = 0; j < half; ++j)
            {
                auto w = twiddles[(size_t) (j * step)];
                if (inv)
                    w = std::conj (w);
                const auto u = data[i + j];
                const auto v = data[i + j + half] * w;
                data[i + j] = u + v;
                data[i + j + half] = u - v;
            }
        }
    }
}

void FFT::inverse (std::complex<float>* data) const
{
    transform (data, true);
    const float s = 1.0f / (float) size;
    for (int i = 0; i < size; ++i)
        data[i] *= s;
}
} // namespace sc
