#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "model/ProjectModel.h"

// Collapsible graph lane under the rack grid: one bar per switched-on step of
// the selected channel, editable by clicking or dragging.
class StepGraphLane : public juce::Component
{
public:
    explicit StepGraphLane (ProjectModel&);

    enum class Mode { velocity, pan, pitch };

    void setPattern (juce::ValueTree pattern);
    void setChannel (juce::ValueTree channel);
    // Keeps the bars aligned with the horizontally scrolled row viewport.
    void setScrollOffset (int pixels);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    static constexpr int laneHeight = 80;

private:
    Mode getMode() const;
    int numSteps() const;
    juce::Rectangle<int> barsArea() const;
    int stepAt (juce::Point<int>) const;
    juce::ValueTree noteAtStep (int step) const;
    double valueOf (const juce::ValueTree& note) const;
    void applyDrag (juce::Point<int> pos);
    bool isEditable() const;

    ProjectModel& model;
    juce::ValueTree pattern, channel;

    juce::ComboBox modeBox;
    juce::Label channelLabel;
    int scrollOffset = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepGraphLane)
};
