#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include "Generator.h"

// FL-style sampler channel: one sample, repitched by note, one-shot playback
// (note-offs are ignored; reset() kills voices). Sample loading happens on the
// message thread; the audio thread only reads the buffer via shared_ptr.
class SamplerGenerator : public Generator
{
public:
    SamplerGenerator();

    void prepare (double sampleRate, int maxBlockSize) override;
    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi) override;
    void reset() override;

    // Message thread. Returns false if the file couldn't be read.
    bool loadSampleFile (const juce::File&);

    // Synthesized fallback so default channels make sound with no sample set.
    void useSynthesizedDrum (const juce::String& kind, double sampleRate);

    void setRootNote (int note)      { rootNote.store (note); }
    void setSampleGain (float gain)  { sampleGain.store (gain); }

    juce::String getSamplePath() const { return samplePath; }

private:
    struct Sample
    {
        juce::AudioBuffer<float> data;   // always stereo
        double sourceSampleRate = 44100.0;
    };

    struct Voice
    {
        bool active = false;
        double pos = 0.0;
        double rate = 1.0;
        float gainL = 0.0f, gainR = 0.0f;
        std::shared_ptr<const Sample> sample;   // voice keeps its sample alive
    };

    void startVoice (int key, float velocity);

    static constexpr int maxVoices = 32;
    std::array<Voice, maxVoices> voices;

    // Swapped on message thread, read on audio thread.
    std::shared_ptr<const Sample> currentSample;
    juce::SpinLock sampleLock;

    std::atomic<int>   rootNote { 60 };
    std::atomic<float> sampleGain { 1.0f };
    double deviceSampleRate = 44100.0;
    juce::String samplePath;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplerGenerator)
};
