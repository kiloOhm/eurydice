#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "HostedPlugin.h"
#include "app/Theme.h"

// Opens/coalesces native plugin editor windows. Call closeFor() before
// destroying an instance the window points at.
class PluginWindowManager
{
public:
    PluginWindowManager() = default;
    ~PluginWindowManager() { windows.clear(); }

    void showEditorFor (const std::shared_ptr<HostedPlugin>& plugin, const juce::String& title)
    {
        if (plugin == nullptr || plugin->getInstance() == nullptr)
            return;

        for (auto& w : windows)
            if (w->plugin.get() == plugin.get())
            {
                w->toFront (true);
                return;
            }

        windows.push_back (std::make_unique<Window> (*this, plugin, title));
    }

    void closeFor (const HostedPlugin* plugin)
    {
        windows.erase (std::remove_if (windows.begin(), windows.end(),
                                       [plugin] (const auto& w) { return w->plugin.get() == plugin; }),
                       windows.end());
    }

private:
    struct Window : juce::DocumentWindow
    {
        Window (PluginWindowManager& ownerRef, std::shared_ptr<HostedPlugin> p, const juce::String& title)
            : juce::DocumentWindow (title, theme::panelHeader, closeButton),
              owner (ownerRef), plugin (std::move (p))
        {
            setUsingNativeTitleBar (true);

            auto* instance = plugin->getInstance();
            juce::Component* editor = instance->hasEditor()
                                          ? instance->createEditorIfNeeded()
                                          : new juce::GenericAudioProcessorEditor (*instance);
            setContentNonOwned (editor, true);
            ownedEditor.reset (editor);
            setResizable (true, false);
            centreWithSize (juce::jmax (300, editor->getWidth()),
                            juce::jmax (150, editor->getHeight()));
            setVisible (true);
        }

        ~Window() override
        {
            clearContentComponent();
            ownedEditor = nullptr;   // AudioProcessorEditor unregisters itself
        }

        void closeButtonPressed() override
        {
            auto* self = this;
            auto* ownerPtr = &owner;
            juce::MessageManager::callAsync ([ownerPtr, self] { ownerPtr->closeFor (self->plugin.get()); });
        }

        PluginWindowManager& owner;
        std::shared_ptr<HostedPlugin> plugin;   // keeps the instance alive while open
        std::unique_ptr<juce::Component> ownedEditor;
    };

    std::vector<std::unique_ptr<Window>> windows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginWindowManager)
};
