#include <gtest/gtest.h>
#include "TestHelpers.h"
#include "engine/WavWriter.h"
#include "control/ControlDispatcher.h"
#include "engine/Drive.h"
#include "engine/KickGenerator.h"
#include "engine/SamplerGenerator.h"

// Coverage for the kick-design tooling: the sampler's trim / reverse / pitch
// envelope / drive / envelope-shape stages, and the synthesised kick channel.

namespace
{
juce::AudioBuffer<float> renderGen (Generator& gen, int numSamples, int key = 60)
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

// Zero crossings per window, a cheap stand-in for dominant frequency.
int zeroCrossings (const juce::AudioBuffer<float>& buffer, int start, int length)
{
    const float* data = buffer.getReadPointer (0);
    const int end = juce::jmin (start + length, buffer.getNumSamples());
    int crossings = 0;
    for (int i = juce::jmax (1, start); i < end; ++i)
        if ((data[i - 1] < 0.0f) != (data[i] < 0.0f))
            ++crossings;
    return crossings;
}

// Energy above the fundamental, via a naive Goertzel-style correlation.
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

// A 200 Hz sine sample, so pitch and harmonic content are easy to reason about.
std::unique_ptr<SamplerGenerator> makeSineSampler (double seconds = 0.5, double frequency = 200.0)
{
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getNonexistentChildFile ("eurytest-kickdesign", ".wav");
    auto writer = wavwriter::forFile (file, test::kSampleRate, 1, 16);
    if (writer == nullptr)
        return nullptr;

    const int n = (int) (seconds * test::kSampleRate);
    juce::AudioBuffer<float> tone (1, n);
    for (int i = 0; i < n; ++i)
        tone.setSample (0, i, 0.6f * std::sin ((float) (juce::MathConstants<double>::twoPi
                                                        * frequency * i / test::kSampleRate)));
    writer->writeFromAudioSampleBuffer (tone, 0, n);
    writer.reset();

    auto sampler = std::make_unique<SamplerGenerator>();
    sampler->prepare (test::kSampleRate, 512);
    const bool loaded = sampler->loadSampleFile (file);
    file.deleteFile();
    return loaded ? std::move (sampler) : nullptr;
}

// A ramp from -1 to +1 across the sample: position is readable from the value.
std::unique_ptr<SamplerGenerator> makeRampSampler (double seconds = 0.2)
{
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getNonexistentChildFile ("eurytest-ramp", ".wav");
    auto writer = wavwriter::forFile (file, test::kSampleRate, 1, 16);
    if (writer == nullptr)
        return nullptr;

    const int n = (int) (seconds * test::kSampleRate);
    juce::AudioBuffer<float> ramp (1, n);
    for (int i = 0; i < n; ++i)
        ramp.setSample (0, i, juce::jmap ((float) i, 0.0f, (float) (n - 1), -0.9f, 0.9f));
    writer->writeFromAudioSampleBuffer (ramp, 0, n);
    writer.reset();

    auto sampler = std::make_unique<SamplerGenerator>();
    sampler->prepare (test::kSampleRate, 512);
    sampler->params().attack.store (0.0f);   // read the ramp value back untouched
    const bool loaded = sampler->loadSampleFile (file);
    file.deleteFile();
    return loaded ? std::move (sampler) : nullptr;
}

std::unique_ptr<KickGenerator> makeKick()
{
    auto kick = std::make_unique<KickGenerator>();
    kick->prepare (test::kSampleRate, 512);
    return kick;
}
} // namespace

// ---------------- sample trim ----------------

TEST (SamplerTrim, StartOffsetSkipsTheAttack)
{
    auto sampler = makeRampSampler();
    ASSERT_NE (sampler, nullptr);

    auto full = renderGen (*sampler, 4096);
    ASSERT_LT (full.getSample (0, 0), -0.5f) << "ramp should start near -0.9";

    sampler->params().sampleStart.store (0.5f);
    auto trimmed = renderGen (*sampler, 4096);

    // Half-way into a -0.9..0.9 ramp is ~0, so the skipped attack is gone.
    EXPECT_NEAR (trimmed.getSample (0, 0), 0.0f, 0.1f);
    EXPECT_GT (trimmed.getSample (0, 0), full.getSample (0, 0) + 0.5f);
}

TEST (SamplerTrim, StartOffsetShortensPlayback)
{
    auto sampler = makeRampSampler (0.2);
    ASSERT_NE (sampler, nullptr);

    auto full = renderGen (*sampler, 16384);
    sampler->params().sampleStart.store (0.75f);
    auto trimmed = renderGen (*sampler, 16384);

    const int fullLength = (int) (0.2 * test::kSampleRate);
    EXPECT_GT (std::abs (full.getSample (0, fullLength - 200)), 0.1f);
    EXPECT_LT (std::abs (trimmed.getSample (0, fullLength - 200)), 1.0e-4f)
        << "start offset did not shorten the played region";
}

TEST (SamplerTrim, EndTrimStopsEarly)
{
    auto sampler = makeRampSampler (0.2);
    ASSERT_NE (sampler, nullptr);

    sampler->params().sampleEnd.store (0.25f);
    auto out = renderGen (*sampler, 16384);

    const int quarter = (int) (0.05 * test::kSampleRate);
    EXPECT_GT (std::abs (out.getSample (0, quarter - 200)), 0.1f);
    EXPECT_LT (std::abs (out.getSample (0, quarter + 400)), 1.0e-4f);
}

TEST (SamplerTrim, DegenerateRangeStillProducesSomething)
{
    auto sampler = makeRampSampler (0.05);
    ASSERT_NE (sampler, nullptr);

    // End below start: the region is clamped, not inverted, and nothing hangs.
    sampler->params().sampleStart.store (0.9f);
    sampler->params().sampleEnd.store (0.1f);
    auto out = renderGen (*sampler, 8192);
    EXPECT_LT (out.getMagnitude (0, 0, 8192), 1.0f);
}

// ---------------- reverse ----------------

TEST (SamplerReverse, PlaysTheSampleBackwards)
{
    auto sampler = makeRampSampler (0.1);
    ASSERT_NE (sampler, nullptr);

    auto forward = renderGen (*sampler, 8192);
    sampler->params().reverse.store (true);
    auto backward = renderGen (*sampler, 8192);

    const int length = (int) (0.1 * test::kSampleRate);

    // The ramp climbs forwards and falls backwards.
    EXPECT_LT (forward.getSample (0, 16), forward.getSample (0, length - 200));
    EXPECT_GT (backward.getSample (0, 16), backward.getSample (0, length - 200));

    // And the reversed render mirrors the forward one, sample for sample.
    for (int i = 200; i < length - 200; i += 137)
        EXPECT_NEAR (backward.getSample (0, i), -forward.getSample (0, i), 0.02f) << "at " << i;
}

TEST (SamplerReverse, RespectsTheTrimmedRegion)
{
    auto sampler = makeRampSampler (0.1);
    ASSERT_NE (sampler, nullptr);

    sampler->params().reverse.store (true);
    sampler->params().sampleEnd.store (0.5f);
    auto out = renderGen (*sampler, 16384);

    // Reversed playback starts at the trim point, i.e. the middle of the ramp.
    EXPECT_NEAR (out.getSample (0, 8), 0.0f, 0.12f);
    EXPECT_LT (out.getSample (0, (int) (0.05 * test::kSampleRate) - 200), -0.5f);
}

// ---------------- pitch envelope ----------------

TEST (SamplerPitchEnvelope, StartsHigherThanItEnds)
{
    auto sampler = makeSineSampler (0.5, 200.0);
    ASSERT_NE (sampler, nullptr);

    const int window = 2048;
    const int tailAt = 12000;

    auto flat = renderGen (*sampler, 20000);
    const int flatHead = zeroCrossings (flat, 64, window);
    const int flatTail = zeroCrossings (flat, tailAt, window);
    EXPECT_NEAR (flatHead, flatTail, 4) << "no pitch envelope should mean a steady pitch";

    sampler->params().pitchEnvDepth.store (24.0f);   // two octaves up at the transient
    sampler->params().pitchEnvDecay.store (0.05f);
    auto swept = renderGen (*sampler, 20000);

    const int sweptHead = zeroCrossings (swept, 64, window);
    const int sweptTail = zeroCrossings (swept, tailAt, window);

    EXPECT_GT (sweptHead, sweptTail * 2) << "the sweep did not start high";
    EXPECT_NEAR (sweptTail, flatTail, 4) << "the tail should settle back to the sample pitch";
}

TEST (SamplerPitchEnvelope, DecayControlsHowLongTheSweepLasts)
{
    auto measureHeadPitch = [] (float decaySeconds)
    {
        auto sampler = makeSineSampler (0.5, 200.0);
        sampler->params().pitchEnvDepth.store (24.0f);
        sampler->params().pitchEnvDecay.store (decaySeconds);
        auto out = renderGen (*sampler, 20000);
        return zeroCrossings (out, 4096, 2048);
    };

    // At 4096 samples in (~93 ms) the short envelope has already collapsed.
    EXPECT_LT (measureHeadPitch (0.01f), measureHeadPitch (0.4f));
}

TEST (SamplerPitchEnvelope, NegativeDepthSweepsUpwards)
{
    auto sampler = makeSineSampler (0.5, 400.0);
    ASSERT_NE (sampler, nullptr);

    sampler->params().pitchEnvDepth.store (-18.0f);
    sampler->params().pitchEnvDecay.store (0.06f);
    auto out = renderGen (*sampler, 20000);

    EXPECT_LT (zeroCrossings (out, 64, 2048), zeroCrossings (out, 12000, 2048));
}

// ---------------- drive ----------------

TEST (DriveStage, CurvesStayBounded)
{
    for (int curve = 0; curve < drive::numCurves; ++curve)
        for (float x = -4.0f; x <= 4.0f; x += 0.05f)
        {
            const float y = drive::process (x, 1.0f, curve);
            EXPECT_LE (std::abs (y), 1.0001f) << "curve " << curve << " at " << x;
        }
}

TEST (DriveStage, IsTransparentAtZeroAmount)
{
    for (int curve = 0; curve < drive::numCurves; ++curve)
        EXPECT_NEAR (drive::process (0.3f, 0.0f, curve), 0.3f, 0.02f) << "curve " << curve;
}

TEST (SamplerDrive, AddsHarmonicsAndLoudness)
{
    auto clean = makeSineSampler (0.5, 200.0);
    ASSERT_NE (clean, nullptr);
    auto cleanOut = renderGen (*clean, 16384);

    auto driven = makeSineSampler (0.5, 200.0);
    driven->params().drive.store (0.8f);
    auto drivenOut = renderGen (*driven, 16384);

    EXPECT_GT (drivenOut.getRMSLevel (0, 1024, 8192), cleanOut.getRMSLevel (0, 1024, 8192) * 1.3f)
        << "drive did not make it louder";
    EXPECT_GT (drivenOut.getMagnitude (0, 1024, 8192), cleanOut.getMagnitude (0, 1024, 8192));

    // Third harmonic relative to the fundamental — the shape of the distortion.
    auto harmonicRatio = [] (const juce::AudioBuffer<float>& b)
    {
        return energyAt (b, 1024, 8192, 600.0) / juce::jmax (1.0e-6f, energyAt (b, 1024, 8192, 200.0));
    };
    EXPECT_GT (harmonicRatio (drivenOut), harmonicRatio (cleanOut) * 5.0f)
        << "drive did not add harmonic content";
}

TEST (SamplerDrive, EveryCurveDistorts)
{
    for (int curve = 0; curve < drive::numCurves; ++curve)
    {
        auto sampler = makeSineSampler (0.3, 200.0);
        ASSERT_NE (sampler, nullptr) << curve;
        sampler->params().drive.store (0.6f);
        sampler->params().driveCurve.store (curve);
        auto out = renderGen (*sampler, 8192);

        const float harmonics = energyAt (out, 512, 4096, 600.0);
        EXPECT_GT (harmonics, 0.005f) << "curve " << curve << " produced no third harmonic";
    }
}

// ---------------- envelope shape ----------------

TEST (SamplerEnvShape, ExponentialDecaysFasterThanLinear)
{
    auto render = [] (float shape)
    {
        auto sampler = makeSineSampler (1.0, 200.0);
        sampler->params().decay.store (0.5f);
        sampler->params().sustain.store (0.0f);
        sampler->params().envShape.store (shape);
        return renderGen (*sampler, 22050);
    };

    auto linear = render (0.0f);
    auto exponential = render (1.0f);

    // Same peak at the top of the envelope...
    EXPECT_NEAR (linear.getMagnitude (0, 0, 256), exponential.getMagnitude (0, 0, 256), 0.02f);
    // ...but the shaped curve is well under the linear one half-way down.
    EXPECT_LT (exponential.getRMSLevel (0, 8192, 4096), linear.getRMSLevel (0, 8192, 4096) * 0.5f);
}

// ---------------- kick synth ----------------

TEST (KickGeneratorTests, ProducesSound)
{
    auto kick = makeKick();
    auto out = renderGen (*kick, 44100);

    EXPECT_GT (out.getMagnitude (0, 0, 22050), 0.2f);
    EXPECT_LE (out.getMagnitude (0, 0, 44100), 1.001f) << "the drive stage should bound the output";
    // The amp decay is 0.5 s by default, so the last quarter second is silent.
    EXPECT_LT (out.getMagnitude (0, 33075, 11025), 1.0e-4f);
}

TEST (KickGeneratorTests, PitchEnvelopeSweepsDown)
{
    auto kick = makeKick();
    kick->params().clickLevel.store (0.0f);
    kick->params().noiseLevel.store (0.0f);
    kick->params().drive.store (0.0f);
    kick->params().startFreq.store (400.0f);
    kick->params().endFreq.store (50.0f);
    kick->params().pitchDecay.store (0.03f);
    kick->params().ampDecay.store (0.8f);

    auto out = renderGen (*kick, 30000);

    const int head = zeroCrossings (out, 0, 1024);
    const int tail = zeroCrossings (out, 20000, 1024);
    EXPECT_GT (head, tail * 2) << "kick body did not sweep down";

    // The tail should sit close to endFreq: 50 Hz over 1024 samples is ~2 cycles.
    EXPECT_GE (tail, 2);
    EXPECT_LE (tail, 8);
}

TEST (KickGeneratorTests, PitchDecayShortensTheSweep)
{
    auto measureTail = [] (float pitchDecay)
    {
        auto kick = makeKick();
        kick->params().clickLevel.store (0.0f);
        kick->params().noiseLevel.store (0.0f);
        kick->params().drive.store (0.0f);
        kick->params().startFreq.store (400.0f);
        kick->params().endFreq.store (50.0f);
        kick->params().pitchDecay.store (pitchDecay);
        kick->params().ampDecay.store (1.0f);
        auto out = renderGen (*kick, 30000);
        return zeroCrossings (out, 4410, 2048);   // 100 ms in
    };

    EXPECT_LT (measureTail (0.01f), measureTail (0.3f));
}

TEST (KickGeneratorTests, TracksTheKeyboard)
{
    auto renderKey = [] (int key)
    {
        auto kick = makeKick();
        kick->params().clickLevel.store (0.0f);
        kick->params().noiseLevel.store (0.0f);
        kick->params().drive.store (0.0f);
        kick->params().pitchDecay.store (0.005f);   // settle on endFreq quickly
        kick->params().ampDecay.store (1.0f);
        auto out = renderGen (*kick, 30000, key);
        return zeroCrossings (out, 8820, 8820);
    };

    const int atRoot = renderKey (60);
    const int anOctaveUp = renderKey (72);
    EXPECT_NEAR ((double) anOctaveUp / juce::jmax (1, atRoot), 2.0, 0.25);
}

TEST (KickGeneratorTests, ClickAndNoiseLayersAddTopEnd)
{
    auto bodyOnly = makeKick();
    bodyOnly->params().clickLevel.store (0.0f);
    bodyOnly->params().noiseLevel.store (0.0f);
    bodyOnly->params().drive.store (0.0f);
    auto body = renderGen (*bodyOnly, 8192);

    auto withClick = makeKick();
    withClick->params().clickLevel.store (0.8f);
    withClick->params().clickDecay.store (0.01f);
    withClick->params().noiseLevel.store (0.0f);
    withClick->params().drive.store (0.0f);
    auto clicked = renderGen (*withClick, 8192);

    EXPECT_GT (energyAt (clicked, 0, 512, 1400.0), energyAt (body, 0, 512, 1400.0) * 4.0f)
        << "click layer is inaudible";

    auto withNoise = makeKick();
    withNoise->params().clickLevel.store (0.0f);
    withNoise->params().noiseLevel.store (0.9f);
    withNoise->params().noiseDecay.store (0.05f);
    withNoise->params().drive.store (0.0f);
    auto noisy = renderGen (*withNoise, 8192);

    EXPECT_GT (zeroCrossings (noisy, 0, 1024), zeroCrossings (body, 0, 1024) * 3);
}

TEST (KickGeneratorTests, DriveIncreasesLoudness)
{
    auto clean = makeKick();
    clean->params().drive.store (0.0f);
    auto cleanOut = renderGen (*clean, 22050);

    auto driven = makeKick();
    driven->params().drive.store (0.9f);
    auto drivenOut = renderGen (*driven, 22050);

    EXPECT_GT (drivenOut.getRMSLevel (0, 0, 16384), cleanOut.getRMSLevel (0, 0, 16384) * 1.5f);
    EXPECT_LE (drivenOut.getMagnitude (0, 0, 22050), 1.001f);
}

TEST (KickGeneratorTests, ResetKillsVoices)
{
    auto kick = makeKick();
    juce::AudioBuffer<float> out (2, 512);
    out.clear();
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
    kick->render (out, midi);
    ASSERT_GT (out.getMagnitude (0, 0, 512), 0.01f);

    kick->reset();
    out.clear();
    juce::MidiBuffer empty;
    kick->render (out, empty);
    EXPECT_FLOAT_EQ (out.getMagnitude (0, 0, 512), 0.0f);
}

TEST (KickGeneratorTests, IgnoresNoteOff)
{
    auto kick = makeKick();
    juce::AudioBuffer<float> out (2, 8192);
    out.clear();
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
    midi.addEvent (juce::MidiMessage::noteOff (1, 60), 256);
    kick->render (out, midi);

    EXPECT_GT (out.getMagnitude (0, 4096, 4096), 0.05f);
}

TEST (KickGeneratorTests, PolyphonyDoesNotStarve)
{
    auto kick = makeKick();
    juce::AudioBuffer<float> out (2, 4096);
    out.clear();
    juce::MidiBuffer midi;
    for (int i = 0; i < 16; ++i)
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), i * 128);
    kick->render (out, midi);

    EXPECT_GT (out.getMagnitude (0, 3000, 1096), 0.05f) << "voice stealing killed everything";
}

// ---------------- parameter plumbing ----------------

TEST (KickChannelParams, SyncFromTree)
{
    test::EngineFixture fx;
    auto channel = fx.model.addChannel ("kick", "Kick Synth");
    channel.setProperty (ids::kickStartFreq, 500.0, nullptr);
    channel.setProperty (ids::kickEndFreq, 35.0, nullptr);
    channel.setProperty (ids::kickPitchDecay, 0.07, nullptr);
    channel.setProperty (ids::kickAmpDecay, 1.2, nullptr);
    channel.setProperty (ids::kickBodyShape, 0.6, nullptr);
    channel.setProperty (ids::kickClickLevel, 0.45, nullptr);
    channel.setProperty (ids::kickClickDecay, 0.008, nullptr);
    channel.setProperty (ids::kickNoiseLevel, 0.33, nullptr);
    channel.setProperty (ids::kickNoiseDecay, 0.05, nullptr);
    channel.setProperty (ids::drive, 0.75, nullptr);
    channel.setProperty (ids::driveCurve, 2, nullptr);
    channel.setProperty (ids::envShape, 0.25, nullptr);
    channel.setProperty (ids::rootNote, 36, nullptr);

    auto generator = fx.generators.getOrCreate (channel);
    auto* kick = dynamic_cast<KickGenerator*> (generator.get());
    ASSERT_NE (kick, nullptr);

    EXPECT_FLOAT_EQ (kick->params().startFreq.load(), 500.0f);
    EXPECT_FLOAT_EQ (kick->params().endFreq.load(), 35.0f);
    EXPECT_FLOAT_EQ (kick->params().pitchDecay.load(), 0.07f);
    EXPECT_FLOAT_EQ (kick->params().ampDecay.load(), 1.2f);
    EXPECT_FLOAT_EQ (kick->params().bodyShape.load(), 0.6f);
    EXPECT_FLOAT_EQ (kick->params().clickLevel.load(), 0.45f);
    EXPECT_FLOAT_EQ (kick->params().clickDecay.load(), 0.008f);
    EXPECT_FLOAT_EQ (kick->params().noiseLevel.load(), 0.33f);
    EXPECT_FLOAT_EQ (kick->params().noiseDecay.load(), 0.05f);
    EXPECT_FLOAT_EQ (kick->params().drive.load(), 0.75f);
    EXPECT_EQ (kick->params().driveCurve.load(), 2);
    EXPECT_FLOAT_EQ (kick->params().envShape.load(), 0.25f);
    EXPECT_EQ (kick->getRootNote(), 36);
}

TEST (KickChannelParams, DefaultsApplyToAFreshChannel)
{
    test::EngineFixture fx;
    auto channel = fx.model.addChannel ("kick", "Kick Synth");
    auto* kick = dynamic_cast<KickGenerator*> (fx.generators.getOrCreate (channel).get());
    ASSERT_NE (kick, nullptr);

    EXPECT_FLOAT_EQ (kick->params().startFreq.load(), 240.0f);
    EXPECT_FLOAT_EQ (kick->params().endFreq.load(), 48.0f);
    EXPECT_EQ (kick->params().driveCurve.load(), 0);
}

TEST (KickChannelParams, OutOfRangeDriveCurveIsClamped)
{
    test::EngineFixture fx;
    auto channel = fx.model.addChannel ("kick", "Kick Synth");
    channel.setProperty (ids::driveCurve, 99, nullptr);
    auto* kick = dynamic_cast<KickGenerator*> (fx.generators.getOrCreate (channel).get());
    ASSERT_NE (kick, nullptr);
    EXPECT_EQ (kick->params().driveCurve.load(), drive::numCurves - 1);
}

TEST (KickChannelParams, LiveEditsReachAnExistingGenerator)
{
    test::EngineFixture fx;
    auto channel = fx.model.addChannel ("kick", "Kick Synth");
    auto* kick = dynamic_cast<KickGenerator*> (fx.generators.getOrCreate (channel).get());
    ASSERT_NE (kick, nullptr);

    channel.setProperty (ids::kickEndFreq, 30.0, nullptr);
    fx.generators.getOrCreate (channel);
    EXPECT_FLOAT_EQ (kick->params().endFreq.load(), 30.0f);
}

TEST (KickChannelParams, KickChannelRendersThroughTheEngine)
{
    test::EngineFixture fx;
    auto channel = fx.model.addChannel ("kick", "Kick Synth");
    auto pattern = fx.model.getPattern (0);
    auto lane = fx.model.getOrCreateLane (pattern, (int) channel[ids::id]);
    fx.model.addNote (lane, 60, 0, ids::ticksPerStep);
    fx.sync.rebuildNow();

    auto out = fx.renderFromStart (22050);
    EXPECT_GT (out.getMagnitude (0, 0, 22050), 0.05f);
}

TEST (SamplerChannelParams, KickDesignParamsSyncFromTree)
{
    test::EngineFixture fx;
    auto channel = fx.model.getChannel (0);
    channel.setProperty (ids::sampleStart, 0.2, nullptr);
    channel.setProperty (ids::sampleEnd, 0.7, nullptr);
    channel.setProperty (ids::reverse, true, nullptr);
    channel.setProperty (ids::pitchEnvDepth, 18.5, nullptr);
    channel.setProperty (ids::pitchEnvDecay, 0.12, nullptr);
    channel.setProperty (ids::drive, 0.66, nullptr);
    channel.setProperty (ids::driveCurve, 1, nullptr);
    channel.setProperty (ids::envShape, 0.8, nullptr);

    auto* sampler = dynamic_cast<SamplerGenerator*> (fx.generators.getOrCreate (channel).get());
    ASSERT_NE (sampler, nullptr);

    EXPECT_FLOAT_EQ (sampler->params().sampleStart.load(), 0.2f);
    EXPECT_FLOAT_EQ (sampler->params().sampleEnd.load(), 0.7f);
    EXPECT_TRUE (sampler->params().reverse.load());
    EXPECT_FLOAT_EQ (sampler->params().pitchEnvDepth.load(), 18.5f);
    EXPECT_FLOAT_EQ (sampler->params().pitchEnvDecay.load(), 0.12f);
    EXPECT_FLOAT_EQ (sampler->params().drive.load(), 0.66f);
    EXPECT_EQ (sampler->params().driveCurve.load(), 1);
    EXPECT_FLOAT_EQ (sampler->params().envShape.load(), 0.8f);
}

TEST (SamplerChannelParams, KickDesignDefaultsAreNeutral)
{
    test::EngineFixture fx;
    auto* sampler = dynamic_cast<SamplerGenerator*> (
        fx.generators.getOrCreate (fx.model.getChannel (0)).get());
    ASSERT_NE (sampler, nullptr);

    EXPECT_FLOAT_EQ (sampler->params().sampleStart.load(), 0.0f);
    EXPECT_FLOAT_EQ (sampler->params().sampleEnd.load(), 1.0f);
    EXPECT_FALSE (sampler->params().reverse.load());
    EXPECT_FLOAT_EQ (sampler->params().pitchEnvDepth.load(), 0.0f);
    EXPECT_FLOAT_EQ (sampler->params().drive.load(), 0.0f);
    EXPECT_FLOAT_EQ (sampler->params().envShape.load(), 0.0f);
}

// ---------------- persistence ----------------

TEST (KickDesignPersistence, AllNewParametersRoundTrip)
{
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getNonexistentChildFile ("eurytest-kickdesign", ".eury");
    int kickChannelId = 0;
    {
        ProjectModel model;
        auto sampler = model.getChannel (0);
        sampler.setProperty (ids::sampleStart, 0.15, nullptr);
        sampler.setProperty (ids::sampleEnd, 0.85, nullptr);
        sampler.setProperty (ids::reverse, true, nullptr);
        sampler.setProperty (ids::pitchEnvDepth, -12.5, nullptr);
        sampler.setProperty (ids::pitchEnvDecay, 0.09, nullptr);
        sampler.setProperty (ids::drive, 0.42, nullptr);
        sampler.setProperty (ids::driveCurve, 2, nullptr);
        sampler.setProperty (ids::envShape, 0.6, nullptr);

        auto kick = model.addChannel ("kick", "Frenchcore Kick");
        kickChannelId = kick[ids::id];
        kick.setProperty (ids::kickStartFreq, 620.0, nullptr);
        kick.setProperty (ids::kickEndFreq, 41.5, nullptr);
        kick.setProperty (ids::kickPitchDecay, 0.022, nullptr);
        kick.setProperty (ids::kickAmpDecay, 0.9, nullptr);
        kick.setProperty (ids::kickBodyShape, 0.4, nullptr);
        kick.setProperty (ids::kickClickLevel, 0.55, nullptr);
        kick.setProperty (ids::kickClickDecay, 0.003, nullptr);
        kick.setProperty (ids::kickNoiseLevel, 0.2, nullptr);
        kick.setProperty (ids::kickNoiseDecay, 0.04, nullptr);
        kick.setProperty (ids::drive, 0.88, nullptr);
        kick.setProperty (ids::driveCurve, 1, nullptr);
        kick.setProperty (ids::envShape, 0.95, nullptr);

        ASSERT_TRUE (model.saveToFile (file));
    }
    {
        ProjectModel model;
        ASSERT_TRUE (model.loadFromFile (file));

        auto sampler = model.getChannel (0);
        EXPECT_DOUBLE_EQ ((double) sampler[ids::sampleStart], 0.15);
        EXPECT_DOUBLE_EQ ((double) sampler[ids::sampleEnd], 0.85);
        EXPECT_TRUE ((bool) sampler[ids::reverse]);
        EXPECT_DOUBLE_EQ ((double) sampler[ids::pitchEnvDepth], -12.5);
        EXPECT_DOUBLE_EQ ((double) sampler[ids::pitchEnvDecay], 0.09);
        EXPECT_DOUBLE_EQ ((double) sampler[ids::drive], 0.42);
        EXPECT_EQ ((int) sampler[ids::driveCurve], 2);
        EXPECT_DOUBLE_EQ ((double) sampler[ids::envShape], 0.6);

        auto kick = model.getChannelById (kickChannelId);
        ASSERT_TRUE (kick.isValid());
        EXPECT_EQ (kick[ids::type].toString(), "kick");
        EXPECT_DOUBLE_EQ ((double) kick[ids::kickStartFreq], 620.0);
        EXPECT_DOUBLE_EQ ((double) kick[ids::kickEndFreq], 41.5);
        EXPECT_DOUBLE_EQ ((double) kick[ids::kickPitchDecay], 0.022);
        EXPECT_DOUBLE_EQ ((double) kick[ids::kickAmpDecay], 0.9);
        EXPECT_DOUBLE_EQ ((double) kick[ids::kickBodyShape], 0.4);
        EXPECT_DOUBLE_EQ ((double) kick[ids::kickClickLevel], 0.55);
        EXPECT_DOUBLE_EQ ((double) kick[ids::kickClickDecay], 0.003);
        EXPECT_DOUBLE_EQ ((double) kick[ids::kickNoiseLevel], 0.2);
        EXPECT_DOUBLE_EQ ((double) kick[ids::kickNoiseDecay], 0.04);
        EXPECT_DOUBLE_EQ ((double) kick[ids::drive], 0.88);
        EXPECT_EQ ((int) kick[ids::driveCurve], 1);
        EXPECT_DOUBLE_EQ ((double) kick[ids::envShape], 0.95);
    }
    file.deleteFile();
}

// ---------------- control API ----------------

TEST (KickDesignControl, AddsKickChannelsAndSetsParameters)
{
    AppServices services { false };   // no audio device in tests
    ControlDispatcher dispatcher (services);

    const auto added = dispatcher.dispatch ("channel.add",
        juce::JSON::parse (R"({"type": "kick", "name": "Uptempo Kick"})"));
    const int channelId = added["id"];
    auto channel = services.project.getChannelById (channelId);
    ASSERT_TRUE (channel.isValid());
    EXPECT_EQ (channel[ids::type].toString(), "kick");

    dispatcher.dispatch ("channel.set", juce::JSON::parse (
        R"({"channelId": )" + juce::String (channelId)
        + R"(, "kickStartFreq": 700, "kickEndFreq": 40, "drive": 0.9, "driveCurve": 2,
             "sampleStart": 0.1, "reverse": true, "pitchEnvDepth": -6})"));

    EXPECT_DOUBLE_EQ ((double) channel[ids::kickStartFreq], 700.0);
    EXPECT_DOUBLE_EQ ((double) channel[ids::kickEndFreq], 40.0);
    EXPECT_DOUBLE_EQ ((double) channel[ids::drive], 0.9);
    EXPECT_DOUBLE_EQ ((double) channel[ids::driveCurve], 2.0);
    EXPECT_DOUBLE_EQ ((double) channel[ids::sampleStart], 0.1);
    EXPECT_TRUE ((bool) channel[ids::reverse]);
    EXPECT_DOUBLE_EQ ((double) channel[ids::pitchEnvDepth], -6.0);
}

TEST (KickDesignPersistence, LoadedKickChannelRebuildsItsGenerator)
{
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getNonexistentChildFile ("eurytest-kickreload", ".eury");
    {
        ProjectModel model;
        auto kick = model.addChannel ("kick", "Kick Synth");
        kick.setProperty (ids::kickEndFreq, 38.0, nullptr);
        ASSERT_TRUE (model.saveToFile (file));
    }

    test::EngineFixture fx;
    ASSERT_TRUE (fx.model.loadFromFile (file));
    auto kickChannel = fx.model.getChannel (fx.model.numChannels() - 1);
    ASSERT_EQ (kickChannel[ids::type].toString(), "kick");

    auto* kick = dynamic_cast<KickGenerator*> (fx.generators.getOrCreate (kickChannel).get());
    ASSERT_NE (kick, nullptr);
    EXPECT_FLOAT_EQ (kick->params().endFreq.load(), 38.0f);
    file.deleteFile();
}
