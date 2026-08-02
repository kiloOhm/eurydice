#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "app/Theme.h"
#include "ui/common/IconButton.h"

// JUCE wrapper around a (usually native) plugin editor: a themed header strip
// above the editor view. The header is our hook for custom per-plugin UI —
// presets, bypass, channel routing — without touching the native view.
//
// Instrument shells (constructed with a NoteSink) get a piano toggle in the
// header: a keyboard slides in under the editor for plugins that don't bring
// their own, routed through the engine's preview path.
//
// Sizing works both ways: plugin-driven resizes (UI scale changes, page
// switches) bubble up so the host window follows, and window-driven resizes
// hand the editor the area under the header.
class PluginEditorShell : public juce::Component,
                          private juce::ComponentListener,
                          private juce::MidiKeyboardState::Listener
{
public:
    static constexpr int headerHeight = 26;
    static constexpr int keyboardHeight = 64;

    // Note callbacks for the built-in piano; pass {} for effects.
    struct NoteSink
    {
        std::function<void (int note, float velocity)> noteOn;
        std::function<void (int note)> noteOff;
        bool isValid() const { return noteOn != nullptr && noteOff != nullptr; }
    };

    PluginEditorShell (juce::AudioPluginInstance& inst, const juce::String& titleText,
                       NoteSink sink = {})
        : instance (inst), title (titleText), notes (std::move (sink))
    {
        editor.reset (instance.hasEditor() ? instance.createEditorAndMakeActive()
                                           : new juce::GenericAudioProcessorEditor (instance));
        addAndMakeVisible (*editor);
        editor->addComponentListener (this);

        if (notes.isValid())
        {
            pianoButton = std::make_unique<IconButton> ("piano", icons::piano());
            pianoButton->setTooltip ("Piano: play this instrument from the mouse "
                                     "(the computer keyboard plays it too)");
            pianoButton->setClickingTogglesState (true);
            pianoButton->onClick = [this] { setPianoVisible (pianoButton->getToggleState()); };
            addAndMakeVisible (*pianoButton);

            keyboardState.addListener (this);
            keyboard = std::make_unique<juce::MidiKeyboardComponent> (
                keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard);
            keyboard->setKeyWidth (14.0f);
            keyboard->setLowestVisibleKey (36);
            addChildComponent (*keyboard);
        }

        setSize (juce::jmax (300, editor->getWidth()), heightAround (editor->getHeight()));
    }

    ~PluginEditorShell() override
    {
        if (keyboard != nullptr)
            keyboardState.removeListener (this);
        editor->removeComponentListener (this);
        // Hosted editors must be detached from the processor before deletion
        // (the AudioProcessorEditor destructor asserts on this).
        instance.editorBeingDeleted (editor.get());
        editor = nullptr;
    }

    void paint (juce::Graphics& g) override
    {
        auto header = getLocalBounds().removeFromTop (headerHeight);
        g.setColour (theme::panelHeader);
        g.fillRect (header);
        g.setColour (theme::outline);
        g.fillRect (header.withTop (header.getBottom() - 1));

        g.setColour (theme::textPrimary);
        g.setFont (theme::uiFont (12.5f, true));
        g.drawText (title, header.reduced (10, 0), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto header = area.removeFromTop (headerHeight);
        if (pianoButton != nullptr)
            pianoButton->setBounds (header.removeFromRight (30).reduced (2));

        if (keyboard != nullptr && keyboard->isVisible())
            keyboard->setBounds (area.removeFromBottom (keyboardHeight));

        if (editor != nullptr && ! followingEditor)
            editor->setBounds (area);
    }

    void setPianoVisible (bool visible)
    {
        if (keyboard == nullptr)
            return;
        keyboard->setVisible (visible);
        pianoButton->setToggleState (visible, juce::dontSendNotification);
        // The guard keeps the editor where it is; only the shell (and, via
        // resize-to-fit, the window) grows or shrinks by the keyboard strip.
        const juce::ScopedValueSetter<bool> guard (followingEditor, true);
        setSize (getWidth(), heightAround (editor->getHeight()));
    }

    bool isPianoVisible() const { return keyboard != nullptr && keyboard->isVisible(); }

private:
    int heightAround (int editorHeight) const
    {
        const bool pianoOn = keyboard != nullptr && keyboard->isVisible();
        return headerHeight + juce::jmax (150, editorHeight)
             + (pianoOn ? keyboardHeight : 0);
    }

    void componentMovedOrResized (juce::Component&, bool, bool wasResized) override
    {
        // The plugin resized its own UI: grow the shell (and, through the
        // host window's resize-to-fit, the window) around it.
        if (! wasResized || editor == nullptr)
            return;
        const juce::ScopedValueSetter<bool> guard (followingEditor, true);
        setSize (juce::jmax (300, editor->getWidth()), heightAround (editor->getHeight()));
    }

    void handleNoteOn (juce::MidiKeyboardState*, int, int note, float velocity) override
    {
        notes.noteOn (note, juce::jmax (0.3f, velocity));
    }

    void handleNoteOff (juce::MidiKeyboardState*, int, int note, float) override
    {
        notes.noteOff (note);
    }

    juce::AudioPluginInstance& instance;
    juce::String title;
    NoteSink notes;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::unique_ptr<IconButton> pianoButton;
    juce::MidiKeyboardState keyboardState;
    std::unique_ptr<juce::MidiKeyboardComponent> keyboard;
    bool followingEditor = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditorShell)
};
