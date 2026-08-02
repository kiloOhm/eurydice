#include <gtest/gtest.h>
#include "effects/ClipperEffect.h"
#include "effects/CompressorEffect.h"
#include "effects/DelayEffect.h"
#include "effects/EffectRegistry.h"
#include "effects/EqEffect.h"
#include "effects/FilterEffect.h"
#include "effects/ReverbEffect.h"
#include "ui/mixer/BuiltinEffectEditor.h"
#include "TestHelpers.h"

namespace
{
constexpr double sr = test::kSampleRate;
constexpr int block = test::kBlockSize;

juce::AudioBuffer<float> makeTone (double freq, float amp, int numSamples, double phase = 0.0)
{
    juce::AudioBuffer<float> buffer (2, numSamples);
    for (int i = 0; i < numSamples; ++i)
    {
        const auto s = (float) (amp * std::sin (juce::MathConstants<double>::twoPi * freq * i / sr + phase));
        buffer.setSample (0, i, s);
        buffer.setSample (1, i, s);
    }
    return buffer;
}

juce::AudioBuffer<float> makeSilence (int numSamples)
{
    juce::AudioBuffer<float> buffer (2, numSamples);
    buffer.clear();
    return buffer;
}

juce::AudioBuffer<float> makeImpulse (int numSamples, float amp = 1.0f, bool leftOnly = false)
{
    auto buffer = makeSilence (numSamples);
    buffer.setSample (0, 0, amp);
    if (! leftOnly)
        buffer.setSample (1, 0, amp);
    return buffer;
}

// Processes in place, in blocks, exactly like the engine does.
void renderThrough (Effect& effect, juce::AudioBuffer<float>& buffer,
                    const Effect::Context& context = {})
{
    const int total = buffer.getNumSamples();
    for (int pos = 0; pos < total; pos += block)
    {
        const int len = juce::jmin (block, total - pos);
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(),
                                       buffer.getNumChannels(), pos, len);
        effect.process (view, len, context);
    }
}

// Amplitude of one frequency component, via a Hann-windowed single-bin DFT.
double toneAmplitude (const juce::AudioBuffer<float>& buffer, int channel,
                      int start, int length, double freq)
{
    double re = 0.0, im = 0.0, windowSum = 0.0;
    for (int i = 0; i < length; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / (length - 1));
        const double s = (double) buffer.getSample (channel, start + i) * w;
        const double phase = juce::MathConstants<double>::twoPi * freq * i / sr;
        re += s * std::cos (phase);
        im -= s * std::sin (phase);
        windowSum += w;
    }
    return 2.0 * std::sqrt (re * re + im * im) / windowSum;
}

void setParams (BuiltinEffect& effect, std::initializer_list<std::pair<juce::Identifier, double>> values)
{
    for (const auto& [id, value] : values)
        effect.setParameter (id, value);
}

// Every parameter at its documented default, as a fresh slot would be.
void applyDefaults (BuiltinEffect& effect)
{
    effect.applyParameters (juce::ValueTree (ids::SLOT));
}
} // namespace

// ============================= registry ==============================

TEST (EffectRegistry, ExposesEveryBuiltinAndCreatesIt)
{
    const auto& entries = fx::builtinEffects();
    EXPECT_EQ (entries.size(), 6u);

    for (const auto& entry : entries)
    {
        EXPECT_TRUE (fx::isBuiltinId (entry.id)) << entry.id;
        EXPECT_FALSE (entry.specs.empty()) << entry.id;

        auto effect = fx::createBuiltin (entry.id);
        ASSERT_NE (effect, nullptr) << entry.id;
        effect->prepare (sr, block);
        applyDefaults (*effect);

        auto buffer = makeTone (440.0, 0.3f, block * 4);
        renderThrough (*effect, buffer);
        EXPECT_TRUE (std::isfinite (buffer.getMagnitude (0, 0, buffer.getNumSamples()))) << entry.id;
    }

    EXPECT_EQ (fx::createBuiltin ("builtin:nope"), nullptr);
    EXPECT_FALSE (fx::isBuiltinId ("VST3-1234"));
}

TEST (EffectRegistry, SilenceInSilenceOut)
{
    for (const auto& entry : fx::builtinEffects())
    {
        auto effect = fx::createBuiltin (entry.id);
        ASSERT_NE (effect, nullptr);
        effect->prepare (sr, block);
        applyDefaults (*effect);

        auto buffer = makeSilence (block * 8);
        renderThrough (*effect, buffer);
        EXPECT_EQ (buffer.getMagnitude (0, 0, buffer.getNumSamples()), 0.0f) << entry.id;
        EXPECT_EQ (buffer.getMagnitude (1, 0, buffer.getNumSamples()), 0.0f) << entry.id;
    }
}

TEST (EffectRegistry, ParameterDefaultsRoundTripThroughASlotTree)
{
    for (const auto& entry : fx::builtinEffects())
    {
        juce::ValueTree slot (ids::SLOT);
        BuiltinEffect::writeDefaults (slot, entry.specs, nullptr);
        for (const auto& spec : entry.specs)
        {
            ASSERT_TRUE (slot.hasProperty (spec.id)) << entry.id << " / " << spec.name;
            EXPECT_DOUBLE_EQ ((double) slot[spec.id], spec.defaultValue);
            EXPECT_LE (spec.minValue, spec.defaultValue);
            EXPECT_GE (spec.maxValue, spec.defaultValue);
        }
    }
}

// ============================== clipper ==============================

TEST (ClipperEffect, CurvesStayBounded)
{
    for (int curve = 0; curve < 4; ++curve)
        for (float x = -40.0f; x <= 40.0f; x += 0.01f)
        {
            const float y = ClipperEffect::shapeSample (curve, x);
            ASSERT_TRUE (std::isfinite (y)) << curve << " @ " << x;
            ASSERT_LE (std::abs (y), 1.001f) << curve << " @ " << x;
        }
}

TEST (ClipperEffect, HardCurveLimitsPeaks)
{
    ClipperEffect clipper;
    clipper.prepare (sr, block);
    setParams (clipper, { { ids::fxCurve, 1.0 }, { ids::fxDrive, 24.0 },
                          { ids::fxOversample, 0.0 }, { ids::fxOutput, 0.0 },
                          { ids::fxMix, 1.0 } });

    auto buffer = makeTone (50.0, 0.5f, block * 8);
    renderThrough (clipper, buffer);

    const float peak = buffer.getMagnitude (0, 0, buffer.getNumSamples());
    EXPECT_LE (peak, 1.001f);
    EXPECT_GT (peak, 0.95f);   // driven well into the ceiling
}

TEST (ClipperEffect, OutputTrimScalesTheCeiling)
{
    ClipperEffect clipper;
    clipper.prepare (sr, block);
    setParams (clipper, { { ids::fxCurve, 1.0 }, { ids::fxDrive, 24.0 },
                          { ids::fxOversample, 0.0 }, { ids::fxOutput, -6.0 },
                          { ids::fxMix, 1.0 } });

    auto buffer = makeTone (50.0, 0.5f, block * 8);
    renderThrough (clipper, buffer);
    EXPECT_NEAR (buffer.getMagnitude (0, 0, buffer.getNumSamples()), 0.5012f, 0.01f);
}

TEST (ClipperEffect, OversamplingLowersAliasing)
{
    // A 5 kHz sine squared off puts harmonics at 15/25/35/45 kHz. At 44.1 kHz
    // the 45 kHz one folds down to 900 Hz, which is nowhere near a harmonic —
    // so energy there is aliasing and nothing else.
    constexpr double toneHz = 5000.0;
    constexpr double aliasHz = 900.0;
    const int numSamples = block * 32;

    auto measure = [&] (int oversampleIndex)
    {
        ClipperEffect clipper;
        clipper.prepare (sr, block);
        setParams (clipper, { { ids::fxCurve, 1.0 }, { ids::fxDrive, 18.0 },
                              { ids::fxOversample, (double) oversampleIndex },
                              { ids::fxOutput, 0.0 }, { ids::fxMix, 1.0 } });
        auto buffer = makeTone (toneHz, 0.8f, numSamples);
        renderThrough (clipper, buffer);
        return toneAmplitude (buffer, 0, block * 4, block * 24, aliasHz);
    };

    const double naive = measure (0);
    const double oversampled = measure (3);   // 8x

    EXPECT_GT (naive, 0.01);                       // the naive path really does alias
    EXPECT_LT (oversampled, naive * 0.2);          // and oversampling kills most of it
}

TEST (ClipperEffect, FullyDryPassesTheInputThrough)
{
    ClipperEffect clipper;
    clipper.prepare (sr, block);
    setParams (clipper, { { ids::fxCurve, 1.0 }, { ids::fxDrive, 36.0 },
                          { ids::fxOversample, 0.0 }, { ids::fxMix, 0.0 } });

    const auto input = makeTone (220.0, 0.4f, block * 4);
    auto buffer = input;
    renderThrough (clipper, buffer);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
        ASSERT_NEAR (buffer.getSample (0, i), input.getSample (0, i), 1.0e-6f) << i;
}

// ============================== filter ===============================

TEST (FilterEffect, LowPassAttenuatesAboveCutoff)
{
    auto measure = [] (double freq)
    {
        FilterEffect filter;
        filter.prepare (sr, block);
        applyDefaults (filter);
        setParams (filter, { { ids::fxFilterType, 0.0 }, { ids::fxCutoff, 1000.0 },
                             { ids::fxResonance, 0.0 }, { ids::fxMix, 1.0 } });
        auto buffer = makeTone (freq, 0.5f, block * 16);
        renderThrough (filter, buffer);
        return toneAmplitude (buffer, 0, block * 4, block * 10, freq);
    };

    EXPECT_GT (measure (200.0), 0.4);      // passband barely touched
    EXPECT_LT (measure (8000.0), 0.05);    // three octaves up, well down
}

TEST (FilterEffect, HighPassAttenuatesBelowCutoff)
{
    auto measure = [] (double freq)
    {
        FilterEffect filter;
        filter.prepare (sr, block);
        applyDefaults (filter);
        setParams (filter, { { ids::fxFilterType, 1.0 }, { ids::fxCutoff, 1000.0 },
                             { ids::fxResonance, 0.0 }, { ids::fxMix, 1.0 } });
        auto buffer = makeTone (freq, 0.5f, block * 16);
        renderThrough (filter, buffer);
        return toneAmplitude (buffer, 0, block * 4, block * 10, freq);
    };

    EXPECT_LT (measure (100.0), 0.1);
    EXPECT_GT (measure (8000.0), 0.4);
}

TEST (FilterEffect, BandPassRejectsBothSides)
{
    auto measure = [] (double freq)
    {
        FilterEffect filter;
        filter.prepare (sr, block);
        applyDefaults (filter);
        setParams (filter, { { ids::fxFilterType, 2.0 }, { ids::fxCutoff, 1000.0 },
                             { ids::fxResonance, 0.3 }, { ids::fxMix, 1.0 } });
        auto buffer = makeTone (freq, 0.5f, block * 16);
        renderThrough (filter, buffer);
        return toneAmplitude (buffer, 0, block * 4, block * 10, freq);
    };

    const double centre = measure (1000.0);
    EXPECT_GT (centre, measure (60.0) * 4.0);
    EXPECT_GT (centre, measure (12000.0) * 4.0);
}

TEST (FilterEffect, TempoSyncedLfoSweepsTheCutoff)
{
    FilterEffect filter;
    filter.prepare (sr, block);
    applyDefaults (filter);
    // One-bar sine LFO at 120 BPM = 2 s; a lowpass parked low sweeps a bright
    // tone in and out over that period.
    setParams (filter, { { ids::fxFilterType, 0.0 }, { ids::fxCutoff, 300.0 },
                         { ids::fxResonance, 0.0 }, { ids::fxLfoAmount, 1.0 },
                         { ids::fxLfoRate, 12.0 }, { ids::fxLfoShape, 0.0 },
                         { ids::fxMix, 1.0 } });

    Effect::Context context;
    context.tempo = 120.0;
    auto buffer = makeTone (4000.0, 0.5f, (int) (sr * 2.0));
    renderThrough (filter, buffer, context);

    // Quarter of the way in the sine LFO is at its peak (cutoff way above the
    // tone); three quarters in it is at its trough (tone filtered out).
    const float open = buffer.getRMSLevel (0, (int) (sr * 0.45), 4410);
    const float closed = buffer.getRMSLevel (0, (int) (sr * 1.45), 4410);
    EXPECT_GT (open, closed * 10.0f);
}

TEST (FilterEffect, EnvelopeFollowerOpensOnLoudInput)
{
    auto measure = [] (float amplitude)
    {
        FilterEffect filter;
        filter.prepare (sr, block);
        applyDefaults (filter);
        setParams (filter, { { ids::fxFilterType, 0.0 }, { ids::fxCutoff, 300.0 },
                             { ids::fxResonance, 0.0 }, { ids::fxEnvAmount, 1.0 },
                             { ids::fxEnvAttack, 1.0 }, { ids::fxEnvRelease, 50.0 },
                             { ids::fxMix, 1.0 } });
        auto buffer = makeTone (4000.0, amplitude, block * 32);
        renderThrough (filter, buffer);
        // Normalised against the input level so this measures the filter, not
        // the amplitude difference.
        return toneAmplitude (buffer, 0, block * 8, block * 20, 4000.0) / amplitude;
    };

    EXPECT_GT (measure (0.9f), measure (0.02f) * 4.0);
}

TEST (FilterEffect, ResetClearsTheFilterState)
{
    FilterEffect filter;
    filter.prepare (sr, block);
    applyDefaults (filter);
    setParams (filter, { { ids::fxCutoff, 400.0 }, { ids::fxResonance, 0.9 } });

    auto loud = makeTone (100.0, 0.9f, block * 4);
    renderThrough (filter, loud);
    filter.reset();

    auto silence = makeSilence (block * 4);
    renderThrough (filter, silence);
    EXPECT_EQ (silence.getMagnitude (0, 0, silence.getNumSamples()), 0.0f);
}

// ================================ EQ =================================

TEST (EqEffect, DefaultsArePerfectlyFlat)
{
    EqEffect eq;
    eq.prepare (sr, block);
    applyDefaults (eq);

    const auto input = makeTone (700.0, 0.5f, block * 4);
    auto buffer = input;
    renderThrough (eq, buffer);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
        ASSERT_FLOAT_EQ (buffer.getSample (0, i), input.getSample (0, i)) << i;
}

TEST (EqEffect, BellBoostsAndCutsOnlyItsOwnBand)
{
    auto measure = [] (double gainDb, double freq)
    {
        EqEffect eq;
        eq.prepare (sr, block);
        applyDefaults (eq);
        setParams (eq, { { ids::fxBandType2, 0.0 }, { ids::fxBandFreq2, 1000.0 },
                         { ids::fxBandGain2, gainDb }, { ids::fxBandQ2, 2.0 } });
        auto buffer = makeTone (freq, 0.25f, block * 16);
        renderThrough (eq, buffer);
        return toneAmplitude (buffer, 0, block * 4, block * 10, freq);
    };

    EXPECT_NEAR (measure (12.0, 1000.0), 0.25 * 3.981, 0.02);   // +12 dB at centre
    EXPECT_NEAR (measure (-12.0, 1000.0), 0.25 / 3.981, 0.01);  // -12 dB at centre
    EXPECT_NEAR (measure (12.0, 100.0), 0.25, 0.02);            // a decade below: untouched
}

TEST (EqEffect, ShelvesTiltTheCorrectEnd)
{
    auto measure = [] (const juce::Identifier& type, double typeValue, double freq)
    {
        juce::ignoreUnused (type);
        EqEffect eq;
        eq.prepare (sr, block);
        applyDefaults (eq);
        setParams (eq, { { ids::fxBandType1, typeValue }, { ids::fxBandFreq1, 500.0 },
                         { ids::fxBandGain1, 12.0 }, { ids::fxBandQ1, 0.7 } });
        auto buffer = makeTone (freq, 0.25f, block * 16);
        renderThrough (eq, buffer);
        return toneAmplitude (buffer, 0, block * 4, block * 10, freq);
    };

    EXPECT_GT (measure (ids::fxBandType1, 1.0, 60.0), 0.8);     // low shelf lifts the sub
    EXPECT_NEAR (measure (ids::fxBandType1, 1.0, 8000.0), 0.25, 0.02);
    EXPECT_GT (measure (ids::fxBandType1, 2.0, 8000.0), 0.8);   // high shelf lifts the top
    EXPECT_NEAR (measure (ids::fxBandType1, 2.0, 60.0), 0.25, 0.02);
}

TEST (EqEffect, HighPassAndLowPassSweepsCutTheEnds)
{
    auto measure = [] (const juce::Identifier& param, double cutoff, double freq)
    {
        EqEffect eq;
        eq.prepare (sr, block);
        applyDefaults (eq);
        eq.setParameter (param, cutoff);
        auto buffer = makeTone (freq, 0.5f, block * 16);
        renderThrough (eq, buffer);
        return toneAmplitude (buffer, 0, block * 4, block * 10, freq);
    };

    EXPECT_LT (measure (ids::fxHpFreq, 400.0, 50.0), 0.02);
    EXPECT_GT (measure (ids::fxHpFreq, 400.0, 4000.0), 0.45);
    EXPECT_LT (measure (ids::fxLpFreq, 1000.0, 12000.0), 0.02);
    EXPECT_GT (measure (ids::fxLpFreq, 1000.0, 100.0), 0.45);
}

TEST (EqEffect, NotchRemovesItsFrequency)
{
    EqEffect eq;
    eq.prepare (sr, block);
    applyDefaults (eq);
    setParams (eq, { { ids::fxBandType3, 3.0 }, { ids::fxBandFreq3, 1000.0 },
                     { ids::fxBandQ3, 4.0 } });

    auto buffer = makeTone (1000.0, 0.5f, block * 16);
    renderThrough (eq, buffer);
    EXPECT_LT (toneAmplitude (buffer, 0, block * 6, block * 8, 1000.0), 0.02);
}

// ============================ compressor =============================

TEST (CompressorEffect, ReducesGainAboveThreshold)
{
    CompressorEffect comp;
    comp.prepare (sr, block);
    applyDefaults (comp);
    setParams (comp, { { ids::fxThreshold, -20.0 }, { ids::fxRatio, 8.0 },
                       { ids::fxAttack, 1.0 }, { ids::fxRelease, 50.0 },
                       { ids::fxKnee, 0.0 }, { ids::fxMakeup, 0.0 },
                       { ids::fxMix, 1.0 } });

    auto buffer = makeTone (200.0, 0.5f, block * 32);   // ~ -6 dBFS peak
    renderThrough (comp, buffer);

    const float outPeak = buffer.getMagnitude (0, block * 16, block * 8);
    // -6 dBFS into 8:1 above -20 dB lands near -18.25 dBFS.
    EXPECT_NEAR (juce::Decibels::gainToDecibels (outPeak), -18.25f, 1.0f);
    EXPECT_GT (comp.getGainReductionDb(), 10.0f);
}

TEST (CompressorEffect, LeavesSignalBelowThresholdAlone)
{
    CompressorEffect comp;
    comp.prepare (sr, block);
    applyDefaults (comp);
    setParams (comp, { { ids::fxThreshold, -20.0 }, { ids::fxRatio, 8.0 },
                       { ids::fxKnee, 0.0 }, { ids::fxMakeup, 0.0 }, { ids::fxMix, 1.0 } });

    const auto input = makeTone (200.0, 0.01f, block * 8);   // -40 dBFS
    auto buffer = input;
    renderThrough (comp, buffer);

    EXPECT_NEAR (buffer.getMagnitude (0, 0, buffer.getNumSamples()), 0.01f, 1.0e-4f);
    EXPECT_LT (comp.getGainReductionDb(), 0.01f);
}

TEST (CompressorEffect, MakeupGainIsApplied)
{
    CompressorEffect comp;
    comp.prepare (sr, block);
    applyDefaults (comp);
    setParams (comp, { { ids::fxThreshold, 0.0 }, { ids::fxRatio, 1.0 },
                       { ids::fxKnee, 0.0 }, { ids::fxMakeup, 6.0 }, { ids::fxMix, 1.0 } });

    auto buffer = makeTone (200.0, 0.1f, block * 8);
    renderThrough (comp, buffer);
    EXPECT_NEAR (buffer.getMagnitude (0, 0, buffer.getNumSamples()), 0.1f * 1.9953f, 0.005f);
}

TEST (CompressorEffect, ExternalSidechainDucksTheInput)
{
    // Steady pad on the insert, a kick-shaped burst on the sidechain bus.
    const int numSamples = block * 32;
    const int burstStart = block * 8;
    const int burstLength = block * 8;

    auto sidechain = makeSilence (numSamples);
    for (int i = burstStart; i < burstStart + burstLength; ++i)
    {
        const auto s = (float) (0.9 * std::sin (juce::MathConstants<double>::twoPi * 50.0 * i / sr));
        sidechain.setSample (0, i, s);
        sidechain.setSample (1, i, s);
    }

    auto run = [&] (bool useSidechain)
    {
        CompressorEffect comp;
        comp.prepare (sr, block);
        applyDefaults (comp);
        setParams (comp, { { ids::fxThreshold, -24.0 }, { ids::fxRatio, 12.0 },
                           { ids::fxAttack, 1.0 }, { ids::fxRelease, 40.0 },
                           { ids::fxKnee, 0.0 }, { ids::fxMakeup, 0.0 },
                           { ids::fxScHpFreq, 20.0 }, { ids::fxMix, 1.0 },
                           { ids::fxSidechain, useSidechain ? 3.0 : -1.0 } });

        // Well below the threshold, so nothing but the sidechain can trigger it.
        auto buffer = makeTone (400.0, 0.03f, numSamples);   // ~ -30 dBFS
        for (int pos = 0; pos < numSamples; pos += block)
        {
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, pos, block);
            juce::AudioBuffer<float> scView (const_cast<float* const*> (sidechain.getArrayOfWritePointers()),
                                             2, pos, block);
            Effect::Context context;
            context.sidechain = useSidechain ? &scView : nullptr;
            comp.process (view, block, context);
        }
        return buffer;
    };

    const auto ducked = run (true);
    const auto clean = run (false);

    const float duckedDuring = ducked.getRMSLevel (0, burstStart + block * 2, block * 4);
    const float duckedBefore = ducked.getRMSLevel (0, block * 2, block * 4);
    const float cleanDuring = clean.getRMSLevel (0, burstStart + block * 2, block * 4);
    const float cleanBefore = clean.getRMSLevel (0, block * 2, block * 4);

    EXPECT_NEAR (cleanDuring, cleanBefore, 1.0e-4f);          // no sidechain: nothing happens
    EXPECT_NEAR (duckedBefore, cleanBefore, 1.0e-4f);         // and nothing before the kick
    EXPECT_LT (duckedDuring, duckedBefore * 0.5f);            // the kick pushes the pad down
    EXPECT_LT (duckedDuring, cleanDuring * 0.5f);
}

TEST (CompressorEffect, SidechainInsertIsReportedToTheEngine)
{
    CompressorEffect comp;
    applyDefaults (comp);
    EXPECT_EQ (comp.getSidechainInsert(), -1);
    comp.setParameter (ids::fxSidechain, 5.0);
    EXPECT_EQ (comp.getSidechainInsert(), 5);
}

TEST (CompressorEffect, SidechainHighPassIgnoresSubContent)
{
    // A 30 Hz rumble on the detector should stop triggering once the sidechain
    // high-pass is swept above it.
    auto reductionWithHp = [] (double hpFreq)
    {
        CompressorEffect comp;
        comp.prepare (sr, block);
        applyDefaults (comp);
        setParams (comp, { { ids::fxThreshold, -30.0 }, { ids::fxRatio, 10.0 },
                           { ids::fxAttack, 1.0 }, { ids::fxRelease, 30.0 },
                           { ids::fxKnee, 0.0 }, { ids::fxScHpFreq, hpFreq },
                           { ids::fxMix, 1.0 } });
        auto buffer = makeTone (30.0, 0.8f, block * 32);
        renderThrough (comp, buffer);
        return comp.getGainReductionDb();
    };

    EXPECT_GT (reductionWithHp (20.0), 15.0f);
    EXPECT_LT (reductionWithHp (1000.0), reductionWithHp (20.0) * 0.5f);
}

TEST (CompressorEffect, ResetReleasesTheEnvelope)
{
    CompressorEffect comp;
    comp.prepare (sr, block);
    applyDefaults (comp);
    setParams (comp, { { ids::fxThreshold, -40.0 }, { ids::fxRatio, 20.0 },
                       { ids::fxRelease, 1000.0 }, { ids::fxMix, 1.0 } });

    auto loud = makeTone (200.0, 0.9f, block * 8);
    renderThrough (comp, loud);
    EXPECT_GT (comp.getGainReductionDb(), 10.0f);

    comp.reset();
    auto quiet = makeSilence (block);
    renderThrough (comp, quiet);
    EXPECT_LT (comp.getGainReductionDb(), 0.01f);
}

// ============================== delay ================================

TEST (DelayEffect, TempoSyncedRepeatLandsOnTheDivision)
{
    DelayEffect delay;
    delay.prepare (sr, block);
    applyDefaults (delay);
    setParams (delay, { { ids::fxDivision, 8.0 },   // 1/4
                        { ids::fxFeedback, 0.0 }, { ids::fxPingPong, 0.0 },
                        { ids::fxHpFreq, 20.0 }, { ids::fxLpFreq, 20000.0 },
                        { ids::fxMix, 1.0 } });

    Effect::Context context;
    context.tempo = 120.0;                     // one quarter = 0.5 s = 22050 samples
    auto buffer = makeImpulse (block * 100);
    renderThrough (delay, buffer, context);

    EXPECT_NEAR (buffer.getSample (0, 22050), 1.0f, 0.01f);
    EXPECT_LT (std::abs (buffer.getSample (0, 22050 - 50)), 0.01f);
    EXPECT_LT (std::abs (buffer.getSample (0, 22050 + 50)), 0.01f);
}

TEST (DelayEffect, DivisionChangesTheRepeatOffset)
{
    DelayEffect delay;
    delay.prepare (sr, block);
    applyDefaults (delay);
    setParams (delay, { { ids::fxDivision, 5.0 },   // 1/8
                        { ids::fxFeedback, 0.0 }, { ids::fxHpFreq, 20.0 },
                        { ids::fxLpFreq, 20000.0 }, { ids::fxMix, 1.0 } });

    Effect::Context context;
    context.tempo = 174.0;                     // DnB: one 1/8 = 30/174 s
    const int expected = (int) std::lround (sr * 30.0 / 174.0);
    auto buffer = makeImpulse (block * 60);
    renderThrough (delay, buffer, context);

    // The division lands between two samples, so the interpolated impulse is
    // split across the pair straddling it.
    EXPECT_GT (std::abs (buffer.getSample (0, expected - 1))
                   + std::abs (buffer.getSample (0, expected))
                   + std::abs (buffer.getSample (0, expected + 1)), 0.95f);
    EXPECT_LT (buffer.getMagnitude (0, expected + 20, 200), 0.01f);
    EXPECT_LT (buffer.getMagnitude (0, expected - 220, 200), 0.01f);
}

TEST (DelayEffect, FeedbackProducesDecayingRepeats)
{
    DelayEffect delay;
    delay.prepare (sr, block);
    applyDefaults (delay);
    setParams (delay, { { ids::fxDivision, 2.0 },   // 1/16
                        { ids::fxFeedback, 0.6 }, { ids::fxHpFreq, 20.0 },
                        { ids::fxLpFreq, 20000.0 }, { ids::fxMix, 1.0 } });

    Effect::Context context;
    context.tempo = 140.0;
    const int step = (int) std::lround (sr * 0.25 * 60.0 / 140.0);
    auto buffer = makeImpulse (block * 80);
    renderThrough (delay, buffer, context);

    const float first = std::abs (buffer.getSample (0, step));
    const float second = std::abs (buffer.getSample (0, step * 2));
    const float third = std::abs (buffer.getSample (0, step * 3));
    EXPECT_NEAR (second, first * 0.6f, 0.05f);
    EXPECT_NEAR (third, first * 0.36f, 0.05f);
}

TEST (DelayEffect, PingPongBouncesRepeatsBetweenChannels)
{
    DelayEffect delay;
    delay.prepare (sr, block);
    applyDefaults (delay);
    setParams (delay, { { ids::fxDivision, 2.0 },   // 1/16
                        { ids::fxFeedback, 0.7 }, { ids::fxPingPong, 1.0 },
                        { ids::fxHpFreq, 20.0 }, { ids::fxLpFreq, 20000.0 },
                        { ids::fxMix, 1.0 } });

    Effect::Context context;
    context.tempo = 140.0;
    const int step = (int) std::lround (sr * 0.25 * 60.0 / 140.0);
    auto buffer = makeImpulse (block * 80, 1.0f, true);   // left only
    renderThrough (delay, buffer, context);

    EXPECT_GT (std::abs (buffer.getSample (0, step)), 0.9f);
    EXPECT_LT (std::abs (buffer.getSample (1, step)), 0.01f);
    EXPECT_LT (std::abs (buffer.getSample (0, step * 2)), 0.01f);
    EXPECT_GT (std::abs (buffer.getSample (1, step * 2)), 0.5f);
}

TEST (DelayEffect, FilteredFeedbackDarkensRepeats)
{
    auto repeatLevel = [] (double lpFreq)
    {
        DelayEffect delay;
        delay.prepare (sr, block);
        applyDefaults (delay);
        setParams (delay, { { ids::fxDivision, 2.0 }, { ids::fxFeedback, 0.0 },
                            { ids::fxHpFreq, 20.0 }, { ids::fxLpFreq, lpFreq },
                            { ids::fxMix, 1.0 } });
        Effect::Context context;
        context.tempo = 140.0;
        const int step = (int) std::lround (sr * 0.25 * 60.0 / 140.0);
        auto buffer = makeTone (9000.0, 0.5f, block * 40);
        renderThrough (delay, buffer, context);
        return toneAmplitude (buffer, 0, step + block * 4, block * 16, 9000.0);
    };

    EXPECT_GT (repeatLevel (20000.0), 0.4);
    EXPECT_LT (repeatLevel (1000.0), 0.05);
}

TEST (DelayEffect, ResetClearsTheTail)
{
    DelayEffect delay;
    delay.prepare (sr, block);
    applyDefaults (delay);
    setParams (delay, { { ids::fxDivision, 2.0 }, { ids::fxFeedback, 0.9 },
                        { ids::fxMix, 1.0 } });

    auto noisy = makeTone (500.0, 0.8f, block * 20);
    Effect::Context context;
    context.tempo = 140.0;
    renderThrough (delay, noisy, context);
    delay.reset();

    auto silence = makeSilence (block * 40);
    renderThrough (delay, silence, context);
    EXPECT_EQ (silence.getMagnitude (0, 0, silence.getNumSamples()), 0.0f);
}

// ============================== reverb ===============================

TEST (ReverbEffect, TailDecaysAfterTheInputStops)
{
    ReverbEffect reverb;
    reverb.prepare (sr, block);
    applyDefaults (reverb);
    setParams (reverb, { { ids::fxSize, 0.8 }, { ids::fxDamping, 0.3 },
                         { ids::fxPreDelay, 0.0 }, { ids::fxHpFreq, 20.0 },
                         { ids::fxLpFreq, 20000.0 }, { ids::fxMix, 1.0 } });

    const int numSamples = (int) (sr * 8.0);
    auto buffer = makeSilence (numSamples);
    for (int i = 0; i < (int) (sr * 0.1); ++i)
    {
        const auto s = (float) (0.6 * std::sin (juce::MathConstants<double>::twoPi * 300.0 * i / sr));
        buffer.setSample (0, i, s);
        buffer.setSample (1, i, s);
    }
    renderThrough (reverb, buffer);

    const float early = buffer.getRMSLevel (0, (int) (sr * 0.2), 4410);
    const float middle = buffer.getRMSLevel (0, (int) (sr * 1.5), 4410);
    const float late = buffer.getRMSLevel (0, (int) (sr * 7.0), 4410);

    EXPECT_GT (early, 0.001f);
    EXPECT_LT (middle, early);
    EXPECT_LT (late, middle);
    EXPECT_LT (late, early * 0.05f);
}

TEST (ReverbEffect, LowCutKeepsTheTailOutOfTheSub)
{
    auto subLevel = [] (double lowCut)
    {
        ReverbEffect reverb;
        reverb.prepare (sr, block);
        applyDefaults (reverb);
        setParams (reverb, { { ids::fxSize, 0.7 }, { ids::fxDamping, 0.2 },
                             { ids::fxPreDelay, 0.0 }, { ids::fxHpFreq, lowCut },
                             { ids::fxLpFreq, 20000.0 }, { ids::fxMix, 1.0 } });
        auto buffer = makeTone (60.0, 0.6f, block * 64);
        renderThrough (reverb, buffer);
        return toneAmplitude (buffer, 0, block * 32, block * 24, 60.0);
    };

    const double open = subLevel (20.0);
    EXPECT_GT (open, 0.02);
    EXPECT_LT (subLevel (600.0), open * 0.15);
}

TEST (ReverbEffect, PreDelayHoldsBackTheWetSignal)
{
    ReverbEffect reverb;
    reverb.prepare (sr, block);
    applyDefaults (reverb);
    setParams (reverb, { { ids::fxPreDelay, 100.0 }, { ids::fxHpFreq, 20.0 },
                         { ids::fxLpFreq, 20000.0 }, { ids::fxMix, 1.0 } });

    auto buffer = makeImpulse (block * 40);
    renderThrough (reverb, buffer);

    const int preSamples = (int) (0.1 * sr);
    EXPECT_LT (buffer.getMagnitude (0, 0, preSamples - 32), 1.0e-6f);
    EXPECT_GT (buffer.getMagnitude (0, preSamples, 4410), 1.0e-4f);
}

TEST (ReverbEffect, ResetClearsTheTail)
{
    ReverbEffect reverb;
    reverb.prepare (sr, block);
    applyDefaults (reverb);
    setParams (reverb, { { ids::fxSize, 0.9 }, { ids::fxMix, 1.0 } });

    auto loud = makeTone (400.0, 0.8f, block * 20);
    renderThrough (reverb, loud);
    reverb.reset();

    auto silence = makeSilence (block * 20);
    renderThrough (reverb, silence);
    EXPECT_LT (silence.getMagnitude (0, 0, silence.getNumSamples()), 1.0e-12f);
}

// ==================== model / engine integration =====================

namespace
{
juce::ValueTree addBuiltinSlot (test::EngineFixture& fixture, int insertIndex, int slotIndex,
                                const juce::String& pluginId)
{
    const auto* entry = fx::findBuiltin (pluginId);
    juce::ValueTree slot (ids::SLOT);
    slot.setProperty (ids::slotIndex, slotIndex, nullptr);
    slot.setProperty (ids::bypass, false, nullptr);
    slot.setProperty (ids::pluginId, pluginId, nullptr);
    if (entry != nullptr)
        BuiltinEffect::writeDefaults (slot, entry->specs, nullptr);
    fixture.model.getInsert (insertIndex).appendChild (slot, nullptr);
    fixture.sync.rebuildNow();
    return slot;
}

size_t effectCount (const test::EngineFixture& fixture, int insertIndex)
{
    auto snapshot = fixture.engine.getPendingSnapshot();
    return snapshot->inserts[(size_t) insertIndex].effects.size();
}
}

TEST (BuiltinEffectSlot, LandsInTheSnapshotChain)
{
    test::EngineFixture fixture;
    EXPECT_EQ (effectCount (fixture, 1), 0u);

    addBuiltinSlot (fixture, 1, 0, ClipperEffect::identifier());
    EXPECT_EQ (effectCount (fixture, 1), 1u);
    EXPECT_NE (fixture.builtinEffects.peek (1, 0), nullptr);
}

TEST (BuiltinEffectSlot, BypassRemovesItFromTheChain)
{
    test::EngineFixture fixture;
    auto slot = addBuiltinSlot (fixture, 1, 0, DelayEffect::identifier());
    EXPECT_EQ (effectCount (fixture, 1), 1u);

    slot.setProperty (ids::bypass, true, nullptr);
    fixture.sync.rebuildNow();
    EXPECT_EQ (effectCount (fixture, 1), 0u);
}

TEST (BuiltinEffectSlot, TreeParametersReachTheInstance)
{
    test::EngineFixture fixture;
    auto slot = addBuiltinSlot (fixture, 1, 0, CompressorEffect::identifier());

    auto instance = std::dynamic_pointer_cast<CompressorEffect> (fixture.builtinEffects.peek (1, 0));
    ASSERT_NE (instance, nullptr);
    EXPECT_EQ (instance->getSidechainInsert(), -1);

    slot.setProperty (ids::fxSidechain, 2, nullptr);
    fixture.sync.rebuildNow();
    EXPECT_EQ (instance->getSidechainInsert(), 2);
}

TEST (BuiltinEffectSlot, ChangingThePluginIdSwapsTheInstance)
{
    test::EngineFixture fixture;
    auto slot = addBuiltinSlot (fixture, 1, 0, ClipperEffect::identifier());
    auto first = fixture.builtinEffects.peek (1, 0);
    ASSERT_NE (first, nullptr);

    fixture.sync.rebuildNow();
    EXPECT_EQ (fixture.builtinEffects.peek (1, 0), first);   // reused, not rebuilt

    slot.setProperty (ids::pluginId, ReverbEffect::identifier(), nullptr);
    fixture.sync.rebuildNow();
    auto second = fixture.builtinEffects.peek (1, 0);
    ASSERT_NE (second, nullptr);
    EXPECT_NE (second, first);
    EXPECT_NE (std::dynamic_pointer_cast<ReverbEffect> (second), nullptr);
}

TEST (BuiltinEffectSlot, AutomationTargetsABuiltinParameter)
{
    test::EngineFixture fixture;
    addBuiltinSlot (fixture, 1, 0, FilterEffect::identifier());

    auto automation = fixture.model.addAutomation ("builtin-insert", 1, "0:fxCutoff",
                                                   "Insert 1 cutoff", 0.5);
    auto clip = fixture.model.addPlaylistClip ("automation", 0, 0, ids::ticksPerBar);
    clip.setProperty (ids::automationId, (int) automation[ids::id], nullptr);
    fixture.sync.rebuildNow();

    auto snapshot = fixture.engine.getPendingSnapshot();
    ASSERT_EQ (snapshot->automations.size(), 1u);
    const auto& target = snapshot->automations.front();
    EXPECT_EQ (target.kind, AutomationSnapshot::Kind::builtinParam);
    ASSERT_NE (target.builtinSpec, nullptr);
    EXPECT_EQ (target.builtinSpec->id, ids::fxCutoff);
    EXPECT_EQ (target.builtinEffect, fixture.builtinEffects.peek (1, 0).get());
}

TEST (BuiltinEffectSlot, EngineRunsTheEffectOnTheInsertBus)
{
    // A compressor that never compresses but adds 12 dB of makeup: the master
    // output has to come out four times louder than the same render without it.
    auto renderMaster = [] (bool withEffect)
    {
        test::EngineFixture fixture;
        auto channel = fixture.model.addChannel ("synth", "Lead");
        auto lane = fixture.model.getOrCreateLane (fixture.model.getPattern (0), channel[ids::id]);
        fixture.model.addNote (lane, 60, 0, ids::ticksPerBar);
        fixture.sync.rebuildNow();

        if (withEffect)
        {
            auto slot = addBuiltinSlot (fixture, 0, 0, CompressorEffect::identifier());
            slot.setProperty (ids::fxThreshold, 0.0, nullptr);
            slot.setProperty (ids::fxRatio, 1.0, nullptr);
            slot.setProperty (ids::fxMakeup, 12.0, nullptr);
            slot.setProperty (ids::fxMix, 1.0, nullptr);
            fixture.sync.rebuildNow();
        }

        return fixture.renderFromStart (test::kBlockSize * 16);
    };

    const auto plain = renderMaster (false);
    const auto boosted = renderMaster (true);

    const float plainRms = test::rmsOf (plain, 0, plain.getNumSamples());
    const float boostedRms = test::rmsOf (boosted, 0, boosted.getNumSamples());
    ASSERT_GT (plainRms, 1.0e-4f);
    EXPECT_NEAR (boostedRms / plainRms, 3.981f, 0.05f);
}

// ============================ editor UI ==============================

TEST (BuiltinEffectEditor, LaysOutEveryParameterInsideItsBounds)
{
    ProjectModel model;

    for (const auto& entry : fx::builtinEffects())
    {
        juce::ValueTree slot (ids::SLOT);
        slot.setProperty (ids::slotIndex, 0, nullptr);
        slot.setProperty (ids::pluginId, entry.id, nullptr);
        BuiltinEffect::writeDefaults (slot, entry.specs, nullptr);

        BuiltinEffectEditor editor (model, slot, entry, 1);
        editor.setBounds (editor.getBounds());

        EXPECT_GT (editor.getWidth(), 0) << entry.id;
        EXPECT_GT (editor.getHeight(), 0) << entry.id;

        int visibleControls = 0;
        for (int i = 0; i < editor.getNumChildComponents(); ++i)
        {
            auto* child = editor.getChildComponent (i);
            EXPECT_TRUE (editor.getLocalBounds().contains (child->getBounds()))
                << entry.id << " child " << i;
            if (! child->getBounds().isEmpty())
                ++visibleControls;
        }
        EXPECT_GE (visibleControls, (int) entry.specs.size()) << entry.id;
    }
}

TEST (BuiltinEffectEditor, ComboBoxWritesTheSelectionToTheSlotTree)
{
    ProjectModel model;
    const auto* entry = fx::findBuiltin (CompressorEffect::identifier());
    ASSERT_NE (entry, nullptr);

    juce::ValueTree slot (ids::SLOT);
    slot.setProperty (ids::slotIndex, 0, nullptr);
    slot.setProperty (ids::pluginId, entry->id, nullptr);
    BuiltinEffect::writeDefaults (slot, entry->specs, nullptr);

    BuiltinEffectEditor editor (model, slot, *entry, 0);

    juce::ComboBox* sidechainBox = nullptr;
    for (int i = 0; i < editor.getNumChildComponents(); ++i)
        if (auto* box = dynamic_cast<juce::ComboBox*> (editor.getChildComponent (i)))
            sidechainBox = box;
    ASSERT_NE (sidechainBox, nullptr);
    ASSERT_GE (sidechainBox->getNumItems(), 2);

    EXPECT_EQ ((int) slot[ids::fxSidechain], -1);
    sidechainBox->setSelectedItemIndex (1, juce::sendNotificationSync);
    EXPECT_GE ((int) slot[ids::fxSidechain], 0);
}

TEST (BuiltinEffectEditor, FollowsUndoOfAParameterChange)
{
    ProjectModel model;
    const auto* entry = fx::findBuiltin (ClipperEffect::identifier());
    ASSERT_NE (entry, nullptr);

    juce::ValueTree slot (ids::SLOT);
    slot.setProperty (ids::slotIndex, 0, nullptr);
    slot.setProperty (ids::pluginId, entry->id, nullptr);
    BuiltinEffect::writeDefaults (slot, entry->specs, nullptr);

    BuiltinEffectEditor editor (model, slot, *entry, 1);

    auto& undo = model.getUndoManager();
    undo.beginNewTransaction();
    slot.setProperty (ids::fxDrive, 30.0, &undo);
    EXPECT_DOUBLE_EQ ((double) slot[ids::fxDrive], 30.0);

    undo.undo();
    EXPECT_DOUBLE_EQ ((double) slot[ids::fxDrive], 6.0);
}

TEST (CompressorEffect, DefaultSettingsAreRoughlyUnityOnABusyBus)
{
    // A fresh compressor dropped on a bus must not gut the level: the old
    // defaults (-18 dB, 4:1, no makeup) cost a kick bus ~13 dB.
    test::EngineFixture fixture;

    auto dry = fixture.renderFromStart (44100);
    const float dryRms = test::rmsOf (dry, 0, dry.getNumSamples());

    addBuiltinSlot (fixture, 0, 0, CompressorEffect::identifier());
    auto wet = fixture.renderFromStart (44100);
    const float wetRms = test::rmsOf (wet, 0, wet.getNumSamples());

    const float deltaDb = juce::Decibels::gainToDecibels (wetRms / juce::jmax (1.0e-9f, dryRms));
    std::cout << "COMP_DEFAULTS dryRms=" << dryRms << " wetRms=" << wetRms
              << " delta=" << deltaDb << " dB\n";
    EXPECT_GT (deltaDb, -2.0f) << "default compressor costs more than 2 dB RMS";
    EXPECT_LT (deltaDb,  2.0f) << "default compressor adds more than 2 dB RMS";
}

TEST (CompressorEffect, DuckPresetPumpsABusFromTheKick)
{
    // The one-click "Sidechain duck" menu action writes this exact slot via
    // configureDuckSlot; prove it audibly ducks through the real engine by
    // watching the pad insert's own bus peak around a kick hit.
    test::EngineFixture fixture;

    auto kickChannel = fixture.model.getChannel (0);
    kickChannel.setProperty (ids::insertIndex, 1, nullptr);

    auto pad = fixture.model.addChannel ("synth", "Pad");
    pad.setProperty (ids::insertIndex, 2, nullptr);
    auto pattern = fixture.model.getPattern (0);
    auto lane = fixture.model.getOrCreateLane (pattern, pad[ids::id]);
    fixture.model.addNote (lane, 48, 0, 16 * ids::ticksPerStep);   // whole bar

    juce::ValueTree slot (ids::SLOT);
    slot.setProperty (ids::slotIndex, 0, nullptr);
    CompressorEffect::configureDuckSlot (slot, 1, nullptr);
    fixture.model.getInsert (2).appendChild (slot, nullptr);
    fixture.sync.rebuildNow();

    // Render block by block and track the pad bus peak per block.
    fixture.engine.stop();
    fixture.engine.setPositionTicks (0.0);
    fixture.engine.play();
    std::vector<float> padPeak;
    juce::AudioBuffer<float> out (2, test::kBlockSize);
    for (int b = 0; b < 120; ++b)   // ~1.4 s at 44.1k
    {
        float* ptrs[2] = { out.getWritePointer (0), out.getWritePointer (1) };
        fixture.engine.processBlockOffline (ptrs, 2, test::kBlockSize);
        padPeak.push_back (fixture.engine.getInsertPeak (2, 0));
    }
    fixture.engine.stop();

    // Second kick hit lands at tick 960 = sample ~18900 = block ~37.
    const double samplesPerTick = test::kSampleRate / ((140.0 / 60.0) * 960.0);
    const int hitBlock = (int) (960.0 * samplesPerTick / test::kBlockSize) + 1;
    const int preBlock = hitBlock - 3;   // just before the hit: duck released

    ASSERT_GT ((int) padPeak.size(), hitBlock + 2);
    const float before = padPeak[(size_t) preBlock];
    const float atHit  = juce::jmin (padPeak[(size_t) hitBlock], padPeak[(size_t) hitBlock + 1]);

    auto* duck = dynamic_cast<CompressorEffect*> (fixture.builtinEffects.peek (2, 0).get());
    ASSERT_NE (duck, nullptr);
    EXPECT_EQ (duck->getSidechainInsert(), 1);

    // The pad bus must visibly pump: several dB down right at the kick.
    EXPECT_LT (atHit, before * 0.72f)
        << "pad peak before hit " << before << ", at hit " << atHit;
}

namespace
{
// Measured gain of a sine through an effect, after settling.
float measuredGainAt (BuiltinEffect& effect, double freqHz, double sampleRate = 44100.0)
{
    const int n = 8192;
    juce::AudioBuffer<float> buffer (2, n);
    for (int i = 0; i < n; ++i)
    {
        const float s = std::sin ((float) (juce::MathConstants<double>::twoPi * freqHz * i / sampleRate));
        buffer.setSample (0, i, s);
        buffer.setSample (1, i, s);
    }
    effect.reset();
    // Feed in prepare-sized chunks; effects bound their scratch to the block size.
    for (int pos = 0; pos < n; pos += 512)
    {
        float* ptrs[2] = { buffer.getWritePointer (0, pos), buffer.getWritePointer (1, pos) };
        juce::AudioBuffer<float> view (ptrs, 2, juce::jmin (512, n - pos));
        effect.process (view, view.getNumSamples(), {});
    }
    // Skip the first half (filter transient), measure steady-state RMS * sqrt(2).
    return buffer.getRMSLevel (0, n / 2, n / 2) * juce::MathConstants<float>::sqrt2;
}
}

TEST (EqEffect, PlottedResponseMatchesTheDsp)
{
    // The editor plots magnitudeAt(); prove it equals what process() does.
    EqEffect eq;
    eq.prepare (44100.0, 512);
    eq.setParameter (ids::fxHpFreq, 80.0);
    eq.setParameter (ids::fxBandType2, 0.0);      // bell
    eq.setParameter (ids::fxBandFreq2, 1000.0);
    eq.setParameter (ids::fxBandGain2, 9.0);
    eq.setParameter (ids::fxBandQ2, 1.5);

    for (const double f : { 40.0, 200.0, 1000.0, 4000.0, 12000.0 })
    {
        const double plotted  = eq.magnitudeAt (f);
        EqEffect dsp;   // fresh instance so filter state can't leak between probes
        dsp.prepare (44100.0, 512);
        dsp.setParameter (ids::fxHpFreq, 80.0);
        dsp.setParameter (ids::fxBandType2, 0.0);
        dsp.setParameter (ids::fxBandFreq2, 1000.0);
        dsp.setParameter (ids::fxBandGain2, 9.0);
        dsp.setParameter (ids::fxBandQ2, 1.5);
        const double measured = measuredGainAt (dsp, f);
        EXPECT_NEAR (plotted, measured, juce::jmax (0.02, measured * 0.06))
            << "at " << f << " Hz";
    }
}

TEST (FilterEffect, PlottedResponseMatchesTheDsp)
{
    for (const int mode : { 0, 1, 2 })   // LP, HP, BP
    {
        FilterEffect analysis;
        analysis.prepare (44100.0, 512);
        analysis.setParameter (ids::fxFilterType, mode);
        analysis.setParameter (ids::fxCutoff, 900.0);
        analysis.setParameter (ids::fxResonance, 0.3);

        for (const double f : { 150.0, 900.0, 5000.0 })
        {
            FilterEffect dsp;
            dsp.prepare (44100.0, 512);
            dsp.setParameter (ids::fxFilterType, mode);
            dsp.setParameter (ids::fxCutoff, 900.0);
            dsp.setParameter (ids::fxResonance, 0.3);
            const double plotted  = analysis.magnitudeAt (f);
            const double measured = measuredGainAt (dsp, f);
            EXPECT_NEAR (plotted, measured, juce::jmax (0.03, measured * 0.08))
                << "mode " << mode << " at " << f << " Hz";
        }
    }
}
