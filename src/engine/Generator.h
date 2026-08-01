#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// A sound source owned by a channel (sampler, synth, hosted plugin).
// render() is called on the audio thread and must be RT-safe.
// Instances outlive engine snapshots; they are created/destroyed on the
// message thread only.
class Generator
{
public:
    virtual ~Generator() = default;

    virtual void prepare (double sampleRate, int maxBlockSize) = 0;

    // Add (not replace) this generator's output into the stereo buffer.
    virtual void render (juce::AudioBuffer<float>& stereoOut, const juce::MidiBuffer& midi) = 0;

    // Kill all voices immediately (transport stop).
    virtual void reset() = 0;
};
