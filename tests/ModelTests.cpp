#include <gtest/gtest.h>
#include "model/LaneUtils.h"
#include "model/ProjectModel.h"

TEST (ProjectModel, DefaultProjectShape)
{
    ProjectModel model;
    EXPECT_EQ (model.numChannels(), 4);
    EXPECT_EQ (model.numPatterns(), 1);
    EXPECT_EQ (model.numInserts(), 33);
    EXPECT_EQ (model.numPlaylistTracks(), 24);
    EXPECT_DOUBLE_EQ (model.getTempo(), 140.0);
    EXPECT_FALSE (model.isSongMode());

    // Active pattern points at the first pattern.
    const int activeId = model.getRoot()[ids::activePattern];
    EXPECT_TRUE (model.getPatternById (activeId).isValid());

    // Starter beat: kick lane has 4 notes, hat lane has 4.
    auto pattern = model.getPattern (0);
    auto kickLane = model.getLane (pattern, model.getChannel (0)[ids::id]);
    ASSERT_TRUE (kickLane.isValid());
    EXPECT_EQ (kickLane.getNumChildren(), 4);
}

TEST (ProjectModel, ChannelIdsAreUnique)
{
    ProjectModel model;
    auto a = model.addChannel ("sampler", "A");
    auto b = model.addChannel ("synth", "B");
    EXPECT_NE ((int) a[ids::id], (int) b[ids::id]);
    EXPECT_TRUE (model.getChannelById (a[ids::id]).isValid());
    EXPECT_EQ (model.getChannelById (a[ids::id])[ids::name].toString(), "A");
}

TEST (ProjectModel, RemoveChannelAlsoRemovesLanes)
{
    ProjectModel model;
    auto channel = model.getChannel (0);
    const int chId = channel[ids::id];
    auto pattern = model.getPattern (0);
    ASSERT_TRUE (model.getLane (pattern, chId).isValid());

    model.removeChannel (channel);
    EXPECT_FALSE (model.getChannelById (chId).isValid());
    EXPECT_FALSE (model.getLane (pattern, chId).isValid());
}

TEST (ProjectModel, NotesRoundTrip)
{
    ProjectModel model;
    auto pattern = model.getPattern (0);
    auto lane = model.getOrCreateLane (pattern, model.getChannel (1)[ids::id]);
    auto note = model.addNote (lane, 64, 480, 240, 0.9, -0.25);

    EXPECT_EQ ((int) note[ids::key], 64);
    EXPECT_EQ ((int) note[ids::startTicks], 480);
    EXPECT_DOUBLE_EQ ((double) note[ids::velocity], 0.9);
    EXPECT_DOUBLE_EQ ((double) note[ids::notePan], -0.25);

    model.removeNote (lane, note);
    EXPECT_EQ (lane.getNumChildren(), 0);
}

TEST (ProjectModel, UndoRedoNoteAdd)
{
    ProjectModel model;
    auto pattern = model.getPattern (0);
    auto lane = model.getOrCreateLane (pattern, model.getChannel (1)[ids::id]);

    model.getUndoManager().beginNewTransaction();
    model.addNote (lane, 60, 0, 240);
    EXPECT_EQ (lane.getNumChildren(), 1);

    EXPECT_TRUE (model.getUndoManager().undo());
    EXPECT_EQ (lane.getNumChildren(), 0);
    EXPECT_TRUE (model.getUndoManager().redo());
    EXPECT_EQ (lane.getNumChildren(), 1);
}

TEST (ProjectModel, SaveLoadRoundTrip)
{
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getNonexistentChildFile ("eurytest-project", ".eury");
    {
        ProjectModel model;
        model.setTempo (173.5);
        model.setSwing (0.33);
        auto pattern = model.getPattern (0);
        auto lane = model.getOrCreateLane (pattern, model.getChannel (1)[ids::id]);
        model.addNote (lane, 72, 960, 480, 0.6, 0.1);
        ASSERT_TRUE (model.saveToFile (file));
    }
    {
        ProjectModel model;
        ASSERT_TRUE (model.loadFromFile (file));
        EXPECT_DOUBLE_EQ (model.getTempo(), 173.5);
        EXPECT_DOUBLE_EQ (model.getSwing(), 0.33);
        auto pattern = model.getPattern (0);
        auto lane = model.getLane (pattern, model.getChannel (1)[ids::id]);
        ASSERT_TRUE (lane.isValid());
        ASSERT_EQ (lane.getNumChildren(), 1);
        EXPECT_EQ ((int) lane.getChild (0)[ids::key], 72);
    }
    file.deleteFile();
}

// Panels and the engine hold listeners on the root and container *objects*.
// A load must keep those objects alive and notify through them, or the whole
// UI silently keeps showing the previous project.
TEST (ProjectModel, LoadKeepsTreeIdentityAndNotifiesListeners)
{
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getNonexistentChildFile ("eurytest-identity", ".eury");
    {
        ProjectModel saved;
        saved.setTempo (148.0);
        saved.addChannel ("sampler", "Rumble");
        saved.addChannel ("sampler", "Stab");
        ASSERT_TRUE (saved.saveToFile (file));
    }

    ProjectModel model;
    auto rootBefore = model.getRoot();
    auto channelsBefore = model.getRoot().getChildWithName (ids::CHANNELS);
    const int defaultChannels = model.numChannels();

    struct Counter : juce::ValueTree::Listener
    {
        int channelsAdded = 0;
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child) override
        {
            if (child.hasType (ids::CHANNEL))
                ++channelsAdded;
        }
    } counter;
    rootBefore.addListener (&counter);

    ASSERT_TRUE (model.loadFromFile (file));

    // Same objects, new content.
    EXPECT_TRUE (model.getRoot() == rootBefore);
    EXPECT_TRUE (model.getRoot().getChildWithName (ids::CHANNELS) == channelsBefore);
    EXPECT_DOUBLE_EQ ((double) rootBefore[ids::tempo], 148.0);
    EXPECT_EQ (model.numChannels(), defaultChannels + 2);

    // The pre-existing listener heard about the new channels.
    EXPECT_EQ (counter.channelsAdded, defaultChannels + 2);

    rootBefore.removeListener (&counter);
    file.deleteFile();
}

TEST (ProjectModel, PatternSwingFallsBackToTheProject)
{
    ProjectModel model;
    auto pattern = model.getPattern (0);
    model.setSwing (0.25);

    EXPECT_FALSE (model.patternOverridesSwing (pattern));
    EXPECT_DOUBLE_EQ (model.getSwingForPattern (pattern), 0.25);

    model.setPatternSwing (pattern, 0.8);
    EXPECT_TRUE (model.patternOverridesSwing (pattern));
    EXPECT_DOUBLE_EQ (model.getSwingForPattern (pattern), 0.8);

    // A pattern is allowed to pin itself straight against a swinging project.
    model.setPatternSwing (pattern, 0.0);
    EXPECT_DOUBLE_EQ (model.getSwingForPattern (pattern), 0.0);

    model.clearPatternSwing (pattern);
    EXPECT_FALSE (model.patternOverridesSwing (pattern));
    EXPECT_DOUBLE_EQ (model.getSwingForPattern (pattern), 0.25);
}

// Projects saved before per-pattern swing existed carry no pattern property
// and must keep playing with the project's value.
TEST (ProjectModel, PatternSwingSurvivesSaveAndLoad)
{
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getNonexistentChildFile ("eurytest-swing", ".eury");
    {
        ProjectModel model;
        model.setSwing (0.2);
        model.setPatternSwing (model.getPattern (0), 0.7);
        model.addPattern ("Straight");
        ASSERT_TRUE (model.saveToFile (file));
    }
    {
        ProjectModel model;
        ASSERT_TRUE (model.loadFromFile (file));
        EXPECT_DOUBLE_EQ (model.getSwingForPattern (model.getPattern (0)), 0.7);
        EXPECT_FALSE (model.patternOverridesSwing (model.getPattern (1)));
        EXPECT_DOUBLE_EQ (model.getSwingForPattern (model.getPattern (1)), 0.2);
    }
    file.deleteFile();
}

TEST (ProjectModel, LoadRejectsGarbage)
{
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getNonexistentChildFile ("eurytest-garbage", ".eury");
    file.replaceWithText ("not a project");
    ProjectModel model;
    EXPECT_FALSE (model.loadFromFile (file));
    // Model still intact after failed load.
    EXPECT_EQ (model.numChannels(), 4);
    file.deleteFile();
}

TEST (ProjectModel, PlaylistClips)
{
    ProjectModel model;
    auto clip = model.addPlaylistClip ("pattern", 3, 3840, 7680);
    ASSERT_TRUE (clip.isValid());
    EXPECT_EQ ((int) clip[ids::startTicks], 3840);
    EXPECT_EQ (model.playlist().getChild (3).getNumChildren(), 1);

    // out-of-range track rejected
    EXPECT_FALSE (model.addPlaylistClip ("pattern", 99, 0, 100).isValid());
}

TEST (ProjectModel, LoopRangeIsSanitised)
{
    ProjectModel model;
    EXPECT_FALSE (model.isLoopEnabled());

    model.setLoopRange (2 * ids::ticksPerBar, ids::ticksPerBar);   // dragged right to left
    EXPECT_EQ (model.getLoopStart(), ids::ticksPerBar);
    EXPECT_EQ (model.getLoopEnd(), 2 * ids::ticksPerBar);

    model.setLoopRange (-500, ids::ticksPerQuarter);
    EXPECT_EQ (model.getLoopStart(), 0);
    EXPECT_EQ (model.getLoopEnd(), ids::ticksPerQuarter);

    model.setLoopEnabled (true);
    EXPECT_TRUE (model.isLoopEnabled());

    model.clearLoop();
    EXPECT_FALSE (model.isLoopEnabled());
    EXPECT_EQ (model.getLoopStart(), 0);
    EXPECT_EQ (model.getLoopEnd(), 0);
}

TEST (ProjectModel, AutomationDefaults)
{
    ProjectModel model;
    auto automation = model.addAutomation ("channel", 42, "volume", "test vol", 0.7);
    EXPECT_EQ (automation[ids::targetType].toString(), "channel");
    EXPECT_EQ ((int) automation[ids::targetId], 42);
    EXPECT_EQ (automation.getNumChildren(), 2);   // two flat points
    EXPECT_DOUBLE_EQ ((double) automation.getChild (0)[ids::value], 0.7);
    EXPECT_TRUE (model.getAutomationById (automation[ids::id]).isValid());
}

TEST (ProjectModel, ClonePatternIsAnIndependentDeepCopy)
{
    ProjectModel model;
    auto source = model.getPattern (0);
    const int kickId = model.getChannel (0)[ids::id];
    const int sourceNotes = model.getLane (source, kickId).getNumChildren();
    ASSERT_GT (sourceNotes, 0);

    auto copy = model.clonePattern (source[ids::id]);
    ASSERT_TRUE (copy.isValid());
    EXPECT_EQ (copy[ids::name].toString(), "Pattern 1 (copy)");
    EXPECT_NE ((int) copy[ids::id], (int) source[ids::id]);
    EXPECT_EQ (model.numPatterns(), 2);
    EXPECT_EQ (model.patterns().indexOf (copy), model.patterns().indexOf (source) + 1);

    auto copiedLane = model.getLane (copy, kickId);
    ASSERT_TRUE (copiedLane.isValid());
    ASSERT_EQ (copiedLane.getNumChildren(), sourceNotes);

    // Editing the copy leaves the original untouched.
    model.addNote (copiedLane, 67, 7 * ids::ticksPerStep, ids::ticksPerStep);
    copiedLane.getChild (0).setProperty (ids::velocity, 0.11, nullptr);
    copy.setProperty (ids::lengthTicks, 2 * ids::ticksPerBar, nullptr);

    auto sourceLane = model.getLane (source, kickId);
    EXPECT_EQ (sourceLane.getNumChildren(), sourceNotes);
    EXPECT_NE ((double) sourceLane.getChild (0)[ids::velocity], 0.11);
    EXPECT_EQ ((int) source[ids::lengthTicks], ids::ticksPerBar);
}

TEST (ProjectModel, CloneRejectsUnknownPattern)
{
    ProjectModel model;
    EXPECT_FALSE (model.clonePattern (987654).isValid());
    EXPECT_EQ (model.numPatterns(), 1);
}

TEST (ProjectModel, RemovePatternDropsReferencingClips)
{
    ProjectModel model;
    auto victim = model.addPattern ("Doomed");
    auto keeper = model.getPattern (0);

    auto doomedClip = model.addPlaylistClip ("pattern", 0, 0, ids::ticksPerBar);
    doomedClip.setProperty (ids::patternId, (int) victim[ids::id], nullptr);
    auto keptClip = model.addPlaylistClip ("pattern", 0, ids::ticksPerBar, ids::ticksPerBar);
    keptClip.setProperty (ids::patternId, (int) keeper[ids::id], nullptr);

    ASSERT_TRUE (model.removePattern (victim[ids::id]));
    EXPECT_FALSE (model.getPatternById (victim[ids::id]).isValid());

    auto track = model.playlist().getChild (0);
    ASSERT_EQ (track.getNumChildren(), 1);
    EXPECT_EQ ((int) track.getChild (0)[ids::patternId], (int) keeper[ids::id]);
}

TEST (ProjectModel, RemovePatternRefusesTheLastOne)
{
    ProjectModel model;
    ASSERT_EQ (model.numPatterns(), 1);
    EXPECT_FALSE (model.removePattern (model.getPattern (0)[ids::id]));
    EXPECT_EQ (model.numPatterns(), 1);
    EXPECT_FALSE (model.removePattern (987654));
}

TEST (ProjectModel, RemovingActivePatternSelectsNeighbour)
{
    ProjectModel model;
    auto second = model.addPattern ("Second");
    model.getRoot().setProperty (ids::activePattern, (int) second[ids::id], nullptr);

    ASSERT_TRUE (model.removePattern (second[ids::id]));
    const int activeId = model.getRoot()[ids::activePattern];
    EXPECT_TRUE (model.getPatternById (activeId).isValid());
    EXPECT_EQ (activeId, (int) model.getPattern (0)[ids::id]);
}

TEST (ProjectModel, MovePatternReorders)
{
    ProjectModel model;
    const int firstId = model.getPattern (0)[ids::id];
    const int secondId = model.addPattern ("Second")[ids::id];

    ASSERT_TRUE (model.movePattern (1, 0));
    EXPECT_EQ ((int) model.getPattern (0)[ids::id], secondId);
    EXPECT_EQ ((int) model.getPattern (1)[ids::id], firstId);

    EXPECT_FALSE (model.movePattern (0, 0));
    EXPECT_FALSE (model.movePattern (-1, 0));
    EXPECT_FALSE (model.movePattern (0, 9));
    EXPECT_EQ ((int) model.getPattern (0)[ids::id], secondId);
}

TEST (LaneUtils, PlainStepLaneIsNotPianoRoll)
{
    ProjectModel model;
    auto lane = model.getOrCreateLane (model.getPattern (0), model.getChannel (1)[ids::id]);
    EXPECT_FALSE (laneUsesPianoRoll (lane, 60));   // empty lane

    for (int step = 0; step < 4; ++step)
        model.addNote (lane, 60, step * ids::ticksPerStep, ids::ticksPerStep);
    EXPECT_FALSE (laneUsesPianoRoll (lane, 60));
}

TEST (LaneUtils, OffGridStartMakesItPianoRoll)
{
    ProjectModel model;
    auto lane = model.getOrCreateLane (model.getPattern (0), model.getChannel (1)[ids::id]);
    model.addNote (lane, 60, ids::ticksPerStep + 17, ids::ticksPerStep);
    EXPECT_TRUE (laneUsesPianoRoll (lane, 60));
}

TEST (LaneUtils, NonStepLengthMakesItPianoRoll)
{
    ProjectModel model;
    auto pattern = model.getPattern (0);
    auto longLane = model.getOrCreateLane (pattern, model.getChannel (1)[ids::id]);
    model.addNote (longLane, 60, 0, ids::ticksPerStep * 2);
    EXPECT_TRUE (laneUsesPianoRoll (longLane, 60));

    auto shortLane = model.getOrCreateLane (pattern, model.getChannel (3)[ids::id]);
    model.addNote (shortLane, 60, 0, ids::ticksPerStep / 2);
    EXPECT_TRUE (laneUsesPianoRoll (shortLane, 60));
}

TEST (LaneUtils, NonRootPitchMakesItPianoRoll)
{
    ProjectModel model;
    auto lane = model.getOrCreateLane (model.getPattern (0), model.getChannel (1)[ids::id]);
    model.addNote (lane, 67, 0, ids::ticksPerStep);
    EXPECT_TRUE (laneUsesPianoRoll (lane, 60));
    EXPECT_FALSE (laneUsesPianoRoll (lane, 67));   // same notes, different root
}

TEST (ProjectModel, InsertsDefaultRouteToMaster)
{
    ProjectModel model;
    auto insert5 = model.getInsert (5);
    auto sends = insert5.getChildWithName (ids::SENDS);
    ASSERT_EQ (sends.getNumChildren(), 1);
    EXPECT_EQ ((int) sends.getChild (0)[ids::destInsert], 0);

    // Master has no sends.
    EXPECT_EQ (model.getInsert (0).getChildWithName (ids::SENDS).getNumChildren(), 0);
}

// ---------------- lane classification ----------------

namespace
{
juce::ValueTree makeStepLane()
{
    juce::ValueTree lane (ids::LANE);
    lane.setProperty (ids::channelId, 1, nullptr);
    for (int step = 0; step < 4; ++step)
    {
        juce::ValueTree note (ids::NOTE);
        note.setProperty (ids::key, 60, nullptr);
        note.setProperty (ids::startTicks, step * ids::ticksPerStep, nullptr);
        note.setProperty (ids::lengthTicks, ids::ticksPerStep, nullptr);
        lane.appendChild (note, nullptr);
    }
    return lane;
}
}

TEST (LaneClassification, PlainStepLaneIsNotPianoRoll)
{
    EXPECT_FALSE (laneUsesPianoRoll (makeStepLane(), 60));
}

TEST (LaneClassification, HeuristicCatchesOffGridContent)
{
    auto lane = makeStepLane();
    lane.getChild (0).setProperty (ids::startTicks, 120, nullptr);
    EXPECT_TRUE (laneUsesPianoRoll (lane, 60));
}

TEST (LaneClassification, ExplicitFlagWinsOverHeuristic)
{
    // A step lane whose pitch was nudged in the rack graph lane still reads as
    // steps — otherwise editing pitch would flip the row to a note preview and
    // hide the very control being used.
    auto lane = makeStepLane();
    lane.getChild (0).setProperty (ids::key, 67, nullptr);
    EXPECT_TRUE (laneUsesPianoRoll (lane, 60)) << "heuristic alone would say piano roll";

    lanes::markEditedWithSteps (lane);
    EXPECT_FALSE (laneUsesPianoRoll (lane, 60)) << "the step editor claimed this lane";
}

TEST (LaneClassification, PianoRollFlagSticksForStepShapedNotes)
{
    // Notes drawn in the piano roll that happen to land on the grid at the
    // root pitch must still count as piano-roll content.
    auto lane = makeStepLane();
    lanes::markEditedWithPianoRoll (lane);
    EXPECT_TRUE (laneUsesPianoRoll (lane, 60));
}

TEST (LaneClassification, EmptyLaneIsNotPianoRoll)
{
    EXPECT_FALSE (laneUsesPianoRoll (juce::ValueTree (ids::LANE), 60));
}

TEST (ProjectModel, ChannelsRoutedToInsert)
{
    ProjectModel model;
    // Fresh project: every channel routes to the master insert.
    EXPECT_EQ (model.channelsRoutedTo (0).size(), 4);
    EXPECT_TRUE (model.channelsRoutedTo (1).isEmpty());

    // Route exactly one channel to insert 1: the "name after channel"
    // menu item keys off a single routed channel.
    model.getChannel (0).setProperty (ids::insertIndex, 1, nullptr);
    const auto routed = model.channelsRoutedTo (1);
    ASSERT_EQ (routed.size(), 1);
    EXPECT_EQ (routed[0], model.getChannel (0)[ids::name].toString());
    EXPECT_EQ (model.channelsRoutedTo (0).size(), 3);

    // Two channels on one insert: no unambiguous name.
    model.getChannel (1).setProperty (ids::insertIndex, 1, nullptr);
    EXPECT_EQ (model.channelsRoutedTo (1).size(), 2);
}
