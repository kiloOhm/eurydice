#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

// The constrainer configuration that keeps a panel reachable. Mirrors what
// FloatingPanel sets up, exercised without needing a real desktop.
namespace
{
constexpr int titleBarHeight = 24;

std::unique_ptr<juce::ComponentBoundsConstrainer> makePanelConstrainer()
{
    auto constrainer = std::make_unique<juce::ComponentBoundsConstrainer>();
    constrainer->setMinimumSize (240, 140);
    constrainer->setMinimumOnscreenAmounts (titleBarHeight, 80, titleBarHeight, 80);
    return constrainer;
}

// Applies the constrainer the way ComponentDragger does during a move.
juce::Rectangle<int> constrainMove (juce::Rectangle<int> target,
                                    juce::Rectangle<int> previous,
                                    juce::Rectangle<int> desktop)
{
    auto constrainer = makePanelConstrainer();
    constrainer->checkBounds (target, previous, desktop, false, false, false, false);
    return target;
}
}

TEST (PanelLayout, CannotBeDraggedOffTheLeft)
{
    const juce::Rectangle<int> desktop (0, 0, 1200, 800);
    const auto result = constrainMove ({ -4000, 100, 400, 300 }, { 100, 100, 400, 300 }, desktop);
    EXPECT_GT (result.getRight(), desktop.getX())
        << "some of the panel must remain inside the desktop";
}

TEST (PanelLayout, CannotBeDraggedOffTheBottom)
{
    const juce::Rectangle<int> desktop (0, 0, 1200, 800);
    const auto result = constrainMove ({ 100, 5000, 400, 300 }, { 100, 100, 400, 300 }, desktop);
    EXPECT_LT (result.getY(), desktop.getBottom())
        << "the title bar must stay reachable";
}

TEST (PanelLayout, CannotBeDraggedOffTheRight)
{
    const juce::Rectangle<int> desktop (0, 0, 1200, 800);
    const auto result = constrainMove ({ 9000, 100, 400, 300 }, { 100, 100, 400, 300 }, desktop);
    EXPECT_LT (result.getX(), desktop.getRight());
}

TEST (PanelLayout, NormalMoveIsUntouched)
{
    const juce::Rectangle<int> desktop (0, 0, 1200, 800);
    const juce::Rectangle<int> target (300, 200, 400, 300);
    EXPECT_EQ (constrainMove (target, { 100, 100, 400, 300 }, desktop), target);
}

TEST (PanelLayout, MinimumSizeEnforcedOnResize)
{
    juce::Rectangle<int> bounds (100, 100, 10, 10);
    auto constrainer = makePanelConstrainer();
    constrainer->checkBounds (bounds, { 100, 100, 400, 300 }, { 0, 0, 1200, 800 },
                              false, false, true, true);
    EXPECT_GE (bounds.getWidth(), 240);
    EXPECT_GE (bounds.getHeight(), 140);
}
