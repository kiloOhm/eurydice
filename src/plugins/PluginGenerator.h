#pragma once

#include "engine/Generator.h"
#include "HostedPlugin.h"

// Generator for "plugin" channels. The hosted instance arrives asynchronously;
// until then the channel is silent. The audio thread grabs the shared_ptr via
// try-lock so swaps never block or free on the RT thread.
class PluginGenerator : public Generator
{
public:
    PluginGenerator() = default;

    void prepare (double sampleRate, int maxBlockSize) override
    {
        sr = sampleRate; bs = maxBlockSize;
        if (auto p = getPlugin())
            p->prepare (sr, bs);
    }

    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi) override
    {
        std::shared_ptr<HostedPlugin> p;
        {
            const juce::SpinLock::ScopedTryLockType tl (lock);
            if (! tl.isLocked())
                return;
            p = plugin;
        }
        if (p != nullptr)
            p->processInstrument (out, midi, out.getNumSamples());
    }

    void reset() override
    {
        if (auto p = getPlugin())
            p->reset();
    }

    // Message thread.
    void setPlugin (std::shared_ptr<HostedPlugin> newPlugin)
    {
        if (newPlugin != nullptr)
            newPlugin->prepare (sr, bs);
        std::shared_ptr<HostedPlugin> old;
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            old = std::move (plugin);
            plugin = std::move (newPlugin);
        }
        // Parked so the audio thread can never be the one to free it.
        // Swaps are rare (user replacing a plugin), so this stays tiny.
        if (old != nullptr)
            retired.push_back (std::move (old));
    }

    std::shared_ptr<HostedPlugin> getPlugin() const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        return plugin;
    }

private:
    mutable juce::SpinLock lock;
    std::shared_ptr<HostedPlugin> plugin;
    std::vector<std::shared_ptr<HostedPlugin>> retired;
    double sr = 44100.0;
    int bs = 512;
};
