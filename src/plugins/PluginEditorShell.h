#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "app/Theme.h"

// JUCE wrapper around a (usually native) plugin editor: a themed header strip
// above the editor view. The header is our hook for custom per-plugin UI —
// presets, bypass, channel routing — without touching the native view.
//
// Sizing works both ways: plugin-driven resizes (UI scale changes, page
// switches) bubble up so the host window follows, and window-driven resizes
// hand the editor the area under the header.
class PluginEditorShell : public juce::Component,
                          private juce::ComponentListener
{
public:
    static constexpr int headerHeight = 26;

    PluginEditorShell (juce::AudioPluginInstance& inst, const juce::String& titleText)
        : instance (inst), title (titleText)
    {
        editor.reset (instance.hasEditor() ? instance.createEditorIfNeeded()
                                           : new juce::GenericAudioProcessorEditor (instance));
        addAndMakeVisible (*editor);
        editor->addComponentListener (this);
        setSize (juce::jmax (300, editor->getWidth()),
                 headerHeight + juce::jmax (150, editor->getHeight()));
    }

    ~PluginEditorShell() override
    {
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
        if (editor == nullptr || followingEditor)
            return;
        editor->setBounds (getLocalBounds().withTrimmedTop (headerHeight));
    }

private:
    void componentMovedOrResized (juce::Component&, bool, bool wasResized) override
    {
        // The plugin resized its own UI: grow the shell (and, through the
        // host window's resize-to-fit, the window) around it.
        if (! wasResized || editor == nullptr)
            return;
        const juce::ScopedValueSetter<bool> guard (followingEditor, true);
        setSize (juce::jmax (300, editor->getWidth()),
                 headerHeight + editor->getHeight());
    }

    juce::AudioPluginInstance& instance;
    juce::String title;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    bool followingEditor = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditorShell)
};
