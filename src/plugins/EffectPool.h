#pragma once

#include <map>
#include "HostedPlugin.h"
#include "PluginManager.h"

// Owns effect plugin instances for mixer slots, keyed by (insertIndex, slot).
// getReady() returns the instance if loaded; otherwise kicks off async
// creation and returns null — when the instance lands, onInstanceReady fires
// (used to rebuild the engine snapshot).
class EffectPool
{
public:
    explicit EffectPool (PluginManager& pm) : plugins (pm) {}

    void setAudioSpec (double sampleRate, int blockSize)
    {
        sr = sampleRate; bs = blockSize;
        for (auto& [key, entry] : pool)
            if (entry.plugin != nullptr)
                entry.plugin->prepare (sr, bs);
    }

    std::function<void()> onInstanceReady;

    std::shared_ptr<HostedPlugin> getReady (int insertIndex, int slotIndex,
                                            const juce::String& pluginId,
                                            const juce::String& initialStateBase64)
    {
        const auto key = std::make_pair (insertIndex, slotIndex);
        auto it = pool.find (key);

        if (it != pool.end())
        {
            if (it->second.pluginId == pluginId)
                return it->second.plugin;      // may be null while loading
            pool.erase (it);                   // slot changed plugin
        }

        auto desc = plugins.findByIdentifier (pluginId);
        if (! desc)
            return nullptr;

        pool[key] = { pluginId, nullptr };
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
        return nullptr;
    }

    std::shared_ptr<HostedPlugin> peek (int insertIndex, int slotIndex) const
    {
        if (auto it = pool.find ({ insertIndex, slotIndex }); it != pool.end())
            return it->second.plugin;
        return nullptr;
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

private:
    struct Entry
    {
        juce::String pluginId;
        std::shared_ptr<HostedPlugin> plugin;   // null while loading
    };

    PluginManager& plugins;
    std::map<std::pair<int, int>, Entry> pool;
    double sr = 44100.0;
    int bs = 512;
};
