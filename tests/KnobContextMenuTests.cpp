#include <gtest/gtest.h>
#include "model/ChannelParams.h"
#include "ui/common/LabelledKnob.h"
#include "ui/rack/ChannelRow.h"

// Right-clicking a knob has to reach the panel that owns it. These drive the
// real rack row rather than the slider in isolation, because the reported bug
// was that the click died somewhere between the two.
namespace
{
juce::MouseEvent rightClickOn (juce::Component& target)
{
    const juce::Point<float> position (2.0f, 2.0f);
    return { juce::Desktop::getInstance().getMainMouseSource(), position,
             juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier),
             juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
             juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
             juce::MouseInputSource::defaultTiltY, &target, &target, juce::Time::getCurrentTime(),
             position, juce::Time::getCurrentTime(), 1, false };
}

// The knobs sit inside ChannelRow::fixedLeftWidth; findChildWithID would need
// ids, so pick them out by the slider type in layout order (pan, then volume).
std::vector<juce::Slider*> slidersOf (juce::Component& parent)
{
    std::vector<juce::Slider*> out;
    for (auto* child : parent.getChildren())
        if (auto* slider = dynamic_cast<juce::Slider*> (child))
            out.push_back (slider);
    return out;
}
}

TEST (KnobContextMenu, RackRowKnobsReportTheirProperty)
{
    ProjectModel model;
    ChannelRow row (model, model.getChannel (0));
    row.setSize (600, ChannelRow::rowHeight);
    row.resized();

    std::vector<juce::Identifier> seen;
    row.onKnobContextMenu = [&seen] (juce::ValueTree, juce::Identifier prop) { seen.push_back (prop); };

    auto sliders = slidersOf (row);
    ASSERT_EQ (sliders.size(), 2u) << "expected a pan and a volume knob";
    for (auto* slider : sliders)
        slider->mouseDown (rightClickOn (*slider));

    ASSERT_EQ (seen.size(), 2u) << "the right-click never reached the row";
    EXPECT_EQ (seen[0], ids::pan);
    EXPECT_EQ (seen[1], ids::volume);
}

TEST (KnobContextMenu, RackRowKnobMovesAreReportedForRecording)
{
    ProjectModel model;
    ChannelRow row (model, model.getChannel (0));

    int moves = 0;
    row.onKnobMoved = [&moves] (juce::ValueTree, juce::Identifier) { ++moves; };

    auto sliders = slidersOf (row);
    ASSERT_EQ (sliders.size(), 2u);
    sliders[1]->setValue (0.42, juce::sendNotificationSync);

    EXPECT_EQ (moves, 1);
    EXPECT_NEAR ((double) model.getChannel (0)[ids::volume], 0.42, 1.0e-9);
}

TEST (KnobContextMenu, LabelledKnobRaisesItsMenuAndResets)
{
    ProjectModel model;
    auto channel = model.getChannel (0);
    const auto* descriptor = channelparams::find ("sampler", "cutoff");
    ASSERT_NE (descriptor, nullptr);

    LabelledKnob knob (descriptor->caption, model, channel, descriptor->id, descriptor->range,
                       descriptor->defaultValue, descriptor->suffix, descriptor->decimals);
    knob.setSize (LabelledKnob::preferredWidth, LabelledKnob::preferredHeight);
    knob.resized();

    double menuValue = -1.0;
    knob.onContextMenu = [&menuValue] (double value) { menuValue = value; };

    auto sliders = slidersOf (knob);
    ASSERT_EQ (sliders.size(), 1u);
    sliders[0]->setValue (900.0, juce::sendNotificationSync);
    sliders[0]->mouseDown (rightClickOn (*sliders[0]));

    EXPECT_NEAR (menuValue, 900.0, 1.0);
    EXPECT_GT (descriptor->toNormalised (menuValue), 0.0);
    EXPECT_LT (descriptor->toNormalised (menuValue), 1.0);

    knob.resetToDefault();
    EXPECT_NEAR ((double) channel[descriptor->id], descriptor->defaultValue, 1.0e-9);
}
