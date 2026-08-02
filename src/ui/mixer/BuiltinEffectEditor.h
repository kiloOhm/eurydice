#pragma once

#include <memory>
#include <vector>
#include <juce_gui_extra/juce_gui_extra.h>
#include "effects/EffectRegistry.h"
#include "model/ProjectModel.h"
#include "ui/common/LabelledKnob.h"

// Generic editor for a built-in effect: one control per fx::ParamSpec, bound
// straight to the SLOT tree, so every tweak is undoable and automatable
// without the effect knowing a UI exists.
class BuiltinEffectEditor : public juce::Component,
                            private juce::ValueTree::Listener
{
public:
    BuiltinEffectEditor (ProjectModel& projectModel, juce::ValueTree slot,
                         const std::vector<fx::ParamSpec>& specs, int ownInsertIndex);
    ~BuiltinEffectEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Control
    {
        juce::Identifier id;
        std::unique_ptr<LabelledKnob> knob;
        std::unique_ptr<juce::Label> caption;
        std::unique_ptr<juce::ComboBox> combo;
        std::vector<int> comboValues;   // item index -> value stored on the tree
        int row = 0;
        int column = 0;
        int span = 1;
    };

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    ProjectModel& model;
    juce::ValueTree slotTree;
    std::vector<std::unique_ptr<Control>> controls;

    void layOutControls (const std::vector<fx::ParamSpec>& specs);

    static constexpr int cellW = 62;
    static constexpr int cellH = 76;
    static constexpr int maxColumns = 5;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BuiltinEffectEditor)
};

// Opens and coalesces built-in effect editor windows, keyed by mixer slot.
class BuiltinEffectWindows
{
public:
    BuiltinEffectWindows() = default;
    ~BuiltinEffectWindows() { windows.clear(); }

    void show (ProjectModel& model, juce::ValueTree slot, const fx::BuiltinEntry& entry,
               int insertIndex, int slotIndex, const juce::String& title);
    void closeFor (int insertIndex, int slotIndex);
    void closeAll() { windows.clear(); }

private:
    struct Window : juce::DocumentWindow
    {
        Window (BuiltinEffectWindows& ownerRef, const juce::String& title,
                std::unique_ptr<BuiltinEffectEditor> content, int insert, int slotIdx);
        void closeButtonPressed() override;

        BuiltinEffectWindows& owner;
        int insertIndex, slotIndex;
    };

    std::vector<std::unique_ptr<Window>> windows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BuiltinEffectWindows)
};
