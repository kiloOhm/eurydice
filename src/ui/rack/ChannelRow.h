#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "model/ProjectModel.h"

// One row in the channel rack: mute LED, name, pan/vol knobs, step cells.
class ChannelRow : public juce::Component
{
public:
    ChannelRow (ProjectModel&, juce::ValueTree channel);

    void setPattern (juce::ValueTree pattern);   // which pattern's lane we edit
    void setPlayStep (int step);                 // -1 = not playing
    void refreshFromModel();

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

    ProjectModel& model;
    juce::ValueTree channel;
    juce::ValueTree pattern;

    juce::TextButton muteLed;
    juce::TextButton nameButton;
    juce::Slider panKnob, volKnob;
    juce::TextButton insertButton;   // target mixer insert, click to reassign

    int playStep = -1;
    int dragPaintMode = -1;   // 1 = painting on, 0 = erasing, -1 = idle

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelRow)
};
