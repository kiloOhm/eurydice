#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// A slider that leaves the right mouse button to its owner, so every
// automatable control can hang an automation menu off it.
//
// Listening in on the slider is not enough: juce::Slider consumes the click
// and, for linear styles, snaps the value to the click position on the way
// down. The right button has to be kept away from Slider entirely.
class AutomatableSlider : public juce::Slider
{
public:
    std::function<void()> onContextMenu;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu())
            juce::Slider::mouseDown (e);
        else if (onContextMenu)
            onContextMenu();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu())
            juce::Slider::mouseDrag (e);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu())
            juce::Slider::mouseUp (e);
    }
};
