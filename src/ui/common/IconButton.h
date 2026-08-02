#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "app/Theme.h"

// Vector icons for the app chrome, drawn in a 0..1 square and scaled to fit
// the button. Kept as plain paths so they render crisply at any size.
namespace icons
{
inline juce::Path play()
{
    juce::Path p;
    p.addTriangle (0.05f, 0.0f, 1.0f, 0.5f, 0.05f, 1.0f);
    return p;
}

inline juce::Path stop()
{
    juce::Path p;
    p.addRoundedRectangle (0.08f, 0.08f, 0.84f, 0.84f, 0.08f);
    return p;
}

inline juce::Path record()
{
    juce::Path p;
    p.addEllipse (0.05f, 0.05f, 0.9f, 0.9f);
    return p;
}

// Clip blocks of differing lengths on stacked tracks.
inline juce::Path playlist()
{
    juce::Path p;
    p.addRoundedRectangle (0.00f, 0.06f, 0.62f, 0.20f, 0.05f);
    p.addRoundedRectangle (0.30f, 0.40f, 0.70f, 0.20f, 0.05f);
    p.addRoundedRectangle (0.10f, 0.74f, 0.52f, 0.20f, 0.05f);
    return p;
}

// A 4x2 step grid.
inline juce::Path rack()
{
    juce::Path p;
    for (int row = 0; row < 2; ++row)
        for (int col = 0; col < 4; ++col)
            p.addRoundedRectangle ((float) col * 0.26f, 0.18f + (float) row * 0.38f, 0.20f, 0.26f, 0.04f);
    return p;
}

// Three white keys with two black keys.
inline juce::Path piano()
{
    juce::Path p;
    for (int i = 0; i < 3; ++i)
        p.addRectangle ((float) i * 0.35f, 0.0f, 0.30f, 1.0f);
    p.addRectangle (0.24f, 0.0f, 0.17f, 0.55f);
    p.addRectangle (0.59f, 0.0f, 0.17f, 0.55f);
    p.setUsingNonZeroWinding (false);   // black keys punch through
    return p;
}

// Three fader tracks with caps at different heights.
inline juce::Path mixer()
{
    juce::Path p;
    const float caps[3] = { 0.55f, 0.20f, 0.70f };
    for (int i = 0; i < 3; ++i)
    {
        const float x = 0.08f + (float) i * 0.36f;
        p.addRectangle (x + 0.05f, 0.0f, 0.06f, 1.0f);      // track
        p.addRoundedRectangle (x, caps[i], 0.16f, 0.18f, 0.04f);   // cap
    }
    return p;
}

// Folder.
inline juce::Path browser()
{
    juce::Path p;
    p.startNewSubPath (0.0f, 0.2f);
    p.lineTo (0.36f, 0.2f);
    p.lineTo (0.46f, 0.34f);
    p.lineTo (1.0f, 0.34f);
    p.lineTo (1.0f, 0.92f);
    p.lineTo (0.0f, 0.92f);
    p.closeSubPath();
    return p;
}
} // namespace icons

// Flat icon button for the app chrome. Fills a rounded background when
// toggled on (accent) or hovered, and draws the icon path centred at a
// consistent optical size.
class IconButton : public juce::Button
{
public:
    IconButton (const juce::String& name, juce::Path iconPath,
                juce::Colour toggleOnColour = theme::accentDim)
        : juce::Button (name), icon (std::move (iconPath)), onColour (toggleOnColour)
    {
        setWantsKeyboardFocus (false);
    }

    // Overrides the automatic (toggle-based) icon colour; pass a transparent
    // colour to go back to automatic.
    void setIconColour (juce::Colour c)
    {
        if (iconColour != c)
        {
            iconColour = c;
            repaint();
        }
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto bounds = getLocalBounds().toFloat();

        if (getToggleState())
        {
            g.setColour (onColour.withAlpha (down ? 1.0f : 0.9f));
            g.fillRoundedRectangle (bounds, 4.0f);
        }
        else if (highlighted || down)
        {
            g.setColour (theme::raised);
            g.fillRoundedRectangle (bounds, 4.0f);
        }

        const float side = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.52f;
        auto scaled = icon;
        scaled.applyTransform (icon.getTransformToScaleToFit (
            bounds.withSizeKeepingCentre (side, side), true));

        auto colour = iconColour;
        if (colour == juce::Colours::transparentBlack)
            colour = getToggleState() ? theme::textPrimary
                   : highlighted      ? theme::textPrimary
                                      : theme::textDim;
        g.setColour (colour.withAlpha (isEnabled() ? 1.0f : 0.4f));
        g.fillPath (scaled);
    }

private:
    juce::Path icon;
    juce::Colour onColour;
    juce::Colour iconColour = juce::Colours::transparentBlack;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IconButton)
};
