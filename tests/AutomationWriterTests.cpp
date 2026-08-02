#include "TestHelpers.h"
#include "app/AutomationWriter.h"

using test::EngineFixture;
using test::rmsOf;

namespace
{
struct WriterFixture : ::testing::Test
{
    EngineFixture fx;
    AutomationWriter writer { fx.model, fx.engine };

    int firstChannelId() const { return fx.model.getChannel (0)[ids::id]; }

    AutomationWriter::Target volumeTarget() const
    {
        return { "channel", firstChannelId(), "volume", "Kick volume" };
    }

    juce::ValueTree source() const
    {
        return AutomationWriter::findSource (fx.model, volumeTarget());
    }

    std::vector<autorec::Point> points() const
    {
        return AutomationWriter::readPoints (source());
    }

    int numAutomationClips() const
    {
        int count = 0;
        for (const auto track : fx.model.playlist())
            for (const auto clip : track)
                if (clip.hasType (ids::CLIP) && clip[ids::clipType].toString() == "automation")
                    ++count;
        return count;
    }
};
}

TEST_F (WriterFixture, FirstTouchCreatesTheSourceAndItsClip)
{
    ASSERT_FALSE (source().isValid());
    ASSERT_EQ (numAutomationClips(), 0);

    writer.touchAt (volumeTarget(), 0.4, 0.0);

    auto created = source();
    ASSERT_TRUE (created.isValid());
    EXPECT_EQ (created[ids::targetType].toString(), "channel");
    EXPECT_EQ ((int) created[ids::targetId], firstChannelId());
    EXPECT_EQ (created[ids::paramId].toString(), "volume");
    EXPECT_EQ (numAutomationClips(), 1);

    // A second touch reuses the source instead of stacking up new ones.
    writer.touchAt (volumeTarget(), 0.6, 960.0);
    EXPECT_EQ (numAutomationClips(), 1);
    EXPECT_EQ (fx.model.automations().getNumChildren(), 1);
}

TEST_F (WriterFixture, RecordingMarksTheSourceSoTheEngineYields)
{
    writer.touchAt (volumeTarget(), 0.4, 0.0);
    EXPECT_TRUE ((bool) source()[ids::writing]);
    EXPECT_TRUE (writer.isRecording ((int) source()[ids::id]));

    fx.sync.rebuildNow();
    auto snap = fx.engine.getPendingSnapshot();
    ASSERT_NE (snap, nullptr);
    ASSERT_EQ (snap->automations.size(), 1u);
    EXPECT_TRUE (snap->automations[0].writing);

    writer.finaliseAll();
    EXPECT_FALSE (source().hasProperty (ids::writing));

    fx.sync.rebuildNow();
    snap = fx.engine.getPendingSnapshot();
    ASSERT_EQ (snap->automations.size(), 1u);
    EXPECT_FALSE (snap->automations[0].writing);
}

TEST_F (WriterFixture, RecordedGestureLandsOnTheCurve)
{
    for (int tick = 0; tick <= 1920; tick += 120)
        writer.touchAt (volumeTarget(), 1.0 - tick / 3840.0, tick);
    writer.finaliseAll();

    const auto recorded = points();
    ASSERT_GE (recorded.size(), 2u);
    EXPECT_EQ (recorded.front().posTicks, 0);
    EXPECT_NEAR (recorded.front().value, 1.0, 1.0e-6);

    // The pass covers 0..1920; the flat seed point past its end stays put.
    const auto* atGestureEnd = &recorded.front();
    for (const auto& point : recorded)
        if (point.posTicks <= 1920)
            atGestureEnd = &point;
    EXPECT_EQ (atGestureEnd->posTicks, 1920);
    EXPECT_NEAR (atGestureEnd->value, 0.5, 1.0e-6);

    // A straight fade should not need a point per callback.
    EXPECT_LT (recorded.size(), 8u);
}

TEST_F (WriterFixture, SecondPassOverTheSameRangeLeavesNoStalePoints)
{
    for (int tick = 0; tick <= 1920; tick += 160)
        writer.touchAt (volumeTarget(), tick % 320 == 0 ? 0.1 : 0.9, tick);
    writer.finaliseAll();
    ASSERT_GT (points().size(), 3u);

    for (int tick = 0; tick <= 1920; tick += 480)
        writer.touchAt (volumeTarget(), 0.5, tick);
    writer.finaliseAll();

    for (const auto& point : points())
        if (point.posTicks <= 1920)
            EXPECT_NEAR (point.value, 0.5, 1.0e-6)
                << "stale point survived at " << point.posTicks;
}

TEST_F (WriterFixture, RecordingAcrossALoopBoundaryKeepsBothLaps)
{
    // Two laps of a one-bar loop: the second lap only covers the first half.
    for (int tick = 0; tick < ids::ticksPerBar; tick += 240)
        writer.touchAt (volumeTarget(), tick % 480 == 0 ? 0.2 : 0.7, tick);
    for (int tick = 0; tick < ids::ticksPerBar / 2; tick += 240)
        writer.touchAt (volumeTarget(), tick % 480 == 0 ? 0.95 : 0.55, tick);
    writer.finaliseAll();

    const auto recorded = points();
    ASSERT_GE (recorded.size(), 3u);
    for (size_t i = 1; i < recorded.size(); ++i)
        EXPECT_LT (recorded[i - 1].posTicks, recorded[i].posTicks) << "points out of order";

    for (const auto& point : recorded)
    {
        if (point.posTicks < ids::ticksPerBar / 2)
            EXPECT_GT (point.value, 0.5) << "first lap survived under the second at "
                                         << point.posTicks;
        else if (point.posTicks < ids::ticksPerBar)
            EXPECT_LT (point.value, 0.75);
    }
}

TEST_F (WriterFixture, TouchGrowsAClipTheGestureRunsPast)
{
    writer.touchAt (volumeTarget(), 0.5, 0.0);
    auto clip = AutomationWriter::findClip (fx.model, (int) source()[ids::id]);
    ASSERT_TRUE (clip.isValid());
    const int originalLength = clip[ids::lengthTicks];

    writer.touchAt (volumeTarget(), 0.2, originalLength + 100.0);
    EXPECT_GT ((int) clip[ids::lengthTicks], originalLength);
}

TEST_F (WriterFixture, TheArmGatesRecordingAndDisarmingFinalises)
{
    writer.touch (volumeTarget(), 0.3);
    EXPECT_FALSE (source().isValid()) << "recorded while disarmed";

    writer.setArmed (true);
    writer.touch (volumeTarget(), 0.3);
    EXPECT_FALSE (source().isValid()) << "recorded while stopped";

    fx.engine.play();
    writer.touch (volumeTarget(), 0.3);
    ASSERT_TRUE (source().isValid());
    EXPECT_TRUE ((bool) source()[ids::writing]);

    writer.setArmed (false);
    EXPECT_FALSE (source().hasProperty (ids::writing)) << "disarming left the pass open";
    fx.engine.stop();
}

TEST_F (WriterFixture, FinalisedPassIsOneUndoStep)
{
    for (int tick = 0; tick <= 960; tick += 120)
        writer.touchAt (volumeTarget(), tick / 1920.0, tick);
    writer.finaliseAll();
    const auto recorded = points();
    ASSERT_GE (recorded.size(), 2u);

    fx.model.getUndoManager().undo();
    const auto restored = points();
    // Back to the flat pair addAutomation seeds a new source with.
    EXPECT_EQ (restored.size(), 2u);
    EXPECT_NEAR (restored.front().value, restored.back().value, 1.0e-9);
}

TEST_F (WriterFixture, ReloadClearsAStaleWriteFlag)
{
    writer.touchAt (volumeTarget(), 0.4, 0.0);
    ASSERT_TRUE ((bool) source()[ids::writing]);

    // Save mid-pass, exactly what a crash or a Cmd-S while performing leaves.
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getNonexistentChildFile ("eurytest-writing", ".eury");
    ASSERT_TRUE (fx.model.saveToFile (file));
    ASSERT_TRUE (fx.model.loadFromFile (file));

    EXPECT_FALSE (source().hasProperty (ids::writing));
    file.deleteFile();
}

TEST_F (WriterFixture, GeneratorParamAutomationReachesTheSnapshot)
{
    auto channel = fx.model.addChannel ("synth", "Lead");
    const AutomationWriter::Target target { "channel-param", (int) channel[ids::id],
                                            "cutoff", "Lead CUT" };
    writer.touchAt (target, 0.25, 0.0);
    writer.finaliseAll();
    fx.sync.rebuildNow();

    auto snap = fx.engine.getPendingSnapshot();
    ASSERT_NE (snap, nullptr);
    ASSERT_EQ (snap->automations.size(), 1u);
    const auto& automation = snap->automations[0];
    EXPECT_EQ (automation.kind, AutomationSnapshot::Kind::generatorParam);
    ASSERT_NE (automation.genParam, nullptr);
    // The synth's cutoff runs 40..18000 Hz with a skew, so a quarter turn is
    // well inside the range rather than at either end.
    const float mapped = automation.genRange.convertFrom0to1 (0.25f);
    EXPECT_GT (mapped, 40.0f);
    EXPECT_LT (mapped, 18000.0f);
}

TEST_F (WriterFixture, UnknownGeneratorParamIsDroppedFromTheSnapshot)
{
    auto channel = fx.model.addChannel ("synth", "Lead");
    AutomationWriter::createWithClip (fx.model, "channel-param", (int) channel[ids::id],
                                      "notAParam", "bogus", 0.5);
    fx.sync.rebuildNow();

    auto snap = fx.engine.getPendingSnapshot();
    ASSERT_NE (snap, nullptr);
    EXPECT_TRUE (snap->automations.empty());
}

// ---------------- engine-level round trip ----------------

TEST (AutomationRecordingEngine, RecordedCurvePlaysBackAndChangesTheLevel)
{
    EngineFixture fx;
    AutomationWriter writer { fx.model, fx.engine };

    // Two bars of the default pattern on the playlist, so there is something
    // to hear for the whole pass.
    const int patId = fx.model.getRoot()[ids::activePattern];
    for (int bar = 0; bar < 2; ++bar)
    {
        auto clip = fx.model.addPlaylistClip ("pattern", 0, bar * ids::ticksPerBar,
                                              ids::ticksPerBar);
        clip.setProperty (ids::patternId, patId, nullptr);
    }
    fx.model.setSongMode (true);
    fx.sync.rebuildNow();

    const double tps = fx.ticksPerSample();
    const int barSamples = (int) (ids::ticksPerBar / tps);
    const AutomationWriter::Target target { "channel", (int) fx.model.getChannel (0)[ids::id],
                                            "volume", "Kick volume" };

    // Perform a fade: loud through bar 1, quiet through bar 2. Rendering in
    // chunks advances the real transport, which is what touch() reads.
    writer.setArmed (true);
    fx.engine.stop();
    fx.engine.setPositionTicks (0.0);
    fx.engine.play();

    constexpr int chunk = 512;
    for (int done = 0; done < 2 * barSamples; done += chunk)
    {
        fx.render (chunk);
        writer.touch (target, done < barSamples ? 1.0 : 0.05);
    }
    fx.engine.stop();
    writer.setArmed (false);

    auto automation = AutomationWriter::findSource (fx.model, target);
    ASSERT_TRUE (automation.isValid()) << "nothing was recorded";
    ASSERT_GE (AutomationWriter::readPoints (automation).size(), 2u);
    EXPECT_FALSE (automation.hasProperty (ids::writing));

    // Play the recorded curve back.
    fx.sync.rebuildNow();
    auto out = fx.renderFromStart (2 * barSamples);
    const float loud = rmsOf (out, 0, 4096);
    const float quiet = rmsOf (out, barSamples + 2048, 4096);

    EXPECT_GT (loud, 0.01f) << "no audio in the loud bar";
    EXPECT_LT (quiet, loud * 0.4f) << "the recorded fade did not reduce the level";
}
