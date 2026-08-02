#include <gtest/gtest.h>
#include "ui/common/AutomatableSlider.h"

// The bug these guard: juce::Slider consumes right-clicks (so a context menu
// never appears) and, for linear styles, snaps its value to the click position
// on the way down (so opening one would move the fader).
namespace
{
juce::MouseEvent eventAt (juce::Component& target, juce::Point<float> position,
                          juce::ModifierKeys mods)
{
    return { juce::Desktop::getInstance().getMainMouseSource(), position, mods,
             juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
             juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
             juce::MouseInputSource::defaultTiltY, &target, &target, juce::Time::getCurrentTime(),
             position, juce::Time::getCurrentTime(), 1, false };
}

struct SliderFixture : ::testing::Test
{
    AutomatableSlider slider;
    int menuCount = 0;

    void SetUp() override
    {
        slider.setSliderStyle (juce::Slider::LinearVertical);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.setRange (0.0, 1.0, 0.001);
        slider.setSize (20, 200);
        slider.setValue (0.8, juce::dontSendNotification);
        slider.onContextMenu = [this] { ++menuCount; };
    }
};
}

TEST_F (SliderFixture, RightClickRaisesTheMenuAndLeavesTheValueAlone)
{
    const auto before = slider.getValue();
    const auto e = eventAt (slider, { 10.0f, 10.0f },
                            juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier));

    slider.mouseDown (e);
    slider.mouseDrag (e);
    slider.mouseUp (e);

    EXPECT_EQ (menuCount, 1);
    EXPECT_DOUBLE_EQ (slider.getValue(), before) << "the right-click moved the fader";
}

TEST_F (SliderFixture, LeftClickStillDragsAndRaisesNoMenu)
{
    // Near the top of a vertical slider, so the absolute drag lands high.
    const auto e = eventAt (slider, { 10.0f, 4.0f },
                            juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier));

    slider.mouseDown (e);
    slider.mouseUp (e);

    EXPECT_EQ (menuCount, 0);
    EXPECT_GT (slider.getValue(), 0.9) << "left-click no longer drags the fader";
}

TEST_F (SliderFixture, RotaryStyleAlsoRoutesTheRightButton)
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.mouseDown (eventAt (slider, { 10.0f, 10.0f },
                               juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier)));
    EXPECT_EQ (menuCount, 1);
}

TEST_F (SliderFixture, NoHandlerIsHarmless)
{
    slider.onContextMenu = nullptr;
    const auto before = slider.getValue();
    slider.mouseDown (eventAt (slider, { 10.0f, 10.0f },
                               juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier)));
    EXPECT_DOUBLE_EQ (slider.getValue(), before);
}
