#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "app/AppServices.h"
#include "ui/common/LabelledKnob.h"

// Editor panels for the built-in generators, opened by clicking a channel
// name in the rack. All of them write straight to the channel's ValueTree; the
// GeneratorPool picks the values up on the next snapshot rebuild.

// Knobs grouped under painted section captions and wrapped into rows. Shared
// by the sampler, synth and kick editors so they lay out identically.
class KnobGrid
{
public:
    void beginSection (const juce::String& caption);

    void add (juce::Component& owner, const juce::String& caption, ProjectModel&,
              juce::ValueTree channel, const juce::Identifier& property,
              juce::NormalisableRange<double> range, double defaultValue,
              const juce::String& suffix = {}, int decimals = 2);

    // Takes ownership of a knob built elsewhere (the channel-parameter table),
    // so automation wiring and grid layout do not need separate knob lists.
    void adopt (std::unique_ptr<LabelledKnob>);

    // Lays the grid out inside area and returns the height it actually used.
    int layout (juce::Rectangle<int> area);
    void paintCaptions (juce::Graphics&) const;
    void refresh();

    static constexpr int captionHeight = 14;
    static constexpr int rowGap = 8;
    static constexpr int sectionGap = 18;
    static int rowHeight() { return captionHeight + LabelledKnob::preferredHeight; }

private:
    std::vector<std::unique_ptr<LabelledKnob>> knobs;
    std::vector<std::pair<juce::String, int>> sections;   // caption, first knob index
    std::vector<std::pair<juce::String, juce::Point<int>>> captionPositions;
};

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
    juce::ToggleButton oneShotButton { "One-shot" };
    juce::ToggleButton reverseButton { "Reverse" };
    juce::Label pathLabel;

    KnobGrid grid;
    std::vector<float> waveform;
    juce::String waveformForPath;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplerEditor)
};

// One Serum-style module box: a title strip, an optional display (wave,
// response curve, envelope, scope) and a row of knobs underneath.
class SynthModule : public juce::Component
{
public:
    SynthModule (juce::String titleText, std::unique_ptr<juce::Component> displayComponent);

    void addKnob (std::unique_ptr<LabelledKnob>);
    // Pulls every knob back from the tree — what a preset load, an undo or an
    // edit made anywhere else needs before the panel tells the truth again.
    void refreshKnobs();
    void paint (juce::Graphics&) override;
    void resized() override;

    int preferredWidth() const;

    static constexpr int titleHeight = 16;
    static constexpr int displayHeight = 56;
    static constexpr int padding = 6;
    static int preferredHeight()
    {
        return titleHeight + displayHeight + padding * 3 + LabelledKnob::preferredHeight;
    }

private:
    juce::String title;
    std::unique_ptr<juce::Component> display;
    std::vector<std::unique_ptr<LabelledKnob>> knobs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthModule)
};

class SynthEditor : public juce::Component,
                    private AppServices::LiveNoteListener
{
public:
    SynthEditor (AppServices&, juce::ValueTree channel);
    ~SynthEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // Builds one module from parameter ids out of the synth descriptor table.
    SynthModule& addModule (const juce::String& title,
                            std::unique_ptr<juce::Component> display,
                            std::initializer_list<juce::Identifier> params);

    // MIDI / typing-piano input lights the on-screen keys.
    void liveNoteOn (int channelId, int key, float velocity) override;
    void liveNoteOff (int channelId, int key) override;
    void echoLiveNote (int channelId, int key, float velocity, bool on);

    AppServices& services;
    juce::ValueTree channel;
    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard { keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };
    std::vector<std::unique_ptr<SynthModule>> modules;
    std::unique_ptr<juce::MidiKeyboardState::Listener> bridge;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthEditor)
};

// The kick channel's editor lives in KickEditor.h — it outgrew this file.

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

    // Shared typing-piano listener; attached to every editor window so the
    // laptop keyboard keeps playing while an editor has focus.
    juce::KeyListener* typingKeys = nullptr;

private:
    struct Window;
    std::map<int, std::unique_ptr<Window>> windows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelEditorManager)
};
