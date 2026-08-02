#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Generator.h"

// Synthesised hardcore kick: a sine/triangle body swept down by a pitch
// envelope, a click transient, a noise layer and a drive stage. One-shot —
// note-offs are ignored, every note plays its full amp decay. Parameters are
// atomics written from the message thread, like SynthGenerator.
class KickGenerator : public Generator
{
public:
    struct Params
    {
        std::atomic<float> startFreq { 240.0f };    // Hz at the root note
        std::atomic<float> endFreq { 48.0f };
        std::atomic<float> pitchDecay { 0.035f };   // seconds
        std::atomic<float> ampDecay { 0.5f };
        std::atomic<float> bodyShape { 0.0f };      // 0 = sine, 1 = triangle
        std::atomic<float> clickLevel { 0.3f };
        std::atomic<float> clickDecay { 0.004f };
        std::atomic<float> noiseLevel { 0.12f };
        std::atomic<float> noiseDecay { 0.02f };
        std::atomic<float> drive { 0.25f };         // 0..1
        std::atomic<int>   driveCurve { 0 };
        std::atomic<float> envShape { 1.0f };       // 0 = linear, 1 = exponential
        std::atomic<float> gain { 0.9f };
    };

    KickGenerator();

    void prepare (double sampleRate, int maxBlockSize) override;
    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi) override;
    void reset() override;

    void setRootNote (int note) { rootNote.store (note); }
    int  getRootNote() const    { return rootNote.load(); }
    Params& params()            { return p; }

private:
    float pendingPan = 0.0f;   // CC10 latch, consumed by the next note-on

    struct Voice
    {
        bool active = false;
        float velocity = 1.0f;
        float panL = 1.0f, panR = 1.0f;   // balance law, set per note
        double bodyPhase = 0.0, clickPhase = 0.0;
        double transpose = 1.0;
        double pitchEnv = 0.0, pitchEnvCoef = 0.0;
        double ampExp = 0.0, ampExpCoef = 0.0;
        double ampLinear = 0.0, ampLinearStep = 0.0;
        double clickEnv = 0.0, clickEnvCoef = 0.0;
        double noiseEnv = 0.0, noiseEnvCoef = 0.0;
        float noiseLowpass = 0.0f;
        int samplesRemaining = 0;
    };

    void noteOn (int key, float velocity);
    void renderSegment (juce::AudioBuffer<float>& out, int from, int to);

    static constexpr int maxVoices = 8;
    static constexpr int fadeOutSamples = 128;   // kills the tail discontinuity

    std::array<Voice, maxVoices> voices;
    Params p;
    std::atomic<int> rootNote { 60 };
    double sampleRate = 44100.0;
    juce::Random rng { 0x4b1c };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickGenerator)
};
