#pragma once

#include <map>
#include <memory>
#include "EffectRegistry.h"

// Owns Eurydice's own effect instances for mixer slots, keyed by
// (insertIndex, slotIndex). Unlike hosted plugins these construct instantly,
// so getReady() hands back a prepared instance straight away. Parameters live
// on the SLOT tree and are pushed into the instance on every snapshot rebuild,
// which is also how undo and project load reach the audio thread.
class BuiltinEffectPool
{
public:
    void setAudioSpec (double sampleRate, int blockSize)
    {
        sr = sampleRate;
        bs = blockSize;
        for (auto& [key, entry] : pool)
        {
            juce::ignoreUnused (key);
            entry.effect->prepare (sr, bs);
        }
    }

    std::shared_ptr<BuiltinEffect> getReady (int insertIndex, int slotIndex,
                                             const juce::String& pluginId, juce::ValueTree slot)
    {
        const auto key = std::make_pair (insertIndex, slotIndex);
        auto it = pool.find (key);

        if (it != pool.end() && it->second.pluginId != pluginId)
        {
            pool.erase (it);
            it = pool.end();
        }

        if (it == pool.end())
        {
            auto created = fx::createBuiltin (pluginId);
            if (created == nullptr)
                return nullptr;
            created->prepare (sr, bs);
            Entry entry { pluginId, std::shared_ptr<BuiltinEffect> (std::move (created)) };
            it = pool.emplace (key, std::move (entry)).first;
        }

        it->second.effect->applyParameters (slot);
        return it->second.effect;
    }

    std::shared_ptr<BuiltinEffect> peek (int insertIndex, int slotIndex) const
    {
        if (auto it = pool.find ({ insertIndex, slotIndex }); it != pool.end())
            return it->second.effect;
        return nullptr;
    }

    void remove (int insertIndex, int slotIndex)
    {
        pool.erase ({ insertIndex, slotIndex });   // snapshot history keeps it alive till swap
    }

    // Reorder support: instances follow their slots (state, meters and open
    // editors included) instead of being torn down and recreated.
    void swapSlots (int insertIndex, int slotA, int slotB)
    {
        auto a = pool.extract ({ insertIndex, slotA });
        auto b = pool.extract ({ insertIndex, slotB });
        if (! a.empty()) { a.key() = { insertIndex, slotB }; pool.insert (std::move (a)); }
        if (! b.empty()) { b.key() = { insertIndex, slotA }; pool.insert (std::move (b)); }
    }

private:
    struct Entry
    {
        juce::String pluginId;
        std::shared_ptr<BuiltinEffect> effect;
    };

    std::map<std::pair<int, int>, Entry> pool;
    double sr = 44100.0;
    int bs = 512;
};
