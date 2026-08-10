#pragma once

#include <atomic>
#include "BuiltinEffect.h"
#include "EffectRegistry.h"

// Auto-pan / tremolo in the spirit of the classic utility: one unipolar LFO
// attenuates each channel, with the right channel offset in phase — 0° dips
// both channels together (tremolo), 180° alternates them (auto-pan). The rate
// free-runs in Hz or locks to the beat grid, and a short gain glide keeps
// square edges click-free.
class AutoPanEffect : public BuiltinEffect
{
public:
    enum class Shape { sine = 0, triangle, sawDown, square };

    AutoPanEffect() = default;

    static const juce::String& identifier();
    static const juce::String& displayName();
    static const std::vector<fx::ParamSpec>& specs();
    static const std::vector<fx::BuiltinPreset>& presets();

    // Gain the LFO applies to one channel at a phase (in cycles). The editor's
    // waveform display draws through this, so the plot can't drift from the DSP.
    static float channelGain (int shape, double phase, float amount) noexcept;

    void prepare (double sampleRate, int maxBlockSize) override;
    void process (juce::AudioBuffer<float>& stereoBus, int numSamples, const Context& context) override;
    void reset() override;

    const std::vector<fx::ParamSpec>& getParamSpecs() const override { return specs(); }
    void setParameter (const juce::Identifier& paramId, double value) override;

private:
    std::atomic<float> amount { 0.5f };
    std::atomic<float> rateHz { 1.0f };
    std::atomic<float> phaseDeg { 180.0f };
    std::atomic<float> mix { 1.0f };
    std::atomic<int> shape { 0 };
    std::atomic<int> division { 8 };   // 1/4
    std::atomic<bool> synced { true };

    double sampleRateHz = 44100.0;
    double lfoPhase = 0.0;
    float smoothedGain[2] { 1.0f, 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoPanEffect)
};
