#include "PianoRollPanel.h"
#include "app/Theme.h"
#include "model/UndoGesture.h"
#include <algorithm>

namespace
{
const int scaleIntervals[][7] = {
    { 0, 2, 4, 5, 7, 9, 11 },   // Major
    { 0, 2, 3, 5, 7, 8, 10 },   // Natural minor
    { 0, 2, 3, 5, 7, 8, 11 },   // Harmonic minor
    { 0, 2, 4, 7, 9, 0, 0 },    // Major pentatonic (5)
    { 0, 3, 5, 7, 10, 0, 0 },   // Minor pentatonic (5)
};
const int scaleSizes[] = { 7, 7, 7, 5, 5 };

const int chordIntervalsMajor[]  = { 0, 4, 7 };
const int chordIntervalsMinor[]  = { 0, 3, 7 };
const int chordIntervalsMaj7[]   = { 0, 4, 7, 11 };
const int chordIntervalsMin7[]   = { 0, 3, 7, 10 };
const int chordIntervalsDom7[]   = { 0, 4, 7, 10 };

struct Division { const char* name; int ticks; };
const Division rollDivisions[] = {
    { "1/8",   480 }, { "1/16",  240 }, { "1/32",  120 }, { "1/64",   60 },
    { "1/8T",  320 }, { "1/16T", 160 }, { "1/32T",  80 },
};
constexpr int numRollDivisions = (int) std::size (rollDivisions);

// Draw and Select are one either/or pair.
constexpr int toolRadioGroup = 1701;

// Tool menu ids.
enum ToolMenu
{
    menuRoll = 1, menuChop, menuGlue, menuStrumForward, menuStrumBack, menuDelete,
    menuCut, menuCopy, menuDuplicate,
    menuRampFlat = 10, menuRampRising, menuRampFalling,
    menuRollDivisionBase = 100
};

// First start and last end of a set of notes: the block a copy or duplicate
// moves around. Empty input gives an empty range at 0.
juce::Range<int> noteSpan (const std::vector<notetools::Note>& notes)
{
    if (notes.empty())
        return {};

    int first = notes.front().startTicks, last = first;
    for (const auto& note : notes)
    {
        first = std::min (first, note.startTicks);
        last  = std::max (last, note.startTicks + note.lengthTicks);
    }
    return { first, last };
}

// Reading order, so pasted notes land in the lane the way they were drawn.
void sortByTimeThenPitch (std::vector<notetools::Note>& notes)
{
    std::sort (notes.begin(), notes.end(), [] (const notetools::Note& a, const notetools::Note& b)
    {
        if (a.startTicks != b.startTicks)
            return a.startTicks < b.startTicks;
        return a.key < b.key;
    });
}
}

PianoRollPanel::PianoRollPanel (AppServices& s)
    : services (s)
{
    observedRoot = services.project.getRoot();
    observedRoot.addListener (this);

    setWantsKeyboardFocus (true);

    drawToolButton.setTooltip ("Draw tool: click or drag on the grid to paint notes");
    selectToolButton.setTooltip ("Select tool: drag over empty grid to lasso notes "
                                 "(Cmd-drag does the same from the Draw tool), "
                                 "double-click to draw one");
    for (auto* button : { &drawToolButton, &selectToolButton })
    {
        button->setClickingTogglesState (true);
        button->setRadioGroupId (toolRadioGroup);
        button->setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    }
    drawToolButton.setToggleState (true, juce::dontSendNotification);
    drawToolButton.onClick   = [this] { setTool (Tool::draw); };
    selectToolButton.onClick = [this] { setTool (Tool::select); };

    snapBox.addItem ("Snap: Step",     240);
    snapBox.addItem ("Snap: 1/2 Step", 120);
    snapBox.addItem ("Snap: 1/3 Step",  80);
    snapBox.addItem ("Snap: Beat",     960);
    snapBox.addItem ("Snap: Bar",     3840);
    snapBox.addItem ("Snap: None",       1);
    snapBox.setSelectedId (240, juce::dontSendNotification);
    addAndMakeVisible (snapBox);

    chordBox.addItem ("Chord: None",  1);
    chordBox.addItem ("Chord: Major", 2);
    chordBox.addItem ("Chord: Minor", 3);
    chordBox.addItem ("Chord: Maj7",  4);
    chordBox.addItem ("Chord: Min7",  5);
    chordBox.addItem ("Chord: Dom7",  6);
    chordBox.setSelectedId (1, juce::dontSendNotification);
    addAndMakeVisible (chordBox);

    static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    scaleRootBox.addItem ("Key: Off", 1);
    for (int i = 0; i < 12; ++i)
        scaleRootBox.addItem (noteNames[i], i + 2);
    scaleRootBox.setSelectedId (1, juce::dontSendNotification);
    scaleRootBox.onChange = [this] { repaint(); };
    addAndMakeVisible (scaleRootBox);

    scaleTypeBox.addItem ("Major", 1);
    scaleTypeBox.addItem ("Minor", 2);
    scaleTypeBox.addItem ("Harm. minor", 3);
    scaleTypeBox.addItem ("Penta maj", 4);
    scaleTypeBox.addItem ("Penta min", 5);
    scaleTypeBox.setSelectedId (1, juce::dontSendNotification);
    scaleTypeBox.onChange = [this] { repaint(); };
    addAndMakeVisible (scaleTypeBox);

    targetLabel.setFont (theme::uiFont (12.0f, true));
    targetLabel.setColour (juce::Label::textColourId, theme::secondary);
    addAndMakeVisible (targetLabel);

    for (const auto& division : rollDivisions)
        rollDivBox.addItem (juce::String ("Roll: ") + division.name, division.ticks);
    rollDivBox.setSelectedId (240, juce::dontSendNotification);
    rollDivBox.setTooltip ("Note repeat division");
    addAndMakeVisible (rollDivBox);

    rampBox.addItem ("Ramp: Flat",    1);
    rampBox.addItem ("Ramp: Rising",  2);
    rampBox.addItem ("Ramp: Falling", 3);
    rampBox.setSelectedId (1, juce::dontSendNotification);
    rampBox.setTooltip ("Velocity shape across a roll");
    addAndMakeVisible (rampBox);

    for (const int ticks : { 5, 10, 20, 40, 80 })
        strumBox.addItem ("Strum: " + juce::String (ticks) + "t", ticks);
    strumBox.setSelectedId (20, juce::dontSendNotification);
    strumBox.setTooltip ("Offset applied per note when strumming");
    addAndMakeVisible (strumBox);

    rollButton.setTooltip ("Repeat each selected note at the roll division");
    rollButton.onClick = [this] { applyRoll (rollDivisionTicks()); };
    chopButton.setTooltip ("Split selected notes at the snap division");
    chopButton.onClick = [this] { applyChop(); };
    glueButton.setTooltip ("Merge touching or overlapping selected notes");
    glueButton.onClick = [this] { applyGlue(); };
    strumBackButton.setTooltip ("Strum earlier");
    strumBackButton.onClick = [this] { applyStrum (-strumOffsetTicks()); };
    strumForwardButton.setTooltip ("Strum later");
    strumForwardButton.onClick = [this] { applyStrum (strumOffsetTicks()); };

    zoomOutButton.setTooltip ("Zoom out horizontally (Cmd -, or Cmd-wheel over the grid)");
    zoomOutButton.onClick = [this] { zoomHorizontally (1.0 / 1.3, gridArea().getCentreX()); };
    zoomInButton.setTooltip ("Zoom in horizontally (Cmd +, or Cmd-wheel over the grid)");
    zoomInButton.onClick = [this] { zoomHorizontally (1.3, gridArea().getCentreX()); };
    zoomFitButton.setTooltip ("Fit the whole pattern in the window (Cmd 0)");
    zoomFitButton.onClick = [this] { zoomToFitPattern(); };

    for (auto* button : { &drawToolButton, &selectToolButton,
                          &rollButton, &chopButton, &glueButton, &strumBackButton, &strumForwardButton,
                          &zoomOutButton, &zoomInButton, &zoomFitButton })
    {
        button->setWantsKeyboardFocus (false);
        addAndMakeVisible (button);
    }

    scrollKeysY = (127 - 72) * keyHeight;   // start around C5 at the top
    startTimerHz (30);
}

PianoRollPanel::~PianoRollPanel()
{
    observedRoot.removeListener (this);
}

// ---------- geometry ----------

juce::Rectangle<int> PianoRollPanel::headerArea() const   { return getLocalBounds().withHeight (headerH); }
juce::Rectangle<int> PianoRollPanel::keyboardArea() const
{
    return { 0, headerH, keyboardW, getHeight() - headerH - velocityH };
}
juce::Rectangle<int> PianoRollPanel::gridArea() const
{
    return { keyboardW, headerH, getWidth() - keyboardW, getHeight() - headerH - velocityH };
}
juce::Rectangle<int> PianoRollPanel::velocityArea() const
{
    return { keyboardW, getHeight() - velocityH, getWidth() - keyboardW, velocityH };
}

double PianoRollPanel::xToTicks (int x) const
{
    return scrollTicks + (x - keyboardW) / pxPerTick;
}
int PianoRollPanel::ticksToX (double ticks) const
{
    return keyboardW + (int) std::round ((ticks - scrollTicks) * pxPerTick);
}
int PianoRollPanel::yToKey (int y) const
{
    return 127 - (y - headerH + scrollKeysY) / keyHeight;
}
int PianoRollPanel::keyToY (int key) const
{
    return headerH + (127 - key) * keyHeight - scrollKeysY;
}
int PianoRollPanel::snapTicks() const
{
    return juce::jmax (1, snapBox.getSelectedId());
}
double PianoRollPanel::snapDown (double ticks) const
{
    const int s = snapTicks();
    return std::floor (juce::jmax (0.0, ticks) / s) * s;
}

// ---------- model ----------

juce::ValueTree PianoRollPanel::activePattern() const
{
    return services.project.getPatternById (observedRoot[ids::activePattern]);
}

int PianoRollPanel::selectedChannelId() const
{
    const int id = observedRoot[ids::selectedChannel];
    if (services.project.getChannelById (id).isValid())
        return id;
    if (services.project.numChannels() > 0)
        return services.project.getChannel (0)[ids::id];
    return -1;
}

juce::ValueTree PianoRollPanel::currentLane (bool createIfMissing)
{
    auto pattern = activePattern();
    const int chId = selectedChannelId();
    if (! pattern.isValid() || chId < 0)
        return {};
    return createIfMissing ? services.project.getOrCreateLane (pattern, chId)
                           : services.project.getLane (pattern, chId);
}

juce::ValueTree PianoRollPanel::noteAt (juce::Point<int> pos, bool& overRightEdge)
{
    overRightEdge = false;
    auto lane = currentLane (false);
    if (! lane.isValid())
        return {};

    const int key = yToKey (pos.y);
    // Iterate backwards so the most recently added (topmost) wins.
    for (int i = lane.getNumChildren(); --i >= 0;)
    {
        auto note = lane.getChild (i);
        if ((int) note[ids::key] != key)
            continue;
        const int x0 = ticksToX ((int) note[ids::startTicks]);
        const int x1 = ticksToX ((int) note[ids::startTicks] + (int) note[ids::lengthTicks]);
        if (pos.x >= x0 && pos.x <= x1)
        {
            overRightEdge = (x1 - pos.x) <= 6;
            return note;
        }
    }
    return {};
}

void PianoRollPanel::preview (int key)
{
    const int chId = selectedChannelId();
    if (chId >= 0)
        services.engine.previewNote (chId, key, 0.8f, 250);
}

// Drawing or dragging a note past the end of the pattern extends the pattern
// to cover it, rounded up to a whole bar. Shrinking stays manual: deleting the
// last note should not silently retune the loop length.
void PianoRollPanel::growPatternToFitNotes()
{
    auto pattern = activePattern();
    if (! pattern.isValid())
        return;

    double lastEnd = 0.0;
    for (auto lane : pattern)
    {
        if (! lane.hasType (ids::LANE))
            continue;
        for (auto note : lane)
            lastEnd = juce::jmax (lastEnd, (double) note[ids::startTicks]
                                               + (double) note[ids::lengthTicks]);
    }

    const int current = juce::jmax (1, (int) pattern[ids::lengthTicks]);
    if (lastEnd <= (double) current)
        return;

    const int bars = juce::jmax (1, (int) std::ceil (lastEnd / (double) ids::ticksPerBar));
    pattern.setProperty (ids::lengthTicks, bars * ids::ticksPerBar,
                         &services.project.getUndoManager());
}

void PianoRollPanel::addNoteAt (juce::Point<int> pos)
{
    auto lane = currentLane (true);
    if (! lane.isValid())
        return;

    lanes::markEditedWithPianoRoll (lane);

    const int key = juce::jlimit (0, 127, yToKey (pos.y));
    const int start = (int) snapDown (xToTicks (pos.x));

    auto& model = services.project;
    dragNote = model.addNote (lane, key, start, lastNoteLength);
    selection.clearQuick();
    selection.add (dragNote);

    // Chord stamp adds the extra intervals as independent notes.
    const int chord = chordBox.getSelectedId();
    if (chord > 1)
    {
        const int* intervals = nullptr; int n = 0;
        switch (chord)
        {
            case 2: intervals = chordIntervalsMajor; n = 3; break;
            case 3: intervals = chordIntervalsMinor; n = 3; break;
            case 4: intervals = chordIntervalsMaj7;  n = 4; break;
            case 5: intervals = chordIntervalsMin7;  n = 4; break;
            case 6: intervals = chordIntervalsDom7;  n = 4; break;
            default: break;
        }
        for (int i = 1; i < n; ++i)   // 0 is the clicked note itself
            if (key + intervals[i] <= 127)
                selection.add (model.addNote (lane, key + intervals[i], start, lastNoteLength));
    }

    preview (key);
    repaint();
}

void PianoRollPanel::deleteNoteAt (juce::Point<int> pos)
{
    bool edge;
    if (auto note = noteAt (pos, edge); note.isValid())
    {
        auto lane = currentLane (false);
        selection.removeAllInstancesOf (note);
        services.project.removeNote (lane, note);
        repaint();
    }
}

void PianoRollPanel::removeSelectedNotes()
{
    auto lane = currentLane (false);
    if (! lane.isValid())
        return;

    // Take a copy and empty the selection first: removing a note calls back
    // into valueTreeChildRemoved, which drops it from `selection` — shifting
    // the array out from under an iterator walking it, so every second note
    // used to survive.
    const auto doomed = selection;
    selection.clearQuick();

    for (const auto& note : doomed)
        services.project.removeNote (lane, note);
}

void PianoRollPanel::deleteSelected()
{
    const undoGesture::Scoped step (services.project, "Delete notes");
    removeSelectedNotes();
    repaint();
}

// ---------- clipboard ----------

void PianoRollPanel::copySelection()
{
    if (selection.isEmpty())
        return;

    auto notes = selectedNotes();
    sortByTimeThenPitch (notes);
    const auto span = noteSpan (notes);

    for (auto& note : notes)
        note.startTicks -= span.getStart();

    auto& clip = clipboard();
    clip.notes = std::move (notes);
    clip.spanTicks = juce::jmax (1, span.getLength());
    clip.originTicks = span.getStart();
}

void PianoRollPanel::cutSelection()
{
    if (selection.isEmpty())
        return;

    copySelection();
    const undoGesture::Scoped step (services.project, "Cut notes");
    removeSelectedNotes();
    repaint();
}

// Where a paste lands: under the pointer when it is over the grid, so the
// mouse aims the block, and otherwise one block-length on from the last paste,
// so repeated Cmd-V from the keyboard marches along instead of stacking up.
int PianoRollPanel::pasteAnchorTicks() const
{
    if (isMouseOver (true))
    {
        const auto pos = getMouseXYRelative();
        if (gridArea().contains (pos))
            return (int) snapDown (xToTicks (pos.x));
    }

    const auto& clip = clipboard();
    const int step = snapTicks();
    const int span = (clip.spanTicks + step - 1) / step * step;   // whole snap steps
    return clip.originTicks + juce::jmax (step, span);
}

void PianoRollPanel::insertNotes (const std::vector<notetools::Note>& notes, int anchorTicks)
{
    if (notes.empty())
        return;

    auto lane = currentLane (true);
    if (! lane.isValid())
        return;

    lanes::markEditedWithPianoRoll (lane);

    auto& model = services.project;
    selection.clearQuick();
    for (const auto& note : notes)
        selection.add (model.addNote (lane,
                                      juce::jlimit (0, 127, note.key),
                                      juce::jmax (0, anchorTicks + note.startTicks),
                                      juce::jmax (1, note.lengthTicks),
                                      note.velocity, note.pan));

    // A paste past the loop end takes the pattern with it, as drawing does.
    growPatternToFitNotes();
    repaint();
}

void PianoRollPanel::pasteClipboard()
{
    auto& clip = clipboard();
    if (clip.isEmpty())
        return;

    const int anchor = pasteAnchorTicks();
    const undoGesture::Scoped step (services.project, "Paste notes");
    insertNotes (clip.notes, anchor);
    clip.originTicks = anchor;
}

// Cmd-D: a copy right after the selection, without disturbing the clipboard.
void PianoRollPanel::duplicateSelection()
{
    if (selection.isEmpty())
        return;

    auto notes = selectedNotes();
    sortByTimeThenPitch (notes);
    const auto span = noteSpan (notes);
    for (auto& note : notes)
        note.startTicks -= span.getStart();

    const int step = snapTicks();
    const int length = (span.getLength() + step - 1) / step * step;

    const undoGesture::Scoped gesture (services.project, "Duplicate notes");
    insertNotes (notes, span.getStart() + juce::jmax (step, length));
}

// ---------- editing tools ----------

int PianoRollPanel::rollDivisionTicks() const
{
    return juce::jmax (1, rollDivBox.getSelectedId());
}

notetools::Ramp PianoRollPanel::velocityRamp() const
{
    switch (rampBox.getSelectedId())
    {
        case 2:  return notetools::Ramp::rising;
        case 3:  return notetools::Ramp::falling;
        default: return notetools::Ramp::flat;
    }
}

int PianoRollPanel::strumOffsetTicks() const
{
    return juce::jmax (1, strumBox.getSelectedId());
}

std::vector<notetools::Note> PianoRollPanel::selectedNotes() const
{
    std::vector<notetools::Note> notes;
    notes.reserve ((size_t) selection.size());
    for (const auto& tree : selection)
        notes.push_back ({ (int) tree[ids::key],
                           (int) tree[ids::startTicks],
                           (int) tree[ids::lengthTicks],
                           (double) tree[ids::velocity],
                           (double) tree[ids::notePan] });
    return notes;
}

// Swaps the selection for a freshly built set of notes. The caller opens the
// undo transaction, so the whole swap collapses into one step.
void PianoRollPanel::replaceSelection (const std::vector<notetools::Note>& notes)
{
    lanes::markEditedWithPianoRoll (currentLane (true));

    auto lane = currentLane (false);
    if (! lane.isValid())
        return;

    auto& model = services.project;
    const auto replaced = selection;
    selection.clearQuick();

    for (const auto& tree : replaced)
        model.removeNote (lane, tree);
    for (const auto& note : notes)
        selection.add (model.addNote (lane, note.key, note.startTicks, note.lengthTicks,
                                      note.velocity, note.pan));

    // Rolls and strums can push notes past the loop; take the pattern with it.
    growPatternToFitNotes();

    // Close the batch so the next edit cannot merge into this undo step.
    undoGesture::end (services.project);
    repaint();
}

void PianoRollPanel::applyRoll (int divisionTicks)
{
    if (selection.isEmpty())
        return;
    undoGesture::begin (services.project, "Roll notes");
    replaceSelection (notetools::rollAll (selectedNotes(), divisionTicks, velocityRamp()));
}

void PianoRollPanel::applyChop()
{
    if (selection.isEmpty())
        return;
    undoGesture::begin (services.project, "Chop notes");
    replaceSelection (notetools::chopAll (selectedNotes(), snapTicks()));
}

void PianoRollPanel::applyGlue()
{
    if (selection.isEmpty())
        return;
    undoGesture::begin (services.project, "Glue notes");
    replaceSelection (notetools::glue (selectedNotes()));
}

void PianoRollPanel::applyStrum (int offsetTicks)
{
    if (selection.isEmpty())
        return;

    const undoGesture::Scoped step (services.project, "Strum notes");
    auto& undo = services.project.getUndoManager();

    const auto strummed = notetools::strum (selectedNotes(), offsetTicks);
    for (int i = 0; i < selection.size(); ++i)
        selection.getReference (i).setProperty (ids::startTicks, strummed[(size_t) i].startTicks, &undo);

    repaint();
}

void PianoRollPanel::showToolMenu()
{
    juce::PopupMenu divisionMenu;
    for (int i = 0; i < numRollDivisions; ++i)
        divisionMenu.addItem (menuRollDivisionBase + i, rollDivisions[i].name, true,
                              rollDivisions[i].ticks == rollDivisionTicks());

    juce::PopupMenu rampMenu;
    rampMenu.addItem (menuRampFlat,    "Flat",    true, velocityRamp() == notetools::Ramp::flat);
    rampMenu.addItem (menuRampRising,  "Rising",  true, velocityRamp() == notetools::Ramp::rising);
    rampMenu.addItem (menuRampFalling, "Falling", true, velocityRamp() == notetools::Ramp::falling);

    const auto strum = juce::String (strumOffsetTicks()) + " ticks";

    juce::PopupMenu menu;
    menu.addSectionHeader (juce::String (selection.size()) + " note(s) selected");
    menu.addItem (menuRoll, "Roll " + rollDivBox.getText().fromFirstOccurrenceOf (" ", false, false));
    menu.addSubMenu ("Roll at", divisionMenu);
    menu.addSubMenu ("Velocity ramp", rampMenu);
    menu.addSeparator();
    menu.addItem (menuChop, "Chop at snap");
    menu.addItem (menuGlue, "Glue", selection.size() > 1);
    menu.addSeparator();
    menu.addItem (menuStrumForward, "Strum later (" + strum + ")", selection.size() > 1);
    menu.addItem (menuStrumBack,    "Strum earlier (" + strum + ")", selection.size() > 1);
    menu.addSeparator();
    menu.addItem (menuCut,       "Cut");
    menu.addItem (menuCopy,      "Copy");
    menu.addItem (menuDuplicate, "Duplicate");
    menu.addSeparator();
    menu.addItem (menuDelete, "Delete");

    menu.showMenuAsync (juce::PopupMenu::Options().withMinimumWidth (180),
        [safe = juce::Component::SafePointer<PianoRollPanel> (this)] (int result)
        {
            if (safe != nullptr)
                safe->handleToolMenu (result);
        });
}

void PianoRollPanel::handleToolMenu (int result)
{
    if (result >= menuRollDivisionBase)
    {
        const int index = result - menuRollDivisionBase;
        if (index < numRollDivisions)
        {
            rollDivBox.setSelectedId (rollDivisions[index].ticks, juce::dontSendNotification);
            applyRoll (rollDivisions[index].ticks);
        }
        return;
    }

    switch (result)
    {
        case menuRoll:         applyRoll (rollDivisionTicks()); break;
        case menuChop:         applyChop(); break;
        case menuGlue:         applyGlue(); break;
        case menuStrumForward: applyStrum (strumOffsetTicks()); break;
        case menuStrumBack:    applyStrum (-strumOffsetTicks()); break;
        case menuCut:          cutSelection(); break;
        case menuCopy:         copySelection(); break;
        case menuDuplicate:    duplicateSelection(); break;
        case menuDelete:       deleteSelected(); break;
        case menuRampFlat:     rampBox.setSelectedId (1, juce::dontSendNotification); break;
        case menuRampRising:   rampBox.setSelectedId (2, juce::dontSendNotification); break;
        case menuRampFalling:  rampBox.setSelectedId (3, juce::dontSendNotification); break;
        default: break;
    }
}

// ---------- painting ----------

void PianoRollPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelBg);
    paintGrid (g);
    paintNotes (g);
    paintKeyboard (g);
    paintVelocityLane (g);

    // header background
    g.setColour (theme::panelHeader);
    g.fillRect (headerArea());
    g.setColour (theme::outline);
    g.drawHorizontalLine (headerH - 1, 0.0f, (float) getWidth());

    if (drag == Drag::marquee)
    {
        g.setColour (theme::secondary.withAlpha (0.15f));
        g.fillRect (marqueeRect);
        g.setColour (theme::secondary);
        g.drawRect (marqueeRect);
    }
}

bool PianoRollPanel::isKeyInScale (int key) const
{
    const int rootSel = scaleRootBox.getSelectedId();
    if (rootSel <= 1)
        return false;
    const int root = rootSel - 2;
    const int type = juce::jlimit (1, 5, scaleTypeBox.getSelectedId()) - 1;
    const int rel = ((key - root) % 12 + 12) % 12;
    for (int i = 0; i < scaleSizes[type]; ++i)
        if (scaleIntervals[type][i] == rel)
            return true;
    return false;
}

void PianoRollPanel::paintGrid (juce::Graphics& g)
{
    const auto area = gridArea();
    g.saveState();
    g.reduceClipRegion (area);

    const bool scaleOn = scaleRootBox.getSelectedId() > 1;
    static const bool isBlack[12] = { false, true, false, true, false, false,
                                      true, false, true, false, true, false };

    // Key rows
    for (int key = 127; key >= 0; --key)
    {
        const int y = keyToY (key);
        if (y + keyHeight < area.getY() || y > area.getBottom())
            continue;

        juce::Colour row = isBlack[key % 12] ? theme::sunken : theme::panelBg.brighter (0.04f);
        if (scaleOn && isKeyInScale (key))
            row = row.brighter (0.09f);
        g.setColour (row);
        g.fillRect (area.getX(), y, area.getWidth(), keyHeight);

        if (key % 12 == 0)   // C line
        {
            g.setColour (theme::outlineLight.withAlpha (0.6f));
            g.drawHorizontalLine (y + keyHeight, (float) area.getX(), (float) area.getRight());
        }
    }

    // Vertical lines per step/beat/bar
    const double firstTick = snapDown (scrollTicks);
    for (double t = firstTick; ; t += ids::ticksPerStep)
    {
        const int x = ticksToX (t);
        if (x > area.getRight())
            break;
        if (x < area.getX())
            continue;

        const juce::int64 ti = (juce::int64) std::llround (t);
        if (ti % ids::ticksPerBar == 0)       g.setColour (theme::outlineLight);
        else if (ti % ids::ticksPerQuarter == 0) g.setColour (theme::outlineLight.withAlpha (0.5f));
        else                                   g.setColour (theme::outline.withAlpha (0.6f));
        g.drawVerticalLine (x, (float) area.getY(), (float) area.getBottom());
    }

    // Pattern end marker
    if (auto pattern = activePattern(); pattern.isValid())
    {
        const int endX = ticksToX ((int) pattern[ids::lengthTicks]);
        g.setColour (theme::accent.withAlpha (0.4f));
        g.drawVerticalLine (endX, (float) area.getY(), (float) area.getBottom());
    }

    // Playhead
    if (playheadTicks >= 0.0)
    {
        const int x = ticksToX (playheadTicks);
        if (x >= area.getX() && x <= area.getRight())
        {
            g.setColour (theme::accent);
            g.drawVerticalLine (x, (float) area.getY(), (float) area.getBottom());
        }
    }

    g.restoreState();
}

void PianoRollPanel::paintNotes (juce::Graphics& g)
{
    const auto area = gridArea();
    g.saveState();
    g.reduceClipRegion (area);

    auto pattern = activePattern();
    if (! pattern.isValid())
    {
        g.restoreState();
        return;
    }

    const int currentChId = selectedChannelId();

    // Ghost notes from other channels, but only from lanes that hold real
    // piano-roll content: step-sequencer lanes would just be a row of clutter.
    g.setColour (theme::ghostNote);
    for (const auto lane : pattern)
    {
        if (! lane.hasType (ids::LANE) || (int) lane[ids::channelId] == currentChId)
            continue;

        const auto channel = services.project.getChannelById ((int) lane[ids::channelId]);
        const int rootNote = channel.isValid() ? (int) channel.getProperty (ids::rootNote, 60) : 60;
        if (! notetools::laneUsesPianoRoll (lane, rootNote))
            continue;
        for (const auto note : lane)
        {
            const int y = keyToY ((int) note[ids::key]);
            const int x0 = ticksToX ((int) note[ids::startTicks]);
            const int x1 = ticksToX ((int) note[ids::startTicks] + (int) note[ids::lengthTicks]);
            g.fillRoundedRectangle ((float) x0, (float) y + 1, (float) juce::jmax (3, x1 - x0) - 1,
                                    (float) keyHeight - 2, 2.0f);
        }
    }

    // Current lane notes.
    const auto lane = services.project.getLane (pattern, currentChId);
    if (lane.isValid())
    {
        for (const auto note : lane)
        {
            const int y = keyToY ((int) note[ids::key]);
            const int x0 = ticksToX ((int) note[ids::startTicks]);
            const int x1 = ticksToX ((int) note[ids::startTicks] + (int) note[ids::lengthTicks]);
            const bool sel = selection.contains (note);
            const float vel = (float) (double) note[ids::velocity];

            auto fill = sel ? theme::accent : theme::noteFill.withMultipliedBrightness (0.6f + 0.4f * vel);
            g.setColour (fill);
            g.fillRoundedRectangle ((float) x0, (float) y + 1, (float) juce::jmax (4, x1 - x0) - 1,
                                    (float) keyHeight - 2, 2.0f);
            g.setColour (sel ? juce::Colours::white.withAlpha (0.8f) : theme::noteOutline.withAlpha (0.5f));
            g.drawRoundedRectangle ((float) x0 + 0.5f, (float) y + 1.5f,
                                    (float) juce::jmax (4, x1 - x0) - 2.0f,
                                    (float) keyHeight - 3.0f, 2.0f, 1.0f);
        }
    }

    g.restoreState();
}

void PianoRollPanel::setLiveKey (int key, bool on)
{
    if (key < 0 || key > 127 || liveKeys[(size_t) key] == on)
        return;
    liveKeys[(size_t) key] = on;
    repaint (keyboardArea());
}

void PianoRollPanel::paintKeyboard (juce::Graphics& g)
{
    const auto area = keyboardArea();
    g.saveState();
    g.reduceClipRegion (area);

    static const bool isBlack[12] = { false, true, false, true, false, false,
                                      true, false, true, false, true, false };

    for (int key = 127; key >= 0; --key)
    {
        const int y = keyToY (key);
        if (y + keyHeight < area.getY() || y > area.getBottom())
            continue;

        g.setColour (isBlack[key % 12] ? theme::pianoBlackKey : theme::pianoWhiteKey);
        g.fillRect (area.getX(), y + 1, area.getWidth() - 2, keyHeight - 1);

        if (liveKeys[(size_t) key])
        {
            g.setColour (theme::accent.withAlpha (0.55f));
            g.fillRect (area.getX(), y + 1, area.getWidth() - 2, keyHeight - 1);
        }

        if (key % 12 == 0)
        {
            g.setColour (theme::textDim);
            g.setFont (theme::uiFont (9.0f));
            g.drawText ("C" + juce::String (key / 12 - 1),
                        area.getX(), y, area.getWidth() - 6, keyHeight,
                        juce::Justification::centredRight);
        }
    }

    g.setColour (theme::outline);
    g.drawVerticalLine (area.getRight() - 1, (float) area.getY(), (float) area.getBottom());
    g.restoreState();
}

void PianoRollPanel::paintVelocityLane (juce::Graphics& g)
{
    const auto area = velocityArea();
    g.setColour (theme::sunken);
    g.fillRect (getLocalBounds().removeFromBottom (velocityH));
    g.setColour (theme::outline);
    g.drawHorizontalLine (area.getY(), 0.0f, (float) getWidth());

    g.saveState();
    g.reduceClipRegion (area);

    const auto lane = currentLane (false);
    if (lane.isValid())
    {
        for (const auto note : lane)
        {
            const int x = ticksToX ((int) note[ids::startTicks]);
            const float vel = (float) (double) note[ids::velocity];
            const int h = (int) (vel * (float) (area.getHeight() - 8));
            const bool sel = selection.contains (note);
            g.setColour (sel ? theme::accent : theme::noteFill.withAlpha (0.85f));
            g.fillRect (x, area.getBottom() - 4 - h, 5, h + 2);
        }
    }
    g.restoreState();
}

// ---------- interaction ----------

void PianoRollPanel::setTool (Tool newTool)
{
    tool = newTool;
    drawToolButton.setToggleState (tool == Tool::draw, juce::dontSendNotification);
    selectToolButton.setToggleState (tool == Tool::select, juce::dontSendNotification);
}

// Shift keeps whatever was already selected, so a lasso can be built up in
// several passes; without it the drag starts from nothing.
void PianoRollPanel::beginMarquee (juce::Point<int> pos, bool additive)
{
    drag = Drag::marquee;
    dragStart = pos;
    marqueeRect = { pos, pos };
    marqueeBase = additive ? selection : juce::Array<juce::ValueTree>();
    selection = marqueeBase;
    repaint();
}

void PianoRollPanel::updateMarquee (juce::Point<int> pos)
{
    marqueeRect = juce::Rectangle<int>::leftTopRightBottom (
        juce::jmin (dragStart.x, pos.x), juce::jmin (dragStart.y, pos.y),
        juce::jmax (dragStart.x, pos.x), juce::jmax (dragStart.y, pos.y));

    selection = marqueeBase;
    if (auto lane = currentLane (false); lane.isValid())
    {
        for (auto note : lane)
        {
            const int y = keyToY ((int) note[ids::key]);
            const int x0 = ticksToX ((int) note[ids::startTicks]);
            const int x1 = ticksToX ((int) note[ids::startTicks] + (int) note[ids::lengthTicks]);
            if (marqueeRect.intersects (juce::Rectangle<int>::leftTopRightBottom (x0, y, x1, y + keyHeight))
                && ! selection.contains (note))
                selection.add (note);
        }
    }
    repaint();
}

void PianoRollPanel::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto pos = e.getPosition();

    if (velocityArea().contains (pos))
    {
        drag = Drag::velocity;
        undoGesture::begin (services.project, "Set velocity");
        setVelocityAt (pos);
        return;
    }

    if (keyboardArea().contains (pos))
    {
        const int key = yToKey (pos.y);
        preview (key);
        lastPreviewKey = key;
        return;
    }

    if (! gridArea().contains (pos))
        return;

    if (e.mods.isPopupMenu())
    {
        // Right-clicking inside the selection offers the tools; anywhere else
        // keeps the right-drag erase.
        bool overRightEdge = false;
        if (auto note = noteAt (pos, overRightEdge); note.isValid() && selection.contains (note))
        {
            showToolMenu();
            return;
        }
        drag = Drag::erase;
        undoGesture::begin (services.project, "Delete notes");
        deleteNoteAt (pos);
        return;
    }

    bool overRightEdge = false;
    auto note = noteAt (pos, overRightEdge);

    // Marquee: the Select tool makes it the plain left-drag over empty grid,
    // and Cmd-drag reaches it from the Draw tool without switching. Dragging a
    // note still moves it either way, so the Select tool never blocks editing.
    if (e.mods.isCommandDown() || (tool == Tool::select && ! note.isValid()))
    {
        beginMarquee (pos, e.mods.isShiftDown());
        return;
    }

    if (note.isValid())
    {
        if (! selection.contains (note))
        {
            if (! e.mods.isShiftDown())
                selection.clearQuick();
            selection.add (note);
        }
        dragNote = note;
        drag = overRightEdge ? Drag::resize : Drag::move;
        dragTickOffset = xToTicks (pos.x) - (double) (int) note[ids::startTicks];
        dragKeyOffset  = yToKey (pos.y) - (int) note[ids::key];
        undoGesture::begin (services.project, overRightEdge ? "Resize notes" : "Move notes");
        repaint();
    }
    else
    {
        drag = Drag::create;
        undoGesture::begin (services.project, "Add note");
        addNoteAt (pos);
        dragTickOffset = 0.0;
        dragKeyOffset = 0;
    }
}

void PianoRollPanel::mouseDrag (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();

    switch (drag)
    {
        case Drag::velocity:
            setVelocityAt (pos);
            return;

        case Drag::erase:
            deleteNoteAt (pos);
            return;

        case Drag::marquee:
            updateMarquee (pos);
            return;

        case Drag::move:
        case Drag::create:
        {
            if (! dragNote.isValid())
                return;
            auto& undo = services.project.getUndoManager();

            const int newStart = (int) snapDown (xToTicks (pos.x) - dragTickOffset);
            const int newKey   = juce::jlimit (0, 127, yToKey (pos.y) - dragKeyOffset);
            const int dTicks = newStart - (int) dragNote[ids::startTicks];
            const int dKeys  = newKey - (int) dragNote[ids::key];
            if (dTicks == 0 && dKeys == 0)
                return;

            for (auto& n : selection)
            {
                n.setProperty (ids::startTicks, juce::jmax (0, (int) n[ids::startTicks] + dTicks), &undo);
                n.setProperty (ids::key, juce::jlimit (0, 127, (int) n[ids::key] + dKeys), &undo);
            }
            if (dKeys != 0 && (int) dragNote[ids::key] != lastPreviewKey)
            {
                lastPreviewKey = (int) dragNote[ids::key];
                preview (lastPreviewKey);
            }
            repaint();
            return;
        }

        case Drag::resize:
        {
            if (! dragNote.isValid())
                return;
            auto& undo = services.project.getUndoManager();
            const double end = xToTicks (pos.x);
            const int start = dragNote[ids::startTicks];
            int len = (int) (std::ceil ((end - start) / snapTicks()) * snapTicks());
            len = juce::jmax (snapTicks() == 1 ? 24 : snapTicks(), len);
            for (auto& n : selection)
                n.setProperty (ids::lengthTicks, len, &undo);
            lastNoteLength = len;
            repaint();
            return;
        }

        case Drag::none:
            return;
    }
}

void PianoRollPanel::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::create && dragNote.isValid())
        lastNoteLength = dragNote[ids::lengthTicks];

    // Grow on release rather than mid-drag, so overshooting while dragging
    // does not leave the pattern permanently stretched.
    if (drag == Drag::create || drag == Drag::move || drag == Drag::resize)
        growPatternToFitNotes();

    // Harmless when the gesture never wrote anything: an empty transaction is
    // never recorded.
    undoGesture::end (services.project);
    drag = Drag::none;
    dragNote = {};
    marqueeRect = {};
    lastPreviewKey = -1;
    repaint();
}

void PianoRollPanel::mouseMove (const juce::MouseEvent& e)
{
    bool overRightEdge = false;
    if (gridArea().contains (e.getPosition()))
    {
        const bool overNote = noteAt (e.getPosition(), overRightEdge).isValid();
        setMouseCursor (overRightEdge          ? juce::MouseCursor::LeftRightResizeCursor
                        : (tool == Tool::select && ! overNote)
                                               ? juce::MouseCursor::CrosshairCursor
                                               : juce::MouseCursor::NormalCursor);
    }
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

// The Select tool still needs a way to write a note: double-click draws one,
// the way clicking does under the Draw tool.
void PianoRollPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    if (tool != Tool::select || e.mods.isPopupMenu() || ! gridArea().contains (pos))
        return;

    bool overRightEdge = false;
    if (noteAt (pos, overRightEdge).isValid())
        return;

    const undoGesture::Scoped step (services.project, "Add note");
    addNoteAt (pos);
    growPatternToFitNotes();
    drag = Drag::none;
    dragNote = {};
    marqueeRect = {};
}

void PianoRollPanel::setVelocityAt (juce::Point<int> pos)
{
    const auto area = velocityArea();
    const float vel = juce::jlimit (0.05f, 1.0f,
                                    1.0f - (float) (pos.y - area.getY() - 4) / (float) (area.getHeight() - 8));
    auto& undo = services.project.getUndoManager();

    auto lane = currentLane (false);
    if (! lane.isValid())
        return;

    // Selected notes take priority; otherwise the note nearest in x.
    if (! selection.isEmpty())
    {
        for (auto& n : selection)
            n.setProperty (ids::velocity, vel, &undo);
    }
    else
    {
        juce::ValueTree best;
        int bestDist = 12;
        for (auto note : lane)
        {
            const int d = std::abs (ticksToX ((int) note[ids::startTicks]) - pos.x);
            if (d < bestDist) { bestDist = d; best = note; }
        }
        if (best.isValid())
            best.setProperty (ids::velocity, vel, &undo);
    }
    repaint();
}

// Zooms about anchorX: the tick under that pixel stays put, so wheel zoom
// follows the pointer and the buttons hold the middle of the view.
void PianoRollPanel::zoomHorizontally (double factor, int anchorX)
{
    const double anchorTicks = xToTicks (anchorX);
    pxPerTick = juce::jlimit (0.01, 2.0, pxPerTick * factor);
    scrollTicks = juce::jmax (0.0, anchorTicks - (anchorX - keyboardW) / pxPerTick);
    repaint();
}

void PianoRollPanel::zoomToFitPattern()
{
    const auto pattern = activePattern();
    const double lengthTicks = pattern.isValid()
        ? (double) juce::jmax (1, (int) pattern[ids::lengthTicks]) : (double) ids::ticksPerBar;
    const int gridW = gridArea().getWidth();
    if (gridW <= 0 || lengthTicks <= 0.0)
        return;

    // A little air on the right so the pattern end is not flush with the edge.
    pxPerTick = juce::jlimit (0.01, 2.0, gridW / (lengthTicks * 1.02));
    scrollTicks = 0.0;
    repaint();
}

void PianoRollPanel::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCommandDown())
    {
        zoomHorizontally (wheel.deltaY > 0 ? 1.15 : 1.0 / 1.15, e.getPosition().x);
        return;
    }
    else if (e.mods.isShiftDown())
    {
        scrollTicks = juce::jmax (0.0, scrollTicks - wheel.deltaX * 2000.0 - wheel.deltaY * 2000.0);
    }
    else
    {
        scrollKeysY = juce::jlimit (0, 128 * keyHeight - gridArea().getHeight(),
                                    scrollKeysY - (int) (wheel.deltaY * 120.0f));
        scrollTicks = juce::jmax (0.0, scrollTicks - wheel.deltaX * 2000.0);
    }
    repaint();
}

bool PianoRollPanel::keyPressed (const juce::KeyPress& key)
{
    const auto cmd = juce::ModifierKeys::commandModifier;

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        deleteSelected();
        return true;
    }
    if (key == juce::KeyPress ('a', cmd, 0))
    {
        selection.clearQuick();
        if (auto lane = currentLane (false); lane.isValid())
            for (auto note : lane)
                selection.add (note);
        repaint();
        return true;
    }

    // Clipboard. Claimed even with an empty selection: these are the roll's
    // keys while it has focus, and falling through would play typing-piano
    // notes instead.
    if (key == juce::KeyPress ('c', cmd, 0)) { copySelection();     return true; }
    if (key == juce::KeyPress ('x', cmd, 0)) { cutSelection();      return true; }
    if (key == juce::KeyPress ('v', cmd, 0)) { pasteClipboard();    return true; }
    if (key == juce::KeyPress ('d', cmd, 0)) { duplicateSelection(); return true; }

    // Horizontal zoom. '=' is the unshifted '+' key, so accept both.
    if (key == juce::KeyPress ('+', cmd, 0) || key == juce::KeyPress ('=', cmd, 0))
    {
        zoomHorizontally (1.3, gridArea().getCentreX());
        return true;
    }
    if (key == juce::KeyPress ('-', cmd, 0))
    {
        zoomHorizontally (1.0 / 1.3, gridArea().getCentreX());
        return true;
    }
    if (key == juce::KeyPress ('0', cmd, 0))
    {
        zoomToFitPattern();
        return true;
    }
    return false;
}

// ---------- listeners ----------

void PianoRollPanel::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& prop)
{
    if (tree.hasType (ids::NOTE) || prop == ids::activePattern || prop == ids::selectedChannel
        || prop == ids::lengthTicks)
    {
        if (prop == ids::selectedChannel || prop == ids::activePattern)
            selection.clearQuick();
        repaint();
    }
}

void PianoRollPanel::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child)
{
    if (child.hasType (ids::NOTE) || child.hasType (ids::LANE))
        repaint();
}

void PianoRollPanel::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int)
{
    if (child.hasType (ids::NOTE) || child.hasType (ids::LANE))
    {
        for (int i = selection.size(); --i >= 0;)
            if (selection.getReference (i) == child)
                selection.remove (i);
        repaint();
    }
}

void PianoRollPanel::timerCallback()
{
    // Playhead + target label refresh.
    double newPlayhead = -1.0;
    if (services.engine.isPlaying() && ! services.project.isSongMode())
    {
        if (auto pattern = activePattern(); pattern.isValid())
        {
            const double len = (double) (int) pattern[ids::lengthTicks];
            if (len > 0)
                newPlayhead = std::fmod (services.engine.getPositionTicks(), len);
        }
    }
    if (! juce::approximatelyEqual (newPlayhead, playheadTicks))
    {
        playheadTicks = newPlayhead;
        repaint (gridArea());
    }

    auto channel = services.project.getChannelById (selectedChannelId());
    const auto name = channel.isValid() ? channel[ids::name].toString() : juce::String ("--");
    if (targetLabel.getText() != name)
        targetLabel.setText (name, juce::dontSendNotification);
}

void PianoRollPanel::resized()
{
    auto header = headerArea().reduced (6, 4);
    auto viewRow = header.removeFromTop (headerRowH);
    drawToolButton.setBounds (viewRow.removeFromLeft (50));
    viewRow.removeFromLeft (2);
    selectToolButton.setBounds (viewRow.removeFromLeft (56));
    viewRow.removeFromLeft (10);
    snapBox.setBounds (viewRow.removeFromLeft (110));
    viewRow.removeFromLeft (6);
    chordBox.setBounds (viewRow.removeFromLeft (110));
    viewRow.removeFromLeft (6);
    scaleRootBox.setBounds (viewRow.removeFromLeft (80));
    viewRow.removeFromLeft (4);
    scaleTypeBox.setBounds (viewRow.removeFromLeft (100));
    viewRow.removeFromLeft (10);

    // Zoom sits at the far right of the view row, always reachable.
    zoomFitButton.setBounds (viewRow.removeFromRight (36));
    viewRow.removeFromRight (2);
    zoomInButton.setBounds (viewRow.removeFromRight (26));
    viewRow.removeFromRight (2);
    zoomOutButton.setBounds (viewRow.removeFromRight (26));
    viewRow.removeFromRight (8);

    targetLabel.setBounds (viewRow);

    header.removeFromTop (6);
    auto toolRow = header.removeFromTop (headerRowH);
    rollDivBox.setBounds (toolRow.removeFromLeft (92));
    toolRow.removeFromLeft (4);
    rollButton.setBounds (toolRow.removeFromLeft (48));
    toolRow.removeFromLeft (8);
    rampBox.setBounds (toolRow.removeFromLeft (104));
    toolRow.removeFromLeft (10);
    chopButton.setBounds (toolRow.removeFromLeft (48));
    toolRow.removeFromLeft (4);
    glueButton.setBounds (toolRow.removeFromLeft (48));
    toolRow.removeFromLeft (10);
    strumBox.setBounds (toolRow.removeFromLeft (92));
    toolRow.removeFromLeft (4);
    strumBackButton.setBounds (toolRow.removeFromLeft (26));
    toolRow.removeFromLeft (2);
    strumForwardButton.setBounds (toolRow.removeFromLeft (26));
}
