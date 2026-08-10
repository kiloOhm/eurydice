#include "BuiltinEffectEditor.h"
#include "app/Theme.h"
#include "ui/mixer/RetroRackDisplay.h"
#include "ui/mixer/ShaperDisplay.h"

BuiltinEffectEditor::BuiltinEffectEditor (ProjectModel& projectModel, juce::ValueTree slot,
                                          const fx::BuiltinEntry& entry, int ownInsertIndex,
                                          std::shared_ptr<BuiltinEffect> liveInstance)
    : model (projectModel), slotTree (slot)
{
    const auto& specs = entry.specs;
    auto& undo = model.getUndoManager();

    buildDisplay (entry, std::move (liveInstance));

    presets = entry.presets;
    buildPresetChooser();

    for (const auto& spec : specs)
    {
        // The effect's own display draws this one; it stays a parameter, it just
        // doesn't get a knob in the grid as well.
        if (spec.ownDrawn)
            continue;

        auto control = std::make_unique<Control>();
        control->id = spec.id;
        control->group = spec.group;
        control->startsRow = spec.startsRow;

        // Combos with long option names need two cells to read.
        control->span = 1;
        if (spec.insertChooser)
            control->span = 2;
        else
            for (const auto& choice : spec.choices)
                if (choice.length() > 5)
                    control->span = 2;

        if (spec.insertChooser || ! spec.choices.isEmpty())
        {
            control->caption = std::make_unique<juce::Label>();
            control->caption->setText (spec.name, juce::dontSendNotification);
            control->caption->setFont (theme::uiFont (9.5f, true));
            control->caption->setColour (juce::Label::textColourId, theme::textDim);
            control->caption->setJustificationType (juce::Justification::centred);
            addAndMakeVisible (*control->caption);

            control->combo = std::make_unique<juce::ComboBox>();
            control->combo->setWantsKeyboardFocus (false);

            if (spec.insertChooser)
            {
                control->comboValues.push_back (-1);
                control->combo->addItem ("Internal", 1);
                for (int i = 0; i < model.numInserts(); ++i)
                {
                    if (i == ownInsertIndex)
                        continue;
                    control->comboValues.push_back (i);
                    control->combo->addItem (model.getInsert (i)[ids::name].toString(),
                                             (int) control->comboValues.size());
                }
            }
            else
            {
                for (int i = 0; i < spec.choices.size(); ++i)
                {
                    control->comboValues.push_back (i);
                    control->combo->addItem (spec.choices[i], i + 1);
                }
            }

            const int stored = (int) std::lround ((double) slotTree.getProperty (spec.id, spec.defaultValue));
            int itemIndex = 0;
            for (size_t i = 0; i < control->comboValues.size(); ++i)
                if (control->comboValues[i] == stored)
                    itemIndex = (int) i;
            control->combo->setSelectedId (itemIndex + 1, juce::dontSendNotification);

            auto* raw = control.get();
            auto tree = slotTree;
            const auto paramId = spec.id;
            control->combo->onChange = [raw, tree, paramId, &undo]() mutable
            {
                const int index = raw->combo->getSelectedId() - 1;
                if (index >= 0 && index < (int) raw->comboValues.size())
                    tree.setProperty (paramId, raw->comboValues[(size_t) index], &undo);
            };
            addAndMakeVisible (*control->combo);
        }
        else
        {
            const juce::NormalisableRange<double> range (spec.minValue, spec.maxValue, 0.0, spec.skew);
            control->knob = std::make_unique<LabelledKnob> (spec.name, model, slotTree, spec.id,
                                                            range, spec.defaultValue, spec.suffix,
                                                            spec.decimals);
            addAndMakeVisible (*control->knob);
        }

        controls.push_back (std::move (control));
    }

    layOutControls();
    slotTree.addListener (this);
}

// Effects that can show their behaviour get a display strip above the knobs.
void BuiltinEffectEditor::buildDisplay (const fx::BuiltinEntry& entry,
                                        std::shared_ptr<BuiltinEffect> liveInstance)
{
    if (entry.id == EqEffect::identifier())
        display = std::make_unique<ResponseCurveDisplay> (slotTree, entry.specs,
            std::make_unique<EqEffect>(),
            [] (BuiltinEffect& fx, double f) { return static_cast<EqEffect&> (fx).magnitudeAt (f); });
    else if (entry.id == FilterEffect::identifier())
        display = std::make_unique<ResponseCurveDisplay> (slotTree, entry.specs,
            std::make_unique<FilterEffect>(),
            [] (BuiltinEffect& fx, double f) { return static_cast<FilterEffect&> (fx).magnitudeAt (f); });
    else if (entry.id == CompressorEffect::identifier())
        display = std::make_unique<CompressorDisplay> (slotTree, std::move (liveInstance));
    else if (entry.id == AutoPanEffect::identifier())
        display = std::make_unique<AutoPanDisplay> (slotTree);
    else if (entry.id == SaturatorEffect::identifier())
        display = std::make_unique<SaturatorDisplay> (slotTree);
    else if (entry.id == RetroEffect::identifier())
    {
        // Six module rows and the Magnitude slider: the rack *is* how this one
        // is played, so it gets the room a rack needs.
        display = std::make_unique<RetroRackDisplay> (model, slotTree, std::move (liveInstance));
        displayHeight = 180;
        displayMinWidth = 560;
    }
    else if (entry.id == ShaperEffect::identifier())
    {
        // The wave *is* the interface here rather than a readout beside the
        // knobs, so it gets room to draw in.
        display = std::make_unique<ShaperDisplay> (model, slotTree, std::move (liveInstance));
        displayHeight = 210;
        displayMinWidth = 560;
    }

    if (display != nullptr)
    {
        if (displayHeight == 0)
        {
            displayHeight = 110;
            displayMinWidth = 330;
        }
        addAndMakeVisible (*display);
    }
}

// Effects that ship presets get a chooser strip. Picking one writes every
// preset value onto the slot in a single undoable step; touching any knob
// afterwards drops the selection back to "Preset…".
void BuiltinEffectEditor::buildPresetChooser()
{
    if (presets == nullptr || presets->empty())
        return;

    presetCombo = std::make_unique<juce::ComboBox>();
    presetCombo->setWantsKeyboardFocus (false);
    // Explicitly UTF-8: the ellipsis is multi-byte, and juce::String reads a
    // bare char* as ASCII (which asserts in debug and mangles the glyph).
    presetCombo->setTextWhenNothingSelected (
        juce::String (juce::CharPointer_UTF8 ("Preset\xe2\x80\xa6")));
    for (size_t i = 0; i < presets->size(); ++i)
        presetCombo->addItem ((*presets)[i].name, (int) i + 1);

    presetCombo->onChange = [this]
    {
        const int index = presetCombo->getSelectedId() - 1;
        if (index < 0 || index >= (int) presets->size())
            return;
        const juce::ScopedValueSetter<bool> applying (applyingPreset, true);
        auto& undo = model.getUndoManager();
        undo.beginNewTransaction ("Load preset");
        for (const auto& [paramId, value] : (*presets)[(size_t) index].values)
            slotTree.setProperty (paramId, value, &undo);
    };

    addAndMakeVisible (*presetCombo);
    presetHeight = 30;
}

// Packs the controls into rows: combos with long labels take two cells, and a
// spec can force a fresh row so grouped parameters (EQ bands, the Retro
// modules) stay together. A named group also claims a left gutter, which is
// where its heading goes — cheaper vertically than a heading strip per row.
void BuiltinEffectEditor::layOutControls()
{
    for (const auto& control : controls)
        if (control->group.isNotEmpty())
            groupGutter = 58;

    maxColumns = juce::jmax (5, (displayMinWidth - 16 - groupGutter) / cellW);

    int row = 0, column = 0, widest = 1;

    for (auto& owned : controls)
    {
        auto& control = *owned;

        if ((control.startsRow && column > 0) || column + control.span > maxColumns)
        {
            ++row;
            column = 0;
        }

        control.row = row;
        control.column = column;
        column += control.span;
        widest = juce::jmax (widest, column);
    }

    setSize (juce::jmax (widest * cellW + 16 + groupGutter, displayMinWidth),
             displayHeight + presetHeight + (row + 1) * cellH + 16);
}

// Where the knob grid starts, below the display and the preset strip.
juce::Rectangle<int> BuiltinEffectEditor::gridArea() const
{
    auto area = getLocalBounds().reduced (8);
    area.removeFromTop (displayHeight);
    area.removeFromTop (presetHeight);
    return area;
}

BuiltinEffectEditor::~BuiltinEffectEditor()
{
    slotTree.removeListener (this);
}

void BuiltinEffectEditor::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree != slotTree)
        return;

    // A manual tweak means the slot no longer matches the loaded preset.
    if (presetCombo != nullptr && ! applyingPreset)
        presetCombo->setSelectedId (0, juce::dontSendNotification);

    for (auto& control : controls)
    {
        if (control->id != property)
            continue;
        if (control->knob != nullptr)
            control->knob->refresh();
        else if (control->combo != nullptr)
        {
            const int stored = (int) std::lround ((double) tree.getProperty (property, 0.0));
            for (size_t i = 0; i < control->comboValues.size(); ++i)
                if (control->comboValues[i] == stored)
                    control->combo->setSelectedId ((int) i + 1, juce::dontSendNotification);
        }
    }
}

void BuiltinEffectEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelBg);

    if (groupGutter == 0)
        return;

    // Group headings, one per named row, in the gutter beside their controls.
    const auto area = gridArea();
    g.setFont (theme::uiFont (10.0f, true));
    for (const auto& control : controls)
    {
        if (control->group.isEmpty())
            continue;

        const juce::Rectangle<int> label (area.getX(), area.getY() + control->row * cellH,
                                          groupGutter - 8, cellH);
        g.setColour (theme::outlineLight.withAlpha (0.35f));
        g.drawHorizontalLine (label.getY(), (float) label.getX(), (float) area.getRight());
        g.setColour (theme::textDim);
        g.drawText (control->group, label, juce::Justification::centredLeft);
    }
}

void BuiltinEffectEditor::resized()
{
    auto area = getLocalBounds().reduced (8);
    if (display != nullptr)
        display->setBounds (area.removeFromTop (displayHeight - 6));
    if (displayHeight > 0)
        area.removeFromTop (6);
    if (presetCombo != nullptr)
    {
        presetCombo->setBounds (area.removeFromTop (22).reduced (2, 0));
        area.removeFromTop (presetHeight - 22);
    }
    area.removeFromLeft (groupGutter);
    for (auto& control : controls)
    {
        const juce::Rectangle<int> cell (area.getX() + control->column * cellW,
                                         area.getY() + control->row * cellH,
                                         control->span * cellW, cellH);
        auto inner = cell.reduced (2);
        if (control->knob != nullptr)
        {
            control->knob->setBounds (inner);
        }
        else
        {
            control->caption->setBounds (inner.removeFromTop (14));
            control->combo->setBounds (inner.removeFromTop (22));
        }
    }
}

// ===================== BuiltinEffectWindows =====================

BuiltinEffectWindows::Window::Window (BuiltinEffectWindows& ownerRef, const juce::String& title,
                                      std::unique_ptr<BuiltinEffectEditor> content,
                                      int insert, int slotIdx)
    : juce::DocumentWindow (title, theme::panelHeader, closeButton),
      owner (ownerRef), insertIndex (insert), slotIndex (slotIdx)
{
    setUsingNativeTitleBar (true);
    const int w = content->getWidth();
    const int h = content->getHeight();
    setContentOwned (content.release(), true);
    setResizable (false, false);
    centreWithSize (juce::jmax (200, w), juce::jmax (100, h));
    setVisible (true);
}

void BuiltinEffectWindows::Window::closeButtonPressed()
{
    auto* ownerPtr = &owner;
    const int insert = insertIndex;
    const int slotIdx = slotIndex;
    juce::MessageManager::callAsync ([ownerPtr, insert, slotIdx] { ownerPtr->closeFor (insert, slotIdx); });
}

void BuiltinEffectWindows::show (ProjectModel& model, juce::ValueTree slot, const fx::BuiltinEntry& entry,
                                 int insertIndex, int slotIndex, const juce::String& title,
                                 std::shared_ptr<BuiltinEffect> liveInstance)
{
    for (auto& w : windows)
        if (w->insertIndex == insertIndex && w->slotIndex == slotIndex)
        {
            w->toFront (true);
            return;
        }

    auto editor = std::make_unique<BuiltinEffectEditor> (model, slot, entry, insertIndex,
                                                         std::move (liveInstance));
    windows.push_back (std::make_unique<Window> (*this, title, std::move (editor),
                                                 insertIndex, slotIndex));
}

void BuiltinEffectWindows::closeFor (int insertIndex, int slotIndex)
{
    windows.erase (std::remove_if (windows.begin(), windows.end(),
                                   [insertIndex, slotIndex] (const auto& w)
                                   { return w->insertIndex == insertIndex && w->slotIndex == slotIndex; }),
                   windows.end());
}
