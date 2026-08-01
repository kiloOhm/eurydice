#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// An FL-style internal window: draggable title bar, close button (hides),
// resizable via bottom-right corner. Lives inside the main desktop area.
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

    juce::Component* getContent() const noexcept { return content.get(); }

    void toggleVisibility();
    void bringToFrontAndShow();

    std::function<void()> onVisibilityToggled;

    static constexpr int titleBarHeight = 24;

private:
    juce::String title;
    std::unique_ptr<juce::Component> content;
    juce::TextButton closeButton { "x" };
    juce::ComponentDragger dragger;
    juce::ComponentBoundsConstrainer constrainer;
    std::unique_ptr<juce::ResizableCornerComponent> resizer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FloatingPanel)
};
