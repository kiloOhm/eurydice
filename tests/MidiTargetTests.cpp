#include "TestHelpers.h"

// Exactly one channel is always the MIDI input target, and both the router
// and the rack indicator resolve it the same way.

TEST (MidiTarget, FallsBackToTheFirstChannelWhenUnset)
{
    ProjectModel model;
    model.getRoot().removeProperty (ids::selectedChannel, nullptr);

    ASSERT_GT (model.numChannels(), 0);
    EXPECT_EQ (model.midiTargetChannelId(), (int) model.getChannel (0)[ids::id])
        << "with no selection stored, the first channel should take input";
}

TEST (MidiTarget, FallsBackWhenTheSelectedChannelIsGone)
{
    ProjectModel model;
    auto extra = model.addChannel ("synth", "Temp");
    const int extraId = extra[ids::id];
    model.getRoot().setProperty (ids::selectedChannel, extraId, nullptr);
    ASSERT_EQ (model.midiTargetChannelId(), extraId);

    model.removeChannel (extra);

    EXPECT_NE (model.midiTargetChannelId(), extraId) << "a deleted channel still holds MIDI input";
    EXPECT_TRUE (model.getChannelById (model.midiTargetChannelId()).isValid())
        << "the MIDI target must always be a real channel";
}

TEST (MidiTarget, SelectionIsHonouredAndStable)
{
    ProjectModel model;
    auto a = model.addChannel ("synth", "A");
    auto b = model.addChannel ("synth", "B");

    model.getRoot().setProperty (ids::selectedChannel, (int) b[ids::id], nullptr);
    EXPECT_EQ (model.midiTargetChannelId(), (int) b[ids::id]);

    // Nothing but an explicit selection changes it: unrelated edits (the kind
    // window focus changes trigger) must leave the target alone.
    a.setProperty (ids::mute, true, nullptr);
    b.setProperty (ids::volume, 0.5, nullptr);
    model.setTempo (140.0);

    EXPECT_EQ (model.midiTargetChannelId(), (int) b[ids::id])
        << "the MIDI target drifted off the selected channel";
}

TEST (MidiTarget, NeverPointsAtNothingWhileChannelsExist)
{
    ProjectModel model;
    model.getRoot().setProperty (ids::selectedChannel, 987654, nullptr);   // never valid

    EXPECT_GE (model.midiTargetChannelId(), 0);
    EXPECT_TRUE (model.getChannelById (model.midiTargetChannelId()).isValid());
}
