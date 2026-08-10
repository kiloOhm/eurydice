#pragma once

#include "Biquad.h"

namespace fx
{
// Linkwitz-Riley 4th-order split into two or three bands, for one channel —
// hold one per channel. Two cascaded Butterworth sections per edge make the LR4
// pair, whose low and high halves sum to a 2nd-order allpass rather than to
// unity, so in the three-band case the low band is pushed through the matching
// allpass at the high edge. That keeps all three bands phase-coherent and the
// sum flat, which is what lets an effect touch one band and leave the spectrum
// otherwise intact.
struct Crossover3
{
    void setFrequencies (double sampleRate, double lowHz, double highHz) noexcept
    {
        const double lo = lowHz;
        const double hi = juce::jmax (highHz, lowHz);

        for (int s = 0; s < 2; ++s)
        {
            lowLp[s].setLowPass   (sampleRate, lo, butterworthQ);
            lowHp[s].setHighPass  (sampleRate, lo, butterworthQ);
            midLp[s].setLowPass   (sampleRate, hi, butterworthQ);
            midHp[s].setHighPass  (sampleRate, hi, butterworthQ);
        }
        lowAp.setAllPass (sampleRate, hi, butterworthQ);
    }

    void reset() noexcept
    {
        for (int s = 0; s < 2; ++s)
        {
            lowLp[s].reset(); lowHp[s].reset();
            midLp[s].reset(); midHp[s].reset();
        }
        lowAp.reset();
    }

    // Splits one sample low-to-high into bands[0..numBands-1]. numBands is 2
    // (split at the low edge only) or 3.
    void processSample (float x, float* bands, int numBands) noexcept
    {
        if (numBands < 3)
        {
            bands[0] = lowLp[1].processSample (lowLp[0].processSample (x));
            bands[1] = lowHp[1].processSample (lowHp[0].processSample (x));
            return;
        }

        bands[0] = lowAp.processSample (lowLp[1].processSample (lowLp[0].processSample (x)));
        const float top = lowHp[1].processSample (lowHp[0].processSample (x));
        bands[1] = midLp[1].processSample (midLp[0].processSample (top));
        bands[2] = midHp[1].processSample (midHp[0].processSample (top));
    }

private:
    static constexpr double butterworthQ = 0.70710678;   // two cascaded == LR4

    Biquad lowLp[2], lowHp[2], midLp[2], midHp[2], lowAp;
};
} // namespace fx
