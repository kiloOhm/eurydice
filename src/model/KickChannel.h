#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include "Ids.h"
#include "KickEnvelope.h"
#include "engine/Drive.h"
#include "engine/KickGenerator.h"

// A kick CHANNEL tree pushed into a KickGenerator, and the offline render that
// falls out of it. The engine's snapshot rebuild and the editor's output
// display both go through here, so what the editor draws is what plays.
namespace kickchannel
{
inline float prop (const juce::ValueTree& tree, const juce::Identifier& id, float fallback)
{
    return tree.hasProperty (id) ? (float) (double) tree[id] : fallback;
}

inline void apply (KickGenerator& kick, const juce::ValueTree& channel)
{
    auto& p = kick.params();
    p.startFreq.store   (prop (channel, ids::kickStartFreq, 240.0f));
    p.endFreq.store     (prop (channel, ids::kickEndFreq, 48.0f));
    p.pitchDecay.store  (prop (channel, ids::kickPitchDecay, 0.035f));
    p.bodyShape.store   (prop (channel, ids::kickBodyShape, 0.0f));
    p.bodyHarm.store    (prop (channel, ids::kickBodyHarm, 0.0f));
    p.bodyPhase.store   (prop (channel, ids::kickBodyPhase, 0.0f));
    p.bodyLevel.store   (prop (channel, ids::kickBodyLevel, 1.0f));
    p.ampDecay.store    (prop (channel, ids::kickAmpDecay, 0.5f));
    p.hold.store        (prop (channel, ids::kickHold, 0.0f));
    p.envShape.store    (prop (channel, ids::envShape, 1.0f));
    p.punch.store       (prop (channel, ids::kickPunch, 0.0f));
    p.subLevel.store    (prop (channel, ids::kickSubLevel, 0.0f));
    p.subTune.store     (prop (channel, ids::kickSubTune, 0.0f));
    p.subDecay.store    (prop (channel, ids::kickSubDecay, 0.4f));
    p.clickLevel.store  (prop (channel, ids::kickClickLevel, 0.3f));
    p.clickDecay.store  (prop (channel, ids::kickClickDecay, 0.004f));
    p.clickFreq.store   (prop (channel, ids::kickClickFreq, 1400.0f));
    p.clickType.store   (juce::jlimit (0, kickdsp::numClickTypes - 1,
                                       juce::roundToInt (prop (channel, ids::kickClickType, 0.0f))));
    p.noiseLevel.store  (prop (channel, ids::kickNoiseLevel, 0.12f));
    p.noiseDecay.store  (prop (channel, ids::kickNoiseDecay, 0.02f));
    p.noiseTone.store   (prop (channel, ids::kickNoiseTone, 0.4f));
    p.drive.store       (prop (channel, ids::drive, 0.25f));
    p.driveCurve.store  (juce::jlimit (0, drive::numCurves - 1,
                                       juce::roundToInt (prop (channel, ids::driveCurve, 0.0f))));
    p.eqLowFreq.store   (prop (channel, ids::kickEqLowFreq, 90.0f));
    p.eqLowGain.store   (prop (channel, ids::kickEqLowGain, 0.0f));
    p.eqMidFreq.store   (prop (channel, ids::kickEqMidFreq, 500.0f));
    p.eqMidGain.store   (prop (channel, ids::kickEqMidGain, 0.0f));
    p.eqHighFreq.store  (prop (channel, ids::kickEqHighFreq, 4000.0f));
    p.eqHighGain.store  (prop (channel, ids::kickEqHighGain, 0.0f));
    p.compression.store (prop (channel, ids::kickComp, 0.0f));
    p.limiter.store     (prop (channel, ids::kickLimit, 0.0f));
    p.outputDb.store    (prop (channel, ids::kickOutput, 0.0f));
    kick.setRootNote ((int) channel.getProperty (ids::rootNote, 60));

    kick.setPitchEnvelope (kickenv::read (channel, kickenv::pitchRole));
    kick.setAmpEnvelope   (kickenv::read (channel, kickenv::ampRole));
    kick.setClickSample (channel[ids::samplePath].toString());
}

// How long one hit of this kick actually lasts, so a render or a display can
// size itself without guessing.
inline double lengthSeconds (const juce::ValueTree& channel)
{
    const double hold = juce::jmax (0.0f, prop (channel, ids::kickHold, 0.0f));
    const double ampDecay = juce::jmax (0.01f, prop (channel, ids::kickAmpDecay, 0.5f));
    const double tail = kickenv::isDrawn (channel, kickenv::ampRole) ? ampDecay : ampDecay * 1.15;
    return hold + tail + 0.01;
}

// One hit, rendered offline at the note the channel is tuned to. Message
// thread only — it builds a generator of its own, so it never touches the one
// the engine is playing.
inline juce::AudioBuffer<float> render (const juce::ValueTree& channel, double sampleRate,
                                        int note = -1, float velocity = 1.0f,
                                        double seconds = 0.0)
{
    KickGenerator kick;
    kick.prepare (sampleRate, 512);
    apply (kick, channel);

    if (note < 0)
        note = (int) channel.getProperty (ids::rootNote, 60);
    if (seconds <= 0.0)
        seconds = lengthSeconds (channel);

    const int numSamples = juce::jlimit (256, (int) (sampleRate * 10.0),
                                         (int) (seconds * sampleRate));
    juce::AudioBuffer<float> out (2, numSamples);
    out.clear();

    int pos = 0;
    while (pos < numSamples)
    {
        const int n = juce::jmin (512, numSamples - pos);
        juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, pos, n);
        juce::MidiBuffer midi;
        if (pos == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, note, velocity), 0);
        kick.render (view, midi);
        pos += n;
    }
    return out;
}
} // namespace kickchannel
