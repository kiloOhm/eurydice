#include "GeneratorPool.h"
#include "SamplerGenerator.h"
#include "SynthGenerator.h"
#include "model/Ids.h"
#include "plugins/PluginManager.h"
#include "plugins/PluginGenerator.h"

std::shared_ptr<Generator> GeneratorPool::getOrCreate (const juce::ValueTree& channel)
{
    const int chId = channel[ids::id];
    const juce::String type = channel[ids::type].toString();

    if (auto it = pool.find (chId); it != pool.end())
    {
        // Keep sampler in sync if the sample path changed.
        if (auto* sampler = dynamic_cast<SamplerGenerator*> (it->second.get()))
        {
            const auto path = channel[ids::samplePath].toString();
            if (path.isNotEmpty() && path != sampler->getSamplePath())
                sampler->loadSampleFile (juce::File (path));
            sampler->setRootNote (channel[ids::rootNote]);
        }
        return it->second;
    }

    std::shared_ptr<Generator> gen;

    if (type == "sampler")
    {
        auto sampler = std::make_shared<SamplerGenerator>();
        sampler->prepare (sampleRate, blockSize);
        const auto path = channel[ids::samplePath].toString();
        bool loaded = path.isNotEmpty() && sampler->loadSampleFile (juce::File (path));
        if (! loaded)
            sampler->useSynthesizedDrum (channel[ids::name].toString(), sampleRate);
        sampler->setRootNote (channel[ids::rootNote]);
        gen = sampler;
    }
    else if (type == "synth")
    {
        auto synth = std::make_shared<SynthGenerator>();
        synth->prepare (sampleRate, blockSize);
        gen = synth;
    }
    else if (type == "plugin" && pluginManager != nullptr)
    {
        auto pluginGen = std::make_shared<PluginGenerator>();
        pluginGen->prepare (sampleRate, blockSize);
        gen = pluginGen;

        const auto pluginId = channel[ids::pluginId].toString();
        const auto stateBase64 = channel[ids::pluginState].toString();
        if (auto desc = pluginManager->findByIdentifier (pluginId))
        {
            pluginManager->createInstance (*desc, sampleRate, blockSize,
                [pluginGen, desc, stateBase64, rebuild = onPluginReady]
                (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
                {
                    juce::ignoreUnused (error);
                    if (instance == nullptr)
                    {
                        DBG ("Instrument load failed: " + error);
                        return;
                    }
                    auto hosted = std::make_shared<HostedPlugin> (std::move (instance), *desc);
                    if (stateBase64.isNotEmpty())
                        hosted->setStateFromBase64 (stateBase64);
                    pluginGen->setPlugin (std::move (hosted));
                    if (rebuild)
                        rebuild();
                });
        }
    }

    if (gen != nullptr)
        pool[chId] = gen;
    return gen;
}

void GeneratorPool::setAudioSpec (double newSampleRate, int newBlockSize)
{
    sampleRate = newSampleRate;
    blockSize  = newBlockSize;
    for (auto& [id, gen] : pool)
        gen->prepare (sampleRate, blockSize);
}
