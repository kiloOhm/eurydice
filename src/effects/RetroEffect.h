#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <juce_dsp/juce_dsp.h>
#include "BuiltinEffect.h"
#include "DelayLine.h"
#include "EffectRegistry.h"

// The lo-fi character box, in the spirit of the big "retro colour" plugins: six
// wear-and-tear modules in one slot, each with an on/off and an Amount, and one
// Magnitude that scales every amount at once — so a single control takes the
// whole chain from a hint of tape to a broken cassette, and automating it turns
// an intro into a drop.
//
// The modules run in the order real gear degrades a signal: Wobble bends the
// playback speed, Distort drives it, Digital quantises what came out, Noise
// lays the machine's own hiss on top, Space puts it in a small room, and Drops
// eat the level the way worn tape does. Noise sits after the degraders so drive
// can't pump the hiss, but before Space so the room hears it.
//
// The thirteen rack values (six on/offs, six amounts, Magnitude) are marked
// drawnByDisplay(): the editor's rack draws them instead of the generic knob
// grid, while they stay ordinary parameters for automation, presets and the
// control API.
class RetroEffect : public BuiltinEffect
{
public:
    static constexpr int numModules = 6;

    // Rack order, which is also the processing order.
    enum class Module { wobble = 0, distort, digital, noise, space, drops };

    enum class NoiseType { vinyl = 0, crackle, hiss, studio, hum50, hum60, radio };
    enum class WobbleType { tape = 0, vinyl, random, sine };
    enum class DistortType { tube = 0, tape, diode, fuzz, fold, rectify };

    // One rack row: its name and the two properties its LED and slider write.
    struct ModuleInfo
    {
        juce::String name;
        const juce::Identifier* enableId;
        const juce::Identifier* amountId;
    };

    RetroEffect() = default;

    static const juce::String& identifier();
    static const juce::String& displayName();
    static const std::vector<fx::ParamSpec>& specs();
    static const std::vector<fx::BuiltinPreset>& presets();
    static const std::array<ModuleInfo, numModules>& modules();

    static const juce::StringArray& noiseTypeNames();
    static const juce::StringArray& wobbleTypeNames();
    static const juce::StringArray& distortTypeNames();

    // The distortion curve for one sample of already-driven input. Static so
    // the rack's little curve and the tests draw exactly what runs.
    static float shapeSample (int type, float x) noexcept;

    void prepare (double sampleRate, int maxBlockSize) override;
    void process (juce::AudioBuffer<float>& stereoBus, int numSamples, const Context& context) override;
    void reset() override;

    const std::vector<fx::ParamSpec>& getParamSpecs() const override { return specs(); }
    void setParameter (const juce::Identifier& paramId, double value) override;

    // Live activity for the rack's meters, written once per block.
    float getWobbleOffset() const noexcept { return displayWobble.load (std::memory_order_relaxed); }
    float getDropDepth()    const noexcept { return displayDrop.load (std::memory_order_relaxed); }
    float getNoiseLevel()   const noexcept { return displayNoise.load (std::memory_order_relaxed); }

private:
    // Wobble reads out of a delay line whose length is modulated; the base
    // length is the latency the whole wet path picks up when it runs, and the
    // depth is how far either side of it the playback speed can bend.
    static constexpr double wobbleBaseMs = 12.0;
    static constexpr double wobbleDepthMs = 9.0;
    static constexpr double maxPreDelayMs = 200.0;

    struct ModuleState
    {
        std::atomic<bool> on { false };
        std::atomic<float> amount { 0.35f };
    };

    // amount * magnitude, or zero when the module is switched off.
    float amountOf (Module m) const noexcept;

    void applyWobble (juce::AudioBuffer<float>& buffer, int numCh, int numSamples, float amount) noexcept;
    float wobbleMod (int type, double phaseOffset, float flutterAmount, float driftAmount) const noexcept;
    void applyDistort (juce::AudioBuffer<float>& buffer, int numCh, int numSamples,
                       float amount, int osIndex) noexcept;
    void applyDigital (juce::AudioBuffer<float>& buffer, int numCh, int numSamples, float amount) noexcept;
    void addNoise (juce::AudioBuffer<float>& buffer, int numCh, int numSamples, float amount) noexcept;
    void nextNoise (int type, float& left, float& right) noexcept;
    float nextPink (int channel, float white) noexcept;
    float nextClick (int channel, float perSecond) noexcept;
    void applySpace (juce::AudioBuffer<float>& buffer, int numCh, int numSamples, float amount) noexcept;
    void applyDrops (juce::AudioBuffer<float>& buffer, int numCh, int numSamples, float amount) noexcept;
    void scheduleDrop (float rateHz, float vary) noexcept;
    void startDrop (float amount, float lengthMs, float vary) noexcept;

    std::array<ModuleState, numModules> moduleState;
    std::atomic<float> magnitude { 1.0f };

    std::atomic<int> noiseType { 0 };
    std::atomic<float> noiseToneHz { 8000.0f };
    std::atomic<float> noiseFollow { 0.0f };
    std::atomic<float> noiseWidth { 1.0f };

    std::atomic<int> wobbleType { 0 };
    std::atomic<float> wobbleRateHz { 0.8f };
    std::atomic<float> flutterAmount { 0.35f };
    std::atomic<float> driftAmount { 0.25f };

    std::atomic<int> distortType { 0 };
    std::atomic<float> distortToneHz { 20000.0f };
    std::atomic<float> distortBias { 0.0f };

    std::atomic<float> bitDepth { 12.0f };
    std::atomic<float> downsampleHz { 24000.0f };
    std::atomic<float> jitterAmount { 0.0f };

    std::atomic<float> spaceSize { 0.45f };
    std::atomic<float> spaceDampHz { 4000.0f };
    std::atomic<float> spacePreMs { 0.0f };
    std::atomic<float> spaceWidthAmount { 1.0f };

    std::atomic<float> dropRateHz { 0.5f };
    std::atomic<float> dropLengthMs { 180.0f };
    std::atomic<float> dropVary { 0.5f };

    std::atomic<int> oversampleIndex { 1 };
    std::atomic<float> outputDb { 0.0f };
    std::atomic<float> mix { 1.0f };

    std::atomic<float> displayWobble { 0.0f };
    std::atomic<float> displayDrop { 0.0f };
    std::atomic<float> displayNoise { 0.0f };

    double sampleRateHz = 44100.0;

    // --- wobble
    fx::DelayLine wobbleLine;
    double wowPhase = 0.0;
    double flutterPhase = 0.0;
    float driftValue = 0.0f;
    float driftTarget = 0.0f;
    bool wobbleRunning = false;

    // --- distort
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 3> oversamplers;
    std::array<int, 4> latencySamples {};
    int activeOversample = -1;
    float dcX[2] {};
    float dcY[2] {};
    juce::dsp::StateVariableTPTFilter<float> distortTone;

    // --- digital
    double holdPhase = 1.0;
    float held[2] {};

    // --- noise
    juce::Random rng { 0x5eed1a1e };
    float pinkState[2][3] {};
    float clickAmp[2] {};
    float clickDecay = 0.98f;
    float rumbleCoeff = 0.02f;
    float rumbleState = 0.0f;
    double humPhase = 0.0;
    float burstEnv = 0.5f;
    int burstCountdown = 0;
    float inputEnv = 0.0f;
    juce::dsp::StateVariableTPTFilter<float> noiseTone;

    // --- space
    juce::Reverb reverb;
    fx::DelayLine preDelay;
    juce::AudioBuffer<float> spaceBuffer;

    // --- drops
    int dropCountdown = -1;   // -1 = nothing scheduled yet
    int dropPos = 0;
    int dropLength = 0;
    float dropDepth = 0.0f;

    juce::AudioBuffer<float> dry;
    fx::DelayLine dryDelay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RetroEffect)
};
