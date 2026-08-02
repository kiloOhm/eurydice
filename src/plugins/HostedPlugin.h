#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "effects/Effect.h"

// RT-safe wrapper around an AudioPluginInstance that adapts our stereo buses
// to whatever channel layout the plugin wants. Created and prepared on the
// message thread; process* methods are called on the audio thread.
class HostedPlugin : public Effect
{
public:
    HostedPlugin (std::unique_ptr<juce::AudioPluginInstance> inst, const juce::PluginDescription& d)
        : instance (std::move (inst)), description (d) {}

    ~HostedPlugin() override
    {
        if (prepared)
            instance->releaseResources();
    }

    void prepare (double sampleRate, int blockSize) override
    {
        if (prepared)
            instance->releaseResources();

        instance->enableAllBuses();
        instance->setRateAndBufferSizeDetails (sampleRate, blockSize);
        instance->prepareToPlay (sampleRate, blockSize);

        const int chans = juce::jmax (2, instance->getTotalNumInputChannels(),
                                      instance->getTotalNumOutputChannels());
        scratch.setSize (chans, blockSize);
        midiScratch.ensureSize (4096);
        prepared = true;
    }

    // Effects: process the stereo bus in place.
    void process (juce::AudioBuffer<float>& stereoBus, int numSamples, const Context& context) override
    {
        juce::ignoreUnused (context);
        if (! prepared)
            return;

        scratch.clear (0, numSamples);
        for (int ch = 0; ch < juce::jmin (2, scratch.getNumChannels()); ++ch)
            scratch.copyFrom (ch, 0, stereoBus, ch, 0, numSamples);

        juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(),
                                       scratch.getNumChannels(), 0, numSamples);
        midiScratch.clear();
        instance->processBlock (view, midiScratch);

        for (int ch = 0; ch < juce::jmin (2, scratch.getNumChannels()); ++ch)
            stereoBus.copyFrom (ch, 0, scratch, ch, 0, numSamples);
    }

    // Instruments: render midi and ADD the plugin's stereo output to out.
    void processInstrument (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, int numSamples)
    {
        if (! prepared)
            return;

        scratch.clear (0, numSamples);
        midiScratch.clear();
        midiScratch.addEvents (midi, 0, numSamples, 0);

        juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(),
                                       scratch.getNumChannels(), 0, numSamples);
        instance->processBlock (view, midiScratch);

        const int outs = juce::jmax (1, instance->getTotalNumOutputChannels());
        out.addFrom (0, 0, scratch, 0, 0, numSamples);
        out.addFrom (1, 0, scratch, outs > 1 ? 1 : 0, 0, numSamples);
    }

    void reset() override
    {
        if (prepared)
            instance->reset();
    }

    juce::String getStateBase64() const
    {
        juce::MemoryBlock state;
        instance->getStateInformation (state);
        return state.toBase64Encoding();
    }

    void setStateFromBase64 (const juce::String& base64)
    {
        juce::MemoryBlock state;
        if (state.fromBase64Encoding (base64) && state.getSize() > 0)
            instance->setStateInformation (state.getData(), (int) state.getSize());
    }

    juce::AudioPluginInstance* getInstance() const { return instance.get(); }
    const juce::PluginDescription& getDescription() const { return description; }

private:
    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::PluginDescription description;
    juce::AudioBuffer<float> scratch;
    juce::MidiBuffer midiScratch;
    bool prepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HostedPlugin)
};
