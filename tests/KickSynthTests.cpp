#include <gtest/gtest.h>
#include "TestHelpers.h"
#include "control/ControlDispatcher.h"
#include "engine/KickDsp.h"
#include "engine/KickGenerator.h"
#include "model/ChannelParams.h"
#include "model/KickChannel.h"
#include "model/KickEnvelope.h"
#include "model/KickPresets.h"
#include "ui/rack/KickEditor.h"

// The kick designer: layers, drawn envelopes, the output chain, the factory
// bank, and the promise underpinning all of it — a kick channel saved before
// any of this existed still renders exactly the samples it used to.

namespace
{
juce::AudioBuffer<float> renderKick (Generator& gen, int numSamples, int key = 60)
{
    juce::AudioBuffer<float> out (2, numSamples);
    out.clear();
    int pos = 0;
    while (pos < numSamples)
    {
        const int n = juce::jmin (512, numSamples - pos);
        juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, pos, n);
        juce::MidiBuffer midi;
        if (pos == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, key, 1.0f), 0);
        gen.render (view, midi);
        pos += n;
    }
    return out;
}

float rms (const juce::AudioBuffer<float>& buffer, int start = 0, int length = -1)
{
    if (length < 0)
        length = buffer.getNumSamples() - start;
    return buffer.getRMSLevel (0, start, juce::jmax (0, length));
}

// Energy at a frequency, by correlation — enough to tell layers apart.
float energyAt (const juce::AudioBuffer<float>& buffer, int start, int length, double frequency)
{
    const float* data = buffer.getReadPointer (0);
    const int end = juce::jmin (start + length, buffer.getNumSamples());
    double re = 0.0, im = 0.0;
    for (int i = start; i < end; ++i)
    {
        const double phase = juce::MathConstants<double>::twoPi * frequency * (i - start) / test::kSampleRate;
        re += data[i] * std::cos (phase);
        im += data[i] * std::sin (phase);
    }
    const double n = juce::jmax (1, end - start);
    return (float) (std::sqrt (re * re + im * im) / n);
}

// A kick channel with nothing but the pre-layer parameters set, exactly as a
// project saved before the redesign would carry it.
juce::ValueTree legacyKickChannel (ProjectModel& model)
{
    auto channel = model.addChannel ("kick", "Legacy");
    channel.setProperty (ids::kickStartFreq, 320.0, nullptr);
    channel.setProperty (ids::kickEndFreq, 45.0, nullptr);
    channel.setProperty (ids::kickPitchDecay, 0.03, nullptr);
    channel.setProperty (ids::kickAmpDecay, 0.45, nullptr);
    channel.setProperty (ids::kickBodyShape, 0.2, nullptr);
    channel.setProperty (ids::kickClickLevel, 0.35, nullptr);
    channel.setProperty (ids::kickClickDecay, 0.005, nullptr);
    channel.setProperty (ids::kickNoiseLevel, 0.15, nullptr);
    channel.setProperty (ids::kickNoiseDecay, 0.025, nullptr);
    channel.setProperty (ids::drive, 0.4, nullptr);
    channel.setProperty (ids::driveCurve, 1.0, nullptr);
    channel.setProperty (ids::envShape, 0.8, nullptr);
    return channel;
}

// The pre-layer generator, driven straight through its Params — the reference
// the back-compatibility test compares against.
std::unique_ptr<KickGenerator> legacyGenerator()
{
    auto kick = std::make_unique<KickGenerator>();
    kick->prepare (test::kSampleRate, 512);
    auto& p = kick->params();
    p.startFreq.store (320.0f);
    p.endFreq.store (45.0f);
    p.pitchDecay.store (0.03f);
    p.ampDecay.store (0.45f);
    p.bodyShape.store (0.2f);
    p.clickLevel.store (0.35f);
    p.clickDecay.store (0.005f);
    p.noiseLevel.store (0.15f);
    p.noiseDecay.store (0.025f);
    p.drive.store (0.4f);
    p.driveCurve.store (1);
    p.envShape.store (0.8f);
    return kick;
}
} // namespace

// ============================ back compatibility ============================

TEST (KickBackCompat, ChannelWithoutTheNewParametersIsSampleIdentical)
{
    test::EngineFixture fixture;
    auto channel = legacyKickChannel (fixture.model);

    KickGenerator fromTree;
    fromTree.prepare (test::kSampleRate, 512);
    kickchannel::apply (fromTree, channel);

    auto reference = legacyGenerator();

    const int numSamples = (int) (test::kSampleRate * 0.6);
    const auto viaTree = renderKick (fromTree, numSamples);
    const auto viaParams = renderKick (*reference, numSamples);

    ASSERT_GT (rms (viaParams), 0.01f);
    for (int i = 0; i < numSamples; ++i)
        ASSERT_FLOAT_EQ (viaTree.getSample (0, i), viaParams.getSample (0, i))
            << "diverged at sample " << i;
}

TEST (KickBackCompat, TheNewDefaultsAddNothingToTheSound)
{
    // Every added layer and chain stage is off at its default, so a channel
    // that names none of them must render like one that names all of them.
    test::EngineFixture fixture;
    auto bare = legacyKickChannel (fixture.model);
    auto explicitDefaults = legacyKickChannel (fixture.model);
    for (const auto& descriptor : channelparams::kick())
        if (! explicitDefaults.hasProperty (descriptor.id))
            explicitDefaults.setProperty (descriptor.id, descriptor.defaultValue, nullptr);

    KickGenerator a, b;
    a.prepare (test::kSampleRate, 512);
    b.prepare (test::kSampleRate, 512);
    kickchannel::apply (a, bare);
    kickchannel::apply (b, explicitDefaults);

    const int numSamples = (int) (test::kSampleRate * 0.6);
    const auto bareOut = renderKick (a, numSamples);
    const auto defaultOut = renderKick (b, numSamples);

    for (int i = 0; i < numSamples; ++i)
        ASSERT_FLOAT_EQ (bareOut.getSample (0, i), defaultOut.getSample (0, i))
            << "diverged at sample " << i;
}

// ================================= layers ==================================

TEST (KickLayers, SubAddsLowEndAtItsOwnTuning)
{
    KickGenerator kick;
    kick.prepare (test::kSampleRate, 512);
    auto& p = kick.params();
    p.clickLevel.store (0.0f);
    p.noiseLevel.store (0.0f);
    p.drive.store (0.0f);
    p.endFreq.store (50.0f);
    p.ampDecay.store (0.6f);

    const int numSamples = (int) (test::kSampleRate * 0.7);
    const auto dry = renderKick (kick, numSamples);

    p.subLevel.store (0.8f);
    p.subTune.store (-12.0f);   // one octave under the body's end frequency
    p.subDecay.store (0.6f);
    kick.reset();
    const auto withSub = renderKick (kick, numSamples);

    const int window = (int) (test::kSampleRate * 0.25);
    EXPECT_GT (energyAt (withSub, 0, window, 25.0), energyAt (dry, 0, window, 25.0) * 4.0f);
}

TEST (KickLayers, BodyLevelScalesTheBodyAndZeroSilencesIt)
{
    KickGenerator kick;
    kick.prepare (test::kSampleRate, 512);
    auto& p = kick.params();
    p.clickLevel.store (0.0f);
    p.noiseLevel.store (0.0f);
    p.drive.store (0.0f);

    const int numSamples = (int) (test::kSampleRate * 0.6);
    const float full = rms (renderKick (kick, numSamples));

    p.bodyLevel.store (0.5f);
    kick.reset();
    const float half = rms (renderKick (kick, numSamples));

    p.bodyLevel.store (0.0f);
    kick.reset();
    const float silent = rms (renderKick (kick, numSamples));

    EXPECT_NEAR (half, full * 0.5f, full * 0.05f);
    EXPECT_LT (silent, 1.0e-6f);
}

TEST (KickLayers, HarmonicsGrowTheUpperHarmonicsOfTheBody)
{
    KickGenerator kick;
    kick.prepare (test::kSampleRate, 512);
    auto& p = kick.params();
    p.clickLevel.store (0.0f);
    p.noiseLevel.store (0.0f);
    p.drive.store (0.0f);
    p.startFreq.store (60.0f);   // no sweep: the body sits on one note
    p.endFreq.store (60.0f);
    p.ampDecay.store (0.6f);

    const int numSamples = (int) (test::kSampleRate * 0.6);
    const int window = (int) (test::kSampleRate * 0.2);

    const auto clean = renderKick (kick, numSamples);
    p.bodyHarm.store (0.9f);
    kick.reset();
    const auto dirty = renderKick (kick, numSamples);

    EXPECT_GT (energyAt (dirty, 0, window, 180.0), energyAt (clean, 0, window, 180.0) * 3.0f);
}

TEST (KickLayers, EveryClickTypeMakesADifferentTransient)
{
    const int numSamples = (int) (test::kSampleRate * 0.3);
    const int transient = (int) (test::kSampleRate * 0.01);

    std::vector<float> levels;
    for (int type = 0; type < 3; ++type)   // 3 = sample, covered separately
    {
        KickGenerator kick;
        kick.prepare (test::kSampleRate, 512);
        auto& p = kick.params();
        p.bodyLevel.store (0.0f);
        p.noiseLevel.store (0.0f);
        p.drive.store (0.0f);
        p.clickLevel.store (0.8f);
        p.clickDecay.store (0.004f);
        p.clickType.store (type);
        levels.push_back (rms (renderKick (kick, numSamples), 0, transient));
    }

    for (const float level : levels)
        EXPECT_GT (level, 0.005f);
    // The pulse is a square edge, so it carries more energy than the sine tick.
    EXPECT_GT (levels[2], levels[0]);
}

TEST (KickLayers, PunchAndHoldReshapeTheAmplitude)
{
    KickGenerator kick;
    kick.prepare (test::kSampleRate, 512);
    auto& p = kick.params();
    p.clickLevel.store (0.0f);
    p.noiseLevel.store (0.0f);
    p.drive.store (0.0f);
    p.ampDecay.store (0.4f);

    const int numSamples = (int) (test::kSampleRate * 0.9);
    const auto plain = renderKick (kick, numSamples);

    p.punch.store (1.0f);
    kick.reset();
    const auto punched = renderKick (kick, numSamples);
    EXPECT_GT (punched.getMagnitude (0, (int) (test::kSampleRate * 0.01)),
               plain.getMagnitude (0, (int) (test::kSampleRate * 0.01)) * 1.5f);

    p.punch.store (0.0f);
    p.hold.store (0.2f);
    kick.reset();
    const auto held = renderKick (kick, numSamples);
    // 150 ms in, the held kick is still at full level while the plain one has
    // decayed well past it.
    const int at150ms = (int) (test::kSampleRate * 0.15);
    EXPECT_GT (rms (held, at150ms, 2000), rms (plain, at150ms, 2000) * 1.5f);
}

// =============================== envelopes =================================

TEST (KickEnvelope, ValueAtInterpolatesAndTensionBendsTheSegment)
{
    kickdsp::Envelope envelope;
    envelope.points = { { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };

    EXPECT_FLOAT_EQ (envelope.valueAt (0.0f), 1.0f);
    EXPECT_FLOAT_EQ (envelope.valueAt (1.0f), 0.0f);
    EXPECT_NEAR (envelope.valueAt (0.5f), 0.5f, 1.0e-5f);

    envelope.points[0].tension = 1.0f;    // fast then slow
    EXPECT_LT (envelope.valueAt (0.5f), 0.5f);
    envelope.points[0].tension = -1.0f;   // slow then fast
    EXPECT_GT (envelope.valueAt (0.5f), 0.5f);
}

TEST (KickEnvelope, TidySortsClampsAndPinsTheEnds)
{
    kickdsp::Envelope envelope;
    envelope.points = { { 0.8f, 2.0f, 5.0f }, { 0.2f, -1.0f, 0.0f }, { 0.5f, 0.5f, 0.0f } };
    envelope.tidy();

    ASSERT_EQ (envelope.points.size(), 3u);
    EXPECT_FLOAT_EQ (envelope.points.front().pos, 0.0f);
    EXPECT_FLOAT_EQ (envelope.points.back().pos, 1.0f);
    EXPECT_TRUE (envelope.points[0].pos <= envelope.points[1].pos);
    EXPECT_TRUE (envelope.points[1].pos <= envelope.points[2].pos);
    for (const auto& point : envelope.points)
    {
        EXPECT_GE (point.value, 0.0f);
        EXPECT_LE (point.value, 1.0f);
        EXPECT_LE (point.tension, 1.0f);
    }
}

TEST (KickEnvelope, RoundTripsThroughTheChannelTree)
{
    test::EngineFixture fixture;
    auto channel = fixture.model.addChannel ("kick", "Kick");

    EXPECT_FALSE (kickenv::isDrawn (channel, kickenv::ampRole));
    EXPECT_TRUE (kickenv::read (channel, kickenv::ampRole).empty());

    kickdsp::Envelope envelope;
    envelope.points = { { 0.0f, 1.0f, 0.4f }, { 0.35f, 0.6f, -0.2f }, { 1.0f, 0.0f, 0.0f } };
    kickenv::write (channel, kickenv::ampRole, envelope, nullptr);

    EXPECT_TRUE (kickenv::isDrawn (channel, kickenv::ampRole));
    const auto back = kickenv::read (channel, kickenv::ampRole);
    ASSERT_EQ (back.points.size(), 3u);
    EXPECT_FLOAT_EQ (back.points[1].pos, 0.35f);
    EXPECT_FLOAT_EQ (back.points[1].value, 0.6f);
    EXPECT_FLOAT_EQ (back.points[1].tension, -0.2f);
    // The pitch role is stored separately and stays untouched.
    EXPECT_FALSE (kickenv::isDrawn (channel, kickenv::pitchRole));

    kickenv::write (channel, kickenv::ampRole, {}, nullptr);
    EXPECT_FALSE (kickenv::isDrawn (channel, kickenv::ampRole));
}

TEST (KickEnvelope, ADrawnAmpCurveDrivesTheVoice)
{
    KickGenerator kick;
    kick.prepare (test::kSampleRate, 512);
    auto& p = kick.params();
    p.clickLevel.store (0.0f);
    p.noiseLevel.store (0.0f);
    p.drive.store (0.0f);
    p.ampDecay.store (0.5f);

    // A curve that holds full level to halfway, then falls off a cliff.
    kickdsp::Envelope envelope;
    envelope.points = { { 0.0f, 1.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    kick.setAmpEnvelope (envelope);

    const int numSamples = (int) (test::kSampleRate * 0.7);
    const auto out = renderKick (kick, numSamples);

    const int early = (int) (test::kSampleRate * 0.02);
    const int mid   = (int) (test::kSampleRate * 0.22);
    const int late  = (int) (test::kSampleRate * 0.47);
    EXPECT_NEAR (rms (out, mid, 2000), rms (out, early, 2000), rms (out, early, 2000) * 0.15f);
    EXPECT_LT (rms (out, late, 2000), rms (out, mid, 2000) * 0.5f);
}

TEST (KickEnvelope, ADrawnPitchCurveOverridesTheAnalyticSweep)
{
    KickGenerator kick;
    kick.prepare (test::kSampleRate, 512);
    auto& p = kick.params();
    p.clickLevel.store (0.0f);
    p.noiseLevel.store (0.0f);
    p.drive.store (0.0f);
    p.startFreq.store (400.0f);
    p.endFreq.store (50.0f);
    p.pitchDecay.store (0.25f);
    p.ampDecay.store (0.8f);

    // Flat at the top for the first half of the span: the body should still be
    // near 400 Hz well past where the analytic decay would have collapsed.
    kickdsp::Envelope envelope;
    envelope.points = { { 0.0f, 1.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    kick.setPitchEnvelope (envelope);

    const auto out = renderKick (kick, (int) (test::kSampleRate * 0.5));
    const int at100ms = (int) (test::kSampleRate * 0.1);
    const int window = (int) (test::kSampleRate * 0.02);
    EXPECT_GT (energyAt (out, at100ms, window, 400.0), energyAt (out, at100ms, window, 60.0));
}

// ============================== output chain ===============================

TEST (KickChain, EqShapesTheOutput)
{
    KickGenerator kick;
    kick.prepare (test::kSampleRate, 512);
    auto& p = kick.params();
    p.clickLevel.store (0.0f);
    p.drive.store (0.0f);
    p.noiseLevel.store (0.6f);       // broadband, so the shelves have something to grab
    p.noiseDecay.store (0.4f);
    p.noiseTone.store (1.0f);
    p.bodyLevel.store (0.0f);

    const int numSamples = (int) (test::kSampleRate * 0.5);
    const auto flat = renderKick (kick, numSamples);

    p.eqHighFreq.store (4000.0f);
    p.eqHighGain.store (-18.0f);
    kick.reset();
    const auto cut = renderKick (kick, numSamples);

    const int window = (int) (test::kSampleRate * 0.2);
    EXPECT_LT (energyAt (cut, 0, window, 9000.0), energyAt (flat, 0, window, 9000.0) * 0.6f);
}

TEST (KickChain, CompressorAndLimiterTameThePeaks)
{
    const int numSamples = (int) (test::kSampleRate * 0.6);

    KickGenerator plain;
    plain.prepare (test::kSampleRate, 512);
    plain.params().drive.store (0.0f);
    const auto dry = renderKick (plain, numSamples);

    KickGenerator squashed;
    squashed.prepare (test::kSampleRate, 512);
    squashed.params().drive.store (0.0f);
    squashed.params().compression.store (1.0f);
    const auto wet = renderKick (squashed, numSamples);

    // Same hit, flatter: less crest between the transient and the tail.
    const int transient = (int) (test::kSampleRate * 0.02);
    const int tail = (int) (test::kSampleRate * 0.3);
    const float dryRatio = rms (dry, 0, transient) / juce::jmax (1.0e-6f, rms (dry, tail, 4000));
    const float wetRatio = rms (wet, 0, transient) / juce::jmax (1.0e-6f, rms (wet, tail, 4000));
    EXPECT_LT (wetRatio, dryRatio);

    KickGenerator limited;
    limited.prepare (test::kSampleRate, 512);
    limited.params().drive.store (0.0f);
    limited.params().limiter.store (1.0f);
    const auto ceilinged = renderKick (limited, numSamples);
    EXPECT_LE (ceilinged.getMagnitude (0, numSamples), 0.99f);
}

TEST (KickChain, OutputTrimIsPlainGain)
{
    const int numSamples = (int) (test::kSampleRate * 0.5);

    KickGenerator unity;
    unity.prepare (test::kSampleRate, 512);
    unity.params().drive.store (0.0f);
    const float reference = rms (renderKick (unity, numSamples));

    KickGenerator trimmed;
    trimmed.prepare (test::kSampleRate, 512);
    trimmed.params().drive.store (0.0f);
    trimmed.params().outputDb.store (-6.0f);
    const float attenuated = rms (renderKick (trimmed, numSamples));

    EXPECT_NEAR (attenuated, reference * juce::Decibels::decibelsToGain (-6.0f), reference * 0.02f);
}

TEST (KickChain, ToneEqResponseMatchesWhatItDoesToAudio)
{
    // The editor draws magnitudeDb(); this pins it to the filters' behaviour.
    kickdsp::ToneEq eq;
    eq.prepare (test::kSampleRate);
    eq.setSettings ({ 90.0f, 0.0f, 1000.0f, 12.0f, 4000.0f, 0.0f });

    const double gain = juce::Decibels::decibelsToGain (eq.magnitudeDb (1000.0));
    EXPECT_NEAR (gain, juce::Decibels::decibelsToGain (12.0), 0.5);

    // Drive a 1 kHz sine through and compare the measured gain.
    const int n = (int) test::kSampleRate;
    double peakIn = 0.0, peakOut = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const float x = std::sin ((float) (juce::MathConstants<double>::twoPi * 1000.0 * i / test::kSampleRate));
        const float y = eq.processSample (0, x);
        if (i > n / 2)
        {
            peakIn = juce::jmax (peakIn, (double) std::abs (x));
            peakOut = juce::jmax (peakOut, (double) std::abs (y));
        }
    }
    EXPECT_NEAR (peakOut / peakIn, gain, gain * 0.1);
}

// ============================== automation =================================

TEST (KickAutomation, EveryAutomatableKnobResolvesToAParameter)
{
    KickGenerator kick;
    for (const auto& descriptor : channelparams::kick())
    {
        auto* param = kick.getAutomatableParam (descriptor.id.toString());
        if (descriptor.automatable)
            EXPECT_NE (param, nullptr) << descriptor.id.toString() << " is automatable but unreachable";
    }
}

TEST (KickAutomation, IndexKnobsAreNotAutomatable)
{
    // A curve sweeping an index would step through unrelated shapes, so the
    // click source, the drive curve and the root note stay off the list.
    for (const auto& descriptor : channelparams::kick())
        if (descriptor.id == ids::kickClickType || descriptor.id == ids::driveCurve
            || descriptor.id == ids::rootNote)
            EXPECT_FALSE (descriptor.automatable) << descriptor.id.toString();
}

// ================================ presets ==================================

TEST (KickPresets, BankIsWellFormed)
{
    const auto& bank = kickpresets::all();
    ASSERT_GT (bank.size(), 20u);

    juce::StringArray seen;
    for (const auto& preset : bank)
    {
        EXPECT_FALSE (preset.name.isEmpty());
        EXPECT_FALSE (preset.category.isEmpty());
        EXPECT_FALSE (seen.contains (preset.name)) << "duplicate preset " << preset.name;
        seen.add (preset.name);

        for (const auto& [id, value] : preset.values)
        {
            const auto* descriptor = channelparams::find ("kick", id.toString());
            ASSERT_NE (descriptor, nullptr) << preset.name << ": unknown parameter " << id.toString();
            EXPECT_GE (value, descriptor->range.start) << preset.name << ": " << id.toString();
            EXPECT_LE (value, descriptor->range.end) << preset.name << ": " << id.toString();
            EXPECT_NE (id, ids::rootNote) << preset.name << ": presets must not retune the channel";
        }

        for (const auto* envelope : { &preset.pitchEnvelope, &preset.ampEnvelope })
            if (! envelope->points.empty())
            {
                EXPECT_GE (envelope->points.size(), 2u) << preset.name;
                EXPECT_FLOAT_EQ (envelope->points.front().pos, 0.0f) << preset.name;
                EXPECT_FLOAT_EQ (envelope->points.back().pos, 1.0f) << preset.name;
            }
    }

    EXPECT_GE (kickpresets::categories().size(), 5);
}

TEST (KickPresets, ApplyWritesEveryParameterAndLeavesTheTuningAlone)
{
    test::EngineFixture fixture;
    auto channel = fixture.model.addChannel ("kick", "Kick");
    channel.setProperty (ids::rootNote, 41, nullptr);
    // A value the preset does not name, left over from whatever was loaded before.
    channel.setProperty (ids::kickSubLevel, 0.9, nullptr);

    const auto* preset = kickpresets::find ("TR-808 Boom");
    ASSERT_NE (preset, nullptr);
    kickpresets::apply (channel, *preset, nullptr);

    EXPECT_EQ ((int) channel[ids::rootNote], 41);
    EXPECT_EQ (channel[ids::presetName].toString(), "TR-808 Boom");
    // Named by the preset...
    EXPECT_DOUBLE_EQ ((double) channel[ids::kickStartFreq], 90.0);
    // ...and not named, so back to its default rather than the stale 0.9.
    const auto* subLevel = channelparams::find ("kick", ids::kickSubLevel.toString());
    ASSERT_NE (subLevel, nullptr);
    EXPECT_DOUBLE_EQ ((double) channel[ids::kickSubLevel], subLevel->defaultValue);

    for (const auto& descriptor : channelparams::kick())
        if (descriptor.id != ids::rootNote)
            EXPECT_TRUE (channel.hasProperty (descriptor.id)) << descriptor.id.toString();
}

TEST (KickPresets, SwitchingPresetsClearsTheOneBefore)
{
    test::EngineFixture fixture;
    auto channel = fixture.model.addChannel ("kick", "Kick");

    const auto* drawn = kickpresets::find ("Rumble Room");     // ships an amp curve
    const auto* plain = kickpresets::find ("TR-909 Punch");    // ships none
    ASSERT_NE (drawn, nullptr);
    ASSERT_NE (plain, nullptr);

    kickpresets::apply (channel, *drawn, nullptr);
    EXPECT_TRUE (kickenv::isDrawn (channel, kickenv::ampRole));

    kickpresets::apply (channel, *plain, nullptr);
    EXPECT_FALSE (kickenv::isDrawn (channel, kickenv::ampRole));
    EXPECT_FALSE (kickenv::isDrawn (channel, kickenv::pitchRole));
}

TEST (KickPresets, EveryPresetRendersSomethingSaneThroughTheEngine)
{
    test::EngineFixture fixture;
    auto channel = fixture.model.addChannel ("kick", "Kick");

    for (const auto& preset : kickpresets::all())
    {
        kickpresets::apply (channel, preset, nullptr);
        const auto out = kickchannel::render (channel, test::kSampleRate);
        ASSERT_GT (out.getNumSamples(), 1000) << preset.name;

        const float peak = out.getMagnitude (0, out.getNumSamples());
        EXPECT_GT (peak, 0.05f) << preset.name << " is inaudible";
        EXPECT_LE (peak, 1.05f) << preset.name << " clips hard";

        // No NaNs or infinities anywhere in the tail.
        for (int i = 0; i < out.getNumSamples(); ++i)
            ASSERT_TRUE (std::isfinite (out.getSample (0, i))) << preset.name << " at " << i;
    }
}

TEST (KickPresets, PresetsSoundDifferentFromEachOther)
{
    test::EngineFixture fixture;
    auto a = fixture.model.addChannel ("kick", "A");
    auto b = fixture.model.addChannel ("kick", "B");

    kickpresets::apply (a, *kickpresets::find ("808 Long"), nullptr);
    kickpresets::apply (b, *kickpresets::find ("Gabber 190"), nullptr);

    const auto longOne = kickchannel::render (a, test::kSampleRate);
    const auto hardOne = kickchannel::render (b, test::kSampleRate);

    // The 808 rings on for seconds and settles a fifth lower; the gabber kick
    // is a short, much higher-tuned spike.
    EXPECT_GT (longOne.getNumSamples(), hardOne.getNumSamples() * 4);

    const auto pitchOf = [] (const juce::AudioBuffer<float>& buffer)
    {
        const int from = (int) (test::kSampleRate * 0.05);
        return kickdsp::dominantFrequency (buffer.getReadPointer (0) + from,
                                           buffer.getNumSamples() - from, test::kSampleRate);
    };
    EXPECT_GT (pitchOf (hardOne), pitchOf (longOne) * 1.3);
}

// ============================ channel rendering ============================

TEST (KickRender, LengthFollowsHoldAndDecay)
{
    test::EngineFixture fixture;
    auto channel = fixture.model.addChannel ("kick", "Kick");
    channel.setProperty (ids::kickAmpDecay, 0.5, nullptr);

    const double base = kickchannel::lengthSeconds (channel);
    channel.setProperty (ids::kickHold, 0.25, nullptr);
    EXPECT_NEAR (kickchannel::lengthSeconds (channel), base + 0.25, 1.0e-9);

    const auto out = kickchannel::render (channel, test::kSampleRate);
    EXPECT_NEAR ((double) out.getNumSamples() / test::kSampleRate,
                 kickchannel::lengthSeconds (channel), 0.01);
}

TEST (KickRender, DominantFrequencyReadsTheTailPitch)
{
    // A steady 55 Hz body: the tuning readout must land on it.
    KickGenerator kick;
    kick.prepare (test::kSampleRate, 512);
    auto& p = kick.params();
    p.startFreq.store (55.0f);
    p.endFreq.store (55.0f);
    p.clickLevel.store (0.0f);
    p.noiseLevel.store (0.0f);
    p.drive.store (0.0f);
    p.ampDecay.store (1.0f);

    const auto out = renderKick (kick, (int) (test::kSampleRate * 0.5));
    const double hz = kickdsp::dominantFrequency (out.getReadPointer (0),
                                                  (int) (test::kSampleRate * 0.3),
                                                  test::kSampleRate);
    EXPECT_NEAR (hz, 55.0, 2.0);
}

// ============================== control API ================================

TEST (KickControl, ListsAndLoadsFactoryPresets)
{
    AppServices services { false };   // no audio device in tests
    ControlDispatcher dispatcher (services);

    const auto listed = dispatcher.dispatch ("kick.presets", {});
    ASSERT_TRUE (listed.isArray());
    EXPECT_GE (listed.size(), 20);
    EXPECT_TRUE (listed[0]["name"].toString().isNotEmpty());
    EXPECT_TRUE (listed[0]["category"].toString().isNotEmpty());

    const auto added = dispatcher.dispatch ("channel.add",
        juce::JSON::parse (R"({"type": "kick", "name": "Kick"})"));
    const int channelId = added["id"];

    const auto loaded = dispatcher.dispatch ("kick.loadPreset", juce::JSON::parse (
        R"({"channelId": )" + juce::String (channelId) + R"(, "preset": "Berlin Tunnel"})"));
    EXPECT_EQ (loaded["preset"].toString(), "Berlin Tunnel");

    auto channel = services.project.getChannelById (channelId);
    EXPECT_EQ (channel[ids::presetName].toString(), "Berlin Tunnel");
    EXPECT_DOUBLE_EQ ((double) channel[ids::kickStartFreq], 480.0);

    EXPECT_ANY_THROW (dispatcher.dispatch ("kick.loadPreset", juce::JSON::parse (
        R"({"channelId": )" + juce::String (channelId) + R"(, "preset": "Nope"})")));
}

TEST (KickControl, SetsEveryDescriptorParameter)
{
    AppServices services { false };
    ControlDispatcher dispatcher (services);

    const auto added = dispatcher.dispatch ("channel.add",
        juce::JSON::parse (R"({"type": "kick", "name": "Kick"})"));
    const int channelId = added["id"];
    auto channel = services.project.getChannelById (channelId);

    // Halfway up each knob's range, so nothing lands on a default by accident.
    auto* request = new juce::DynamicObject();
    request->setProperty ("channelId", channelId);
    for (const auto& descriptor : channelparams::kick())
    {
        if (descriptor.id == ids::rootNote)
            continue;
        request->setProperty (descriptor.id.toString(),
                              descriptor.range.convertFrom0to1 (0.5));
    }
    dispatcher.dispatch ("channel.set", juce::var (request));

    for (const auto& descriptor : channelparams::kick())
        if (descriptor.id != ids::rootNote)
            EXPECT_NEAR ((double) channel[descriptor.id],
                         descriptor.range.convertFrom0to1 (0.5), 1.0e-9)
                << descriptor.id.toString() << " did not reach the tree";
}

// ============================= envelope editor =============================

TEST (KickEnvelopeEditor, DrawModeRoundTripsThroughTheAnalyticDecay)
{
    AppServices services { false };
    auto channel = services.project.addChannel ("kick", "Kick");
    channel.setProperty (ids::kickPitchDecay, 0.04, nullptr);

    KickEnvelopeCanvas canvas (services, channel);
    canvas.setRole (kickenv::pitchRole);
    EXPECT_FALSE (canvas.isDrawn());

    // Drawing the pitch curve stretches its span: the analytic decay keeps
    // falling past its time constant, a drawn one stops at the end of its own.
    canvas.setDrawn (true);
    EXPECT_TRUE (canvas.isDrawn());
    EXPECT_NEAR ((double) channel[ids::kickPitchDecay], 0.04 * kickenv::pitchSpanFactor, 1.0e-9);
    EXPECT_GE (kickenv::read (channel, kickenv::pitchRole).points.size(), 2u);

    canvas.setDrawn (false);
    EXPECT_FALSE (canvas.isDrawn());
    EXPECT_NEAR ((double) channel[ids::kickPitchDecay], 0.04, 1.0e-9);

    // The amplitude envelope shares no such rescaling.
    canvas.setRole (kickenv::ampRole);
    canvas.setDrawn (true);
    EXPECT_TRUE (kickenv::isDrawn (channel, kickenv::ampRole));
    EXPECT_FALSE (kickenv::isDrawn (channel, kickenv::pitchRole));
    EXPECT_NEAR ((double) channel[ids::kickPitchDecay], 0.04, 1.0e-9);
}

TEST (KickEnvelopeEditor, TheEditorBuildsEveryKickKnobExactlyOnce)
{
    AppServices services { false };
    auto channel = services.project.addChannel ("kick", "Kick");
    KickEditor editor (services, channel);

    // Walk the module boxes and count the knobs; every automatable descriptor
    // must have exactly one, or a parameter is unreachable from the UI.
    std::function<int (juce::Component&)> countKnobs = [&] (juce::Component& parent)
    {
        int found = 0;
        for (int i = 0; i < parent.getNumChildComponents(); ++i)
        {
            auto* child = parent.getChildComponent (i);
            found += dynamic_cast<LabelledKnob*> (child) != nullptr ? 1 : countKnobs (*child);
        }
        return found;
    };
    EXPECT_EQ (countKnobs (editor), (int) channelparams::kick().size());
    EXPECT_EQ (editor.getWidth(), KickEditor::preferredWidth);
}

TEST (KickPresetUi, LoadingAPresetMovesTheKnobs)
{
    AppServices services { false };
    auto channel = services.project.addChannel ("kick", "Kick");
    const auto* before = kickpresets::find ("TR-808 Boom");
    const auto* after = kickpresets::find ("Gabber 190");
    ASSERT_NE (before, nullptr);
    ASSERT_NE (after, nullptr);
    kickpresets::apply (channel, *before, nullptr);

    KickEditor editor (services, channel);

    // The rotaries themselves, not the panel around them: every module display
    // paints straight from the tree, so a whole-panel snapshot would change
    // even with the knobs left showing the patch before.
    std::vector<LabelledKnob*> knobs;
    std::function<void (juce::Component&)> collect = [&] (juce::Component& parent)
    {
        for (int i = 0; i < parent.getNumChildComponents(); ++i)
        {
            auto* child = parent.getChildComponent (i);
            if (auto* knob = dynamic_cast<LabelledKnob*> (child))
                knobs.push_back (knob);
            else
                collect (*child);
        }
    };
    collect (editor);
    ASSERT_FALSE (knobs.empty());

    const auto snapshotAll = [&knobs]
    {
        std::vector<juce::Image> images;
        for (auto* knob : knobs)
            images.push_back (knob->createComponentSnapshot (knob->getLocalBounds()));
        return images;
    };
    const auto shownBefore = snapshotAll();

    // Writing the channel tree is what the combo box, the control API and an
    // undo all come down to.
    kickpresets::apply (channel, *after, nullptr);
    const auto shownAfter = snapshotAll();

    int moved = 0;
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const auto& a = shownBefore[i];
        const auto& b = shownAfter[i];
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (a.getPixelAt (x, y) != b.getPixelAt (x, y))
                {
                    ++moved;
                    y = a.getHeight();
                    break;
                }
    }
    // The two patches disagree about far more than one parameter, so a single
    // knob following along would mean something is only half-wired.
    EXPECT_GT (moved, 5) << "the knobs did not follow the preset";
}
