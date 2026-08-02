#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "Generator.h"

// Built-in polyphonic subtractive synth: 2 oscillators (polyBLEP saw/square),
// SVF lowpass with envelope, amp ADSR. Parameters are atomics set from the
// message thread (channel editor / automation).
class SynthGenerator : public Generator
{
public:
    struct Params
    {
        std::atomic<float> osc2DetuneCents { 7.0f };
        std::atomic<float> osc2Mix   { 0.35f };     // 0..1
        std::atomic<float> oscShape  { 0.0f };      // 0 = saw, 1 = square
        std::atomic<float> cutoffHz  { 4000.0f };
        std::atomic<float> resonance { 0.3f };      // 0..1
        std::atomic<float> filterEnvAmount { 0.35f };
        std::atomic<float> attack  { 0.004f }, decay { 0.25f }, sustain { 0.7f }, release { 0.08f };
        std::atomic<float> masterGain { 0.5f };
    };

    SynthGenerator();

    void prepare (double sampleRate, int maxBlockSize) override;
    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi) override;
    void reset() override;

    std::atomic<float>* getAutomatableParam (const juce::String& paramId) override
    {
        if (paramId == "attack")       return &p.attack;
        if (paramId == "decay")        return &p.decay;
        if (paramId == "sustain")      return &p.sustain;
        if (paramId == "release")      return &p.release;
        if (paramId == "cutoff")       return &p.cutoffHz;
        if (paramId == "resonance")    return &p.resonance;
        if (paramId == "osc2Detune")   return &p.osc2DetuneCents;
        if (paramId == "osc2Mix")      return &p.osc2Mix;
        if (paramId == "oscShape")     return &p.oscShape;
        if (paramId == "filterEnvAmt") return &p.filterEnvAmount;
        return nullptr;
    }

    Params& params() { return p; }

private:
    float pendingPan = 0.0f;   // CC10 latch, consumed by the next note-on

    struct Voice
    {
        bool active = false;
        int key = -1;
        double phase1 = 0.0, phase2 = 0.0;
        float velocity = 1.0f;
        float panL = 1.0f, panR = 1.0f;   // balance law, set per note
        juce::ADSR ampEnv, filterEnv;
        juce::dsp::StateVariableTPTFilter<float> filter;
    };

    void noteOn (int key, float velocity);
    void noteOff (int key);
    void renderSegment (juce::AudioBuffer<float>& out, int from, int to);

    static float polyBlep (double t, double dt);

    static constexpr int maxVoices = 16;
    std::array<Voice, maxVoices> voices;
    Params p;
    double sampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthGenerator)
};
