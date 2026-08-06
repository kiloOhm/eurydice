#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "Generator.h"

// Built-in polyphonic synth, Serum-flavoured: two morphing oscillators
// (sine/tri/saw/square with warp), unison with stereo spread, sub and noise,
// an LP/BP/HP filter with keytracking and its own bipolar envelope, one LFO
// with four destinations, and glide. Parameters are atomics set from the
// message thread (channel editor / automation).
class SynthGenerator : public Generator
{
public:
    struct Params
    {
        // oscillators
        std::atomic<float> oscShape  { 0.0f };      // morph: -2 sine .. 1 square
        std::atomic<float> oscWarp   { 0.0f };      // 0..1: bend / pulse width
        std::atomic<float> osc2Semi  { 0.0f };      // -24..24 semitones
        std::atomic<float> osc2DetuneCents { 7.0f };
        std::atomic<float> osc2Mix   { 0.35f };     // 0..1

        // unison
        std::atomic<float> unisonVoices { 1.0f };   // 1..7
        std::atomic<float> unisonDetune { 18.0f };  // spread in cents
        std::atomic<float> unisonWidth  { 0.7f };   // 0..1 stereo spread

        // layers
        std::atomic<float> subLevel   { 0.0f };     // sine, one octave down
        std::atomic<float> noiseLevel { 0.0f };

        // filter
        std::atomic<float> filterType { 0.0f };     // 0 LP, 1 BP, 2 HP
        std::atomic<float> cutoffHz  { 4000.0f };
        std::atomic<float> resonance { 0.3f };      // 0..1
        std::atomic<float> filterKey { 0.0f };      // 0..1 keytracking
        std::atomic<float> filterEnvAmount { 0.35f };   // -1..1

        // filter envelope (its own ADSR; defaults match the old borrowed one)
        std::atomic<float> fenvAttack { 0.004f }, fenvDecay { 0.25f },
                           fenvSustain { 0.2f },  fenvRelease { 0.08f };

        // amp envelope
        std::atomic<float> attack  { 0.004f }, decay { 0.25f }, sustain { 0.7f }, release { 0.08f };

        // LFO
        std::atomic<float> lfoRate   { 5.0f };      // Hz
        std::atomic<float> lfoAmount { 0.0f };      // 0..1
        std::atomic<float> lfoTarget { 0.0f };      // 0 cut, 1 pitch, 2 warp, 3 pan

        // voice
        std::atomic<float> glide { 0.0f };          // seconds to close the gap

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
        if (paramId == "osc2Semi")     return &p.osc2Semi;
        if (paramId == "oscShape")     return &p.oscShape;
        if (paramId == "oscWarp")      return &p.oscWarp;
        if (paramId == "unisonVoices") return &p.unisonVoices;
        if (paramId == "unisonDetune") return &p.unisonDetune;
        if (paramId == "unisonWidth")  return &p.unisonWidth;
        if (paramId == "subLevel")     return &p.subLevel;
        if (paramId == "noiseLevel")   return &p.noiseLevel;
        if (paramId == "filterType")   return &p.filterType;
        if (paramId == "filterKey")    return &p.filterKey;
        if (paramId == "filterEnvAmt") return &p.filterEnvAmount;
        if (paramId == "fenvAttack")   return &p.fenvAttack;
        if (paramId == "fenvDecay")    return &p.fenvDecay;
        if (paramId == "fenvSustain")  return &p.fenvSustain;
        if (paramId == "fenvRelease")  return &p.fenvRelease;
        if (paramId == "lfoRate")      return &p.lfoRate;
        if (paramId == "lfoAmount")    return &p.lfoAmount;
        if (paramId == "lfoTarget")    return &p.lfoTarget;
        if (paramId == "glide")        return &p.glide;
        return nullptr;
    }

    Params& params() { return p; }

    static constexpr int maxUnison = 7;

private:
    float pendingPan = 0.0f;   // CC10 latch, consumed by the next note-on

    struct Voice
    {
        bool active = false;
        int key = -1;
        float velocity = 1.0f;
        float panL = 1.0f, panR = 1.0f;   // balance law, set per note

        // one phase set per unison voice, both oscillators
        std::array<double, maxUnison> phase1 {}, phase2 {};
        std::array<float, maxUnison> uniOffset {};   // -1..1 detune position
        std::array<float, maxUnison> uniPanL {}, uniPanR {};
        int unison = 1;

        double subPhase = 0.0;
        double currentSemi = 60.0, targetSemi = 60.0;   // glide

        juce::ADSR ampEnv, filterEnv;
        juce::dsp::StateVariableTPTFilter<float> filter;
    };

    void noteOn (int key, float velocity);
    void noteOff (int key);
    void renderSegment (juce::AudioBuffer<float>& out, int from, int to);

    static constexpr int maxVoices = 16;
    std::array<Voice, maxVoices> voices;
    Params p;
    double sampleRate = 44100.0;

    double lfoPhase = 0.0;
    juce::uint32 noiseState = 0x9e3779b9;
    double lastSemi = -1.0;         // glide source: the previously played key

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthGenerator)
};
