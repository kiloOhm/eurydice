#include "StepGraphLane.h"
#include "ChannelRow.h"
#include "app/Theme.h"
#include "model/LaneUtils.h"
#include "model/UndoGesture.h"

namespace
{
struct GraphRange { double min, max; bool bipolar; };

GraphRange rangeFor (StepGraphLane::Mode mode)
{
    switch (mode)
    {
        case StepGraphLane::Mode::pan:      return { -1.0, 1.0, true };
        case StepGraphLane::Mode::pitch:    return { -12.0, 12.0, true };
        case StepGraphLane::Mode::velocity: break;
    }
    return { 0.0, 1.0, false };
}
}

StepGraphLane::StepGraphLane (ProjectModel& m)
    : model (m)
{
    modeBox.addItem ("Velocity", 1);
    modeBox.addItem ("Pan", 2);
    modeBox.addItem ("Pitch", 3);
    modeBox.setSelectedId (1, juce::dontSendNotification);
    modeBox.setWantsKeyboardFocus (false);
    modeBox.onChange = [this] { repaint(); };
    addAndMakeVisible (modeBox);

    channelLabel.setFont (theme::uiFont (10.0f, true));
    channelLabel.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (channelLabel);
}

void StepGraphLane::setPattern (juce::ValueTree p)
{
    pattern = p;
    repaint();
}

void StepGraphLane::setChannel (juce::ValueTree c)
{
    channel = c;
    channelLabel.setText (c.isValid() ? c[ids::name].toString().toUpperCase() : "",
                          juce::dontSendNotification);
    repaint();
}

void StepGraphLane::setScrollOffset (int pixels)
{
    if (scrollOffset != pixels)
    {
        scrollOffset = pixels;
        repaint();
    }
}

StepGraphLane::Mode StepGraphLane::getMode() const
{
    return static_cast<Mode> (juce::jmax (0, modeBox.getSelectedId() - 1));
}

int StepGraphLane::numSteps() const
{
    if (! pattern.isValid())
        return 16;
    return juce::jmax (1, (int) pattern[ids::lengthTicks] / ids::ticksPerStep);
}

juce::Rectangle<int> StepGraphLane::barsArea() const
{
    return getLocalBounds().withLeft (ChannelRow::fixedLeftWidth).withTrimmedTop (1);
}

int StepGraphLane::stepAt (juce::Point<int> pos) const
{
    if (pos.x < ChannelRow::fixedLeftWidth)
        return -1;
    const int step = (pos.x - ChannelRow::fixedLeftWidth + scrollOffset) / ChannelRow::stepWidth;
    return step < numSteps() ? step : -1;
}

juce::ValueTree StepGraphLane::noteAtStep (int step) const
{
    if (! channel.isValid())
        return {};

    const auto lane = model.getLane (pattern, (int) channel[ids::id]);
    const int tick = step * ids::ticksPerStep;
    for (const auto note : lane)
        if ((int) note[ids::startTicks] == tick)
            return note;
    return {};
}

double StepGraphLane::valueOf (const juce::ValueTree& note) const
{
    switch (getMode())
    {
        case Mode::pan:      return (double) note[ids::notePan];
        case Mode::pitch:    return (double) ((int) note[ids::key]
                                              - (int) channel.getProperty (ids::rootNote, 60));
        case Mode::velocity: break;
    }
    return (double) note[ids::velocity];
}

bool StepGraphLane::isEditable() const
{
    if (! pattern.isValid() || ! channel.isValid())
        return false;
    return ! laneUsesPianoRoll (model.getLane (pattern, (int) channel[ids::id]),
                                (int) channel.getProperty (ids::rootNote, 60));
}

void StepGraphLane::applyDrag (juce::Point<int> pos)
{
    if (! isEditable())
        return;

    const int step = stepAt (pos);
    if (step < 0)
        return;

    auto note = noteAtStep (step);
    if (! note.isValid())
        return;

    const auto bars = barsArea().reduced (0, 6);
    const auto range = rangeFor (getMode());
    const double norm = juce::jlimit (0.0, 1.0,
                                      (double) (bars.getBottom() - pos.y)
                                          / (double) juce::jmax (1, bars.getHeight()));
    const double value = range.min + norm * (range.max - range.min);
    auto& undo = model.getUndoManager();

    // Editing here is step-sequencer work, even when it changes pitch, so the
    // row must keep showing steps rather than flipping to the note preview.
    lanes::markEditedWithSteps (note.getParent());

    switch (getMode())
    {
        case Mode::velocity: note.setProperty (ids::velocity, value, &undo); break;
        case Mode::pan:      note.setProperty (ids::notePan, value, &undo); break;
        case Mode::pitch:    note.setProperty (ids::key,
                                               (int) channel.getProperty (ids::rootNote, 60)
                                                   + juce::roundToInt (value), &undo); break;
    }
    repaint();
}

void StepGraphLane::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelHeader);
    g.setColour (theme::outline);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());

    const auto area = barsArea();
    g.setColour (theme::sunken);
    g.fillRect (area);

    const juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (area);

    const int steps = numSteps();
    for (int s = 0; s < steps; ++s)
    {
        g.setColour (((s / 4) % 2) == 0 ? theme::stepEvenBg : theme::stepOddBg);
        g.fillRect (juce::Rectangle<int> (area.getX() + s * ChannelRow::stepWidth - scrollOffset,
                                          area.getY(), ChannelRow::stepWidth - 1, area.getHeight()));
    }

    if (! isEditable())
    {
        g.setColour (theme::textFaint);
        g.setFont (theme::uiFont (11.0f));
        g.drawText (channel.isValid() ? "Piano roll lane: edit note values in the piano roll"
                                      : "No channel selected",
                    area, juce::Justification::centred);
        return;
    }

    const auto bars = area.reduced (0, 6);
    const auto range = rangeFor (getMode());
    const float zeroY = range.bipolar ? (float) bars.getCentreY() : (float) bars.getBottom();

    g.setColour (theme::outlineLight);
    g.drawHorizontalLine (juce::roundToInt (zeroY), (float) area.getX(), (float) area.getRight());

    for (int s = 0; s < steps; ++s)
    {
        const auto note = noteAtStep (s);
        if (! note.isValid())
            continue;

        const double norm = juce::jlimit (0.0, 1.0,
                                          (valueOf (note) - range.min) / (range.max - range.min));
        const float y = (float) bars.getBottom() - (float) norm * (float) bars.getHeight();
        const juce::Rectangle<float> bar (
            (float) (area.getX() + s * ChannelRow::stepWidth - scrollOffset) + 3.0f,
            juce::jmin (y, zeroY),
            (float) ChannelRow::stepWidth - 7.0f,
            juce::jmax (2.0f, std::abs (zeroY - y)));

        g.setColour (theme::accent);
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (theme::accent.brighter (0.6f));
        g.fillRect (bar.withY (y - 1.0f).withHeight (2.0f));
    }
}

void StepGraphLane::resized()
{
    auto left = getLocalBounds().withWidth (ChannelRow::fixedLeftWidth).reduced (8, 8);
    channelLabel.setBounds (left.removeFromTop (14));
    left.removeFromTop (4);
    modeBox.setBounds (left.removeFromTop (24).removeFromLeft (110));
}

void StepGraphLane::mouseDown (const juce::MouseEvent& e)
{
    undoGesture::begin (model, "Edit step graph");
    applyDrag (e.getPosition());
}

void StepGraphLane::mouseDrag (const juce::MouseEvent& e)
{
    applyDrag (e.getPosition());
}

void StepGraphLane::mouseUp (const juce::MouseEvent&)
{
    undoGesture::end (model);
}
