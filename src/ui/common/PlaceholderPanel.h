#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "app/Theme.h"

// Temporary stand-in while the real panels are built out.
class PlaceholderPanel : public juce::Component
{
public:
    explicit PlaceholderPanel (const juce::String& name) : label (name) {}

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::panelBg);
        g.setColour (theme::textFaint);
        g.setFont (theme::uiFont (16.0f, true));
        g.drawText (label + " — coming up", getLocalBounds(), juce::Justification::centred);
    }

private:
    juce::String label;
};
