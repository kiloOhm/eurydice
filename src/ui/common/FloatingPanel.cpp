#include "FloatingPanel.h"
#include "app/Theme.h"

FloatingPanel::FloatingPanel (const juce::String& t, std::unique_ptr<juce::Component> c, bool resizable)
    : title (t), content (std::move (c))
{
    jassert (content != nullptr);
    addAndMakeVisible (*content);

    closeButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    closeButton.setWantsKeyboardFocus (false);
    closeButton.onClick = [this]
    {
        setVisible (false);
        if (onVisibilityToggled) onVisibilityToggled();
    };
    addAndMakeVisible (closeButton);

    if (resizable)
    {
        constrainer.setMinimumSize (240, 140);
        resizer = std::make_unique<juce::ResizableCornerComponent> (this, &constrainer);
        addAndMakeVisible (*resizer);
    }

    setWantsKeyboardFocus (false);
}

FloatingPanel::~FloatingPanel() = default;

void FloatingPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    g.setColour (theme::panelBg);
    g.fillRoundedRectangle (bounds.toFloat(), 4.0f);

    auto header = bounds.removeFromTop (titleBarHeight);
    g.setColour (theme::panelHeader);
    g.fillRoundedRectangle (header.toFloat(), 4.0f);
    g.fillRect (header.withTop (header.getBottom() - 4));

    g.setColour (theme::textPrimary);
    g.setFont (theme::uiFont (12.5f, true));
    g.drawText (title, header.reduced (10, 0), juce::Justification::centredLeft);

    g.setColour (theme::outline);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f);
}

void FloatingPanel::resized()
{
    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop (titleBarHeight);
    closeButton.setBounds (header.removeFromRight (titleBarHeight).reduced (4));
    content->setBounds (bounds.reduced (1));

    if (resizer != nullptr)
        resizer->setBounds (getWidth() - 14, getHeight() - 14, 14, 14);
}

void FloatingPanel::mouseDown (const juce::MouseEvent& e)
{
    toFront (true);
    if (e.getPosition().y < titleBarHeight)
        dragger.startDraggingComponent (this, e);
}

void FloatingPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (e.getMouseDownPosition().y < titleBarHeight)
        dragger.dragComponent (this, e, &constrainer);
}

void FloatingPanel::toggleVisibility()
{
    if (isVisible())
        setVisible (false);
    else
        bringToFrontAndShow();

    if (onVisibilityToggled) onVisibilityToggled();
}

void FloatingPanel::bringToFrontAndShow()
{
    setVisible (true);
    toFront (true);
}
