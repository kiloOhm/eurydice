#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// An FL-style internal window: draggable title bar, close button (hides),
// resizable via the bottom-right corner, with magnetic snapping to the
// desktop edges and to other panels while moving or resizing.
class FloatingPanel : public juce::Component
{
public:
    FloatingPanel (const juce::String& title, std::unique_ptr<juce::Component> content,
                   bool resizable = true);
    ~FloatingPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    juce::Component* getContent() const noexcept { return content.get(); }

    void toggleVisibility();
    void bringToFrontAndShow();

    std::function<void()> onVisibilityToggled;

    static constexpr int titleBarHeight = 24;
    static constexpr int snapThreshold = 12;

private:
    // Snaps edges to the parent and to sibling panels, for both moves and
    // resizes. Lives here so the dragger and the resizer share one behaviour.
    struct SnappingConstrainer : juce::ComponentBoundsConstrainer
    {
        explicit SnappingConstrainer (FloatingPanel& p) : panel (p) {}

        void checkBounds (juce::Rectangle<int>& bounds,
                          const juce::Rectangle<int>& previousBounds,
                          const juce::Rectangle<int>& limits,
                          bool isStretchingTop, bool isStretchingLeft,
                          bool isStretchingBottom, bool isStretchingRight) override;

        FloatingPanel& panel;
        bool enabled = true;
    };

    void gatherSnapLines (std::vector<int>& xLines, std::vector<int>& yLines) const;

    juce::String title;
    std::unique_ptr<juce::Component> content;
    juce::TextButton closeButton { "x" };
    juce::ComponentDragger dragger;
    SnappingConstrainer constrainer { *this };
    std::unique_ptr<juce::ResizableCornerComponent> resizer;

    // Set on mouse-down in the title bar and held until mouse-up. Must not be
    // recomputed from the event: MouseEvent::getMouseDownPosition() is
    // relative to the component's *current* position, which moves as we drag.
    bool draggingFromTitleBar = false;
    juce::Rectangle<int> boundsBeforeMaximise;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FloatingPanel)
};
