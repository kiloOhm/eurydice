#include "ChannelRow.h"
#include "app/Theme.h"
#include "model/LaneUtils.h"
#include "model/UndoGesture.h"

ChannelRow::ChannelRow (ProjectModel& m, juce::ValueTree ch)
    : model (m), channel (ch)
{
    muteLed.setClickingTogglesState (true);
    muteLed.setWantsKeyboardFocus (false);
    muteLed.onClick = [this]
    {
        const undoGesture::Scoped step (model, "Mute channel");
        channel.setProperty (ids::mute, ! muteLed.getToggleState(), &model.getUndoManager());
    };

    nameButton.setWantsKeyboardFocus (false);
    nameButton.onClick = [this]
    {
        if (onSelected) onSelected (getChannelId());
        if (onOpenEditor) onOpenEditor (channel);
    };
    // The button would otherwise swallow right-clicks, so route its mouse
    // events here too and handle the context menu ourselves.
    nameButton.addMouseListener (this, false);

    insertButton.setWantsKeyboardFocus (false);
    insertButton.setTooltip ("Mixer insert this channel is routed to");
    insertButton.onClick = [this]
    {
        if (onSelected) onSelected (getChannelId());
        if (onWantsInsertMenu) onWantsInsertMenu (channel);
    };
    insertButton.addMouseListener (this, false);
    addAndMakeVisible (insertButton);

    auto initKnob = [this] (juce::Slider& k, const juce::Identifier& prop, double min, double max,
                            const juce::String& gestureName)
    {
        k.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        k.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        k.setRange (min, max, 0.001);
        k.setWantsKeyboardFocus (false);
        k.setDoubleClickReturnValue (true, prop == ids::pan ? 0.0 : 0.78);
        k.onValueChange = [this, &k, prop]
        {
            channel.setProperty (prop, k.getValue(), &model.getUndoManager());
        };
        undoGesture::attach (k, model, gestureName);
        addAndMakeVisible (k);
    };
    initKnob (panKnob, ids::pan, -1.0, 1.0, "Channel pan");
    initKnob (volKnob, ids::volume, 0.0, 1.0, "Channel volume");

    addAndMakeVisible (muteLed);
    addAndMakeVisible (nameButton);
    refreshFromModel();
}

void ChannelRow::setPattern (juce::ValueTree p)
{
    pattern = p;
    repaint();
}

void ChannelRow::setPlayStep (int step)
{
    if (playStep != step)
    {
        playStep = step;
        repaint (stepsArea());
    }
}

void ChannelRow::refreshFromModel()
{
    nameButton.setButtonText (channel[ids::name].toString());

    const int insertIndex = channel[ids::insertIndex];
    insertButton.setButtonText (insertIndex == 0 ? "MST" : juce::String (insertIndex));
    insertButton.setColour (juce::TextButton::textColourOffId,
                            insertIndex == 0 ? theme::textFaint : theme::secondary);
    const bool muted = channel[ids::mute];
    muteLed.setToggleState (! muted, juce::dontSendNotification);
    muteLed.setColour (juce::TextButton::buttonColourId,    muted ? theme::ledOff : theme::ledGreen);
    muteLed.setColour (juce::TextButton::buttonOnColourId,  muted ? theme::ledOff : theme::ledGreen);
    panKnob.setValue ((double) channel[ids::pan], juce::dontSendNotification);
    volKnob.setValue ((double) channel[ids::volume], juce::dontSendNotification);
    repaint();
}

int ChannelRow::numSteps() const
{
    if (! pattern.isValid())
        return 16;
    return juce::jmax (1, (int) pattern[ids::lengthTicks] / ids::ticksPerStep);
}

juce::Rectangle<int> ChannelRow::stepsArea() const
{
    return getLocalBounds().withLeft (fixedLeftWidth);
}

int ChannelRow::stepAt (juce::Point<int> pos) const
{
    if (pos.x < fixedLeftWidth)
        return -1;
    const int step = (pos.x - fixedLeftWidth) / stepWidth;
    return step < numSteps() ? step : -1;
}

bool ChannelRow::isStepOn (int step) const
{
    const auto lane = model.getLane (pattern, getChannelId());
    if (! lane.isValid())
        return false;
    const int tick = step * ids::ticksPerStep;
    for (const auto note : lane)
        if ((int) note[ids::startTicks] == tick)
            return true;
    return false;
}

void ChannelRow::setStep (int step, bool on)
{
    auto lane = model.getOrCreateLane (pattern, getChannelId());
    lanes::markEditedWithSteps (lane);
    const int tick = step * ids::ticksPerStep;

    if (on)
    {
        if (! isStepOn (step))
            model.addNote (lane, (int) channel.getProperty (ids::rootNote, 60),
                           tick, ids::ticksPerStep);
    }
    else
    {
        for (int i = lane.getNumChildren(); --i >= 0;)
            if ((int) lane.getChild (i)[ids::startTicks] == tick)
                lane.removeChild (i, &model.getUndoManager());
    }
    repaint (stepsArea());
}

bool ChannelRow::usesPianoRoll() const
{
    return laneUsesPianoRoll (model.getLane (pattern, getChannelId()),
                              (int) channel.getProperty (ids::rootNote, 60));
}

void ChannelRow::paintNoteGraph (juce::Graphics& g, juce::Rectangle<int> area) const
{
    const auto lane = model.getLane (pattern, getChannelId());
    const double patternTicks = juce::jmax (1, (int) pattern[ids::lengthTicks]);

    int lowKey = 127, highKey = 0;
    for (const auto note : lane)
    {
        lowKey  = juce::jmin (lowKey,  (int) note[ids::key]);
        highKey = juce::jmax (highKey, (int) note[ids::key]);
    }
    if (highKey <= lowKey)   // a single pitch gets an octave of headroom either way
    {
        lowKey  -= 6;
        highKey += 6;
    }

    const auto band = area.reduced (0, 4).toFloat();
    const float noteH = juce::jlimit (3.0f, 7.0f, band.getHeight() / (float) (highKey - lowKey + 1));
    const float travel = band.getHeight() - noteH;

    const juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (area);
    g.setColour (theme::noteFill);

    for (const auto note : lane)
    {
        const double start = (int) note[ids::startTicks];
        const double len   = juce::jmax (1, (int) note[ids::lengthTicks]);
        const float x = band.getX() + (float) (start / patternTicks) * band.getWidth();
        const float w = juce::jmax (2.0f, (float) (len / patternTicks) * band.getWidth());
        const float y = band.getBottom() - noteH
                        - (float) ((int) note[ids::key] - lowKey) / (float) (highKey - lowKey) * travel;
        g.fillRoundedRectangle (x, y, w, noteH, 1.0f);
    }
}

void ChannelRow::paint (juce::Graphics& g)
{
    const auto area = stepsArea();
    const int steps = numSteps();
    const bool pianoRoll = usesPianoRoll();

    for (int s = 0; s < steps; ++s)
    {
        auto cell = juce::Rectangle<int> (area.getX() + s * stepWidth, area.getY(),
                                          stepWidth, getHeight()).reduced (2, 4);

        const bool evenGroup = ((s / 4) % 2) == 0;
        const bool on = ! pianoRoll && isStepOn (s);

        juce::Colour c = on ? (evenGroup ? theme::stepOn : theme::stepOnDim)
                            : (evenGroup ? theme::stepEvenBg : theme::stepOddBg);
        if (s == playStep)
            c = c.brighter (on ? 0.35f : 0.12f);

        g.setColour (c);
        g.fillRoundedRectangle (cell.toFloat(), 3.0f);

        if (on)
        {
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.drawRoundedRectangle (cell.toFloat(), 3.0f, 1.0f);
        }
    }

    if (pianoRoll)
        paintNoteGraph (g, area.withWidth (steps * stepWidth));
}

void ChannelRow::resized()
{
    auto r = getLocalBounds();
    muteLed.setBounds (r.removeFromLeft (18).reduced (3, 9));
    r.removeFromLeft (4);
    nameButton.setBounds (r.removeFromLeft (118).reduced (0, 3));
    r.removeFromLeft (4);
    panKnob.setBounds (r.removeFromLeft (26).reduced (1));
    volKnob.setBounds (r.removeFromLeft (26).reduced (1));
    r.removeFromLeft (4);
    insertButton.setBounds (r.removeFromLeft (40).reduced (0, 5));
}

void ChannelRow::mouseDown (const juce::MouseEvent& e)
{
    // Events forwarded from the child buttons arrive with their coordinates,
    // so translate before deciding where the click landed.
    const auto pos = e.eventComponent == this ? e.getPosition()
                                              : e.getEventRelativeTo (this).getPosition();

    if (e.mods.isPopupMenu())
    {
        if (pos.x < fixedLeftWidth)
        {
            if (onSelected) onSelected (getChannelId());
            if (onWantsContextMenu) onWantsContextMenu (channel);
            return;
        }
    }
    else if (e.eventComponent != this)
    {
        return;   // let the button handle its own left-click
    }

    const int step = stepAt (pos);
    if (step < 0)
        return;

    if (usesPianoRoll())
    {
        if (onSelected) onSelected (getChannelId());
        if (onWantsPianoRoll) onWantsPianoRoll (channel);
        return;
    }

    dragPaintMode = e.mods.isPopupMenu() ? 0 : 1;
    undoGesture::begin (model, dragPaintMode == 1 ? "Paint steps" : "Erase steps");
    setStep (step, dragPaintMode == 1);
}

void ChannelRow::mouseDrag (const juce::MouseEvent& e)
{
    if (dragPaintMode < 0)
        return;
    const int step = stepAt (e.getPosition());
    if (step >= 0)
        setStep (step, dragPaintMode == 1);
}

void ChannelRow::mouseUp (const juce::MouseEvent&)
{
    if (dragPaintMode < 0)
        return;
    dragPaintMode = -1;
    undoGesture::end (model);
}
