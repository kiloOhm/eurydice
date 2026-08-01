#include "FloatingPanel.h"
#include "Snapping.h"
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

    constrainer.setMinimumSize (240, 140);
    // Always keep the title bar reachable so a panel can't be lost off-screen.
    constrainer.setMinimumOnscreenAmounts (titleBarHeight, 80, titleBarHeight, 80);

    if (resizable)
    {
        resizer = std::make_unique<juce::ResizableCornerComponent> (this, &constrainer);
        addAndMakeVisible (*resizer);
    }

    setWantsKeyboardFocus (false);
}

FloatingPanel::~FloatingPanel() = default;

// ---------------- snapping ----------------

void FloatingPanel::gatherSnapLines (std::vector<int>& xLines, std::vector<int>& yLines) const
{
    auto* parent = getParentComponent();
    if (parent == nullptr)
        return;

    xLines.push_back (0);
    xLines.push_back (parent->getWidth());
    yLines.push_back (0);
    yLines.push_back (parent->getHeight());

    for (auto* sibling : parent->getChildren())
    {
        if (sibling == this || ! sibling->isVisible())
            continue;
        if (dynamic_cast<FloatingPanel*> (sibling) == nullptr)
            continue;

        const auto r = sibling->getBounds();
        xLines.push_back (r.getX());
        xLines.push_back (r.getRight());
        yLines.push_back (r.getY());
        yLines.push_back (r.getBottom());
    }
}

void FloatingPanel::SnappingConstrainer::checkBounds (juce::Rectangle<int>& bounds,
                                                      const juce::Rectangle<int>& previousBounds,
                                                      const juce::Rectangle<int>& limits,
                                                      bool isStretchingTop, bool isStretchingLeft,
                                                      bool isStretchingBottom, bool isStretchingRight)
{
    juce::ComponentBoundsConstrainer::checkBounds (bounds, previousBounds, limits,
                                                   isStretchingTop, isStretchingLeft,
                                                   isStretchingBottom, isStretchingRight);
    if (! enabled)
        return;

    std::vector<int> xLines, yLines;
    panel.gatherSnapLines (xLines, yLines);
    if (xLines.empty())
        return;

    snapping::apply (bounds, xLines, yLines, FloatingPanel::snapThreshold,
                     { isStretchingTop, isStretchingLeft, isStretchingBottom, isStretchingRight });
}

// ---------------- painting / layout ----------------

void FloatingPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    g.setColour (theme::panelBg);
    g.fillRoundedRectangle (bounds.toFloat(), 4.0f);

    auto header = bounds.removeFromTop (titleBarHeight);
    g.setColour (draggingFromTitleBar ? theme::accentDim : theme::panelHeader);
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

// ---------------- interaction ----------------

void FloatingPanel::mouseDown (const juce::MouseEvent& e)
{
    toFront (true);

    draggingFromTitleBar = e.getPosition().y < titleBarHeight;
    if (draggingFromTitleBar)
    {
        dragger.startDraggingComponent (this, e);
        repaint();
    }
}

void FloatingPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (! draggingFromTitleBar)
        return;

    // Hold shift to place freely, ignoring snap lines.
    constrainer.enabled = ! e.mods.isShiftDown();
    dragger.dragComponent (this, e, &constrainer);
}

void FloatingPanel::mouseUp (const juce::MouseEvent&)
{
    if (draggingFromTitleBar)
    {
        draggingFromTitleBar = false;
        constrainer.enabled = true;
        repaint();
    }
}

void FloatingPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.getPosition().y >= titleBarHeight)
        return;

    auto* parent = getParentComponent();
    if (parent == nullptr)
        return;

    // Double-click the title bar to fill the desktop, again to restore.
    if (getBounds() == parent->getLocalBounds())
    {
        if (! boundsBeforeMaximise.isEmpty())
            setBounds (boundsBeforeMaximise);
    }
    else
    {
        boundsBeforeMaximise = getBounds();
        setBounds (parent->getLocalBounds());
    }
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
    if (onVisibilityToggled) onVisibilityToggled();
}
