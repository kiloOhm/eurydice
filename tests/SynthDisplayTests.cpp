#include <gtest/gtest.h>
#include "ui/rack/SynthDisplays.h"

// The synth editor's displays draw from these helpers; they must describe the
// DSP that actually runs (the filter is measured, the envelope simulated).

TEST (SynthDisplays, FilterResponseMatchesTheType)
{
    const std::vector<float> freqs { 100.0f, 1000.0f, 10000.0f };

    const auto lp = synthdisplays::filterResponseDb (0, 1000.0f, 0.0f, freqs);
    EXPECT_GT (lp[0], -3.0f) << "lowpass passband sags";
    EXPECT_LT (lp[2], -20.0f) << "lowpass lets highs through";

    const auto hp = synthdisplays::filterResponseDb (2, 1000.0f, 0.0f, freqs);
    EXPECT_LT (hp[0], -20.0f) << "highpass lets lows through";
    EXPECT_GT (hp[2], -3.0f) << "highpass passband sags";

    const auto bp = synthdisplays::filterResponseDb (1, 1000.0f, 0.0f, freqs);
    EXPECT_GT (bp[1], bp[0] + 10.0f) << "bandpass centre not above the lows";
    EXPECT_GT (bp[1], bp[2] + 10.0f) << "bandpass centre not above the highs";
}

TEST (SynthDisplays, ResonancePeaksAtTheCutoff)
{
    const std::vector<float> atCutoff { 1000.0f };
    const auto flat = synthdisplays::filterResponseDb (0, 1000.0f, 0.0f, atCutoff);
    const auto peaked = synthdisplays::filterResponseDb (0, 1000.0f, 1.0f, atCutoff);
    EXPECT_GT (peaked[0], flat[0] + 6.0f) << "resonance knob does not peak the plot";
}

TEST (SynthDisplays, EnvelopeShapeHasTheFourStages)
{
    const auto curve = synthdisplays::envelopeShape (0.05f, 0.1f, 0.5f, 0.1f, 200);
    ASSERT_EQ ((int) curve.size(), 200);

    const float peak = *std::max_element (curve.begin(), curve.end());
    EXPECT_GT (peak, 0.95f) << "attack never reaches full level";

    // Around two-thirds in, the envelope sits on the sustain level.
    EXPECT_NEAR (curve[130], 0.5f, 0.05f) << "sustain plateau wrong";

    EXPECT_LT (curve.back(), 0.05f) << "release does not come back down";
}

TEST (SynthDisplays, ZeroSustainEnvelopeDecaysToSilence)
{
    const auto curve = synthdisplays::envelopeShape (0.01f, 0.1f, 0.0f, 0.05f, 200);
    EXPECT_NEAR (curve[120], 0.0f, 0.03f) << "plucked envelope should sit at zero";
}
