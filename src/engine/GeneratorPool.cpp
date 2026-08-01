#include "GeneratorPool.h"
#include "SamplerGenerator.h"
#include "SynthGenerator.h"
#include "model/Ids.h"
#include "plugins/PluginManager.h"
#include "plugins/PluginGenerator.h"

namespace
{
// Reads a channel property with a fallback, so older projects and freshly
// created channels both work without migration.
float prop (const juce::ValueTree& tree, const juce::Identifier& id, float fallback)
{
    return tree.hasProperty (id) ? (float) (double) tree[id] : fallback;
}

void applySamplerParams (SamplerGenerator& sampler, const juce::ValueTree& channel)
{
    auto& p = sampler.params();
    p.attack.store    (prop (channel, ids::attack, 0.001f));
    p.decay.store     (prop (channel, ids::decay, 0.0f));
    p.sustain.store   (prop (channel, ids::sustain, 1.0f));
    p.release.store   (prop (channel, ids::release, 0.02f));
    p.cutoff.store    (prop (channel, ids::cutoff, 20000.0f));
    p.resonance.store (prop (channel, ids::resonance, 0.0f));
    p.oneShot.store   (channel.hasProperty (ids::oneShot) ? (bool) channel[ids::oneShot] : true);
    sampler.setRootNote ((int) channel.getProperty (ids::rootNote, 60));
}

void applySynthParams (SynthGenerator& synth, const juce::ValueTree& channel)
{
    auto& p = synth.params();
    p.attack.store          (prop (channel, ids::attack, 0.004f));
    p.decay.store           (prop (channel, ids::decay, 0.25f));
    p.sustain.store         (prop (channel, ids::sustain, 0.7f));
    p.release.store         (prop (channel, ids::release, 0.08f));
    p.cutoffHz.store        (prop (channel, ids::cutoff, 4000.0f));
    p.resonance.store       (prop (channel, ids::resonance, 0.3f));
    p.osc2DetuneCents.store (prop (channel, ids::osc2Detune, 7.0f));
    p.osc2Mix.store         (prop (channel, ids::osc2Mix, 0.35f));
    p.oscShape.store        (prop (channel, ids::oscShape, 0.0f));
    p.filterEnvAmount.store (prop (channel, ids::filterEnvAmt, 0.35f));
}
}

std::shared_ptr<Generator> GeneratorPool::getOrCreate (const juce::ValueTree& channel)
{
    const int chId = channel[ids::id];
    const juce::String type = channel[ids::type].toString();

    if (auto it = pool.find (chId); it != pool.end())
    {
        // Push live parameter edits into the existing generator.
        if (auto* sampler = dynamic_cast<SamplerGenerator*> (it->second.get()))
        {
            const auto path = channel[ids::samplePath].toString();
            if (path.isNotEmpty() && path != sampler->getSamplePath())
                sampler->loadSampleFile (juce::File (path));
            applySamplerParams (*sampler, channel);
        }
        else if (auto* synth = dynamic_cast<SynthGenerator*> (it->second.get()))
        {
            applySynthParams (*synth, channel);
        }
        return it->second;
    }

    std::shared_ptr<Generator> gen;

    if (type == "sampler")
    {
        auto sampler = std::make_shared<SamplerGenerator>();
        sampler->prepare (sampleRate, blockSize);
        const auto path = channel[ids::samplePath].toString();
        const bool loaded = path.isNotEmpty() && sampler->loadSampleFile (juce::File (path));
        if (! loaded)
            sampler->useSynthesizedDrum (channel[ids::name].toString(), sampleRate);
        applySamplerParams (*sampler, channel);
        gen = sampler;
    }
    else if (type == "synth")
    {
        auto synth = std::make_shared<SynthGenerator>();
        synth->prepare (sampleRate, blockSize);
        applySynthParams (*synth, channel);
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
