#include <gtest/gtest.h>
#include "app/AppServices.h"
#include "ui/pianoroll/PianoRollPanel.h"

namespace
{
// A project with one empty channel selected, plus the pixel maths the roll
// uses, so a test can aim at a note by key and tick.
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

    bool hasNote (int key, int startTicks)
    {
        for (const auto note : lane())
            if ((int) note[ids::key] == key && (int) note[ids::startTicks] == startTicks)
                return true;
        return false;
    }

    // Mirrors PianoRollPanel's default view: 24px per step, 12px per key row,
    // C5 (72) at the top of the grid.
    static constexpr int keyboardW = 64, headerH = 62, keyHeight = 12;

    juce::Point<int> pixelOf (int key, int ticks) const
    {
        return { keyboardW + (int) std::round (ticks * (24.0 / 240.0)) + 2,
                 headerH + (72 - key) * keyHeight + keyHeight / 2 };
    }

    ProjectModel& project;
    juce::ValueTree channel;
    PianoRollPanel panel;
};

juce::MouseEvent eventAt (juce::Component& component, juce::Point<int> pos,
                          juce::ModifierKeys mods, juce::Point<int> downPos)
{
    return { juce::Desktop::getInstance().getMainMouseSource(), pos.toFloat(), mods,
             juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
             juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
             juce::MouseInputSource::defaultTiltY, &component, &component, juce::Time(),
             downPos.toFloat(), juce::Time(), 1, false };
}

// A press, a drag and a release, as the roll sees them.
void dragOver (PianoRollPanel& panel, juce::Point<int> from, juce::Point<int> to,
               juce::ModifierKeys mods = {})
{
    panel.mouseDown (eventAt (panel, from, mods, from));
    panel.mouseDrag (eventAt (panel, to, mods, from));
    panel.mouseUp   (eventAt (panel, to, mods, from));
}

// A band across one key's row, starting in the empty row above it: a press
// that lands on a note grabs the note, whichever tool is active.
void lassoRow (Roll& roll, int key, juce::ModifierKeys mods = {})
{
    dragOver (roll.panel,
              { Roll::keyboardW + 1, roll.pixelOf (key + 1, 0).y },
              { roll.pixelOf (key, 6 * ids::ticksPerStep).x, roll.pixelOf (key, 0).y + 4 },
              mods);
}

const auto cmd = juce::ModifierKeys::commandModifier;
const auto shift = juce::ModifierKeys::shiftModifier;
} // namespace

// The Select tool: a plain drag over empty grid lassos, and Delete proves
// which notes it caught.
TEST (PianoRollSelection, SelectToolLassoesWithAPlainDrag)
{
    AppServices services (false);
    Roll roll (services);

    roll.addNote (60, 0);
    roll.addNote (60, 4 * ids::ticksPerStep);
    roll.addNote (48, 0);

    roll.panel.setTool (PianoRollPanel::Tool::select);
    lassoRow (roll, 60);   // wide enough to cover both of key 60's notes

    ASSERT_TRUE (roll.panel.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
    EXPECT_FALSE (roll.hasNote (60, 0));
    EXPECT_FALSE (roll.hasNote (60, 4 * ids::ticksPerStep));
    EXPECT_TRUE (roll.hasNote (48, 0)) << "the lasso reached outside its rows";
}

// ...and it must not paint notes while doing so.
TEST (PianoRollSelection, SelectToolDoesNotDrawNotes)
{
    AppServices services (false);
    Roll roll (services);

    roll.panel.setTool (PianoRollPanel::Tool::select);
    dragOver (roll.panel, roll.pixelOf (60, 0), roll.pixelOf (60, 4 * ids::ticksPerStep));

    EXPECT_EQ (roll.lane().getNumChildren(), 0) << "the lasso drew a note";
}

// The Draw tool keeps painting: the same drag writes a note.
TEST (PianoRollSelection, DrawToolStillPaints)
{
    AppServices services (false);
    Roll roll (services);

    dragOver (roll.panel, roll.pixelOf (60, 0), roll.pixelOf (60, 4 * ids::ticksPerStep));

    EXPECT_EQ (roll.lane().getNumChildren(), 1);
    EXPECT_TRUE (roll.hasNote (60, 4 * ids::ticksPerStep))
        << "the painted note did not follow the drag";
}

// Cmd-drag reaches the lasso from the Draw tool, as it always has.
TEST (PianoRollSelection, CommandDragStillLassoesFromTheDrawTool)
{
    AppServices services (false);
    Roll roll (services);

    roll.addNote (60, 0);
    roll.addNote (48, 0);

    lassoRow (roll, 60, cmd);

    ASSERT_TRUE (roll.panel.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
    EXPECT_FALSE (roll.hasNote (60, 0));
    EXPECT_TRUE (roll.hasNote (48, 0));
    EXPECT_EQ (roll.lane().getNumChildren(), 1) << "cmd-drag drew a note instead of lassoing";
}

// Shift-lasso builds a selection up over several passes.
TEST (PianoRollSelection, ShiftLassoKeepsTheEarlierSelection)
{
    AppServices services (false);
    Roll roll (services);

    roll.addNote (60, 0);
    roll.addNote (48, 0);
    roll.addNote (36, 0);

    roll.panel.setTool (PianoRollPanel::Tool::select);
    lassoRow (roll, 60);
    lassoRow (roll, 48, shift);

    ASSERT_TRUE (roll.panel.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
    EXPECT_FALSE (roll.hasNote (60, 0)) << "the first pass was dropped";
    EXPECT_FALSE (roll.hasNote (48, 0));
    EXPECT_TRUE (roll.hasNote (36, 0));
}

// Removing a note calls back into the panel to drop it from the selection, so
// a delete loop walking that same array used to leave every second note behind.
TEST (PianoRollSelection, DeleteTakesEveryNoteInTheSelection)
{
    AppServices services (false);
    Roll roll (services);

    for (int i = 0; i < 6; ++i)
        roll.addNote (60 + i, i * ids::ticksPerStep);

    ASSERT_TRUE (roll.panel.keyPressed (juce::KeyPress ('a', cmd, 0)));
    ASSERT_TRUE (roll.panel.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));

    EXPECT_EQ (roll.lane().getNumChildren(), 0) << "notes survived the delete";
}

// Dragging a note still moves it under the Select tool.
TEST (PianoRollSelection, SelectToolStillMovesNotes)
{
    AppServices services (false);
    Roll roll (services);

    roll.addNote (60, 0);
    roll.panel.setTool (PianoRollPanel::Tool::select);
    dragOver (roll.panel, roll.pixelOf (60, 0), roll.pixelOf (62, 4 * ids::ticksPerStep));

    EXPECT_EQ (roll.lane().getNumChildren(), 1);
    EXPECT_TRUE (roll.hasNote (62, 4 * ids::ticksPerStep)) << "the note did not follow the drag";
}
