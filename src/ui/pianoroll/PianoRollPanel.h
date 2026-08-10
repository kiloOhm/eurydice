#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <vector>
#include "app/AppServices.h"
#include "NoteTools.h"

// FL-style piano roll for the selected channel's lane in the active pattern.
// Draw notes with left click (click-drag moves, right edge resizes),
// right-click deletes, cmd-drag marquee-selects. Velocity lane at the bottom,
// ghost notes from other channels, chord stamp + scale highlighting.
// Tool row: roll/ratchet, chop, glue and strum over the selection, also
// reachable by right-clicking a selected note.
class PianoRollPanel : public juce::Component,
                       private juce::ValueTree::Listener,
                       private juce::Timer
{
public:
    explicit PianoRollPanel (AppServices&);
    ~PianoRollPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    // Live-input feedback: lights the key on the keyboard column while a
    // MIDI/typing-piano note is held. Message thread.
    void setLiveKey (int key, bool on);

private:
    // --- geometry ---
    juce::Rectangle<int> headerArea() const;
    juce::Rectangle<int> keyboardArea() const;
    juce::Rectangle<int> gridArea() const;
    juce::Rectangle<int> velocityArea() const;

    double xToTicks (int x) const;
    int    ticksToX (double ticks) const;
    int    yToKey (int y) const;
    int    keyToY (int key) const;   // top of the key's row
    int    snapTicks() const;
    double snapDown (double ticks) const;

    // --- model access ---
    juce::ValueTree activePattern() const;
    juce::ValueTree currentLane (bool createIfMissing);
    int selectedChannelId() const;
    juce::ValueTree noteAt (juce::Point<int> gridPos, bool& overRightEdge);

    // Grows the pattern (whole bars) when a note runs past its end, so the
    // loop covers the note instead of cutting it off. Never shrinks.
    void growPatternToFitNotes();

    void addNoteAt (juce::Point<int> gridPos);
    void deleteNoteAt (juce::Point<int> gridPos);
    void deleteSelected();
    void preview (int key);

    // --- editing tools ---
    int rollDivisionTicks() const;
    notetools::Ramp velocityRamp() const;
    int strumOffsetTicks() const;

    std::vector<notetools::Note> selectedNotes() const;
    void replaceSelection (const std::vector<notetools::Note>& notes);
    void applyRoll (int divisionTicks);
    void applyChop();
    void applyGlue();
    void applyStrum (int offsetTicks);

    void showToolMenu();
    void handleToolMenu (int result);

    void paintGrid (juce::Graphics&);
    void paintNotes (juce::Graphics&);
    void paintKeyboard (juce::Graphics&);
    void paintVelocityLane (juce::Graphics&);
    void setVelocityAt (juce::Point<int> pos);

    bool isKeyInScale (int key) const;

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override {}
    void timerCallback() override;

    AppServices& services;
    juce::ValueTree observedRoot;

    // Horizontal zoom, keeping the tick under anchorX where it is.
    void zoomHorizontally (double factor, int anchorX);
    // Scales the view so the whole pattern spans the grid.
    void zoomToFitPattern();

    // header widgets
    juce::ComboBox snapBox, chordBox, scaleRootBox, scaleTypeBox;
    juce::Label targetLabel;
    juce::TextButton zoomOutButton { "-" }, zoomInButton { "+" }, zoomFitButton { "Fit" };

    // tool row
    juce::ComboBox rollDivBox, rampBox, strumBox;
    juce::TextButton rollButton { "Roll" }, chopButton { "Chop" }, glueButton { "Glue" },
                     strumBackButton { "<" }, strumForwardButton { ">" };

    // view state
    double pxPerTick = 24.0 / 240.0;
    int keyHeight = 12;
    double scrollTicks = 0.0;
    int scrollKeysY = 0;        // pixel scroll offset in grid Y
    int lastNoteLength = 240;

    // interaction state
    enum class Drag { none, create, move, resize, marquee, erase, velocity };
    Drag drag = Drag::none;
    juce::ValueTree dragNote;
    juce::Array<juce::ValueTree> selection;
    juce::Point<int> dragStart;
    juce::Rectangle<int> marqueeRect;
    int dragKeyOffset = 0;
    double dragTickOffset = 0.0;
    int lastPreviewKey = -1;

    double playheadTicks = -1.0;
    std::array<bool, 128> liveKeys {};   // held via MIDI/typing piano

    static constexpr int headerH = 62;   // two rows: view settings + tools
    static constexpr int headerRowH = 23;
    static constexpr int keyboardW = 64;
    static constexpr int velocityH = 78;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollPanel)
};
