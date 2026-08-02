#pragma once

#include <atomic>
#include <juce_dsp/juce_dsp.h>
#include "BuiltinEffect.h"

// Resonant multimode filter with an envelope follower and a tempo-synced LFO —
// the sweep/riser workhorse. Cutoff modulation is applied per control block so
// the coefficient update cost stays off the per-sample path.
class FilterEffect : public BuiltinEffect
{
public:
    enum class Mode { lowPass = 0, highPass, bandPass };
    enum class Shape { sine = 0, triangle, sawDown, square };

    FilterEffect() = default;

    static const juce::String& identifier();
    static const juce::String& displayName();
    static const std::vector<fx::ParamSpec>& specs();

    void prepare (double sampleRate, int maxBlockSize) override;
    void process (juce::AudioBuffer<float>& stereoBus, int numSamples, const Context& context) override;
    void reset() override;

    const std::vector<fx::ParamSpec>& getParamSpecs() const override { return specs(); }
    void setParameter (const juce::Identifier& paramId, double value) override;

private:
    static float lfoValue (int shape, double phase) noexcept;
    void processControlBlock (juce::AudioBuffer<float>& bus, int start, int numSamples);

    std::atomic<float> cutoffHz { 1200.0f };
    std::atomic<float> resonance { 0.4f };
    std::atomic<float> envAmount { 0.0f };
    std::atomic<float> envAttackMs { 5.0f };
    std::atomic<float> envReleaseMs { 120.0f };
    std::atomic<float> lfoAmount { 0.0f };
    std::atomic<float> mix { 1.0f };
    std::atomic<int> mode { 0 };
    std::atomic<int> lfoDivision { 12 };   // 1/1
    std::atomic<int> lfoShape { 0 };

    juce::dsp::StateVariableTPTFilter<float> filter;
    juce::AudioBuffer<float> dry;
    double sampleRateHz = 44100.0;
    float envelope = 0.0f;
    double lfoPhase = 0.0;
    double lfoIncrement = 0.0;
    float modOctaves = 0.0f;   // filled per control block

    static constexpr int controlBlock = 32;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilterEffect)
};
