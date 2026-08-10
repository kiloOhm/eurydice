#include <gtest/gtest.h>
#include "app/AppServices.h"
#include "ui/pianoroll/PianoRollPanel.h"

namespace
{
const auto cmd = juce::ModifierKeys::commandModifier;

bool pixelsDiffer (const juce::Image& a, const juce::Image& b)
{
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt (x, y) != b.getPixelAt (x, y))
                return true;
    return false;
}
} // namespace

// Cmd +/- and Cmd 0 drive the horizontal zoom without touching the mouse.
TEST (PianoRollZoom, KeyboardShortcutsChangeTheGrid)
{
    AppServices services (false);
    PianoRollPanel panel (services);
    panel.setSize (800, 500);
    panel.resized();

    // The grid, right of the keyboard column and below the header.
    auto grid = [&panel] { return panel.createComponentSnapshot ({ 64, 70, 700, 300 }); };

    const auto start = grid();

    ASSERT_TRUE (panel.keyPressed (juce::KeyPress ('+', cmd, 0))) << "Cmd + was not handled";
    const auto zoomedIn = grid();
    EXPECT_TRUE (pixelsDiffer (start, zoomedIn)) << "zooming in did not change the grid";

    ASSERT_TRUE (panel.keyPressed (juce::KeyPress ('-', cmd, 0))) << "Cmd - was not handled";
    const auto backOut = grid();
    EXPECT_FALSE (pixelsDiffer (start, backOut))
        << "zooming in then out did not return to the original scale";
}

// '=' is the unshifted '+' key, so it has to zoom in as well.
TEST (PianoRollZoom, EqualsKeyAlsoZoomsIn)
{
    AppServices services (false);
    PianoRollPanel panel (services);
    panel.setSize (800, 500);
    panel.resized();

    EXPECT_TRUE (panel.keyPressed (juce::KeyPress ('=', cmd, 0))) << "Cmd = was not handled";
}

// Fit scales the view so the pattern spans the grid, whatever the zoom was.
TEST (PianoRollZoom, FitIsIndependentOfTheStartingZoom)
{
    AppServices services (false);
    PianoRollPanel panel (services);
    panel.setSize (800, 500);
    panel.resized();

    auto grid = [&panel] { return panel.createComponentSnapshot ({ 64, 70, 700, 300 }); };

    ASSERT_TRUE (panel.keyPressed (juce::KeyPress ('0', cmd, 0))) << "Cmd 0 was not handled";
    const auto fitted = grid();

    // Wander off, then fit again: the view must land in the same place.
    panel.keyPressed (juce::KeyPress ('+', cmd, 0));
    panel.keyPressed (juce::KeyPress ('+', cmd, 0));
    EXPECT_TRUE (pixelsDiffer (fitted, grid())) << "zooming after a fit changed nothing";

    panel.keyPressed (juce::KeyPress ('0', cmd, 0));
    EXPECT_FALSE (pixelsDiffer (fitted, grid())) << "fit is not repeatable";
}

// Keys the roll does not claim must fall through to the rest of the app.
TEST (PianoRollZoom, UnrelatedKeysAreNotSwallowed)
{
    AppServices services (false);
    PianoRollPanel panel (services);
    panel.setSize (800, 500);
    panel.resized();

    EXPECT_FALSE (panel.keyPressed (juce::KeyPress ('k', cmd, 0)));
    EXPECT_FALSE (panel.keyPressed (juce::KeyPress ('+', juce::ModifierKeys(), 0)))
        << "plain + should still reach the typing piano";
}
