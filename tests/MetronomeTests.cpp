#include "TestHelpers.h"
#include "engine/OfflineRenderer.h"

using test::EngineFixture;
using test::rmsOf;

namespace
{
// Strips the starter pattern so only the click can make a sound.
void silenceProject (EngineFixture& fx)
{
    auto pattern = fx.model.getPattern (0);
    for (int i = pattern.getNumChildren(); --i >= 0;)
        if (pattern.getChild (i).hasType (ids::LANE))
            pattern.removeChild (i, nullptr);
    fx.sync.rebuildNow();
}

// Puts a single note on the kick channel at startTicks.
void soloNoteAt (EngineFixture& fx, int startTicks)
{
    silenceProject (fx);
    auto lane = fx.model.getOrCreateLane (fx.model.getPattern (0), fx.model.getChannel (0)[ids::id]);
    fx.model.addNote (lane, 60, startTicks, ids::ticksPerStep);
    fx.sync.rebuildNow();
}

int beatSamples (const EngineFixture& fx)
{
    return (int) (ids::ticksPerQuarter / fx.ticksPerSample());
}

float peakAround (const juce::AudioBuffer<float>& buffer, int start, int length)
{
    start = juce::jlimit (0, juce::jmax (0, buffer.getNumSamples() - 1), start);
    length = juce::jmin (length, buffer.getNumSamples() - start);
    return buffer.getMagnitude (0, start, juce::jmax (1, length));
}

juce::AudioBuffer<float> renderWithCountIn (EngineFixture& fx, int numSamples)
{
    fx.engine.stop();
    fx.engine.setPositionTicks (0.0);
    fx.engine.playWithCountIn();
    auto out = fx.render (numSamples);
    fx.engine.stop();
    return out;
}
}

TEST (Metronome, SilentWhenDisabled)
{
    EngineFixture fx;
    silenceProject (fx);
    fx.engine.setMetronomeEnabled (false);

    auto out = fx.renderFromStart (2 * beatSamples (fx));
    EXPECT_LT (out.getMagnitude (0, out.getNumSamples()), 1.0e-6f);
}

TEST (Metronome, ClicksOnEveryBeatWhenEnabled)
{
    EngineFixture fx;
    silenceProject (fx);
    fx.engine.setMetronomeEnabled (true);
    fx.engine.setMetronomeLevel (1.0f);

    const int beat = beatSamples (fx);
    auto out = fx.renderFromStart (4 * beat + 512);

    for (int i = 0; i < 4; ++i)
        EXPECT_GT (peakAround (out, i * beat, 512), 0.1f) << "no click on beat " << i + 1;

    // The click is short: well before the next beat there is silence again.
    for (int i = 0; i < 4; ++i)
        EXPECT_LT (peakAround (out, i * beat + beat / 2, 1024), 1.0e-4f)
            << "the click rings on past beat " << i + 1;
}

TEST (Metronome, DownbeatIsAccented)
{
    EngineFixture fx;
    silenceProject (fx);
    fx.engine.setMetronomeEnabled (true);
    fx.engine.setMetronomeLevel (1.0f);

    const int beat = beatSamples (fx);
    auto out = fx.renderFromStart (2 * beat);

    EXPECT_GT (peakAround (out, 0, 512), peakAround (out, beat, 512) * 1.15f);
}

TEST (Metronome, LevelScalesTheClick)
{
    EngineFixture fx;
    silenceProject (fx);
    fx.engine.setMetronomeEnabled (true);

    fx.engine.setMetronomeLevel (1.0f);
    const float loud = peakAround (fx.renderFromStart (2048), 0, 2048);
    fx.engine.setMetronomeLevel (0.25f);
    const float quiet = peakAround (fx.renderFromStart (2048), 0, 2048);

    ASSERT_GT (loud, 0.1f);
    EXPECT_NEAR (quiet, loud * 0.25f, loud * 0.05f);

    fx.engine.setMetronomeLevel (0.0f);
    EXPECT_LT (peakAround (fx.renderFromStart (2048), 0, 2048), 1.0e-6f);
}

TEST (Metronome, DoesNotReachTheMixerBuses)
{
    EngineFixture fx;
    silenceProject (fx);
    fx.engine.setMetronomeEnabled (true);
    fx.engine.setMetronomeLevel (1.0f);

    fx.renderFromStart (2048);
    EXPECT_LT (fx.engine.getMasterPeak (0), 1.0e-6f);
    EXPECT_LT (fx.engine.getInsertPeak (0, 0), 1.0e-6f);
}

TEST (Metronome, AbsentFromOfflineRenders)
{
    EngineFixture fx;
    silenceProject (fx);
    fx.engine.setMetronomeEnabled (true);
    fx.engine.setMetronomeLevel (1.0f);

    OfflineRenderer::Options opts;
    opts.wavFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getNonexistentChildFile ("eurytest-click", ".wav");
    opts.tailSeconds = 0.0;

    const auto result = OfflineRenderer::render (fx.engine, fx.model, opts);
    ASSERT_TRUE (result.ok) << result.error;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (opts.wavFile));
    ASSERT_NE (reader, nullptr);
    juce::AudioBuffer<float> rendered (2, (int) reader->lengthInSamples);
    reader->read (&rendered, 0, (int) reader->lengthInSamples, 0, true, true);
    reader = nullptr;

    EXPECT_LT (rendered.getMagnitude (0, rendered.getNumSamples()), 1.0e-6f)
        << "the click was baked into the render";
    // The user's setting survives the render.
    EXPECT_TRUE (fx.engine.isMetronomeEnabled());

    opts.wavFile.deleteFile();
}

// ---------------- count-in ----------------

TEST (CountIn, DelaysTheStartByOneBar)
{
    EngineFixture fx;
    soloNoteAt (fx, 0);
    fx.engine.setMetronomeLevel (0.0f);   // measure the music, not the click
    fx.engine.setCountInBars (1);

    const int bar = (int) (ids::ticksPerBar / fx.ticksPerSample());
    auto out = renderWithCountIn (fx, bar + 8192);

    EXPECT_LT (rmsOf (out, 0, bar - 256), 1.0e-5f) << "the note fired during the count-in";
    EXPECT_GT (rmsOf (out, bar, 2048), 0.01f) << "the note never fired";
}

TEST (CountIn, DelaysTheStartByTwoBars)
{
    EngineFixture fx;
    soloNoteAt (fx, 0);
    fx.engine.setMetronomeLevel (0.0f);
    fx.engine.setCountInBars (2);

    const int bar = (int) (ids::ticksPerBar / fx.ticksPerSample());
    auto out = renderWithCountIn (fx, 2 * bar + 8192);

    EXPECT_LT (rmsOf (out, 0, 2 * bar - 256), 1.0e-5f);
    EXPECT_GT (rmsOf (out, 2 * bar, 2048), 0.01f);
}

TEST (CountIn, HoldsTheTransportAtTheStartPosition)
{
    EngineFixture fx;
    soloNoteAt (fx, 0);
    fx.engine.setMetronomeLevel (0.0f);
    fx.engine.setCountInBars (1);

    fx.engine.stop();
    fx.engine.setPositionTicks (0.0);
    fx.engine.playWithCountIn();

    const int bar = (int) (ids::ticksPerBar / fx.ticksPerSample());
    fx.render (bar / 2);
    EXPECT_TRUE (fx.engine.isCountingIn());
    EXPECT_DOUBLE_EQ (fx.engine.getPositionTicks(), 0.0);

    fx.render (bar);
    EXPECT_FALSE (fx.engine.isCountingIn());
    EXPECT_GT (fx.engine.getPositionTicks(), 0.0);
    fx.engine.stop();
}

TEST (CountIn, ClicksThroughTheCountInEvenWithTheMetronomeOff)
{
    EngineFixture fx;
    silenceProject (fx);
    fx.engine.setMetronomeEnabled (false);
    fx.engine.setMetronomeLevel (1.0f);
    fx.engine.setCountInBars (1);

    const int beat = beatSamples (fx);
    auto out = renderWithCountIn (fx, 4 * beat);

    for (int i = 0; i < 4; ++i)
        EXPECT_GT (peakAround (out, i * beat, 512), 0.1f) << "no count-in click on beat " << i + 1;
}

TEST (CountIn, ZeroBarsStartsImmediately)
{
    EngineFixture fx;
    soloNoteAt (fx, 0);
    fx.engine.setMetronomeLevel (0.0f);
    fx.engine.setCountInBars (0);

    auto out = renderWithCountIn (fx, 8192);
    EXPECT_GT (rmsOf (out, 0, 2048), 0.01f);
    EXPECT_FALSE (fx.engine.isCountingIn());
}

TEST (CountIn, StopCancelsAPendingCountIn)
{
    EngineFixture fx;
    soloNoteAt (fx, 0);
    fx.engine.setMetronomeLevel (0.0f);
    fx.engine.setCountInBars (2);

    fx.engine.stop();
    fx.engine.setPositionTicks (0.0);
    fx.engine.playWithCountIn();
    fx.render (1024);
    fx.engine.stop();
    fx.render (512);
    EXPECT_FALSE (fx.engine.isCountingIn());

    // A plain play() afterwards runs without any leftover count-in.
    fx.engine.setPositionTicks (0.0);
    fx.engine.play();
    auto out = fx.render (8192);
    fx.engine.stop();
    EXPECT_GT (rmsOf (out, 0, 2048), 0.01f);
}

TEST (CountIn, BarsAreClampedToTwo)
{
    EngineFixture fx;
    fx.engine.setCountInBars (7);
    EXPECT_EQ (fx.engine.getCountInBars(), 2);
    fx.engine.setCountInBars (-1);
    EXPECT_EQ (fx.engine.getCountInBars(), 0);
}
