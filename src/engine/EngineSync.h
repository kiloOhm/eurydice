#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include "AudioEngine.h"
#include "GeneratorPool.h"
#include "model/ProjectModel.h"
#include "plugins/EffectPool.h"
#include "AudioClipCache.h"

// Watches the project ValueTree and rebuilds + publishes an EngineSnapshot
// whenever anything changes (coalesced per message-loop tick).
class EngineSync : private juce::ValueTree::Listener,
                   private juce::AsyncUpdater
{
public:
    EngineSync (ProjectModel&, GeneratorPool&, EffectPool&, AudioClipCache&, AudioEngine&);
    ~EngineSync() override;

    void rebuildNow();   // synchronous rebuild+publish (used at startup / after load)

    // Call when the root tree object is replaced (project load).
    void attachToProject();

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { triggerAsyncUpdate(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override             { triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override      { triggerAsyncUpdate(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override              { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { rebuildNow(); }

    std::shared_ptr<const EngineSnapshot> build() const;

    ProjectModel& model;
    GeneratorPool& generators;
    EffectPool& effects;
    AudioClipCache& audioClips;
    AudioEngine& engine;
    juce::ValueTree observedRoot;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EngineSync)
};
