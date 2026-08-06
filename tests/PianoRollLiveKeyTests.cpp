#include <gtest/gtest.h>
#include "app/AppServices.h"
#include "ui/pianoroll/PianoRollPanel.h"

// Live MIDI/typing input lights its key on the roll's keyboard column.
TEST (PianoRollLiveKey, HeldKeyChangesTheKeyboardPixels)
{
    AppServices services (false);
    PianoRollPanel panel (services);
    panel.setSize (800, 500);
    panel.resized();

    auto snapshotKeyboard = [&panel]
    {
        // The keyboard column is the strip left of the grid, below the header.
        return panel.createComponentSnapshot ({ 0, 70, 64, 400 });
    };

    auto idle = snapshotKeyboard();
    panel.setLiveKey (60, true);
    auto held = snapshotKeyboard();
    panel.setLiveKey (60, false);
    auto released = snapshotKeyboard();

    auto pixelsDiffer = [] (const juce::Image& a, const juce::Image& b)
    {
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (a.getPixelAt (x, y) != b.getPixelAt (x, y))
                    return true;
        return false;
    };

    EXPECT_TRUE (pixelsDiffer (idle, held)) << "held key did not light up";
    EXPECT_FALSE (pixelsDiffer (idle, released)) << "released key stayed lit";
}
