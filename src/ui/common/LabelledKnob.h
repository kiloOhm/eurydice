#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "app/Theme.h"
#include "model/ProjectModel.h"
#include "model/UndoGesture.h"
#include "ui/common/AutomatableSlider.h"

// A rotary bound to one property of a ValueTree, with a caption and a value
// readout underneath. Writes go through the UndoManager so channel tweaks are
// undoable like everything else, one undo step per gesture.
class LabelledKnob : public juce::Component
{
public:
    LabelledKnob (const juce::String& caption, ProjectModel& projectModel, juce::ValueTree targetTree,
                  const juce::Identifier& targetProperty, juce::NormalisableRange<double> range,
                  double defaultVal, const juce::String& valueSuffix = {}, int numDecimals = 2)
        : model (projectModel), tree (targetTree), property (targetProperty),
          defaultValue (defaultVal), suffix (valueSuffix), decimals (numDecimals)
    {
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.setNormalisableRange (range);
        slider.setValue (tree.getProperty (property, defaultValue), juce::dontSendNotification);
        slider.setDoubleClickReturnValue (true, defaultValue);
        slider.setWantsKeyboardFocus (false);
        slider.setNumDecimalPlacesToDisplay (numDecimals);
        slider.setTextValueSuffix (valueSuffix);
        slider.onValueChange = [this]
        {
            this->tree.setProperty (this->property, slider.getValue(),
                                    &this->model.getUndoManager());
            updateReadout();
            if (onLiveEdit)
                onLiveEdit (slider.getValue());
        };
        slider.onContextMenu = [this]
        {
            if (onContextMenu)
                onContextMenu (slider.getValue());
        };
        undoGesture::attach (slider, projectModel, caption);
        addAndMakeVisible (slider);

        captionLabel.setText (caption, juce::dontSendNotification);
        captionLabel.setFont (theme::uiFont (9.5f, true));
        captionLabel.setColour (juce::Label::textColourId, theme::textDim);
        captionLabel.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (captionLabel);

        valueLabel.setFont (theme::uiFont (9.5f));
        valueLabel.setColour (juce::Label::textColourId, theme::textFaint);
        valueLabel.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (valueLabel);

        updateReadout();
    }

    void refresh()
    {
        slider.setValue (tree.getProperty (property, defaultValue), juce::dontSendNotification);
        updateReadout();
    }

    void resetToDefault()
    {
        slider.setValue (defaultValue, juce::sendNotificationSync);
    }

    // Both carry the knob's current value in its own units.
    std::function<void (double)> onLiveEdit;
    std::function<void (double)> onContextMenu;

    void resized() override
    {
        auto r = getLocalBounds();
        captionLabel.setBounds (r.removeFromTop (12));
        valueLabel.setBounds (r.removeFromBottom (12));
        slider.setBounds (r.reduced (2));
    }

    static constexpr int preferredWidth = 58;
    static constexpr int preferredHeight = 72;

private:
    void updateReadout()
    {
        valueLabel.setText (juce::String (slider.getValue(), decimals) + suffix,
                            juce::dontSendNotification);
    }

    ProjectModel& model;
    juce::ValueTree tree;
    juce::Identifier property;
    double defaultValue;
    juce::String suffix;
    int decimals = 2;

    AutomatableSlider slider;
    juce::Label captionLabel, valueLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LabelledKnob)
};
