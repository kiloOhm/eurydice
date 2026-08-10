#include "DrumMachineEditor.h"
#include "app/Theme.h"
#include "engine/DrumMachineGenerator.h"
#include "model/DrumKits.h"
#include "model/UndoGesture.h"

namespace
{
constexpr const char* audioExtensions = "wav;aif;aiff;mp3;flac;ogg;m4a";

bool padHasSound (const juce::ValueTree& pad)
{
    return pad[ids::samplePath].toString().isNotEmpty()
        || pad[ids::synthDrum].toString().isNotEmpty();
}
} // namespace

// ================= DrumPadGrid =================

DrumPadGrid::DrumPadGrid (AppServices& s, juce::ValueTree ch)
    : services (s), channel (ch)
{
    startTimerHz (30);
}

int DrumPadGrid::preferredWidth() const
{
    const int cols = drumpads::gridCols (channel);
    return cols * cellSize + (cols - 1) * gap;
}

int DrumPadGrid::preferredHeight() const
{
    const int rows = drumpads::gridRows (channel);
    return rows * cellSize + (rows - 1) * gap;
}

void DrumPadGrid::gridShapeChanged()
{
    selectedPad = juce::jmin (selectedPad,
                              drumpads::gridRows (channel) * drumpads::gridCols (channel) - 1);
    repaint();
}

juce::Rectangle<int> DrumPadGrid::padBounds (int pad) const
{
    const int rows = drumpads::gridRows (channel);
    const int cols = drumpads::gridCols (channel);
    const int column = pad % cols;
    const int visualRow = rows - 1 - pad / cols;
    return { column * (cellSize + gap), visualRow * (cellSize + gap), cellSize, cellSize };
}

int DrumPadGrid::padAtPosition (juce::Point<int> position) const
{
    const int rows = drumpads::gridRows (channel);
    const int cols = drumpads::gridCols (channel);
    const int column = position.x / (cellSize + gap);
    const int visualRow = position.y / (cellSize + gap);
    if (! juce::isPositiveAndBelow (column, cols) || ! juce::isPositiveAndBelow (visualRow, rows))
        return -1;
    if (! padBounds (drumpads::padIndexForCell (column, visualRow, rows, cols))
            .contains (position))
        return -1;   // in the gap between pads
    return drumpads::padIndexForCell (column, visualRow, rows, cols);
}

void DrumPadGrid::paint (juce::Graphics& g)
{
    const int rows = drumpads::gridRows (channel);
    const int cols = drumpads::gridCols (channel);

    for (int pad = 0; pad < rows * cols; ++pad)
    {
        const auto padTree = drumpads::getPad (channel, pad);
        const auto bounds = padBounds (pad).toFloat();
        const bool loaded = padTree.isValid() && padHasSound (padTree);

        g.setColour (loaded ? theme::raised : theme::sunken);
        g.fillRoundedRectangle (bounds, 5.0f);

        if (flashLevel[(size_t) pad] > 0.01f)
        {
            g.setColour (theme::accent.withAlpha (0.65f * flashLevel[(size_t) pad]));
            g.fillRoundedRectangle (bounds, 5.0f);
        }

        if (pad == dropHighlightPad)
        {
            g.setColour (theme::secondary.withAlpha (0.35f));
            g.fillRoundedRectangle (bounds, 5.0f);
        }

        g.setColour (pad == selectedPad ? theme::accent : theme::outlineLight);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, pad == selectedPad ? 1.6f : 1.0f);

        if (! padTree.isValid())
            continue;

        auto inner = bounds.toNearestInt().reduced (5, 3);
        g.setColour (loaded ? theme::textPrimary : theme::textFaint);
        g.setFont (theme::uiFont (10.0f, true));
        g.drawFittedText (padTree[ids::name].toString(), inner.removeFromTop (24),
                          juce::Justification::topLeft, 2);

        g.setColour (theme::textDim);
        g.setFont (theme::uiFont (9.0f));
        g.drawText (drumpads::noteName ((int) padTree.getProperty (ids::key, -1)),
                    inner, juce::Justification::bottomRight);
    }
}

void DrumPadGrid::mouseDown (const juce::MouseEvent& e)
{
    const int pad = padAtPosition (e.getPosition());
    if (pad < 0)
        return;

    setSelectedPad (pad);
    if (e.mods.isPopupMenu())
        showPadMenu (pad);
    else
        triggerPad (pad);
}

void DrumPadGrid::setSelectedPad (int pad)
{
    const int count = drumpads::gridRows (channel) * drumpads::gridCols (channel);
    selectedPad = juce::jlimit (0, juce::jmax (0, count - 1), pad);
    repaint();
    if (onPadSelected)
        onPadSelected (selectedPad);
}

void DrumPadGrid::triggerPad (int pad)
{
    const auto padTree = drumpads::getPad (channel, pad);
    if (! padTree.isValid())
        return;
    const int key = (int) padTree.getProperty (ids::key, -1);
    if (key >= 0)
        services.engine.previewNote (channel[ids::id], key, 0.9f, 400);
    flashLevel[(size_t) pad] = 1.0f;
    repaint (padBounds (pad));
}

void DrumPadGrid::showPadMenu (int pad)
{
    const auto padTree = drumpads::getPad (channel, pad);
    if (! padTree.isValid())
        return;

    juce::PopupMenu chokeMenu;
    const int currentChoke = (int) padTree.getProperty (ids::choke, 0);
    chokeMenu.addItem (100, "Off", true, currentChoke == 0);
    for (int group = 1; group <= 8; ++group)
        chokeMenu.addItem (100 + group, "Group " + juce::String (group), true,
                           currentChoke == group);

    juce::PopupMenu menu;
    menu.addItem (1, "Load sample...");
    menu.addItem (2, "Clear pad", padHasSound (padTree));
    menu.addSeparator();
    menu.addSubMenu ("Choke", chokeMenu);

    juce::Component::SafePointer<DrumPadGrid> self (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
        [self, pad] (int result)
        {
            if (self == nullptr || result == 0)
                return;
            if (result == 1 && self->onLoadSampleRequested)
                self->onLoadSampleRequested (pad);
            else if (result == 2)
                self->clearPad (pad);
            else if (result >= 100 && result <= 108)
            {
                const undoGesture::Scoped step (self->services.project, "Choke group");
                drumpads::getPad (self->channel, pad)
                    .setProperty (ids::choke, result - 100,
                                  &self->services.project.getUndoManager());
            }
        });
}

void DrumPadGrid::loadSampleOntoPad (int pad, const juce::File& file)
{
    auto padTree = drumpads::getPad (channel, pad);
    if (! padTree.isValid())
        return;

    const undoGesture::Scoped step (services.project, "Load pad sample");
    auto& undo = services.project.getUndoManager();
    padTree.setProperty (ids::samplePath, file.getFullPathName(), &undo);
    padTree.setProperty (ids::name, file.getFileNameWithoutExtension(), &undo);
    if (padTree.hasProperty (ids::synthDrum))
        padTree.removeProperty (ids::synthDrum, &undo);
    repaint();
}

void DrumPadGrid::clearPad (int pad)
{
    auto padTree = drumpads::getPad (channel, pad);
    if (! padTree.isValid())
        return;

    const undoGesture::Scoped step (services.project, "Clear pad");
    auto& undo = services.project.getUndoManager();
    if (padTree.hasProperty (ids::samplePath))
        padTree.removeProperty (ids::samplePath, &undo);
    if (padTree.hasProperty (ids::synthDrum))
        padTree.removeProperty (ids::synthDrum, &undo);
    repaint();
}

bool DrumPadGrid::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension (audioExtensions))
            return true;
    return false;
}

void DrumPadGrid::fileDragMove (const juce::StringArray&, int x, int y)
{
    const int pad = padAtPosition ({ x, y });
    if (pad != dropHighlightPad)
    {
        dropHighlightPad = pad;
        repaint();
    }
}

void DrumPadGrid::fileDragExit (const juce::StringArray&)
{
    dropHighlightPad = -1;
    repaint();
}

void DrumPadGrid::filesDropped (const juce::StringArray& files, int x, int y)
{
    dropHighlightPad = -1;
    int pad = padAtPosition ({ x, y });
    if (pad < 0)
    {
        repaint();
        return;
    }

    // Several files fill consecutive pads, so a kit drops in one go.
    const int count = drumpads::gridRows (channel) * drumpads::gridCols (channel);
    for (const auto& f : files)
    {
        if (! juce::File (f).hasFileExtension (audioExtensions))
            continue;
        if (pad >= count)
            break;
        loadSampleOntoPad (pad++, juce::File (f));
    }
    setSelectedPad (juce::jmax (0, pad - 1));
}

void DrumPadGrid::timerCallback()
{
    bool needsRepaint = false;

    // Pads light whenever their sound fires, whoever triggered it.
    if (auto drums = std::dynamic_pointer_cast<DrumMachineGenerator> (
            services.generators.getOrCreate (channel)))
    {
        for (int pad = 0; pad < drums->getNumPads(); ++pad)
        {
            const auto count = drums->getTriggerCount (pad);
            if (count != lastTriggerCount[(size_t) pad])
            {
                lastTriggerCount[(size_t) pad] = count;
                flashLevel[(size_t) pad] = 1.0f;
                needsRepaint = true;
            }
        }
    }

    for (auto& level : flashLevel)
        if (level > 0.01f)
        {
            level *= 0.82f;
            needsRepaint = true;
        }

    if (needsRepaint)
        repaint();
}

// ================= DrumMachineEditor =================

DrumMachineEditor::DrumMachineEditor (AppServices& s, juce::ValueTree ch)
    : services (s), channel (ch), padGrid (s, ch)
{
    // Imported or API-built channels may arrive without pads.
    drumpads::ensurePadCount (channel,
                              drumpads::gridRows (channel) * drumpads::gridCols (channel),
                              nullptr);

    auto styleCaption = [] (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (theme::uiFont (9.5f, true));
        label.setColour (juce::Label::textColourId, theme::textDim);
        label.setJustificationType (juce::Justification::centredLeft);
    };

    kitButton.setWantsKeyboardFocus (false);
    kitButton.setTooltip ("Load a kit preset from " + drumkits::kitsDirectory().getFullPathName());
    kitButton.onClick = [this] { showKitMenu(); };
    addAndMakeVisible (kitButton);

    styleCaption (gridLabel, "GRID");
    addAndMakeVisible (gridLabel);

    for (auto* box : { &rowsBox, &colsBox })
    {
        box->setWantsKeyboardFocus (false);
        for (int n = 1; n <= drumpads::maxGridSide; ++n)
            box->addItem (juce::String (n), n);
        addAndMakeVisible (*box);
    }
    rowsBox.setSelectedId (drumpads::gridRows (channel), juce::dontSendNotification);
    colsBox.setSelectedId (drumpads::gridCols (channel), juce::dontSendNotification);
    rowsBox.setTooltip ("Pad rows, to mirror your controller");
    colsBox.setTooltip ("Pad columns, to mirror your controller");
    rowsBox.onChange = [this] { configureGrid (rowsBox.getSelectedId(), colsBox.getSelectedId()); };
    colsBox.onChange = [this] { configureGrid (rowsBox.getSelectedId(), colsBox.getSelectedId()); };

    styleCaption (baseLabel, "BASE");
    addAndMakeVisible (baseLabel);

    baseNoteSlider.setWantsKeyboardFocus (false);
    baseNoteSlider.setRange (0.0, 127.0, 1.0);
    baseNoteSlider.setValue (drumpads::baseNote (channel), juce::dontSendNotification);
    baseNoteSlider.textFromValueFunction = [] (double v)
    {
        return drumpads::noteName ((int) v) + " (" + juce::String ((int) v) + ")";
    };
    baseNoteSlider.updateText();
    baseNoteSlider.setTooltip ("Note of pad 1 when mapping (36, the FPC/MPC default)");
    baseNoteSlider.onValueChange = [this]
    {
        channel.setProperty (ids::padBaseNote, (int) baseNoteSlider.getValue(),
                             &services.project.getUndoManager());
    };
    addAndMakeVisible (baseNoteSlider);

    mapButton.setWantsKeyboardFocus (false);
    mapButton.setTooltip ("Number every pad from the base note, left to right, bottom row first");
    mapButton.onClick = [this]
    {
        const undoGesture::Scoped step (services.project, "Map pad notes");
        drumpads::autoMapNotes (channel, &services.project.getUndoManager());
    };
    addAndMakeVisible (mapButton);

    learnButton.setWantsKeyboardFocus (false);
    learnButton.setTooltip ("Hit pads on your controller: each note lands on the selected pad, "
                            "then the selection moves to the next");
    addAndMakeVisible (learnButton);

    padGrid.onPadSelected = [this] (int) { rebuildPadStrip(); };
    padGrid.onLoadSampleRequested = [this] (int pad)
    {
        auto chooser = std::make_shared<juce::FileChooser> (
            "Load sample", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
            "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.ogg;*.m4a");
        chooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
            [this, chooser, pad] (const juce::FileChooser& fc)
            {
                if (fc.getResult().existsAsFile())
                    padGrid.loadSampleOntoPad (pad, fc.getResult());
            });
    };
    addAndMakeVisible (padGrid);

    padNameLabel.setFont (theme::uiFont (12.0f, true));
    padNameLabel.setColour (juce::Label::textColourId, theme::textPrimary);
    padNameLabel.setEditable (false, true);
    padNameLabel.setTooltip ("Double-click to rename the pad");
    padNameLabel.onTextChange = [this]
    {
        auto pad = drumpads::getPad (channel, padGrid.getSelectedPad());
        if (pad.isValid())
            pad.setProperty (ids::name, padNameLabel.getText(),
                             &services.project.getUndoManager());
    };
    addAndMakeVisible (padNameLabel);

    padNoteLabel.setFont (theme::uiFont (10.0f));
    padNoteLabel.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (padNoteLabel);

    loadButton.setWantsKeyboardFocus (false);
    loadButton.onClick = [this]
    {
        if (padGrid.onLoadSampleRequested)
            padGrid.onLoadSampleRequested (padGrid.getSelectedPad());
    };
    addAndMakeVisible (loadButton);

    clearButton.setWantsKeyboardFocus (false);
    clearButton.onClick = [this] { padGrid.clearPad (padGrid.getSelectedPad()); };
    addAndMakeVisible (clearButton);

    styleCaption (chokeCaption, "CHOKE");
    chokeCaption.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (chokeCaption);

    chokeBox.setWantsKeyboardFocus (false);
    chokeBox.setTooltip ("Pads in the same group cut each other off (open hat vs closed hat)");
    chokeBox.addItem ("Off", 1);
    for (int group = 1; group <= 8; ++group)
        chokeBox.addItem (juce::String (group), group + 1);
    chokeBox.onChange = [this]
    {
        auto pad = drumpads::getPad (channel, padGrid.getSelectedPad());
        if (pad.isValid())
            pad.setProperty (ids::choke, chokeBox.getSelectedId() - 1,
                             &services.project.getUndoManager());
    };
    addAndMakeVisible (chokeBox);

    rebuildPadStrip();
    channel.addListener (this);
    services.liveNoteListeners.add (this);
    updateWindowSize();
}

DrumMachineEditor::~DrumMachineEditor()
{
    services.liveNoteListeners.remove (this);
    channel.removeListener (this);
}

void DrumMachineEditor::showKitMenu()
{
    const auto kits = std::make_shared<std::vector<drumkits::Kit>> (drumkits::scanKits());

    juce::PopupMenu menu;
    for (size_t i = 0; i < kits->size(); ++i)
        menu.addItem ((int) i + 1, (*kits)[i].name);
    if (kits->empty())
        menu.addItem (-1, "(no kits installed)", false);
    menu.addSeparator();
    menu.addItem (1000, "Open kits folder...");

    juce::Component::SafePointer<DrumMachineEditor> self (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (kitButton),
        [self, kits] (int result)
        {
            if (self == nullptr || result == 0)
                return;
            if (result == 1000)
            {
                drumkits::kitsDirectory().createDirectory();
                drumkits::kitsDirectory().revealToUser();
                return;
            }
            if (result < 1 || result > (int) kits->size())
                return;

            const undoGesture::Scoped step (self->services.project, "Load kit");
            drumkits::applyKit (self->channel, (*kits)[(size_t) result - 1],
                                &self->services.project.getUndoManager());
            self->rebuildPadStrip();
            self->padGrid.repaint();
        });
}

void DrumMachineEditor::configureGrid (int rows, int cols)
{
    rows = juce::jlimit (1, drumpads::maxGridSide, rows);
    cols = juce::jlimit (1, drumpads::maxGridSide, cols);

    const undoGesture::Scoped step (services.project, "Pad grid");
    auto& undo = services.project.getUndoManager();
    channel.setProperty (ids::padRows, rows, &undo);
    channel.setProperty (ids::padCols, cols, &undo);
    drumpads::ensurePadCount (channel, rows * cols, &undo);

    padGrid.gridShapeChanged();
    updateWindowSize();
}

void DrumMachineEditor::rebuildPadStrip()
{
    padKnobs.clear();
    auto pad = drumpads::getPad (channel, padGrid.getSelectedPad());
    if (! pad.isValid())
        return;

    struct KnobSpec { const char* caption; juce::Identifier id;
                      juce::NormalisableRange<double> range; double def;
                      const char* suffix; int decimals; };
    const KnobSpec specs[] = {
        { "VOL",  ids::volume, { 0.0, 1.0 },         0.9, "",    2 },
        { "PAN",  ids::pan,    { -1.0, 1.0 },        0.0, "",    2 },
        { "TUNE", ids::tune,   { -24.0, 24.0, 0.5 }, 0.0, " st", 1 },
    };
    for (const auto& spec : specs)
    {
        auto knob = std::make_unique<LabelledKnob> (spec.caption, services.project, pad,
                                                    spec.id, spec.range, spec.def,
                                                    spec.suffix, spec.decimals);
        addAndMakeVisible (*knob);
        padKnobs.push_back (std::move (knob));
    }

    refreshPadStrip();
    resized();
}

void DrumMachineEditor::refreshPadStrip()
{
    const auto pad = drumpads::getPad (channel, padGrid.getSelectedPad());
    if (! pad.isValid())
        return;

    padNameLabel.setText (pad[ids::name].toString(), juce::dontSendNotification);

    const int key = (int) pad.getProperty (ids::key, -1);
    const auto path = pad[ids::samplePath].toString();
    const auto synth = pad[ids::synthDrum].toString();
    const auto source = path.isNotEmpty() ? juce::File (path).getFileName()
                      : synth.isNotEmpty() ? "built-in " + synth
                      : juce::String ("empty");
    padNoteLabel.setText (drumpads::noteName (key) + " · note " + juce::String (key)
                              + "  ·  " + source,
                          juce::dontSendNotification);

    chokeBox.setSelectedId (1 + juce::jlimit (0, 8, (int) pad.getProperty (ids::choke, 0)),
                            juce::dontSendNotification);
    for (auto& knob : padKnobs)
        knob->refresh();
}

void DrumMachineEditor::liveNoteOn (int key, float velocity)
{
    juce::ignoreUnused (velocity);
    if (! learnButton.getToggleState())
        return;

    auto pad = drumpads::getPad (channel, padGrid.getSelectedPad());
    if (! pad.isValid())
        return;

    pad.setProperty (ids::key, key, &services.project.getUndoManager());
    const int count = drumpads::gridRows (channel) * drumpads::gridCols (channel);
    padGrid.setSelectedPad ((padGrid.getSelectedPad() + 1) % count);
}

void DrumMachineEditor::valueTreePropertyChanged (juce::ValueTree& tree,
                                                  const juce::Identifier& property)
{
    if (tree == channel && (property == ids::padRows || property == ids::padCols))
    {
        // Undo/API changed the shape: follow it.
        rowsBox.setSelectedId (drumpads::gridRows (channel), juce::dontSendNotification);
        colsBox.setSelectedId (drumpads::gridCols (channel), juce::dontSendNotification);
        padGrid.gridShapeChanged();
        updateWindowSize();
        return;
    }
    if (tree == channel && property == ids::padBaseNote)
        baseNoteSlider.setValue (drumpads::baseNote (channel), juce::dontSendNotification);

    if (tree.hasType (ids::PAD))
    {
        padGrid.repaint();
        refreshPadStrip();
    }
}

void DrumMachineEditor::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&)
{
    if (parent == channel)
        padGrid.repaint();
}

void DrumMachineEditor::updateWindowSize()
{
    const int stripWidth = 3 * (LabelledKnob::preferredWidth + 2) + 90 + 240;
    const int headerWidth = 570;   // kit + grid + base + map + learn controls
    const int width = juce::jmax (juce::jmax (padGrid.preferredWidth(), stripWidth),
                                  headerWidth) + 20;
    const int height = 10 + 26 + 8 + padGrid.preferredHeight() + 10
                       + LabelledKnob::preferredHeight + 22 + 10;

    if (auto* window = dynamic_cast<juce::ResizableWindow*> (getTopLevelComponent()))
        window->setContentComponentSize (width, height);
    else
        setSize (width, height);
}

void DrumMachineEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelBg);
}

void DrumMachineEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto header = area.removeFromTop (26);
    kitButton.setBounds (header.removeFromLeft (58).reduced (0, 2));
    header.removeFromLeft (10);
    gridLabel.setBounds (header.removeFromLeft (32));
    rowsBox.setBounds (header.removeFromLeft (52).reduced (0, 2));
    header.removeFromLeft (4);
    colsBox.setBounds (header.removeFromLeft (52).reduced (0, 2));
    header.removeFromLeft (14);
    baseLabel.setBounds (header.removeFromLeft (34));
    baseNoteSlider.setBounds (header.removeFromLeft (130).reduced (0, 1));
    header.removeFromLeft (8);
    mapButton.setBounds (header.removeFromLeft (84).reduced (0, 2));
    header.removeFromLeft (8);
    learnButton.setBounds (header.removeFromLeft (70));

    area.removeFromTop (8);
    padGrid.setBounds (area.removeFromTop (padGrid.preferredHeight())
                           .withWidth (padGrid.preferredWidth()));

    area.removeFromTop (10);
    auto strip = area;

    auto right = strip.removeFromRight (3 * (LabelledKnob::preferredWidth + 2) + 66);
    for (auto& knob : padKnobs)
        knob->setBounds (right.removeFromLeft (LabelledKnob::preferredWidth + 2)
                             .withHeight (LabelledKnob::preferredHeight));
    right.removeFromLeft (6);
    chokeCaption.setBounds (right.removeFromTop (14));
    chokeBox.setBounds (right.removeFromTop (22));

    strip.removeFromRight (10);
    padNameLabel.setBounds (strip.removeFromTop (22));
    padNoteLabel.setBounds (strip.removeFromTop (18));
    strip.removeFromTop (4);
    auto buttons = strip.removeFromTop (24);
    loadButton.setBounds (buttons.removeFromLeft (110));
    buttons.removeFromLeft (6);
    clearButton.setBounds (buttons.removeFromLeft (60));
}
