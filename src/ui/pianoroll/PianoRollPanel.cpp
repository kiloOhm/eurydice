#include "PianoRollPanel.h"
#include "app/Theme.h"

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
}

PianoRollPanel::PianoRollPanel (AppServices& s)
    : services (s)
{
    observedRoot = services.project.getRoot();
    observedRoot.addListener (this);

    setWantsKeyboardFocus (true);

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

void PianoRollPanel::addNoteAt (juce::Point<int> pos)
{
    auto lane = currentLane (true);
    if (! lane.isValid())
        return;

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

void PianoRollPanel::deleteSelected()
{
    auto lane = currentLane (false);
    if (! lane.isValid())
        return;
    for (auto& note : selection)
        services.project.removeNote (lane, note);
    selection.clearQuick();
    repaint();
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

    // Ghost notes from all other channels.
    g.setColour (theme::ghostNote);
    for (const auto lane : pattern)
    {
        if (! lane.hasType (ids::LANE) || (int) lane[ids::channelId] == currentChId)
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

void PianoRollPanel::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto pos = e.getPosition();

    if (velocityArea().contains (pos))
    {
        drag = Drag::velocity;
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
        drag = Drag::erase;
        deleteNoteAt (pos);
        return;
    }

    if (e.mods.isCommandDown())
    {
        drag = Drag::marquee;
        dragStart = pos;
        marqueeRect = { pos, pos };
        return;
    }

    bool overRightEdge = false;
    auto note = noteAt (pos, overRightEdge);

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
        repaint();
    }
    else
    {
        drag = Drag::create;
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
        {
            marqueeRect = juce::Rectangle<int>::leftTopRightBottom (
                juce::jmin (dragStart.x, pos.x), juce::jmin (dragStart.y, pos.y),
                juce::jmax (dragStart.x, pos.x), juce::jmax (dragStart.y, pos.y));

            selection.clearQuick();
            if (auto lane = currentLane (false); lane.isValid())
            {
                for (auto note : lane)
                {
                    const int y = keyToY ((int) note[ids::key]);
                    const int x0 = ticksToX ((int) note[ids::startTicks]);
                    const int x1 = ticksToX ((int) note[ids::startTicks] + (int) note[ids::lengthTicks]);
                    if (marqueeRect.intersects (juce::Rectangle<int>::leftTopRightBottom (x0, y, x1, y + keyHeight)))
                        selection.add (note);
                }
            }
            repaint();
            return;
        }

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
        noteAt (e.getPosition(), overRightEdge);
        setMouseCursor (overRightEdge ? juce::MouseCursor::LeftRightResizeCursor
                                      : juce::MouseCursor::NormalCursor);
    }
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
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

void PianoRollPanel::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCommandDown())
    {
        const double factor = wheel.deltaY > 0 ? 1.15 : 1.0 / 1.15;
        const double mouseTicks = xToTicks (e.getPosition().x);
        pxPerTick = juce::jlimit (0.01, 2.0, pxPerTick * factor);
        scrollTicks = juce::jmax (0.0, mouseTicks - (e.getPosition().x - keyboardW) / pxPerTick);
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
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        deleteSelected();
        return true;
    }
    if (key == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
    {
        selection.clearQuick();
        if (auto lane = currentLane (false); lane.isValid())
            for (auto note : lane)
                selection.add (note);
        repaint();
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
    snapBox.setBounds (header.removeFromLeft (110));
    header.removeFromLeft (6);
    chordBox.setBounds (header.removeFromLeft (110));
    header.removeFromLeft (6);
    scaleRootBox.setBounds (header.removeFromLeft (80));
    header.removeFromLeft (4);
    scaleTypeBox.setBounds (header.removeFromLeft (100));
    header.removeFromLeft (10);
    targetLabel.setBounds (header);
}
