// Sanctuary Choir - minimal radix-2 complex FFT (used off the audio thread and
// for table generation; the real-time paths avoid FFTs).
#pragma once

#include <complex>
#include <vector>

namespace sc
{
class FFT
{
public:
    explicit FFT (int order = 10);

    int getSize() const noexcept { return size; }

    /** In-place forward transform (no scaling). */
    void forward (std::complex<float>* data) const { transform (data, false); }
    /** In-place inverse transform, scaled by 1/N. */
    void inverse (std::complex<float>* data) const;

    /** The same transform a piece at a time (to spread it over several audio blocks): the
        bit-reversal, then pass (len) for len = 2, 4, ... size; an inverse is not scaled. */
    int getOrder() const noexcept { return order; }
    void bitReverse (std::complex<float>* data) const;
    void pass (std::complex<float>* data, int len, bool inverse) const;

private:
    void transform (std::complex<float>* data, bool inverse) const;

    int size = 0, order = 0;
    std::vector<int> bitRev;
    std::vector<std::complex<float>> twiddles;
};
} // namespace sc
