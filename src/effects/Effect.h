#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// Anything that can sit in a mixer insert slot: hosted VST3/AU plugins and
// Eurydice's own built-in effects. prepare()/reset() are message thread,
// process() is the audio thread and must not allocate, lock or touch the model.
class Effect
{
public:
    // Per-block information the engine hands to the effect. sidechain points
    // at the insert bus named by getSidechainInsert(), or is null when no
    // external source is selected.
    struct Context
    {
        double tempo = 140.0;
        double ppqPosition = 0.0;
        bool   playing = false;
        const juce::AudioBuffer<float>* sidechain = nullptr;
    };

    virtual ~Effect() = default;

    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void process (juce::AudioBuffer<float>& stereoBus, int numSamples, const Context& context) = 0;
    virtual void reset() = 0;

    // Mixer insert whose bus should be fed to this effect as a sidechain,
    // or -1 for none.
    virtual int getSidechainInsert() const noexcept { return -1; }
};
