#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include "Ids.h"
#include "engine/SynthGenerator.h"

// A synth CHANNEL tree pushed into a SynthGenerator. The engine's snapshot
// rebuild goes through here, so anything that writes the tree — the editor, a
// factory preset, the control API — reaches the voices the same way.
namespace synthchannel
{
inline float prop (const juce::ValueTree& tree, const juce::Identifier& id, float fallback)
{
    return tree.hasProperty (id) ? (float) (double) tree[id] : fallback;
}

inline void apply (SynthGenerator& synth, const juce::ValueTree& channel)
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
} // namespace synthchannel
