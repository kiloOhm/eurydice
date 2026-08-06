#include <gtest/gtest.h>
#include "engine/SynthGenerator.h"
#include "engine/SynthOsc.h"
#include "TestHelpers.h"

// The Serum-style synth additions, each proven at the signal level.
namespace
{
juce::AudioBuffer<float> renderSynth (SynthGenerator& synth, int key, int numSamples)
{
    juce::AudioBuffer<float> out (2, numSamples);
    out.clear();
    int pos = 0;
    while (pos < numSamples)
    {
        const int n = juce::jmin (512, numSamples - pos);
        juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, pos, n);
        juce::MidiBuffer block;
        if (pos == 0)
            block.addEvent (juce::MidiMessage::noteOn (1, key, 0.9f), 0);
        synth.render (view, block);
        pos += n;
    }
    return out;
}

SynthGenerator& sustained (SynthGenerator& synth)
{
    synth.prepare (test::kSampleRate, 512);
    synth.params().attack.store (0.001f);
    synth.params().sustain.store (1.0f);
    synth.params().cutoffHz.store (18000.0f);   // filter out of the way
    synth.params().resonance.store (0.0f);
    synth.params().filterEnvAmount.store (0.0f);
    return synth;
}

// Energy of the first difference — a cheap proxy for high-frequency content.
float edgeEnergy (const juce::AudioBuffer<float>& buffer, int from, int count)
{
    const auto* data = buffer.getReadPointer (0);
    double sum = 0.0;
    for (int i = from + 1; i < from + count; ++i)
        sum += juce::square ((double) data[i] - data[i - 1]);
    return (float) std::sqrt (sum / count);
}

// Signal energy at one frequency (Goertzel).
float energyAt (const juce::AudioBuffer<float>& buffer, double hz, int from, int count)
{
    const auto* data = buffer.getReadPointer (0);
    const double w = 2.0 * juce::MathConstants<double>::pi * hz / test::kSampleRate;
    const double coeff = 2.0 * std::cos (w);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (int i = from; i < from + count; ++i)
    {
        s0 = data[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return (float) std::sqrt (juce::jmax (0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2)) / count;
}

float correlation (const float* a, const float* b, int count)
{
    double dot = 0.0, na = 0.0, nb = 0.0;
    for (int i = 0; i < count; ++i)
    {
        dot += (double) a[i] * b[i];
        na += juce::square ((double) a[i]);
        nb += juce::square ((double) b[i]);
    }
    return na > 0.0 && nb > 0.0 ? (float) (dot / std::sqrt (na * nb)) : 1.0f;
}
}

TEST (SynthExpansion, MorphSweepsFromSmoothToEdgy)
{
    // Sine (-2) has almost no high-frequency content, saw (0) plenty,
    // square (1) the most. The legacy 0..1 saw-to-square region is intact.
    float edges[3] = {};
    const float morphs[3] = { -2.0f, 0.0f, 1.0f };
    for (int i = 0; i < 3; ++i)
    {
        SynthGenerator synth;
        sustained (synth).params().oscShape.store (morphs[i]);
        synth.params().osc2Mix.store (0.0f);
        auto out = renderSynth (synth, 60, 8192);
        edges[i] = edgeEnergy (out, 2048, 4096);
    }
    EXPECT_LT (edges[0] * 3.0f, edges[1]) << "sine is not smoother than saw";
    EXPECT_LT (edges[1], edges[2]) << "square lost its edges";
}

TEST (SynthExpansion, WarpNarrowsThePulse)
{
    // On the square, warp is pulse width: the fraction of time spent high
    // drops from ~1/2 toward ~1/20.
    auto highFraction = [] (float warp)
    {
        SynthGenerator synth;
        sustained (synth).params().oscShape.store (1.0f);
        synth.params().osc2Mix.store (0.0f);
        synth.params().oscWarp.store (warp);
        auto out = renderSynth (synth, 60, 8192);
        const auto* d = out.getReadPointer (0);
        int high = 0, total = 0;
        for (int i = 2048; i < 8192; ++i, ++total)
            if (d[i] > 0.0f)
                ++high;
        return (float) high / (float) total;
    };

    EXPECT_NEAR (highFraction (0.0f), 0.5f, 0.06f);
    EXPECT_LT (highFraction (0.8f), 0.25f);
}

TEST (SynthExpansion, UnisonSpreadsAcrossTheStereoField)
{
    SynthGenerator mono;
    sustained (mono).params().osc2Mix.store (0.0f);
    auto monoOut = renderSynth (mono, 60, 8192);
    EXPECT_GT (correlation (monoOut.getReadPointer (0), monoOut.getReadPointer (1), 8192), 0.9999f)
        << "a single voice must stay centred";

    SynthGenerator wide;
    sustained (wide).params().osc2Mix.store (0.0f);
    wide.params().unisonVoices.store (7.0f);
    wide.params().unisonDetune.store (30.0f);
    wide.params().unisonWidth.store (1.0f);
    auto wideOut = renderSynth (wide, 60, 8192);
    EXPECT_LT (correlation (wideOut.getReadPointer (0), wideOut.getReadPointer (1), 8192), 0.95f)
        << "unison did not open the stereo image";
    EXPECT_GT (wideOut.getRMSLevel (0, 2048, 4096), 0.02f);
}

TEST (SynthExpansion, SubAddsAnOctaveBelow)
{
    const double f0 = juce::MidiMessage::getMidiNoteInHertz (60);

    SynthGenerator dry;
    sustained (dry).params().osc2Mix.store (0.0f);
    auto dryOut = renderSynth (dry, 60, 16384);

    SynthGenerator withSub;
    sustained (withSub).params().osc2Mix.store (0.0f);
    withSub.params().subLevel.store (1.0f);
    auto subOut = renderSynth (withSub, 60, 16384);

    const float below = energyAt (subOut, f0 / 2.0, 4096, 8192);
    const float dryBelow = energyAt (dryOut, f0 / 2.0, 4096, 8192);
    EXPECT_GT (below, dryBelow * 5.0f) << "no energy an octave down";
}

TEST (SynthExpansion, FilterTypesShapeTheSpectrum)
{
    const double f0 = juce::MidiMessage::getMidiNoteInHertz (36);   // ~65 Hz saw

    auto fundamental = [f0] (float type)
    {
        SynthGenerator synth;
        sustained (synth).params().osc2Mix.store (0.0f);
        synth.params().filterType.store (type);
        synth.params().cutoffHz.store (4000.0f);
        auto out = renderSynth (synth, 36, 16384);
        return energyAt (out, f0, 4096, 8192);
    };

    const float lp = fundamental (0.0f);
    const float hp = fundamental (2.0f);
    EXPECT_GT (lp, hp * 10.0f) << "highpass kept the fundamental a lowpass keeps";
}

TEST (SynthExpansion, FilterKeytrackingOpensWithTheKey)
{
    // Cutoff at 400 Hz, full keytracking: a note three octaves up shifts the
    // effective cutoff up with it, so its own upper harmonics survive where
    // the untracked filter would have shaved them.
    auto harmonicRatio = [] (float keyAmt)
    {
        SynthGenerator synth;
        sustained (synth).params().osc2Mix.store (0.0f);
        synth.params().cutoffHz.store (400.0f);
        synth.params().filterKey.store (keyAmt);
        auto out = renderSynth (synth, 96, 16384);   // ~1976 Hz fundamental
        const double f0 = juce::MidiMessage::getMidiNoteInHertz (96);
        return energyAt (out, f0 * 2.0, 4096, 8192) / juce::jmax (1.0e-9f, energyAt (out, f0, 4096, 8192));
    };

    EXPECT_GT (harmonicRatio (1.0f), harmonicRatio (0.0f) * 2.0f)
        << "keytracking did not open the filter";
}

TEST (SynthExpansion, LfoPansTheVoiceAround)
{
    SynthGenerator synth;
    sustained (synth).params().osc2Mix.store (0.0f);
    synth.params().lfoTarget.store (3.0f);   // pan
    synth.params().lfoAmount.store (1.0f);
    synth.params().lfoRate.store (4.0f);
    auto out = renderSynth (synth, 60, 44100);

    // Quarter-period windows: the L/R balance must swing visibly.
    float minBalance = 1.0e9f, maxBalance = -1.0e9f;
    for (int w = 0; w < 14; ++w)
    {
        const int start = 2048 + w * 2756;
        const float l = out.getRMSLevel (0, start, 2048);
        const float r = out.getRMSLevel (1, start, 2048);
        const float balance = (l - r) / juce::jmax (1.0e-6f, l + r);
        minBalance = juce::jmin (minBalance, balance);
        maxBalance = juce::jmax (maxBalance, balance);
    }
    EXPECT_GT (maxBalance - minBalance, 0.4f) << "autopan barely moved";
}

TEST (SynthExpansion, GlideSlidesIntoTheSecondNote)
{
    SynthGenerator synth;
    sustained (synth).params().osc2Mix.store (0.0f);
    synth.params().oscShape.store (-2.0f);   // sine: clean zero crossings
    synth.params().glide.store (0.4f);

    // First note C3, then C5 two octaves up.
    juce::AudioBuffer<float> out (2, 33075);
    out.clear();
    {
        juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, 0, 11025);
        juce::MidiBuffer first;
        first.addEvent (juce::MidiMessage::noteOn (1, 48, 0.9f), 0);
        synth.render (view, first);
    }
    {
        juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, 11025, 22050);
        juce::MidiBuffer second;
        second.addEvent (juce::MidiMessage::noteOff (1, 48), 0);
        second.addEvent (juce::MidiMessage::noteOn (1, 72, 0.9f), 0);
        synth.render (view, second);
    }

    auto zeroCrossings = [&out] (int from, int count)
    {
        const auto* d = out.getReadPointer (0);
        int n = 0;
        for (int i = from + 1; i < from + count; ++i)
            if ((d[i] >= 0.0f) != (d[i - 1] >= 0.0f))
                ++n;
        return n;
    };

    // Right after the change the pitch is still near C3; near the end of the
    // glide it has arrived at C5 (4x the crossings).
    const int early = zeroCrossings (11025, 4410);
    const int late  = zeroCrossings (11025 + 22050 - 4410 - 1, 4410);
    EXPECT_LT (early * 2, late) << "no slide: pitch jumped immediately";
    EXPECT_GT (early, 0);
}

TEST (SynthExpansion, LegacyShapeValuesKeepTheirMeaning)
{
    // oscShape 0..1 predates the morph axis; 0 must still be the identical
    // polyBLEP saw. Compare against the closed form directly.
    constexpr double dt = 220.0 / 44100.0;
    double phase = 0.3;
    for (int i = 0; i < 1000; ++i)
    {
        const float viaMorph = synthosc::sample (0.0f, 0.0f, phase, dt);
        const float direct   = synthosc::saw (phase, dt);
        ASSERT_FLOAT_EQ (viaMorph, direct);
        phase += dt;
        if (phase >= 1.0)
            phase -= 1.0;
    }
}
