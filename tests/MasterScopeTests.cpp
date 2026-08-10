#include "TestHelpers.h"
#include "app/MasterScope.h"

using test::EngineFixture;

namespace
{
void drainTap (AudioEngine& engine)
{
    std::vector<float> l (65536), r (65536);
    while (engine.popScopeSamples (l.data(), r.data(), (int) l.size()) > 0) {}
}

// Feeds a continuous stereo sine into the scope's pull hook, chunked the way
// the engine tap would deliver it.
void feedSine (MasterScope& scope, double freq, float amplitude,
               double sampleRate = 44100.0)
{
    auto phase = std::make_shared<double> (0.0);
    scope.getSampleRate = [sampleRate] { return sampleRate; };
    scope.pullSamples = [phase, freq, amplitude, sampleRate] (float* l, float* r, int maxFrames)
    {
        const int n = juce::jmin (4096, maxFrames);
        for (int i = 0; i < n; ++i)
        {
            const auto s = amplitude * (float) std::sin (*phase);
            l[i] = r[i] = s;
            *phase += 2.0 * juce::MathConstants<double>::pi * freq / sampleRate;
        }
        return n;
    };

    // Several ticks: enough frames to fill the largest FFT window and let the
    // max-hold traces settle.
    for (int tick = 0; tick < 4; ++tick)
        scope.refreshNow();
}
}

TEST (MasterScopeTap, MirrorsMasterOutput)
{
    EngineFixture fx;
    drainTap (fx.engine);

    auto out = fx.renderFromStart (8192);

    std::vector<float> l (16384), r (16384);
    const int got = fx.engine.popScopeSamples (l.data(), r.data(), (int) l.size());
    ASSERT_EQ (got, 8192);
    for (const int i : { 0, 100, 1000, 4095, 8191 })
    {
        EXPECT_FLOAT_EQ (l[(size_t) i], out.getSample (0, i)) << "left drifts at " << i;
        EXPECT_FLOAT_EQ (r[(size_t) i], out.getSample (1, i)) << "right drifts at " << i;
    }
}

TEST (MasterScopeTap, KeepsOldestAndDropsNewestWhenFull)
{
    EngineFixture fx;
    drainTap (fx.engine);

    auto out = fx.renderFromStart (50000);

    std::vector<float> l (65536), r (65536);
    const int got = fx.engine.popScopeSamples (l.data(), r.data(), (int) l.size());
    EXPECT_GT (got, 0);
    EXPECT_LT (got, 50000) << "an undrained tap should overflow and drop";
    EXPECT_EQ (fx.engine.popScopeSamples (l.data(), r.data(), (int) l.size()), 0);

    // Drop-newest: what survived is the start of the render.
    for (const int i : { 0, 1000, got - 1 })
        EXPECT_FLOAT_EQ (l[(size_t) i], out.getSample (0, i)) << "prefix drifts at " << i;
}

TEST (MasterScope, SpectrumSeesTheToneAtItsFrequency)
{
    MasterScope scope;
    feedSine (scope, 1000.0, 0.5f);

    // Amp 0.5 is -6 dBFS; Hann scalloping can cost up to ~1.4 dB.
    EXPECT_GT (scope.spectrumDbAt (1000.0), -9.0f);
    EXPECT_LT (scope.spectrumDbAt (1000.0), 0.0f);
    EXPECT_GT (scope.spectrumDbAt (1000.0), scope.spectrumDbAt (5000.0) + 30.0f)
        << "tone does not stand out of the floor";
    EXPECT_LT (scope.clipMarkAt (1000.0), 0.05f) << "a -6 dB tone must not read as clipping";
}

TEST (MasterScope, RedlineMarksTheClippingFrequency)
{
    MasterScope scope;
    feedSine (scope, 500.0, 2.0f);   // +6 dBFS: well over the line

    EXPECT_GT (scope.spectrumDbAt (500.0), 0.0f);
    EXPECT_GT (scope.clipMarkAt (500.0), 0.5f) << "over never marked";
    EXPECT_LT (scope.clipMarkAt (5000.0), 0.05f) << "clean frequency wrongly marked";
}

TEST (MasterScope, OptionsRoundTripAndSurviveResolutionChange)
{
    MasterScope scope;

    MasterScope::Options o;
    o.fftOrder = 13;
    o.rangeDb = 120;
    o.decayDbPerSecond = 20;
    o.fill = false;
    o.peakHold = false;
    scope.setOptions (o);

    const auto back = scope.getOptions();
    EXPECT_EQ (back.fftOrder, 13);
    EXPECT_EQ (back.rangeDb, 120);
    EXPECT_EQ (back.decayDbPerSecond, 20);
    EXPECT_FALSE (back.fill);
    EXPECT_FALSE (back.peakHold);

    // The analyser still resolves a tone after the FFT was rebuilt.
    feedSine (scope, 1000.0, 0.5f);
    EXPECT_GT (scope.spectrumDbAt (1000.0), scope.spectrumDbAt (5000.0) + 30.0f);

    // Out-of-menu values are clamped, not trusted.
    o.fftOrder = 20;
    o.rangeDb = 999;
    scope.setOptions (o);
    EXPECT_EQ (scope.getOptions().fftOrder, 13);
    EXPECT_EQ (scope.getOptions().rangeDb, 140);
}
