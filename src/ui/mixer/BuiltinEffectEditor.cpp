#include "BuiltinEffectEditor.h"
#include "app/Theme.h"

BuiltinEffectEditor::BuiltinEffectEditor (ProjectModel& projectModel, juce::ValueTree slot,
                                          const std::vector<fx::ParamSpec>& specs, int ownInsertIndex)
    : model (projectModel), slotTree (slot)
{
    auto& undo = model.getUndoManager();

    for (const auto& spec : specs)
    {
        auto control = std::make_unique<Control>();
        control->id = spec.id;

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

    layOutControls (specs);
    slotTree.addListener (this);
}

// Packs the controls into rows: combos with long labels take two cells, and a
// spec can force a fresh row so grouped parameters (EQ bands) stay together.
void BuiltinEffectEditor::layOutControls (const std::vector<fx::ParamSpec>& specs)
{
    int row = 0, column = 0, widest = 1;

    for (size_t i = 0; i < controls.size(); ++i)
    {
        const auto& spec = specs[i];
        auto& control = *controls[i];

        int span = 1;
        if (spec.insertChooser)
            span = 2;
        else
            for (const auto& choice : spec.choices)
                if (choice.length() > 5)
                    span = 2;

        if ((spec.startsRow && column > 0) || column + span > maxColumns)
        {
            ++row;
            column = 0;
        }

        control.row = row;
        control.column = column;
        control.span = span;
        column += span;
        widest = juce::jmax (widest, column);
    }

    setSize (widest * cellW + 16, (row + 1) * cellH + 16);
}

BuiltinEffectEditor::~BuiltinEffectEditor()
{
    slotTree.removeListener (this);
}

void BuiltinEffectEditor::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree != slotTree)
        return;

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
}

void BuiltinEffectEditor::resized()
{
    auto area = getLocalBounds().reduced (8);
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
                                 int insertIndex, int slotIndex, const juce::String& title)
{
    for (auto& w : windows)
        if (w->insertIndex == insertIndex && w->slotIndex == slotIndex)
        {
            w->toFront (true);
            return;
        }

    auto editor = std::make_unique<BuiltinEffectEditor> (model, slot, entry.specs, insertIndex);
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
