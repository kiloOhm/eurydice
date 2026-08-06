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

void setLoop (EngineFixture& fx, int startTicks, int endTicks, bool enabled)
{
    fx.model.setLoopRange (startTicks, endTicks);
    fx.model.setLoopEnabled (enabled);
    fx.sync.rebuildNow();
}

// First sample at or after `from` that rises above the noise floor.
int firstOnset (const juce::AudioBuffer<float>& buffer, int from)
{
    const auto* data = buffer.getReadPointer (0);
    for (int i = juce::jmax (0, from); i < buffer.getNumSamples(); ++i)
        if (std::abs (data[i]) > 0.01f)
            return i;
    return -1;
}

// The sample the engine wraps on: the first one whose tick reaches loopEnd.
int loopWrapSample (const EngineFixture& fx, int loopEndTicks)
{
    return (int) std::ceil (loopEndTicks / fx.ticksPerSample());
}

// How far ahead of an expected onset the search starts. A kick rings for
// 0.4 s (17,640 samples), so this has to clear the previous one's tail.
constexpr int kQuietLeadIn = 8192;

// 2.5 bars: the wrap lands mid-block, so a block-aligned wrap would show up.
constexpr int kLoopEndTicks = 5 * ids::ticksPerBar / 2;
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

TEST (SequencerEngine, PatternSwingOverridesTheProjectSwing)
{
    EngineFixture fx;
    soloNoteAt (fx, 1 * ids::ticksPerStep);
    fx.model.setSwing (0.0);
    fx.model.setPatternSwing (fx.model.getPattern (0), 1.0);
    fx.sync.rebuildNow();

    const double tps = fx.ticksPerSample();
    const int unswungOnset = (int) (240 / tps);
    const int swungOnset   = (int) ((240 + 120) / tps);

    auto out = fx.renderFromStart (swungOnset + 8192);
    EXPECT_LT (rmsOf (out, unswungOnset, swungOnset - unswungOnset - 256), 1.0e-5f)
        << "the pattern swing was ignored";
    EXPECT_GT (rmsOf (out, swungOnset, 2048), 0.01f);
}

// The override is the presence of the property, not a non-zero value: a
// pattern can deliberately stay straight while the project swings.
TEST (SequencerEngine, PatternSwingOfZeroBeatsANonZeroProjectSwing)
{
    EngineFixture fx;
    soloNoteAt (fx, 1 * ids::ticksPerStep);
    fx.model.setSwing (1.0);
    fx.model.setPatternSwing (fx.model.getPattern (0), 0.0);
    fx.sync.rebuildNow();

    const int unswungOnset = (int) (240 / fx.ticksPerSample());
    auto out = fx.renderFromStart (unswungOnset + 8192);
    EXPECT_GT (rmsOf (out, unswungOnset, 2048), 0.01f) << "the note was swung anyway";
}

TEST (SequencerEngine, PatternWithoutSwingFollowsTheProject)
{
    EngineFixture fx;
    auto pattern = fx.model.getPattern (0);
    ASSERT_FALSE (fx.model.patternOverridesSwing (pattern));

    fx.model.setSwing (0.6);
    fx.sync.rebuildNow();
    auto snap = fx.engine.getPendingSnapshot();
    ASSERT_NE (snap, nullptr);
    ASSERT_FALSE (snap->patterns.empty());
    EXPECT_DOUBLE_EQ (snap->patterns[0].swing, 0.6);

    // Clearing the override goes back to following the project.
    fx.model.setPatternSwing (pattern, 0.1);
    fx.model.clearPatternSwing (pattern);
    fx.sync.rebuildNow();
    snap = fx.engine.getPendingSnapshot();
    ASSERT_NE (snap, nullptr);
    EXPECT_FALSE (fx.model.patternOverridesSwing (pattern));
    EXPECT_DOUBLE_EQ (snap->patterns[0].swing, 0.6);
}

TEST (SequencerEngine, PatternsSwingIndependently)
{
    EngineFixture fx;
    soloNoteAt (fx, 1 * ids::ticksPerStep);

    auto straight = fx.model.getPattern (0);
    auto swung = fx.model.clonePattern ((int) straight[ids::id]);
    fx.model.setPatternSwing (straight, 0.0);
    fx.model.setPatternSwing (swung, 1.0);
    fx.sync.rebuildNow();

    auto snap = fx.engine.getPendingSnapshot();
    ASSERT_NE (snap, nullptr);
    ASSERT_EQ (snap->patterns.size(), 2u);
    EXPECT_DOUBLE_EQ (snap->patterns[0].swing, 0.0);
    EXPECT_DOUBLE_EQ (snap->patterns[1].swing, 1.0);

    const double tps = fx.ticksPerSample();
    const int unswungOnset = (int) (240 / tps);
    const int swungOnset   = (int) ((240 + 120) / tps);

    fx.model.getRoot().setProperty (ids::activePattern, (int) swung[ids::id], nullptr);
    fx.sync.rebuildNow();
    auto out = fx.renderFromStart (swungOnset + 8192);
    EXPECT_LT (rmsOf (out, unswungOnset, swungOnset - unswungOnset - 256), 1.0e-5f);
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

TEST (SequencerEngine, LoopWrapsOnTheExactSample)
{
    EngineFixture fx;
    soloNoteAt (fx, 0);
    setLoop (fx, 0, kLoopEndTicks, true);

    const int wrap = loopWrapSample (fx, kLoopEndTicks);
    ASSERT_NE (wrap % test::kBlockSize, 0) << "test needs a wrap inside a block";

    auto out = fx.renderFromStart (wrap + 4096);
    const int onset = firstOnset (out, wrap - kQuietLeadIn);
    EXPECT_GE (onset, wrap) << "note fired before the loop point";
    EXPECT_LT (onset, wrap + 128) << "wrap was quantised to the block boundary";
}

TEST (SequencerEngine, LoopStartNoteRetriggersOnEveryPass)
{
    EngineFixture fx;
    soloNoteAt (fx, 0);
    setLoop (fx, 0, kLoopEndTicks, true);

    const int wrap = loopWrapSample (fx, kLoopEndTicks);
    auto out = fx.renderFromStart (2 * wrap + 4096);

    const int firstPass = firstOnset (out, wrap - kQuietLeadIn);
    EXPECT_GE (firstPass, wrap);
    EXPECT_LT (firstPass, wrap + 128);

    const int secondPass = firstOnset (out, 2 * wrap - kQuietLeadIn);
    EXPECT_GE (secondPass, 2 * wrap);
    EXPECT_LT (secondPass, 2 * wrap + 128);
}

TEST (SequencerEngine, LoopKeepsThePositionInsideTheRange)
{
    EngineFixture fx;
    soloNoteAt (fx, 0);
    setLoop (fx, ids::ticksPerBar, kLoopEndTicks, true);

    fx.renderFromStart (loopWrapSample (fx, kLoopEndTicks) + 4096);

    const double pos = fx.engine.getPositionTicks();
    EXPECT_GE (pos, (double) ids::ticksPerBar);
    EXPECT_LT (pos, (double) kLoopEndTicks);
}

TEST (SequencerEngine, DisabledLoopPlaysStraightThrough)
{
    EngineFixture fx;
    soloNoteAt (fx, 0);
    setLoop (fx, 0, kLoopEndTicks, false);

    const int wrap = loopWrapSample (fx, kLoopEndTicks);
    const int nextBar = (int) (3 * ids::ticksPerBar / fx.ticksPerSample());

    auto out = fx.renderFromStart (nextBar + 4096);
    const int onset = firstOnset (out, wrap - kQuietLeadIn);
    EXPECT_GE (onset, nextBar) << "wrapped even though the loop is off";
    EXPECT_LT (onset, nextBar + 128);
}

TEST (SequencerEngine, ZeroLengthLoopIsIgnored)
{
    EngineFixture fx;
    soloNoteAt (fx, 0);
    setLoop (fx, ids::ticksPerBar, ids::ticksPerBar, true);

    auto snap = fx.engine.getPendingSnapshot();
    ASSERT_NE (snap, nullptr);
    EXPECT_FALSE (snap->loopEnabled);

    // Pattern mode still retriggers at the pattern length.
    const int bar = (int) (ids::ticksPerBar / fx.ticksPerSample());
    auto out = fx.renderFromStart (bar + 4096);
    const int onset = firstOnset (out, bar - kQuietLeadIn);
    EXPECT_GE (onset, bar);
    EXPECT_LT (onset, bar + 128);
}

TEST (SequencerEngine, InvertedLoopRangeIsNormalised)
{
    EngineFixture fx;
    setLoop (fx, kLoopEndTicks, ids::ticksPerBar, true);

    auto snap = fx.engine.getPendingSnapshot();
    ASSERT_NE (snap, nullptr);
    EXPECT_TRUE (snap->loopEnabled);
    EXPECT_EQ (snap->loopStartTicks, ids::ticksPerBar);
    EXPECT_EQ (snap->loopEndTicks, kLoopEndTicks);
}

TEST (SequencerEngine, LoopWrapsInSongMode)
{
    EngineFixture fx;
    soloNoteAt (fx, 0);

    const int patId = fx.model.getRoot()[ids::activePattern];
    auto clip = fx.model.addPlaylistClip ("pattern", 0, ids::ticksPerBar, ids::ticksPerBar);
    clip.setProperty (ids::patternId, patId, nullptr);
    fx.model.setSongMode (true);
    setLoop (fx, 0, 2 * ids::ticksPerBar, true);

    const int bar = (int) (ids::ticksPerBar / fx.ticksPerSample());
    const int wrap = loopWrapSample (fx, 2 * ids::ticksPerBar);

    auto out = fx.renderFromStart (wrap + bar + 4096);
    EXPECT_LT (rmsOf (out, 0, bar - 256), 1.0e-5f) << "audio before the clip";

    // Second pass hits the clip again one bar after the wrap.
    const int onset = firstOnset (out, wrap + bar - kQuietLeadIn);
    EXPECT_GE (onset, wrap + bar);
    EXPECT_LT (onset, wrap + bar + 128);
}

TEST (SequencerEngine, PreviewNoteProducesAudioWhileStopped)
{
    EngineFixture fx;
    const int chId = fx.model.getChannel (0)[ids::id];
    fx.engine.previewNote (chId, 60, 1.0f, 100);
    auto out = fx.render (8192);
    EXPECT_GT (rmsOf (out, 0, 4096), 0.01f);
}

TEST (SequencerEngine, ProjectSwingAutomationShiftsOddSteps)
{
    // The rack swing knob's automation target: project-level swing, applied
    // to patterns that follow the project value (song mode, like all
    // automation).
    EngineFixture fx;
    soloNoteAt (fx, 1 * ids::ticksPerStep);   // odd step
    fx.model.setSwing (0.0);
    fx.model.setSongMode (true);
    auto patternClip = fx.model.addPlaylistClip ("pattern", 0, 0, ids::ticksPerBar);
    patternClip.setProperty (ids::patternId, (int) fx.model.getPattern (0)[ids::id], nullptr);

    auto automation = fx.model.addAutomation ("project", 0, "swing", "Swing", 1.0);
    auto clip = fx.model.addPlaylistClip ("automation", 1, 0, ids::ticksPerBar);
    clip.setProperty (ids::automationId, (int) automation[ids::id], nullptr);
    fx.sync.rebuildNow();

    ASSERT_EQ (fx.engine.getPendingSnapshot()->automations.size(), 1u);
    EXPECT_EQ (fx.engine.getPendingSnapshot()->automations.front().kind,
               AutomationSnapshot::Kind::projectSwing);

    const double tps = fx.ticksPerSample();
    const int unswungOnset = (int) (240 / tps);
    const int swungOnset   = (int) ((240 + 120) / tps);

    auto out = fx.renderFromStart (swungOnset + 8192);
    EXPECT_LT (rmsOf (out, unswungOnset, swungOnset - unswungOnset - 256), 1.0e-5f)
        << "swing automation was ignored";
    EXPECT_GT (rmsOf (out, swungOnset, 2048), 0.01f);
}

namespace
{
// Stands in for a hosted/sandboxed instrument: releases voices only on MIDI
// it can see, never on reset(). Counts what the engine actually sends.
struct MidiCaptureGenerator : public Generator
{
    void prepare (double, int) override {}
    void reset() override { ++resets; }
    void render (juce::AudioBuffer<float>&, const juce::MidiBuffer& midi) override
    {
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn())            ++noteOns;
            else if (msg.isNoteOff())      ++noteOffs;
            else if (msg.isAllNotesOff())  ++allNotesOffs;
        }
    }
    std::atomic<int> noteOns { 0 }, noteOffs { 0 }, allNotesOffs { 0 }, resets { 0 };
};

// A single 4-bar note at tick 0 on channel 0 — far longer than the render
// windows below, so it is still held when the transport event fires.
void holdLongNote (EngineFixture& fx)
{
    auto pattern = fx.model.getPattern (0);
    for (int i = pattern.getNumChildren(); --i >= 0;)
        if (pattern.getChild (i).hasType (ids::LANE))
            pattern.removeChild (i, nullptr);

    auto lane = fx.model.getOrCreateLane (pattern, fx.model.getChannel (0)[ids::id]);
    fx.model.addNote (lane, 60, 0, 4 * ids::ticksPerBar);
    fx.sync.rebuildNow();
}
}

TEST (SequencerEngine, StopSendsRealNoteOffsToTheGenerator)
{
    // Built-in generators kill voices in reset(), but plugins treat
    // AudioProcessor::reset() as advisory and a sandboxed child never hears
    // it — stop has to put actual note-offs on the wire.
    EngineFixture fx;
    holdLongNote (fx);

    auto capture = std::make_shared<MidiCaptureGenerator>();
    auto snap = std::make_shared<EngineSnapshot> (*fx.engine.getPendingSnapshot());
    ASSERT_FALSE (snap->channels.empty());
    snap->channels[0].generator = capture;
    fx.engine.publishSnapshot (snap);

    fx.engine.setPositionTicks (0.0);
    fx.engine.play();
    fx.render (2048);                       // note-on lands, note still held
    EXPECT_GT (capture->noteOns.load(), 0);
    EXPECT_EQ (capture->noteOffs.load(), 0);

    fx.engine.stop();
    fx.render (test::kBlockSize);           // the block that services the stop

    EXPECT_GT (capture->noteOffs.load(), 0) << "stop must emit a real MIDI note-off";
    EXPECT_GT (capture->allNotesOffs.load(), 0) << "stop must emit CC 123 (all notes off)";
    EXPECT_GT (capture->resets.load(), 0);
}

TEST (SequencerEngine, SeekSendsRealNoteOffsToTheGenerator)
{
    EngineFixture fx;
    holdLongNote (fx);

    auto capture = std::make_shared<MidiCaptureGenerator>();
    auto snap = std::make_shared<EngineSnapshot> (*fx.engine.getPendingSnapshot());
    ASSERT_FALSE (snap->channels.empty());
    snap->channels[0].generator = capture;
    fx.engine.publishSnapshot (snap);

    fx.engine.setPositionTicks (0.0);
    fx.engine.play();
    fx.render (2048);
    ASSERT_GT (capture->noteOns.load(), 0);

    fx.engine.setPositionTicks (2 * ids::ticksPerBar);   // seek mid-note
    fx.render (test::kBlockSize);
    EXPECT_GT (capture->noteOffs.load(), 0) << "seek must release the held note";

    fx.engine.stop();
}

TEST (SequencerEngine, PerNotePanReachesTheVoice)
{
    // notePan was stored but never played back. Two notes, hard left and hard
    // right: each must be silent on its far channel.
    EngineFixture fx;
    auto pattern = fx.model.getPattern (0);
    for (int i = pattern.getNumChildren(); --i >= 0;)
        if (pattern.getChild (i).hasType (ids::LANE))
            pattern.removeChild (i, nullptr);

    auto lane = fx.model.getOrCreateLane (pattern, fx.model.getChannel (0)[ids::id]);
    auto left = fx.model.addNote (lane, 60, 0, ids::ticksPerStep);
    left.setProperty (ids::notePan, -1.0, nullptr);
    auto right = fx.model.addNote (lane, 60, 8 * ids::ticksPerStep, ids::ticksPerStep);
    right.setProperty (ids::notePan, 1.0, nullptr);
    fx.sync.rebuildNow();

    const double tps = fx.ticksPerSample();
    const int rightOnset = (int) (8 * ids::ticksPerStep / tps);
    auto out = fx.renderFromStart (rightOnset + 8192);

    // First note: left channel only.
    EXPECT_GT (out.getRMSLevel (0, 0, 2048), 0.01f);
    EXPECT_LT (out.getRMSLevel (1, 0, 2048), 1.0e-5f);
    // Second note: right channel only.
    EXPECT_GT (out.getRMSLevel (1, rightOnset, 2048), 0.01f);
    EXPECT_LT (out.getRMSLevel (0, rightOnset, 2048), 1.0e-5f);
}
