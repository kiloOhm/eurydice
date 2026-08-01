#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "app/AppServices.h"
#include "ui/common/LabelledKnob.h"

// Editor panels for the built-in generators, opened by clicking a channel
// name in the rack. Both write straight to the channel's ValueTree; the
// GeneratorPool picks the values up on the next snapshot rebuild.

class SamplerEditor : public juce::Component,
                      public juce::FileDragAndDropTarget,
                      private juce::Timer
{
public:
    SamplerEditor (AppServices&, juce::ValueTree channel);

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void loadSample (const juce::File&);
    void refreshWaveform();
    void timerCallback() override;
    juce::Rectangle<int> waveformArea() const;

    AppServices& services;
    juce::ValueTree channel;

    juce::TextButton loadButton { "Load sample..." };
    juce::TextButton previewButton { juce::CharPointer_UTF8 ("\xe2\x96\xb6") };
    juce::ToggleButton oneShotButton { "One-shot (ignore note-off)" };
    juce::Label pathLabel;

    std::vector<std::unique_ptr<LabelledKnob>> knobs;
    std::vector<float> waveform;
    juce::String waveformForPath;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplerEditor)
};

class SynthEditor : public juce::Component
{
public:
    SynthEditor (AppServices&, juce::ValueTree channel);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AppServices& services;
    juce::ValueTree channel;
    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard { keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };
    std::vector<std::unique_ptr<LabelledKnob>> knobs;
    std::vector<std::pair<juce::String, int>> sections;   // caption, knob index it starts at
    std::unique_ptr<juce::MidiKeyboardState::Listener> bridge;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthEditor)
};

// Owns the open channel-editor windows. Lives on MainComponent so the windows
// are destroyed while JUCE is still up (a static map would outlive the leak
// detector and report false positives — and real leaks — at shutdown).
class ChannelEditorManager
{
public:
    ChannelEditorManager();
    ~ChannelEditorManager();

    // Opens, or brings forward, the right editor for a channel. Plugin
    // channels get their native plugin window instead.
    void show (AppServices&, juce::ValueTree channel);
    void close (int channelId);
    void closeAll();

private:
    struct Window;
    std::map<int, std::unique_ptr<Window>> windows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelEditorManager)
};
