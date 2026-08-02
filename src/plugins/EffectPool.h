#pragma once

#include <map>
#include "HostedPlugin.h"
#include "PluginManager.h"
#include "sandbox/SandboxedPlugin.h"

// Owns effect plugin instances for mixer slots, keyed by (insertIndex, slot).
// getReady() returns the instance if loaded; otherwise kicks off async
// creation and returns null — when the instance lands, onInstanceReady fires
// (used to rebuild the engine snapshot).
//
// With sandboxing enabled, new instances load in an EurydiceHelper process
// (SandboxedPlugin) instead of in-process; a crashed helper flags the slot
// rather than taking the app down, and restartSandboxed() brings it back.
class EffectPool
{
public:
    explicit EffectPool (PluginManager& pm) : plugins (pm) {}

    void setAudioSpec (double sampleRate, int blockSize)
    {
        sr = sampleRate; bs = blockSize;
        for (auto& [key, entry] : pool)
        {
            if (entry.plugin != nullptr)
                entry.plugin->prepare (sr, bs);
            if (entry.sandboxed != nullptr && ! entry.crashed)
                entry.sandboxed->prepareChild (sr, bs);
        }
    }

    // Applies to effects loaded from now on; existing instances keep running
    // where they are until their slot reloads.
    void setSandboxEnabled (bool enabled) { sandboxEnabled = enabled; }
    bool isSandboxEnabled() const         { return sandboxEnabled; }

    std::function<void()> onInstanceReady;
    // Fired (message thread) when a helper process dies: (insert, slot, name).
    std::function<void (int, int, juce::String)> onSandboxCrashed;

    std::shared_ptr<Effect> getReady (int insertIndex, int slotIndex,
                                      const juce::String& pluginId,
                                      const juce::String& initialStateBase64)
    {
        const auto key = std::make_pair (insertIndex, slotIndex);
        auto it = pool.find (key);

        if (it != pool.end())
        {
            if (it->second.pluginId == pluginId)
            {
                if (it->second.crashed)
                    return nullptr;   // slot stays silent until restarted
                if (it->second.plugin != nullptr)
                    return it->second.plugin;
                return it->second.sandboxed;   // may be null while loading
            }
            pool.erase (it);                   // slot changed plugin
        }

        if (! plugins.findByIdentifier (pluginId).has_value())
            return nullptr;

        pool[key] = { pluginId, nullptr, nullptr, false };
        if (sandboxEnabled)
            launchSandboxed (key, pluginId, initialStateBase64);
        else
            createInProcess (key, pluginId, initialStateBase64);
        return nullptr;
    }

    std::shared_ptr<HostedPlugin> peek (int insertIndex, int slotIndex) const
    {
        if (auto it = pool.find ({ insertIndex, slotIndex }); it != pool.end())
            return it->second.plugin;
        return nullptr;
    }

    std::shared_ptr<SandboxedPlugin> peekSandboxed (int insertIndex, int slotIndex) const
    {
        if (auto it = pool.find ({ insertIndex, slotIndex }); it != pool.end())
            return it->second.sandboxed;
        return nullptr;
    }

    bool isCrashed (int insertIndex, int slotIndex) const
    {
        if (auto it = pool.find ({ insertIndex, slotIndex }); it != pool.end())
            return it->second.crashed;
        return false;
    }

    // Message thread, cheap (waitpid WNOHANG per live helper). Returns true
    // when something died this call; onSandboxCrashed fires per casualty.
    bool checkHealth()
    {
        bool anyDied = false;
        for (auto& [key, entry] : pool)
        {
            if (entry.sandboxed == nullptr || entry.crashed)
                continue;
            if (! entry.sandboxed->isAlive())
            {
                entry.crashed = true;
                anyDied = true;
                if (onSandboxCrashed)
                    onSandboxCrashed (key.first, key.second, entry.sandboxed->getName());
            }
        }
        return anyDied;
    }

    // Relaunches a crashed (or live) sandboxed slot from the given state.
    void restartSandboxed (int insertIndex, int slotIndex, const juce::String& stateBase64)
    {
        const auto key = std::make_pair (insertIndex, slotIndex);
        auto it = pool.find (key);
        if (it == pool.end())
            return;
        const auto pluginId = it->second.pluginId;
        pool.erase (it);
        pool[key] = { pluginId, nullptr, nullptr, false };
        launchSandboxed (key, pluginId, stateBase64);
    }

    void remove (int insertIndex, int slotIndex)
    {
        pool.erase ({ insertIndex, slotIndex });   // snapshot history keeps it alive till swap
    }

    // For saving plugin state into the project.
    template <typename Fn>
    void forEach (Fn&& fn) const
    {
        for (const auto& [key, entry] : pool)
            if (entry.plugin != nullptr)
                fn (key.first, key.second, *entry.plugin);
    }

    template <typename Fn>
    void forEachSandboxed (Fn&& fn) const
    {
        for (const auto& [key, entry] : pool)
            if (entry.sandboxed != nullptr && ! entry.crashed)
                fn (key.first, key.second, *entry.sandboxed);
    }

private:
    void createInProcess (std::pair<int, int> key, const juce::String& pluginId,
                          const juce::String& initialStateBase64)
    {
        const auto desc = plugins.findByIdentifier (pluginId);
        plugins.createInstance (*desc, sr, bs,
            [this, key, pluginId, desc, initialStateBase64]
            (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
            {
                auto it2 = pool.find (key);
                if (it2 == pool.end() || it2->second.pluginId != pluginId)
                    return;   // slot was cleared/replaced meanwhile
                juce::ignoreUnused (error);
                if (instance == nullptr)
                {
                    DBG ("Effect load failed: " + error);
                    pool.erase (it2);
                    return;
                }
                auto hosted = std::make_shared<HostedPlugin> (std::move (instance), *desc);
                hosted->prepare (sr, bs);
                if (initialStateBase64.isNotEmpty())
                    hosted->setStateFromBase64 (initialStateBase64);
                it2->second.plugin = std::move (hosted);
                if (onInstanceReady)
                    onInstanceReady();
            });
    }

    void launchSandboxed (std::pair<int, int> key, const juce::String& pluginId,
                          const juce::String& initialStateBase64)
    {
        const double sampleRate = sr;
        const int blockSize = bs;
        juce::Thread::launch ([this, key, pluginId, initialStateBase64, sampleRate, blockSize]
        {
            auto sandboxed = std::make_shared<SandboxedPlugin>();
            juce::String error;
            const bool ok = sandboxed->launch (pluginId, sampleRate, blockSize,
                                               initialStateBase64, error);
            juce::MessageManager::callAsync ([this, key, pluginId, sandboxed, ok, error]
            {
                auto it = pool.find (key);
                if (it == pool.end() || it->second.pluginId != pluginId)
                    return;   // slot changed while launching; helper shuts down
                if (! ok)
                {
                    DBG ("Sandboxed load failed: " + error);
                    pool.erase (it);
                    return;
                }
                it->second.sandboxed = sandboxed;
                if (onInstanceReady)
                    onInstanceReady();
            });
        });
    }

    struct Entry
    {
        juce::String pluginId;
        std::shared_ptr<HostedPlugin> plugin;        // in-process (null while loading)
        std::shared_ptr<SandboxedPlugin> sandboxed;  // out-of-process
        bool crashed = false;
    };

    PluginManager& plugins;
    std::map<std::pair<int, int>, Entry> pool;
    double sr = 44100.0;
    int bs = 512;
    bool sandboxEnabled = false;
};
