#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "model/ProjectModel.h"
#include "ui/common/AutomatableSlider.h"

// One row in the channel rack: mute LED, name, pan/vol knobs, step cells.
class ChannelRow : public juce::Component
{
public:
    ChannelRow (ProjectModel&, juce::ValueTree channel);

    void setPattern (juce::ValueTree pattern);   // which pattern's lane we edit
    void setPlayStep (int step);                 // -1 = not playing
    void refreshFromModel();

    // While the rack's swing knob is being dragged, the off-beat cells shift
    // by the amount the engine will delay them, so the groove is visible as
    // it is dialled in. amount is 0..1; negative ends the preview.
    void setSwingPreview (float amount);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    int getChannelId() const { return channel[ids::id]; }
    juce::ValueTree getChannelTree() const { return channel; }

    std::function<void (int channelId)> onSelected;
    std::function<void (juce::ValueTree channel)> onOpenEditor;
    std::function<void (juce::ValueTree channel)> onWantsContextMenu;
    std::function<void (juce::ValueTree channel)> onWantsInsertMenu;
    std::function<void (juce::ValueTree channel)> onWantsPianoRoll;

    // ids::volume or ids::pan. The panel turns these into automation actions;
    // the row itself stays model-only.
    std::function<void (juce::ValueTree channel, juce::Identifier)> onKnobMoved;
    std::function<void (juce::ValueTree channel, juce::Identifier)> onKnobContextMenu;

    // A vertical drag on the name area moves the row within the rack. The row
    // only reports the gesture; the panel maps it to an index and the model.
    std::function<void (ChannelRow&, const juce::MouseEvent&)> onReorderDrag;
    std::function<void (ChannelRow&, const juce::MouseEvent&)> onReorderEnd;

    static constexpr int rowHeight   = 30;
    static constexpr int stepWidth   = 26;
    static constexpr int fixedLeftWidth = 18 + 4 + 118 + 4 + 26 + 26 + 4 + 40 + 8;

    int numSteps() const;

    // True once the lane holds notes the step grid cannot represent; the row
    // then shows a miniature note graph instead of step cells.
    bool usesPianoRoll() const;

private:
    juce::Rectangle<int> stepsArea() const;
    int stepAt (juce::Point<int>) const;
    bool isStepOn (int step) const;
    void setStep (int step, bool on);
    void paintNoteGraph (juce::Graphics&, juce::Rectangle<int> area) const;
    void paintPianoRollStrip (juce::Graphics&, juce::Rectangle<int> area, int steps) const;

    ProjectModel& model;
    juce::ValueTree channel;
    juce::ValueTree pattern;

    juce::TextButton muteLed;
    juce::TextButton nameButton;
    AutomatableSlider panKnob, volKnob;
    juce::TextButton insertButton;   // target mixer insert, click to reassign

    int playStep = -1;
    float swingPreview = -1.0f;   // < 0 = no preview
    int dragPaintMode = -1;   // 1 = painting on, 0 = erasing, -1 = idle
    bool reorderArmed = false;   // pressed in the grab area, not yet dragging
    bool reordering   = false;   // stays set until the next press so the name
                                 // button can tell a finished drag from a click

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelRow)
};
