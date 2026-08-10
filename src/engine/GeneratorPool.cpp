#include "GeneratorPool.h"
#include "Drive.h"
#include "DrumMachineGenerator.h"
#include "KickGenerator.h"
#include "SamplerGenerator.h"
#include "SynthGenerator.h"
#include "model/Ids.h"
#include "plugins/PluginManager.h"
#include "plugins/PluginGenerator.h"
#include "sandbox/SandboxedGenerator.h"

namespace
{
// Reads a channel property with a fallback, so older projects and freshly
// created channels both work without migration.
float prop (const juce::ValueTree& tree, const juce::Identifier& id, float fallback)
{
    return tree.hasProperty (id) ? (float) (double) tree[id] : fallback;
}

int curveProp (const juce::ValueTree& tree, const juce::Identifier& id)
{
    return juce::jlimit (0, drive::numCurves - 1, juce::roundToInt (prop (tree, id, 0.0f)));
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
    p.sampleStart.store    (prop (channel, ids::sampleStart, 0.0f));
    p.sampleEnd.store      (prop (channel, ids::sampleEnd, 1.0f));
    p.reverse.store        (channel.hasProperty (ids::reverse) ? (bool) channel[ids::reverse] : false);
    p.pitchEnvDepth.store  (prop (channel, ids::pitchEnvDepth, 0.0f));
    p.pitchEnvDecay.store  (prop (channel, ids::pitchEnvDecay, 0.08f));
    p.drive.store          (prop (channel, ids::drive, 0.0f));
    p.driveCurve.store     (curveProp (channel, ids::driveCurve));
    p.envShape.store       (prop (channel, ids::envShape, 0.0f));
    sampler.setRootNote ((int) channel.getProperty (ids::rootNote, 60));
}

void applyKickParams (KickGenerator& kick, const juce::ValueTree& channel)
{
    auto& p = kick.params();
    p.startFreq.store  (prop (channel, ids::kickStartFreq, 240.0f));
    p.endFreq.store    (prop (channel, ids::kickEndFreq, 48.0f));
    p.pitchDecay.store (prop (channel, ids::kickPitchDecay, 0.035f));
    p.ampDecay.store   (prop (channel, ids::kickAmpDecay, 0.5f));
    p.bodyShape.store  (prop (channel, ids::kickBodyShape, 0.0f));
    p.clickLevel.store (prop (channel, ids::kickClickLevel, 0.3f));
    p.clickDecay.store (prop (channel, ids::kickClickDecay, 0.004f));
    p.noiseLevel.store (prop (channel, ids::kickNoiseLevel, 0.12f));
    p.noiseDecay.store (prop (channel, ids::kickNoiseDecay, 0.02f));
    p.drive.store      (prop (channel, ids::drive, 0.25f));
    p.driveCurve.store (curveProp (channel, ids::driveCurve));
    p.envShape.store   (prop (channel, ids::envShape, 1.0f));
    kick.setRootNote ((int) channel.getProperty (ids::rootNote, 60));
}

void applyDrumParams (DrumMachineGenerator& drums, const juce::ValueTree& channel)
{
    int padIndex = 0;
    for (const auto& pad : channel)
    {
        if (! pad.hasType (ids::PAD) || padIndex >= DrumMachineGenerator::maxPads)
            continue;

        auto& params = drums.padParams (padIndex);
        params.key.store ((int) pad.getProperty (ids::key, -1));
        params.gain.store (prop (pad, ids::volume, 0.9f));
        params.pan.store (prop (pad, ids::pan, 0.0f));
        params.tune.store (prop (pad, ids::tune, 0.0f));
        params.choke.store ((int) pad.getProperty (ids::choke, 0));
        drums.setPadSource (padIndex, pad[ids::samplePath].toString(),
                            pad[ids::synthDrum].toString());
        ++padIndex;
    }
    drums.setNumPads (padIndex);
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
    p.oscWarp.store         (prop (channel, ids::oscWarp, 0.0f));
    p.osc2Semi.store        (prop (channel, ids::osc2Semi, 0.0f));
    p.unisonVoices.store    (prop (channel, ids::unisonVoices, 1.0f));
    p.unisonDetune.store    (prop (channel, ids::unisonDetune, 18.0f));
    p.unisonWidth.store     (prop (channel, ids::unisonWidth, 0.7f));
    p.subLevel.store        (prop (channel, ids::subLevel, 0.0f));
    p.noiseLevel.store      (prop (channel, ids::noiseLevel, 0.0f));
    p.filterType.store      (prop (channel, ids::filterType, 0.0f));
    p.filterKey.store       (prop (channel, ids::filterKey, 0.0f));
    // Older projects had the filter envelope borrow the amp ADSR (S pinned
    // at 0.2); falling back to those values keeps them sounding the same.
    p.fenvAttack.store      (prop (channel, ids::fenvAttack, prop (channel, ids::attack, 0.004f)));
    p.fenvDecay.store       (prop (channel, ids::fenvDecay, prop (channel, ids::decay, 0.25f)));
    p.fenvSustain.store     (prop (channel, ids::fenvSustain, 0.2f));
    p.fenvRelease.store     (prop (channel, ids::fenvRelease, prop (channel, ids::release, 0.08f)));
    p.lfoRate.store         (prop (channel, ids::lfoRate, 5.0f));
    p.lfoAmount.store       (prop (channel, ids::lfoAmount, 0.0f));
    p.lfoTarget.store       (prop (channel, ids::lfoTarget, 0.0f));
    p.glide.store           (prop (channel, ids::glide, 0.0f));
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
        else if (auto* drums = dynamic_cast<DrumMachineGenerator*> (it->second.get()))
        {
            applyDrumParams (*drums, channel);
        }
        else if (auto* kick = dynamic_cast<KickGenerator*> (it->second.get()))
        {
            applyKickParams (*kick, channel);
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
    else if (type == "kick")
    {
        auto kick = std::make_shared<KickGenerator>();
        kick->prepare (sampleRate, blockSize);
        applyKickParams (*kick, channel);
        gen = kick;
    }
    else if (type == "drums")
    {
        auto drums = std::make_shared<DrumMachineGenerator>();
        drums->prepare (sampleRate, blockSize);
        applyDrumParams (*drums, channel);
        gen = drums;
    }
    else if (type == "plugin" && pluginManager != nullptr)
    {
        const auto pluginId = channel[ids::pluginId].toString();
        const auto stateBase64 = channel[ids::pluginState].toString();

        const bool wantsSandbox = channel.hasProperty (ids::sandboxed)
                                      ? (bool) channel[ids::sandboxed] : sandboxEnabled;
        if (wantsSandbox)
        {
            auto sandboxGen = std::make_shared<SandboxedGenerator>();
            gen = sandboxGen;
            launchSandboxedInstrument (sandboxGen, pluginId, stateBase64);
        }
        else
        {
            auto pluginGen = std::make_shared<PluginGenerator>();
            pluginGen->prepare (sampleRate, blockSize);
            gen = pluginGen;

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
    }

    if (gen != nullptr)
        pool[chId] = gen;
    return gen;
}

void GeneratorPool::launchSandboxedInstrument (std::shared_ptr<SandboxedGenerator> generator,
                                               const juce::String& pluginId,
                                               const juce::String& stateBase64)
{
    const double sr = sampleRate;
    const int bs = blockSize;
    juce::Thread::launch ([generator, pluginId, stateBase64, sr, bs, rebuild = onPluginReady]
    {
        auto sandboxed = std::make_shared<SandboxedPlugin>();
        juce::String error;
        const bool ok = sandboxed->launch (pluginId, sr, bs, stateBase64, error);
        juce::MessageManager::callAsync ([generator, sandboxed, ok, error, rebuild]
        {
            if (! ok)
            {
                DBG ("Sandboxed instrument load failed: " + error);
                return;
            }
            generator->setPlugin (sandboxed);
            if (rebuild)
                rebuild();
        });
    });
}

bool GeneratorPool::checkSandboxHealth()
{
    bool anyDied = false;
    for (auto& [channelId, gen] : pool)
    {
        auto* sandboxGen = dynamic_cast<SandboxedGenerator*> (gen.get());
        if (sandboxGen == nullptr || crashedChannels.count (channelId) > 0)
            continue;
        if (auto plugin = sandboxGen->getPlugin(); plugin != nullptr && ! plugin->isAlive())
        {
            crashedChannels.insert (channelId);
            anyDied = true;
        }
    }
    return anyDied;
}

void GeneratorPool::restartSandboxed (const juce::ValueTree& channel)
{
    const int channelId = channel[ids::id];
    auto it = pool.find (channelId);
    auto* sandboxGen = it != pool.end() ? dynamic_cast<SandboxedGenerator*> (it->second.get())
                                        : nullptr;
    if (sandboxGen == nullptr)
        return;
    crashedChannels.erase (channelId);
    sandboxGen->setPlugin (nullptr);   // silence while the relaunch runs
    launchSandboxedInstrument (std::static_pointer_cast<SandboxedGenerator> (it->second),
                               channel[ids::pluginId].toString(),
                               channel[ids::pluginState].toString());
}

void GeneratorPool::setAudioSpec (double newSampleRate, int newBlockSize)
{
    sampleRate = newSampleRate;
    blockSize  = newBlockSize;
    for (auto& [id, gen] : pool)
        gen->prepare (sampleRate, blockSize);
}
