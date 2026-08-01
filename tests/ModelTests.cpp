#include <gtest/gtest.h>
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
