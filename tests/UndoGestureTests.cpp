#include <gtest/gtest.h>
#include "model/ProjectModel.h"
#include "model/UndoGesture.h"

namespace
{
int countUndoSteps (ProjectModel& model)
{
    int steps = 0;
    while (model.getUndoManager().undo())
        ++steps;
    return steps;
}
}

TEST (UndoGesture, KnobDragCollapsesToOneStep)
{
    ProjectModel model;
    auto channel = model.getChannel (0);
    channel.setProperty (ids::volume, 0.5, nullptr);
    ASSERT_FALSE (model.getUndoManager().canUndo());

    undoGesture::begin (model, "Channel volume");
    for (int i = 1; i <= 40; ++i)
        channel.setProperty (ids::volume, (double) i / 40.0, &model.getUndoManager());
    undoGesture::end (model);

    EXPECT_DOUBLE_EQ ((double) channel[ids::volume], 1.0);
    EXPECT_EQ (model.getUndoManager().getUndoDescription(), "Channel volume");
    EXPECT_EQ (countUndoSteps (model), 1);
    EXPECT_DOUBLE_EQ ((double) channel[ids::volume], 0.5);
}

// Two properties per mouse-move, as a piano-roll note drag does: nothing here
// coalesces on its own, so only the transaction keeps it to one step.
TEST (UndoGesture, MultiPropertyDragCollapsesToOneStep)
{
    ProjectModel model;
    auto lane = model.getOrCreateLane (model.getPattern (0), model.getChannel (1)[ids::id]);
    auto note = model.addNote (lane, 60, 0, 240);
    model.getUndoManager().clearUndoHistory();

    undoGesture::begin (model, "Move notes");
    for (int i = 1; i <= 25; ++i)
    {
        note.setProperty (ids::startTicks, i * 240, &model.getUndoManager());
        note.setProperty (ids::key, 60 + i, &model.getUndoManager());
    }
    undoGesture::end (model);

    EXPECT_EQ ((int) note[ids::startTicks], 25 * 240);
    EXPECT_EQ (countUndoSteps (model), 1);
    EXPECT_EQ ((int) note[ids::startTicks], 0);
    EXPECT_EQ ((int) note[ids::key], 60);
}

TEST (UndoGesture, TwoDragsAreTwoSteps)
{
    ProjectModel model;
    auto channel = model.getChannel (0);
    channel.setProperty (ids::pan, 0.0, nullptr);
    model.getUndoManager().clearUndoHistory();

    for (const double target : { 0.4, -0.7 })
    {
        undoGesture::begin (model, "Channel pan");
        for (int i = 1; i <= 20; ++i)
            channel.setProperty (ids::pan, target * i / 20.0, &model.getUndoManager());
        undoGesture::end (model);
    }

    EXPECT_EQ (countUndoSteps (model), 2);
    EXPECT_DOUBLE_EQ ((double) channel[ids::pan], 0.0);
}

TEST (UndoGesture, SingleClickIsOneStep)
{
    ProjectModel model;
    auto channel = model.getChannel (0);
    channel.setProperty (ids::mute, false, nullptr);
    model.getUndoManager().clearUndoHistory();

    {
        const undoGesture::Scoped step (model, "Mute channel");
        channel.setProperty (ids::mute, true, &model.getUndoManager());
    }

    EXPECT_TRUE ((bool) channel[ids::mute]);
    EXPECT_EQ (countUndoSteps (model), 1);
    EXPECT_FALSE ((bool) channel[ids::mute]);
}

TEST (UndoGesture, UnrelatedEditsDoNotMerge)
{
    ProjectModel model;
    auto first = model.getChannel (0);
    auto second = model.getChannel (1);
    model.getUndoManager().clearUndoHistory();

    {
        const undoGesture::Scoped step (model, "Mute channel");
        first.setProperty (ids::mute, true, &model.getUndoManager());
    }
    {
        const undoGesture::Scoped step (model, "Route channel");
        second.setProperty (ids::insertIndex, 7, &model.getUndoManager());
    }

    EXPECT_EQ (countUndoSteps (model), 2);
}

// Everything a juce::Slider does — drag, click, wheel, text entry — arrives
// bracketed by onDragStart/onDragEnd, so attaching there is the whole fix.
TEST (UndoGesture, AttachedSliderBracketsEachGesture)
{
    ProjectModel model;
    auto channel = model.getChannel (0);
    channel.setProperty (ids::volume, 0.0, nullptr);
    model.getUndoManager().clearUndoHistory();

    juce::Slider slider;
    slider.setRange (0.0, 1.0, 0.001);
    slider.onValueChange = [&model, &channel, &slider]
    {
        channel.setProperty (ids::volume, slider.getValue(), &model.getUndoManager());
    };
    undoGesture::attach (slider, model, "Channel volume");
    ASSERT_TRUE (slider.onDragStart != nullptr);
    ASSERT_TRUE (slider.onDragEnd != nullptr);

    for (int gesture = 0; gesture < 2; ++gesture)
    {
        slider.onDragStart();
        for (int i = 1; i <= 10; ++i)
            slider.setValue (0.4 * gesture + 0.02 * i, juce::sendNotificationSync);
        slider.onDragEnd();
    }

    EXPECT_DOUBLE_EQ ((double) channel[ids::volume], 0.6);
    EXPECT_EQ (model.getUndoManager().getUndoDescription(), "Channel volume");
    EXPECT_EQ (countUndoSteps (model), 2);
    EXPECT_DOUBLE_EQ ((double) channel[ids::volume], 0.0);
}
