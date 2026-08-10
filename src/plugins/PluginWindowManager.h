#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "HostedPlugin.h"
#include "PluginEditorShell.h"
#include "app/Theme.h"

// Opens/coalesces native plugin editor windows. Call closeFor() before
// destroying an instance the window points at.
class PluginWindowManager
{
public:
    PluginWindowManager() = default;
    ~PluginWindowManager() { windows.clear(); }

    // Pass a NoteSink for instruments so the shell can offer its piano;
    // effects leave it empty. channelId names the channel an instrument plays,
    // so live input can light the right window's keys (-1 for effects).
    void showEditorFor (const std::shared_ptr<HostedPlugin>& plugin, const juce::String& title,
                        PluginEditorShell::NoteSink notes = {}, int channelId = -1)
    {
        if (plugin == nullptr || plugin->getInstance() == nullptr)
            return;

        for (auto& w : windows)
            if (w->plugin.get() == plugin.get())
            {
                w->toFront (true);
                return;
            }

        windows.push_back (std::make_unique<Window> (*this, plugin, title, std::move (notes),
                                                     channelId));
        if (typingKeys != nullptr)
            windows.back()->addKeyListener (typingKeys);
    }

    // Live MIDI / typing-piano input: light the keys of the instrument windows
    // playing that channel.
    void reflectLiveNote (int channelId, int note, bool on, float velocity)
    {
        if (channelId < 0)
            return;

        for (auto& w : windows)
            if (w->channelId == channelId && w->shell != nullptr)
                w->shell->reflectExternalNote (note, on, velocity);
    }

    void closeAll() { windows.clear(); }

    // Shared typing-piano listener, attached to each plugin window. Native
    // plugin views often keep key events to themselves, but when the window
    // frame has focus the laptop keyboard still plays.
    juce::KeyListener* typingKeys = nullptr;

    void closeFor (const HostedPlugin* plugin)
    {
        windows.erase (std::remove_if (windows.begin(), windows.end(),
                                       [plugin] (const auto& w) { return w->plugin.get() == plugin; }),
                       windows.end());
    }

private:
    struct Window : juce::DocumentWindow
    {
        Window (PluginWindowManager& ownerRef, std::shared_ptr<HostedPlugin> p, const juce::String& title,
                PluginEditorShell::NoteSink notes, int channel)
            : juce::DocumentWindow (title, theme::panelHeader, closeButton),
              owner (ownerRef), plugin (std::move (p)), channelId (channel)
        {
            setUsingNativeTitleBar (true);

            // The editor sits inside a JUCE shell (header strip + native
            // view), so we have somewhere to put custom UI around plugins.
            auto ownedShell = std::make_unique<PluginEditorShell> (*plugin->getInstance(), title,
                                                                   std::move (notes));
            const auto w = ownedShell->getWidth(), h = ownedShell->getHeight();
            shell = ownedShell.get();
            setContentOwned (ownedShell.release(), true);
            setResizable (true, false);
            centreWithSize (w, h);
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            auto* self = this;
            auto* ownerPtr = &owner;
            juce::MessageManager::callAsync ([ownerPtr, self] { ownerPtr->closeFor (self->plugin.get()); });
        }

        PluginWindowManager& owner;
        std::shared_ptr<HostedPlugin> plugin;   // keeps the instance alive while open
        int channelId = -1;                     // instrument channel, -1 for effects
        PluginEditorShell* shell = nullptr;     // owned by the window's content
    };

    std::vector<std::unique_ptr<Window>> windows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginWindowManager)
};
