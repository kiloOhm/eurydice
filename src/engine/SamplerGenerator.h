#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include "Generator.h"

// FL-style sampler channel: one sample, repitched by note, with an amp
// envelope and a lowpass filter. In one-shot mode (the drum default) note-offs
// are ignored and the sample plays to its end. Sample loading happens on the
// message thread; the audio thread only reads via shared_ptr.
class SamplerGenerator : public Generator
{
public:
    struct Params
    {
        std::atomic<float> attack { 0.001f }, decay { 0.0f }, sustain { 1.0f }, release { 0.02f };
        std::atomic<float> cutoff { 20000.0f };
        std::atomic<float> resonance { 0.0f };
        std::atomic<float> gain { 1.0f };
        std::atomic<bool>  oneShot { true };
    };

    SamplerGenerator();

    void prepare (double sampleRate, int maxBlockSize) override;
    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi) override;
    void reset() override;

    std::atomic<float>* getAutomatableParam (const juce::String& paramId) override
    {
        if (paramId == "attack")    return &p.attack;
        if (paramId == "decay")     return &p.decay;
        if (paramId == "sustain")   return &p.sustain;
        if (paramId == "release")   return &p.release;
        if (paramId == "cutoff")    return &p.cutoff;
        if (paramId == "resonance") return &p.resonance;
        return nullptr;
    }

    // Message thread. Returns false if the file couldn't be read.
    bool loadSampleFile (const juce::File&);

    // Synthesized fallback so default channels make sound with no sample set.
    void useSynthesizedDrum (const juce::String& kind, double sampleRate);

    void setRootNote (int note)      { rootNote.store (note); }
    int  getRootNote() const         { return rootNote.load(); }
    Params& params()                 { return p; }

    juce::String getSamplePath() const { return samplePath; }
    double getSampleLengthSeconds() const;

    // A coarse waveform outline for the editor: peak magnitude per bucket.
    std::vector<float> getWaveformOutline (int numBuckets) const;

private:
    struct Sample
    {
        juce::AudioBuffer<float> data;   // always stereo
        double sourceSampleRate = 44100.0;
    };

    struct Voice
    {
        bool active = false;
        int key = -1;
        double pos = 0.0;
        double rate = 1.0;
        float velocity = 1.0f;
        juce::ADSR env;
        juce::dsp::StateVariableTPTFilter<float> filter;
        std::shared_ptr<const Sample> sample;   // voice keeps its sample alive
    };

    void startVoice (int key, float velocity);
    void stopVoice (int key);
    void renderSegment (juce::AudioBuffer<float>& out, int from, int to);
    std::shared_ptr<const Sample> getSample() const;

    static constexpr int maxVoices = 32;
    std::array<Voice, maxVoices> voices;

    // Swapped on message thread, read on audio thread.
    std::shared_ptr<const Sample> currentSample;
    mutable juce::SpinLock sampleLock;

    Params p;
    std::atomic<int> rootNote { 60 };
    double deviceSampleRate = 44100.0;
    juce::String samplePath;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplerGenerator)
};
