#include "TestHelpers.h"

using test::EngineFixture;
using test::rmsOf;

namespace
{
// Clears all default lanes and puts a single kick note at startTicks.
void soloNoteAt (EngineFixture& fx, int startTicks, int channelIndex = 0)
{
    auto pattern = fx.model.getPattern (0);
    for (int i = pattern.getNumChildren(); --i >= 0;)
        if (pattern.getChild (i).hasType (ids::LANE))
            pattern.removeChild (i, nullptr);

    auto lane = fx.model.getOrCreateLane (pattern, fx.model.getChannel (channelIndex)[ids::id]);
    fx.model.addNote (lane, 60, startTicks, ids::ticksPerStep);
    fx.sync.rebuildNow();
}
}

TEST (SequencerEngine, SilentWhenStopped)
{
    EngineFixture fx;
    auto out = fx.render (4096);
    EXPECT_FLOAT_EQ (out.getMagnitude (0, 0, 4096), 0.0f);
}

TEST (SequencerEngine, NoteAtZeroSoundsImmediately)
{
    EngineFixture fx;
    soloNoteAt (fx, 0);
    auto out = fx.renderFromStart (8192);
    EXPECT_GT (rmsOf (out, 0, 2048), 0.01f);
}

TEST (SequencerEngine, NoteOnsetTimingIsSampleAccurate)
{
    EngineFixture fx;
    soloNoteAt (fx, 8 * ids::ticksPerStep);   // step 8

    const double tps = fx.ticksPerSample();
    const int onset = (int) (8 * ids::ticksPerStep / tps);   // ~37,850 @140bpm

    auto out = fx.renderFromStart (onset + 8192);
    EXPECT_LT (rmsOf (out, 0, onset - 256), 1.0e-5f) << "audio before the note";
    EXPECT_GT (rmsOf (out, onset, 2048), 0.01f) << "no audio at the onset";
}

TEST (SequencerEngine, SwingDelaysOddSteps)
{
    EngineFixture fx;
    soloNoteAt (fx, 1 * ids::ticksPerStep);   // odd step -> swung
    fx.model.setSwing (1.0);                  // shift = 120 ticks
    fx.sync.rebuildNow();

    const double tps = fx.ticksPerSample();
    const int unswungOnset = (int) (240 / tps);
    const int swungOnset   = (int) ((240 + 120) / tps);

    auto out = fx.renderFromStart (swungOnset + 8192);
    EXPECT_LT (rmsOf (out, unswungOnset, swungOnset - unswungOnset - 256), 1.0e-5f)
        << "note played at unswung position";
    EXPECT_GT (rmsOf (out, swungOnset, 2048), 0.01f);
}

TEST (SequencerEngine, PatternLoopsInPatternMode)
{
    EngineFixture fx;
    soloNoteAt (fx, 0);
    const double tps = fx.ticksPerSample();
    const int patternSamples = (int) (ids::ticksPerBar / tps);

    auto out = fx.renderFromStart (patternSamples + 8192);
    // Second iteration retriggers the note right after the wrap.
    EXPECT_GT (rmsOf (out, patternSamples, 2048), 0.01f);
}

TEST (SequencerEngine, SongModePlaysClipAtItsPosition)
{
    EngineFixture fx;
    soloNoteAt (fx, 0);

    const int patId = fx.model.getRoot()[ids::activePattern];
    auto clip = fx.model.addPlaylistClip ("pattern", 0, ids::ticksPerBar, ids::ticksPerBar);
    clip.setProperty (ids::patternId, patId, nullptr);
    fx.model.setSongMode (true);
    fx.sync.rebuildNow();

    const double tps = fx.ticksPerSample();
    const int clipOnset = (int) (ids::ticksPerBar / tps);

    auto out = fx.renderFromStart (clipOnset + 8192);
    EXPECT_LT (rmsOf (out, 0, clipOnset - 256), 1.0e-5f) << "audio before the clip";
    EXPECT_GT (rmsOf (out, clipOnset, 2048), 0.01f);
}

TEST (SequencerEngine, ChannelVolumeAutomationScalesOutput)
{
    EngineFixture fx;
    // Two identical notes: bar 1 with automation=1.0, bar 2 with automation=0.1.
    auto pattern = fx.model.getPattern (0);
    for (int i = pattern.getNumChildren(); --i >= 0;)
        if (pattern.getChild (i).hasType (ids::LANE))
            pattern.removeChild (i, nullptr);
    const int chId = fx.model.getChannel (0)[ids::id];
    auto lane = fx.model.getOrCreateLane (pattern, chId);
    fx.model.addNote (lane, 60, 0, 240);

    const int patId = fx.model.getRoot()[ids::activePattern];
    for (int bar = 0; bar < 2; ++bar)
    {
        auto clip = fx.model.addPlaylistClip ("pattern", 0, bar * ids::ticksPerBar, ids::ticksPerBar);
        clip.setProperty (ids::patternId, patId, nullptr);
    }

    auto automation = fx.model.addAutomation ("channel", chId, "volume", "vol", 1.0);
    automation.removeAllChildren (nullptr);
    auto addPoint = [&automation] (int pos, double value)
    {
        juce::ValueTree point (ids::POINT);
        point.setProperty (ids::posTicks, pos, nullptr);
        point.setProperty (ids::value, value, nullptr);
        point.setProperty (ids::tension, 0.0, nullptr);
        automation.appendChild (point, nullptr);
    };
    addPoint (0, 1.0);
    addPoint (ids::ticksPerBar - 1, 1.0);
    addPoint (ids::ticksPerBar, 0.1);
    addPoint (2 * ids::ticksPerBar, 0.1);

    auto autoClip = fx.model.addPlaylistClip ("automation", 1, 0, 2 * ids::ticksPerBar);
    autoClip.setProperty (ids::automationId, (int) automation[ids::id], nullptr);

    fx.model.setSongMode (true);
    fx.sync.rebuildNow();

    const double tps = fx.ticksPerSample();
    const int barSamples = (int) (ids::ticksPerBar / tps);
    auto out = fx.renderFromStart (2 * barSamples);

    const float loud = rmsOf (out, 0, 4096);
    const float quiet = rmsOf (out, barSamples, 4096);
    EXPECT_GT (loud, 0.01f);
    EXPECT_LT (quiet, loud * 0.3f) << "automation did not reduce the level";
    EXPECT_GT (quiet, 1.0e-5f) << "automation muted instead of scaling";
}

TEST (SequencerEngine, AudioClipPlaysAtTimelinePosition)
{
    EngineFixture fx;
    // Silence the default pattern.
    auto pattern = fx.model.getPattern (0);
    for (int i = pattern.getNumChildren(); --i >= 0;)
        if (pattern.getChild (i).hasType (ids::LANE))
            pattern.removeChild (i, nullptr);

    const auto tone = test::makeToneFile (0.5);
    auto clip = fx.model.addPlaylistClip ("audio", 0, ids::ticksPerBar, ids::ticksPerBar);
    clip.setProperty (ids::audioPath, tone.getFullPathName(), nullptr);
    clip.setProperty (ids::stretchRatio, 1.0, nullptr);
    clip.setProperty (ids::audioOffsetTicks, 0, nullptr);
    fx.model.setSongMode (true);
    fx.sync.rebuildNow();

    const double tps = fx.ticksPerSample();
    const int onset = (int) (ids::ticksPerBar / tps);
    auto out = fx.renderFromStart (onset + 8192);

    EXPECT_LT (rmsOf (out, 0, onset - 256), 1.0e-5f);
    EXPECT_GT (rmsOf (out, onset + 64, 2048), 0.05f);
    tone.deleteFile();
}

TEST (SequencerEngine, MasterInsertGainAppliedToOutput)
{
    EngineFixture fx;
    soloNoteAt (fx, 0);
    auto loud = fx.renderFromStart (4096);

    fx.model.getInsert (0).setProperty (ids::volume, 0.08, nullptr);
    fx.sync.rebuildNow();
    auto quiet = fx.renderFromStart (4096);

    EXPECT_LT (rmsOf (quiet, 0, 4096), rmsOf (loud, 0, 4096) * 0.5f);
}

TEST (SequencerEngine, PreviewNoteProducesAudioWhileStopped)
{
    EngineFixture fx;
    const int chId = fx.model.getChannel (0)[ids::id];
    fx.engine.previewNote (chId, 60, 1.0f, 100);
    auto out = fx.render (8192);
    EXPECT_GT (rmsOf (out, 0, 4096), 0.01f);
}
