#pragma once

#include "AutomationWriter.h"
#include "model/ProjectModel.h"
#include "engine/AudioEngine.h"
#include "engine/GeneratorPool.h"
#include "engine/EngineSync.h"
#include "engine/AudioClipCache.h"
#include "plugins/PluginManager.h"
#include "plugins/EffectPool.h"
#include "plugins/PluginGenerator.h"
#include "plugins/PluginWindowManager.h"
#include "ui/mixer/BuiltinEffectEditor.h"

// Owns the long-lived application services and wires them together.
// Constructed once by MainComponent and passed by reference to panels.
class AppServices : private juce::ChangeListener
{
public:
    // openAudioDevice=false skips CoreAudio entirely (tests, headless tools);
    // the engine still works via its offline path.
    explicit AppServices (bool openAudioDevice = true)
        : effects (plugins),
          engineSync (project, generators, effects, builtinEffects, audioClips, engine)
    {
        if (openAudioDevice)
        {
            const auto err = engine.initialise();
            if (err.isNotEmpty())
                DBG ("Audio device error: " + err);
        }
        else
        {
            engine.prepareOffline (44100.0, 512);
        }

        generators.setPluginContext (&plugins, [this] { engineSync.rebuildNow(); });
        effects.onInstanceReady = [this] { engineSync.rebuildNow(); };

        updateAudioSpec();
        engine.getDeviceManager().addChangeListener (this);
        engineSync.rebuildNow();
    }

    ~AppServices() override
    {
        engine.getDeviceManager().removeChangeListener (this);
    }

    // Creates an automation source plus a playlist clip for it. Returns the
    // automation tree. The clip lands on the first track free at bar 0.
    juce::ValueTree createAutomationWithClip (const juce::String& targetType, int targetId,
                                              const juce::String& paramId, const juce::String& name,
                                              double initialValue)
    {
        auto automation = AutomationWriter::createWithClip (project, targetType, targetId,
                                                            paramId, name, initialValue);
        // Creation used to be silent, which read as "nothing happened"; the
        // host brings the playlist forward and flags the new clip.
        if (onAutomationClipCreated)
            if (auto clip = AutomationWriter::findClip (project, (int) automation[ids::id]);
                clip.isValid())
                onAutomationClipCreated (clip);
        return automation;
    }

    // Set by the host window: show the playlist and highlight a fresh clip.
    std::function<void (juce::ValueTree clip)> onAutomationClipCreated;

    // Captures live plugin state into the tree, then writes the file.
    bool saveProject (const juce::File& file)
    {
        capturePluginState();
        return project.saveToFile (file);
    }

    bool loadProject (const juce::File& file)
    {
        if (! project.loadFromFile (file))
            return false;
        engineSync.attachToProject();
        return true;
    }

    void capturePluginState()
    {
        for (int i = 0; i < project.numChannels(); ++i)
        {
            auto channel = project.getChannel (i);
            if (channel[ids::type].toString() != "plugin")
                continue;
            if (auto gen = std::dynamic_pointer_cast<PluginGenerator> (generators.getOrCreate (channel)))
                if (auto hosted = gen->getPlugin())
                    channel.setProperty (ids::pluginState, hosted->getStateBase64(), nullptr);
        }

        effects.forEach ([this] (int insertIndex, int slotIndex, const HostedPlugin& plugin)
        {
            auto insert = project.getInsert (insertIndex);
            for (auto slot : insert)
                if (slot.hasType (ids::SLOT) && (int) slot[ids::slotIndex] == slotIndex)
                    slot.setProperty (ids::pluginState, plugin.getStateBase64(), nullptr);
        });
    }

    ProjectModel        project;
    PluginManager       plugins;
    GeneratorPool       generators;
    EffectPool          effects;
    BuiltinEffectPool   builtinEffects;
    AudioClipCache      audioClips;
    AudioEngine         engine;
    EngineSync          engineSync;
    PluginWindowManager pluginWindows;
    AutomationWriter    automationWriter { project, engine };
    BuiltinEffectWindows builtinEditors;

private:
    void updateAudioSpec()
    {
        if (auto* device = engine.getDeviceManager().getCurrentAudioDevice())
        {
            generators.setAudioSpec (device->getCurrentSampleRate(),
                                     device->getCurrentBufferSizeSamples());
            effects.setAudioSpec (device->getCurrentSampleRate(),
                                  device->getCurrentBufferSizeSamples());
            builtinEffects.setAudioSpec (device->getCurrentSampleRate(),
                                         device->getCurrentBufferSizeSamples());
            audioClips.setEngineSampleRate (device->getCurrentSampleRate());
        }
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        updateAudioSpec();
        engineSync.rebuildNow();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AppServices)
};
