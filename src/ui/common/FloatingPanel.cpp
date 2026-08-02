#include "FloatingPanel.h"
#include "Snapping.h"
#include "app/Theme.h"

// Translucent preview of the region a panel will occupy if dropped. Added to
// the desktop while dragging and removed on release.
class FloatingPanel::DockPreview : public juce::Component
{
public:
    DockPreview() { setInterceptsMouseClicks (false, false); }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (theme::accent.withAlpha (0.22f));
        g.fillRoundedRectangle (bounds, 5.0f);
        g.setColour (theme::accent.withAlpha (0.9f));
        g.drawRoundedRectangle (bounds, 5.0f, 2.0f);
    }
};


FloatingPanel::FloatingPanel (const juce::String& t, std::unique_ptr<juce::Component> c, bool resizable)
    : title (t), content (std::move (c))
{
    jassert (content != nullptr);
    addAndMakeVisible (*content);

    closeButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    closeButton.setTooltip ("Hide panel");
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
        // Grab any edge or corner to resize; the border only hit-tests within
        // its thickness, so the content underneath stays clickable.
        resizer = std::make_unique<juce::ResizableBorderComponent> (this, &constrainer);
        resizer->setBorderThickness (juce::BorderSize<int> (5));
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

void FloatingPanel::setTitleBarComponent (std::unique_ptr<juce::Component> extra)
{
    titleBarExtra = std::move (extra);
    if (titleBarExtra != nullptr)
        addAndMakeVisible (*titleBarExtra);
    resized();
}

void FloatingPanel::resized()
{
    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop (titleBarHeight);
    closeButton.setBounds (header.removeFromRight (titleBarHeight).reduced (4));
    if (titleBarExtra != nullptr)
        titleBarExtra->setBounds (header.removeFromRight (titleBarExtra->getWidth())
                                        .withSizeKeepingCentre (titleBarExtra->getWidth(),
                                                                titleBarHeight - 4));
    content->setBounds (bounds.reduced (1));

    if (resizer != nullptr)
        resizer->setBounds (getLocalBounds());
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

    // Hold shift to place freely, ignoring both docking and snap lines.
    constrainer.enabled = ! e.mods.isShiftDown();
    dragger.dragComponent (this, e, &constrainer);

    if (e.mods.isShiftDown())
    {
        pendingZone = docking::Zone::none;
        dockPreview = nullptr;
    }
    else
    {
        updateDockPreview (e);
    }
}

void FloatingPanel::updateDockPreview (const juce::MouseEvent& e)
{
    auto* parent = getParentComponent();
    if (parent == nullptr)
        return;

    const auto pointer = e.getEventRelativeTo (parent).getPosition();
    const auto zone = docking::zoneForPointer (pointer, parent->getLocalBounds());

    if (zone == pendingZone)
        return;
    pendingZone = zone;

    if (zone == docking::Zone::none)
    {
        dockPreview = nullptr;
        return;
    }

    if (dockPreview == nullptr)
    {
        dockPreview = std::make_unique<DockPreview>();
        parent->addAndMakeVisible (*dockPreview);
    }
    dockPreview->setBounds (docking::boundsForZone (zone, parent->getLocalBounds()));
    dockPreview->toFront (false);
    toFront (true);
}

void FloatingPanel::applyDock (docking::Zone zone)
{
    auto* parent = getParentComponent();
    if (parent == nullptr || zone == docking::Zone::none)
        return;

    if (dockedZone == docking::Zone::none)
        boundsBeforeDock = getBounds();

    const auto desktop = parent->getLocalBounds();
    const auto target = docking::boundsForZone (zone, desktop);

    // Push an existing occupant of the same region into the complementary one
    // rather than covering it, so the two tile instead of overlapping.
    const auto complement = docking::boundsForZone (docking::complementOf (zone), desktop);
    for (auto* sibling : parent->getChildren())
    {
        auto* other = dynamic_cast<FloatingPanel*> (sibling);
        if (other == nullptr || other == this || ! other->isVisible())
            continue;
        if (other->getBounds().getIntersection (target).getWidth() > target.getWidth() / 2
            && other->getBounds().getIntersection (target).getHeight() > target.getHeight() / 2)
        {
            if (other->dockedZone == docking::Zone::none)
                other->boundsBeforeDock = other->getBounds();
            other->dockedZone = docking::complementOf (zone);
            other->setBounds (complement);
        }
    }

    dockedZone = zone;
    setBounds (target);
}

void FloatingPanel::undock()
{
    if (dockedZone == docking::Zone::none || boundsBeforeDock.isEmpty())
        return;
    dockedZone = docking::Zone::none;
    setBounds (boundsBeforeDock);
}

void FloatingPanel::mouseUp (const juce::MouseEvent&)
{
    if (! draggingFromTitleBar)
        return;

    draggingFromTitleBar = false;
    constrainer.enabled = true;
    dockPreview = nullptr;

    if (pendingZone != docking::Zone::none)
    {
        applyDock (pendingZone);
        pendingZone = docking::Zone::none;
    }
    else
    {
        // Dragged out of its docked region, so it floats freely again.
        dockedZone = docking::Zone::none;
    }
    repaint();
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
