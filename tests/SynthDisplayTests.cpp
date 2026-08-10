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

// ---- unison / layers / voice ----

TEST (SynthDisplays, SingleUnisonVoiceSitsInTheMiddle)
{
    const auto voices = synthdisplays::unisonVoices (1, 25.0f, 1.0f);
    ASSERT_EQ ((int) voices.size(), 1);
    EXPECT_FLOAT_EQ (voices[0].cents, 0.0f) << "a lone voice must not be detuned";
    EXPECT_FLOAT_EQ (voices[0].pan, 0.0f) << "a lone voice must not be panned";
}

TEST (SynthDisplays, UnisonSpreadsSymmetricallyToTheDetuneAmount)
{
    const auto voices = synthdisplays::unisonVoices (7, 30.0f, 1.0f);
    ASSERT_EQ ((int) voices.size(), 7);

    EXPECT_FLOAT_EQ (voices.front().cents, -30.0f) << "outer voice misses the detune amount";
    EXPECT_FLOAT_EQ (voices.back().cents, 30.0f) << "outer voice misses the detune amount";
    EXPECT_NEAR (voices[3].cents, 0.0f, 1.0e-5f) << "centre voice should be at pitch";

    for (size_t i = 1; i < voices.size(); ++i)
        EXPECT_GT (voices[i].cents, voices[i - 1].cents) << "voices are not ordered";
}

TEST (SynthDisplays, UnisonWidthScalesThePanSpread)
{
    const auto wide = synthdisplays::unisonVoices (5, 20.0f, 1.0f);
    const auto narrow = synthdisplays::unisonVoices (5, 20.0f, 0.25f);
    const auto mono = synthdisplays::unisonVoices (5, 20.0f, 0.0f);

    EXPECT_FLOAT_EQ (wide.front().pan, -1.0f) << "full width should reach hard left";
    EXPECT_LT (std::abs (narrow.front().pan), std::abs (wide.front().pan))
        << "narrowing width did not pull the voices in";

    for (const auto& voice : mono)
        EXPECT_FLOAT_EQ (voice.pan, 0.0f) << "zero width must collapse to mono";

    // Detune is independent of width: the stack still spreads in pitch.
    EXPECT_FLOAT_EQ (mono.front().cents, -20.0f);
}

TEST (SynthDisplays, GlideRampReachesTargetAndSlowsWithTime)
{
    const auto instant = synthdisplays::glideShape (0.0f, 120);
    EXPECT_GT (instant.front(), 0.99f) << "zero glide should jump straight to the note";

    const auto quick = synthdisplays::glideShape (0.05f, 120);
    const auto slow = synthdisplays::glideShape (0.8f, 120);

    EXPECT_GT (quick[20], slow[20]) << "a longer glide should still be further from the note";
    EXPECT_GT (quick.back(), 0.95f) << "a short glide should have arrived by the end";
    EXPECT_LT (slow.back(), quick.back()) << "the slow glide should lag the quick one";

    for (size_t i = 1; i < slow.size(); ++i)
        EXPECT_GE (slow[i], slow[i - 1]) << "glide should approach the target monotonically";
}

TEST (SynthDisplays, SubLayerAddsTheOctaveDownComponent)
{
    constexpr int points = 256;   // two cycles, so the sub spans exactly one
    const auto dry = synthdisplays::layerShape (0.0f, 0.0f, 0.0f, 0.0f, points);
    const auto withSub = synthdisplays::layerShape (0.0f, 0.0f, 0.8f, 0.0f, points);

    // Without a sub the two cycles are identical; the sub is an octave down,
    // so it breaks that symmetry.
    auto cycleGap = [points] (const std::vector<float>& wave)
    {
        float worst = 0.0f;
        for (int i = 0; i < points / 2; ++i)
            worst = juce::jmax (worst, std::abs (wave[(size_t) i]
                                                 - wave[(size_t) (i + points / 2)]));
        return worst;
    };

    EXPECT_LT (cycleGap (dry), 0.01f) << "the dry oscillator's two cycles should match";
    EXPECT_GT (cycleGap (withSub), 0.1f) << "the sub octave never showed up";
}

TEST (SynthDisplays, NoiseLayerAddsHighFrequencyFuzz)
{
    constexpr int points = 256;
    auto roughness = [] (const std::vector<float>& wave)
    {
        float sum = 0.0f;
        for (size_t i = 1; i < wave.size(); ++i)
            sum += std::abs (wave[i] - wave[i - 1]);
        return sum / (float) (wave.size() - 1);
    };

    const auto clean = synthdisplays::layerShape (-2.0f, 0.0f, 0.0f, 0.0f, points);
    const auto noisy = synthdisplays::layerShape (-2.0f, 0.0f, 0.0f, 0.8f, points);

    EXPECT_GT (roughness (noisy), roughness (clean) * 3.0f) << "noise layer is not audible";
}

TEST (SynthDisplays, LayerShapeStaysInRange)
{
    // Everything at once still has to fit the display frame.
    const auto wave = synthdisplays::layerShape (0.0f, 0.5f, 1.0f, 1.0f, 256);
    for (const float s : wave)
        EXPECT_LE (std::abs (s), 1.0f) << "layer sum overflows the display";
}
