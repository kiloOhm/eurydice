#pragma once

#include <map>
#include <memory>
#include <set>
#include <juce_data_structures/juce_data_structures.h>
#include "Generator.h"

// Owns the stateful Generator instances keyed by channel id, so they survive
// snapshot rebuilds. Creation/destruction happens on the message thread only;
// snapshots share ownership via shared_ptr so nothing dies mid-block.
class PluginManager;

class GeneratorPool
{
public:
    GeneratorPool() = default;

    // Enables "plugin" channel types. rebuild is called when an async plugin
    // instance lands so the engine snapshot picks it up.
    void setPluginContext (PluginManager* pm, std::function<void()> rebuild)
    {
        pluginManager = pm;
        onPluginReady = std::move (rebuild);
    }

    // Returns (creating if needed) the generator for a channel tree.
    // Implemented in GeneratorPool.cpp where concrete generator types live.
    std::shared_ptr<Generator> getOrCreate (const juce::ValueTree& channel);

    void remove (int channelId) { pool.erase (channelId); }

    // Plugin channels created from now on load in a helper process.
    void setSandboxEnabled (bool enabled) { sandboxEnabled = enabled; }
    bool isSandboxEnabled() const         { return sandboxEnabled; }

    // Message thread. True when a helper died this call; the crashed
    // generator keeps rendering silence until restartSandboxed().
    bool checkSandboxHealth();
    bool isSandboxCrashed (int channelId) const
    {
        return crashedChannels.count (channelId) > 0;
    }
    void restartSandboxed (const juce::ValueTree& channel);

    // Called after device init / device change; re-prepares everything.
    void setAudioSpec (double newSampleRate, int newBlockSize);

    double getSampleRate() const noexcept { return sampleRate; }
    int getBlockSize() const noexcept     { return blockSize; }

private:
    void launchSandboxedInstrument (std::shared_ptr<class SandboxedGenerator>,
                                    const juce::String& pluginId,
                                    const juce::String& stateBase64);

    std::map<int, std::shared_ptr<Generator>> pool;
    PluginManager* pluginManager = nullptr;
    std::function<void()> onPluginReady;
    bool sandboxEnabled = false;
    std::set<int> crashedChannels;
    double sampleRate = 44100.0;
    int blockSize = 512;
};
