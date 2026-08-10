#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Generator.h"
#include "KickDsp.h"

// Synthesised kick drum, built like a dedicated kick designer: four layers
// (body, sub, click, noise) summed into a built-in output chain (drive, three
// bands of EQ, a one-knob compressor and a limiter). The body's pitch and the
// amplitude both follow either their classic analytic decay or a drawn
// breakpoint envelope, whichever the channel carries.
//
// One-shot — note-offs are ignored, every note plays its full amp decay.
// Scalar parameters are atomics written from the message thread, like
// SynthGenerator; the envelopes and the click sample are shared_ptrs swapped
// under a spin lock, like DrumMachineGenerator's pads.
class KickGenerator : public Generator
{
public:
    struct Params
    {
        // Body
        std::atomic<float> startFreq { 240.0f };    // Hz at the root note
        std::atomic<float> endFreq { 48.0f };
        std::atomic<float> pitchDecay { 0.035f };   // seconds: time constant, or drawn span
        std::atomic<float> bodyShape { 0.0f };      // 0 = sine, 1 = triangle
        std::atomic<float> bodyHarm { 0.0f };       // 0..1 self-FM harmonics
        std::atomic<float> bodyPhase { 0.0f };      // 0..1 start phase, cycles
        std::atomic<float> bodyLevel { 1.0f };

        // Amp envelope
        std::atomic<float> ampDecay { 0.5f };
        std::atomic<float> hold { 0.0f };           // seconds at full level first
        std::atomic<float> envShape { 1.0f };       // 0 = linear, 1 = exponential
        std::atomic<float> punch { 0.0f };          // 0..1 transient spike

        // Sub
        std::atomic<float> subLevel { 0.0f };
        std::atomic<float> subTune { 0.0f };        // semitones off the end frequency
        std::atomic<float> subDecay { 0.4f };

        // Click
        std::atomic<float> clickLevel { 0.3f };
        std::atomic<float> clickDecay { 0.004f };
        std::atomic<float> clickFreq { 1400.0f };
        std::atomic<int>   clickType { 0 };         // kickdsp::ClickType

        // Noise
        std::atomic<float> noiseLevel { 0.12f };
        std::atomic<float> noiseDecay { 0.02f };
        std::atomic<float> noiseTone { 0.4f };      // one-pole lowpass coefficient

        // Output chain
        std::atomic<float> drive { 0.25f };         // 0..1
        std::atomic<int>   driveCurve { 0 };
        std::atomic<float> eqLowFreq { 90.0f },   eqLowGain { 0.0f };
        std::atomic<float> eqMidFreq { 500.0f },  eqMidGain { 0.0f };
        std::atomic<float> eqHighFreq { 4000.0f }, eqHighGain { 0.0f };
        std::atomic<float> compression { 0.0f };    // 0..1
        std::atomic<float> limiter { 0.0f };        // 0..1
        std::atomic<float> outputDb { 0.0f };
        std::atomic<float> gain { 0.9f };           // fixed headroom trim
    };

    KickGenerator();

    void prepare (double sampleRate, int maxBlockSize) override;
    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi) override;
    void reset() override;

    std::atomic<float>* getAutomatableParam (const juce::String& paramId) override;

    void setRootNote (int note) { rootNote.store (note); }
    int  getRootNote() const    { return rootNote.load(); }
    Params& params()            { return p; }

    // --- message thread ---

    // Swaps in drawn envelopes. An empty envelope restores the analytic decay,
    // so this is also how the editor's curve mode is turned back off.
    void setPitchEnvelope (kickdsp::Envelope);
    void setAmpEnvelope (kickdsp::Envelope);

    // Points the click layer at a WAV (empty clears it). Only reloads when the
    // path actually changed, so calling it on every snapshot rebuild is free.
    void setClickSample (const juce::String& path);
    juce::String getClickSamplePath() const { return clickSamplePath; }
    bool hasClickSample() const;

private:
    float pendingPan = 0.0f;   // CC10 latch, consumed by the next note-on

    struct Sample
    {
        juce::AudioBuffer<float> data;   // mono: the click layer is centred
        double sourceSampleRate = 44100.0;
    };

    struct Voice
    {
        bool active = false;
        float velocity = 1.0f;
        float panL = 1.0f, panR = 1.0f;   // balance law, set per note
        double bodyPhase = 0.0, clickPhase = 0.0, subPhase = 0.0;
        double transpose = 1.0;
        // Analytic envelopes (used when nothing is drawn)
        double pitchEnv = 0.0, pitchEnvCoef = 0.0;
        double ampExp = 0.0, ampExpCoef = 0.0;
        double ampLinear = 0.0, ampLinearStep = 0.0;
        double clickEnv = 0.0, clickEnvCoef = 0.0;
        double noiseEnv = 0.0, noiseEnvCoef = 0.0;
        double subEnv = 0.0, subEnvCoef = 0.0;
        double punchEnv = 0.0, punchEnvCoef = 0.0;
        float noiseLowpass = 0.0f;
        // Drawn-envelope playback
        int elapsed = 0;                  // samples since note-on
        int holdSamples = 0;
        double pitchSpan = 1.0, ampSpan = 1.0;   // samples
        double clickSamplePos = -1.0, clickSampleStep = 1.0;
        std::shared_ptr<const Sample> clickSampleData;
        int samplesRemaining = 0;
    };

    void noteOn (int key, float velocity);
    void renderSegment (juce::AudioBuffer<float>& out, int from, int to);
    void processChain (juce::AudioBuffer<float>& out, int from, int to);

    std::shared_ptr<const kickdsp::Envelope> getPitchEnvelope() const;
    std::shared_ptr<const kickdsp::Envelope> getAmpEnvelope() const;
    std::shared_ptr<const Sample> getClickSample() const;

    static constexpr int maxVoices = 8;
    static constexpr int fadeOutSamples = 128;   // kills the tail discontinuity

    std::array<Voice, maxVoices> voices;
    Params p;
    std::atomic<int> rootNote { 60 };
    double sampleRate = 44100.0;
    juce::Random rng { 0x4b1c };

    // Message-thread state, read on the audio thread under the lock.
    mutable juce::SpinLock stateLock;
    std::shared_ptr<const kickdsp::Envelope> pitchEnvelope, ampEnvelope;
    std::shared_ptr<const Sample> clickSample;
    juce::String clickSamplePath;   // message thread only

    // Per-block envelope views, refreshed once per render() call.
    std::shared_ptr<const kickdsp::Envelope> blockPitchEnv, blockAmpEnv;

    kickdsp::ToneEq eq;
    kickdsp::Compressor compressor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickGenerator)
};
