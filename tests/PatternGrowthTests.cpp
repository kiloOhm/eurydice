#include <gtest/gtest.h>
#include "app/AppServices.h"
#include "ui/pianoroll/PianoRollPanel.h"

// Drawing past the end of a pattern should extend the pattern, in whole bars,
// so the loop plays the note instead of cutting it off.
namespace
{
juce::ValueTree firstPattern (AppServices& services)
{
    return services.project.getPattern (0);
}

// Drags a note out from x0 to x1 on the given row of the roll's grid.
void drawNote (PianoRollPanel& panel, int x0, int x1, int y)
{
    const juce::ModifierKeys left (juce::ModifierKeys::leftButtonModifier);
    juce::MouseEvent down (juce::Desktop::getInstance().getMainMouseSource(),
                           juce::Point<float> ((float) x0, (float) y), left,
                           1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                           &panel, &panel, juce::Time::getCurrentTime(),
                           juce::Point<float> ((float) x0, (float) y),
                           juce::Time::getCurrentTime(), 1, false);
    panel.mouseDown (down);

    juce::MouseEvent drag (juce::Desktop::getInstance().getMainMouseSource(),
                           juce::Point<float> ((float) x1, (float) y), left,
                           1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                           &panel, &panel, juce::Time::getCurrentTime(),
                           juce::Point<float> ((float) x0, (float) y),
                           juce::Time::getCurrentTime(), 1, false);
    panel.mouseDrag (drag);
    panel.mouseUp (drag);
}
} // namespace

TEST (PatternGrowth, StartsAtOneBar)
{
    AppServices services (false);
    EXPECT_EQ ((int) firstPattern (services)[ids::lengthTicks], ids::ticksPerBar)
        << "a fresh pattern should be one bar";
}

TEST (PatternGrowth, NoteDrawnPastTheEndExtendsThePattern)
{
    AppServices services (false);
    PianoRollPanel panel (services);
    panel.setSize (1200, 500);
    panel.resized();

    auto pattern = firstPattern (services);
    const int before = pattern[ids::lengthTicks];

    // Default zoom is 0.1 px/tick, so the grid starts one bar in and the drag
    // runs well past it.
    drawNote (panel, 64 + 40, 64 + 700, 200);

    const int after = pattern[ids::lengthTicks];
    EXPECT_GT (after, before) << "drawing past the loop did not extend the pattern";
    EXPECT_EQ (after % ids::ticksPerBar, 0) << "pattern length should stay on whole bars";
}

TEST (PatternGrowth, PatternCoversTheLongestNote)
{
    AppServices services (false);
    PianoRollPanel panel (services);
    panel.setSize (1200, 500);
    panel.resized();

    auto pattern = firstPattern (services);
    drawNote (panel, 64 + 40, 64 + 700, 200);

    double lastEnd = 0.0;
    for (auto lane : pattern)
    {
        if (! lane.hasType (ids::LANE))
            continue;
        for (auto note : lane)
            lastEnd = juce::jmax (lastEnd, (double) note[ids::startTicks]
                                               + (double) note[ids::lengthTicks]);
    }

    EXPECT_GE ((double) (int) pattern[ids::lengthTicks], lastEnd)
        << "the loop still cuts the note off";
}

TEST (PatternGrowth, ShortNotesLeaveTheLengthAlone)
{
    AppServices services (false);
    PianoRollPanel panel (services);
    panel.setSize (1200, 500);
    panel.resized();

    auto pattern = firstPattern (services);
    const int before = pattern[ids::lengthTicks];

    drawNote (panel, 64 + 10, 64 + 30, 200);   // well inside the first bar

    EXPECT_EQ ((int) pattern[ids::lengthTicks], before)
        << "a note inside the pattern must not resize it";
}
