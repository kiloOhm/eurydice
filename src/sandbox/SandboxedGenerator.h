#pragma once

#include "engine/Generator.h"
#include "SandboxedPlugin.h"

// Generator for "plugin" channels running in a helper process. Mirrors
// PluginGenerator's swap discipline: the instance arrives asynchronously and
// the audio thread grabs the shared_ptr via try-lock so swaps never block or
// free on the RT thread. A dead helper renders silence; the pool's health
// check reports it for the restart UX.
class SandboxedGenerator : public Generator
{
public:
    SandboxedGenerator() = default;

    void prepare (double sampleRate, int maxBlockSize) override
    {
        sr = sampleRate; bs = maxBlockSize;
        if (auto p = getPlugin())
            p->prepareChild (sr, bs);   // message thread (GeneratorPool::setAudioSpec)
    }

    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi) override
    {
        std::shared_ptr<SandboxedPlugin> p;
        {
            const juce::SpinLock::ScopedTryLockType tl (lock);
            if (! tl.isLocked())
                return;
            p = plugin;
        }
        if (p != nullptr)
            p->renderInstrument (out, midi);
    }

    void reset() override {}

    // Message thread.
    void setPlugin (std::shared_ptr<SandboxedPlugin> newPlugin)
    {
        std::shared_ptr<SandboxedPlugin> old;
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            old = std::move (plugin);
            plugin = std::move (newPlugin);
        }
        // Parked so the audio thread can never be the one to free it (a
        // SandboxedPlugin destructor reaps a process).
        if (old != nullptr)
            retired.push_back (std::move (old));
    }

    std::shared_ptr<SandboxedPlugin> getPlugin() const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        return plugin;
    }

private:
    mutable juce::SpinLock lock;
    std::shared_ptr<SandboxedPlugin> plugin;
    std::vector<std::shared_ptr<SandboxedPlugin>> retired;
    double sr = 44100.0;
    int bs = 512;
};
