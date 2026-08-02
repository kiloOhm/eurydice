#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

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

    // Automation target for one of the channel parameters in ChannelParams.h,
    // or null when this generator has no such parameter. The pointer stays
    // valid for the generator's lifetime, so the audio thread may keep it in a
    // snapshot as long as the snapshot pins the generator.
    virtual std::atomic<float>* getAutomatableParam (const juce::String& paramId)
    {
        juce::ignoreUnused (paramId);
        return nullptr;
    }
};
