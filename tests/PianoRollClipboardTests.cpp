#include <gtest/gtest.h>
#include "app/AppServices.h"
#include "ui/pianoroll/PianoRollPanel.h"

namespace
{
const auto cmd = juce::ModifierKeys::commandModifier;

// A project with one empty channel selected, so the roll edits a lane that
// nothing else has written to.
struct Roll
{
    explicit Roll (AppServices& services) : project (services.project), panel (services)
    {
        channel = project.addChannel ("sampler", "Lead");
        project.getRoot().setProperty (ids::selectedChannel, (int) channel[ids::id], nullptr);
        panel.setSize (800, 500);
        panel.resized();
    }

    juce::ValueTree lane()
    {
        return project.getOrCreateLane (project.getPatternById (project.getRoot()[ids::activePattern]),
                                        (int) channel[ids::id]);
    }

    void addNote (int key, int startTicks, int lengthTicks = ids::ticksPerStep)
    {
        project.addNote (lane(), key, startTicks, lengthTicks);
    }

    // Notes in the lane as (key, start) pairs, sorted, for order-free compares.
    std::vector<std::pair<int, int>> notes()
    {
        std::vector<std::pair<int, int>> result;
        for (const auto note : lane())
            result.emplace_back ((int) note[ids::key], (int) note[ids::startTicks]);
        std::sort (result.begin(), result.end());
        return result;
    }

    ProjectModel& project;
    juce::ValueTree channel;
    PianoRollPanel panel;
};

void selectAllAndCopy (PianoRollPanel& panel)
{
    ASSERT_TRUE (panel.keyPressed (juce::KeyPress ('a', cmd, 0)));
    ASSERT_TRUE (panel.keyPressed (juce::KeyPress ('c', cmd, 0)));
}
} // namespace

// The core round trip: copy a chord, paste it, and the notes come back with
// their pitches and relative timing intact.
TEST (PianoRollClipboard, CopyThenPasteReproducesTheNotes)
{
    AppServices services (false);
    Roll roll (services);

    roll.addNote (60, 0);
    roll.addNote (64, 0);
    roll.addNote (67, ids::ticksPerStep);

    selectAllAndCopy (roll.panel);
    ASSERT_TRUE (roll.panel.keyPressed (juce::KeyPress ('v', cmd, 0)));

    // Pasted one block on: the block spans two steps, snap is one step.
    const auto expected = std::vector<std::pair<int, int>> {
        { 60, 0 }, { 60, 2 * ids::ticksPerStep },
        { 64, 0 }, { 64, 2 * ids::ticksPerStep },
        { 67, ids::ticksPerStep }, { 67, 3 * ids::ticksPerStep },
    };
    EXPECT_EQ (roll.notes(), expected);
}

// Velocity and pan are part of the note, so a copy has to carry them.
TEST (PianoRollClipboard, PasteKeepsVelocityAndPan)
{
    AppServices services (false);
    Roll roll (services);

    roll.project.addNote (roll.lane(), 60, 0, ids::ticksPerStep, 0.31, -0.5);

    selectAllAndCopy (roll.panel);
    ASSERT_TRUE (roll.panel.keyPressed (juce::KeyPress ('v', cmd, 0)));

    ASSERT_EQ (roll.lane().getNumChildren(), 2);
    const auto pasted = roll.lane().getChild (1);
    EXPECT_DOUBLE_EQ ((double) pasted[ids::velocity], 0.31);
    EXPECT_DOUBLE_EQ ((double) pasted[ids::notePan], -0.5);
}

// Repeated pastes march along instead of stacking on the same tick.
TEST (PianoRollClipboard, RepeatedPasteAdvances)
{
    AppServices services (false);
    Roll roll (services);

    roll.addNote (60, 0);
    selectAllAndCopy (roll.panel);
    roll.panel.keyPressed (juce::KeyPress ('v', cmd, 0));
    roll.panel.keyPressed (juce::KeyPress ('v', cmd, 0));

    const auto expected = std::vector<std::pair<int, int>> {
        { 60, 0 }, { 60, ids::ticksPerStep }, { 60, 2 * ids::ticksPerStep },
    };
    EXPECT_EQ (roll.notes(), expected);
}

// Cut clears the notes but keeps them on the clipboard.
TEST (PianoRollClipboard, CutRemovesTheNotesAndPasteBringsThemBack)
{
    AppServices services (false);
    Roll roll (services);

    roll.addNote (72, 4 * ids::ticksPerStep);
    ASSERT_TRUE (roll.panel.keyPressed (juce::KeyPress ('a', cmd, 0)));
    ASSERT_TRUE (roll.panel.keyPressed (juce::KeyPress ('x', cmd, 0)));
    EXPECT_EQ (roll.lane().getNumChildren(), 0) << "cut left notes behind";

    ASSERT_TRUE (roll.panel.keyPressed (juce::KeyPress ('v', cmd, 0)));
    ASSERT_EQ (roll.lane().getNumChildren(), 1);
    EXPECT_EQ ((int) roll.lane().getChild (0)[ids::key], 72);
}

// The point of the clipboard: lift a part off one channel onto another.
TEST (PianoRollClipboard, PasteFollowsTheSelectedChannel)
{
    AppServices services (false);
    Roll roll (services);

    roll.addNote (60, 0);
    roll.addNote (63, ids::ticksPerStep);
    selectAllAndCopy (roll.panel);

    auto other = services.project.addChannel ("sampler", "Pad");
    services.project.getRoot().setProperty (ids::selectedChannel, (int) other[ids::id], nullptr);

    ASSERT_TRUE (roll.panel.keyPressed (juce::KeyPress ('v', cmd, 0)));

    auto pattern = services.project.getPatternById (services.project.getRoot()[ids::activePattern]);
    auto target = services.project.getLane (pattern, (int) other[ids::id]);
    ASSERT_TRUE (target.isValid()) << "paste did not create the lane";
    EXPECT_EQ (target.getNumChildren(), 2);
    EXPECT_EQ (roll.lane().getNumChildren(), 2) << "the source lane changed";
}

// Duplicate is the same shift as paste, but leaves the clipboard alone.
TEST (PianoRollClipboard, DuplicateOffsetsWithoutTouchingTheClipboard)
{
    AppServices services (false);
    Roll roll (services);

    roll.addNote (60, 0);
    selectAllAndCopy (roll.panel);

    // Something else copied since; duplicating must not paste it.
    roll.addNote (48, 8 * ids::ticksPerStep);
    ASSERT_TRUE (roll.panel.keyPressed (juce::KeyPress ('a', cmd, 0)));
    ASSERT_TRUE (roll.panel.keyPressed (juce::KeyPress ('d', cmd, 0)));

    const auto expected = std::vector<std::pair<int, int>> {
        { 48, 8 * ids::ticksPerStep }, { 48, 17 * ids::ticksPerStep },
        { 60, 0 }, { 60, 9 * ids::ticksPerStep },
    };
    EXPECT_EQ (roll.notes(), expected);
}

// Paste is one undo step, however many notes it wrote.
TEST (PianoRollClipboard, PasteUndoesInOneStep)
{
    AppServices services (false);
    Roll roll (services);

    roll.addNote (60, 0);
    roll.addNote (64, 0);
    selectAllAndCopy (roll.panel);
    ASSERT_TRUE (roll.panel.keyPressed (juce::KeyPress ('v', cmd, 0)));
    ASSERT_EQ (roll.lane().getNumChildren(), 4);

    services.project.getUndoManager().undo();
    EXPECT_EQ (roll.lane().getNumChildren(), 2) << "undo did not take the whole paste";
}

// An empty clipboard must not write anything, and the keys must not fall
// through to the typing piano while the roll has focus.
TEST (PianoRollClipboard, ClipboardKeysAreClaimedAndPasteIsSafeWhenEmpty)
{
    AppServices services (false);
    Roll roll (services);

    for (const auto key : { 'c', 'x', 'v', 'd' })
        EXPECT_TRUE (roll.panel.keyPressed (juce::KeyPress (key, cmd, 0)))
            << "Cmd " << key << " fell through to the rest of the app";

    EXPECT_EQ (roll.lane().getNumChildren(), 0);
}
