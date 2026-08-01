#pragma once

#include <map>
#include <memory>
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

    // Called after device init / device change; re-prepares everything.
    void setAudioSpec (double newSampleRate, int newBlockSize);

    double getSampleRate() const noexcept { return sampleRate; }
    int getBlockSize() const noexcept     { return blockSize; }

private:
    std::map<int, std::shared_ptr<Generator>> pool;
    PluginManager* pluginManager = nullptr;
    std::function<void()> onPluginReady;
    double sampleRate = 44100.0;
    int blockSize = 512;
};
