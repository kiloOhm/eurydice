#include "EurydiceLookAndFeel.h"
#include "Theme.h"

EurydiceLookAndFeel::EurydiceLookAndFeel()
{
    auto& s = getCurrentColourScheme();
    s.setUIColour (juce::LookAndFeel_V4::ColourScheme::windowBackground, theme::panelBg);
    s.setUIColour (juce::LookAndFeel_V4::ColourScheme::widgetBackground, theme::raised);
    s.setUIColour (juce::LookAndFeel_V4::ColourScheme::menuBackground,   theme::panelHeader);
    s.setUIColour (juce::LookAndFeel_V4::ColourScheme::outline,          theme::outline);
    s.setUIColour (juce::LookAndFeel_V4::ColourScheme::defaultText,      theme::textPrimary);
    s.setUIColour (juce::LookAndFeel_V4::ColourScheme::defaultFill,      theme::accent);
    s.setUIColour (juce::LookAndFeel_V4::ColourScheme::highlightedText,  juce::Colours::black);
    s.setUIColour (juce::LookAndFeel_V4::ColourScheme::highlightedFill,  theme::accent);
    s.setUIColour (juce::LookAndFeel_V4::ColourScheme::menuText,         theme::textPrimary);

    setColour (juce::ResizableWindow::backgroundColourId, theme::windowBg);
    setColour (juce::TextButton::buttonColourId,          theme::raised);
    setColour (juce::TextButton::buttonOnColourId,        theme::accentDim);
    setColour (juce::TextButton::textColourOffId,         theme::textPrimary);
    setColour (juce::TextButton::textColourOnId,          theme::textPrimary);
    setColour (juce::Label::textColourId,                 theme::textPrimary);
    setColour (juce::Slider::backgroundColourId,          theme::sunken);
    setColour (juce::Slider::trackColourId,               theme::accentDim);
    setColour (juce::Slider::thumbColourId,               theme::accent);
    setColour (juce::Slider::textBoxTextColourId,         theme::textPrimary);
    setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
    setColour (juce::PopupMenu::backgroundColourId,       theme::panelHeader);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::accentDim);
    setColour (juce::TextEditor::backgroundColourId,      theme::sunken);
    setColour (juce::TextEditor::textColourId,            theme::textPrimary);
    setColour (juce::TextEditor::outlineColourId,         theme::outlineLight);
    setColour (juce::TextEditor::focusedOutlineColourId,  theme::accent);
    setColour (juce::ComboBox::backgroundColourId,        theme::raised);
    setColour (juce::ComboBox::textColourId,              theme::textPrimary);
    setColour (juce::ComboBox::outlineColourId,           theme::outline);
    setColour (juce::ComboBox::arrowColourId,             theme::textDim);
    setColour (juce::ListBox::backgroundColourId,         theme::sunken);
    setColour (juce::TreeView::backgroundColourId,        juce::Colours::transparentBlack);
    setColour (juce::ScrollBar::thumbColourId,            theme::outlineLight);
    setColour (juce::TooltipWindow::backgroundColourId,   theme::panelHeader);
    setColour (juce::TooltipWindow::textColourId,         theme::textPrimary);
    setColour (juce::AlertWindow::backgroundColourId,     theme::panelBg);
    setColour (juce::AlertWindow::textColourId,           theme::textPrimary);
}

void EurydiceLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                            float pos, float startAngle, float endAngle,
                                            juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    auto centre = bounds.getCentre();
    auto angle  = startAngle + pos * (endAngle - startAngle);

    // Body
    g.setColour (theme::sunken);
    g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour (theme::outlineLight);
    g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.0f);

    // Value arc
    juce::Path arc;
    arc.addCentredArc (centre.x, centre.y, radius - 1.0f, radius - 1.0f, 0.0f,
                       startAngle, angle, true);
    g.setColour (slider.isEnabled() ? theme::accent : theme::textFaint);
    g.strokePath (arc, juce::PathStrokeType (2.0f));

    // Pointer
    juce::Path pointer;
    pointer.addRoundedRectangle (-1.2f, -radius + 3.0f, 2.4f, radius * 0.55f, 1.0f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre));
    g.setColour (theme::textPrimary);
    g.fillPath (pointer);
}

void EurydiceLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                const juce::Colour& backgroundColour,
                                                bool highlighted, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto base = backgroundColour;
    if (down)             base = base.darker (0.3f);
    else if (highlighted) base = base.brighter (0.15f);

    g.setColour (base);
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (bounds, 3.0f, 1.0f);
}

juce::Font EurydiceLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return theme::uiFont (juce::jmin (13.0f, (float) buttonHeight * 0.65f));
}

juce::Font EurydiceLookAndFeel::getPopupMenuFont()  { return theme::uiFont (13.0f); }
juce::Font EurydiceLookAndFeel::getLabelFont (juce::Label& l) { return theme::uiFont (juce::jmin (13.0f, (float) l.getHeight() * 0.7f)); }

void EurydiceLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y,
                                         int width, int height, bool vertical,
                                         int thumbStart, int thumbSize, bool over, bool down)
{
    juce::Rectangle<int> thumb = vertical ? juce::Rectangle<int> (x + 2, thumbStart, width - 4, thumbSize)
                                          : juce::Rectangle<int> (thumbStart, y + 2, thumbSize, height - 4);
    g.setColour (down ? theme::accentDim : (over ? theme::outlineLight.brighter (0.2f) : theme::outlineLight));
    g.fillRoundedRectangle (thumb.toFloat(), 3.0f);
}
