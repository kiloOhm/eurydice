#pragma once

#include <array>
#include <atomic>
#include <juce_dsp/juce_dsp.h>
#include "BuiltinEffect.h"
#include "Biquad.h"
#include "DelayLine.h"

// Algorithmic reverb around juce::Reverb, with the parts it lacks: a pre-delay
// and a wet-only low cut so the tail never muddies the sub, plus a high cut.
class ReverbEffect : public BuiltinEffect
{
public:
    ReverbEffect() = default;

    static const juce::String& identifier();
    static const juce::String& displayName();
    static const std::vector<fx::ParamSpec>& specs();

    void prepare (double sampleRate, int maxBlockSize) override;
    void process (juce::AudioBuffer<float>& stereoBus, int numSamples, const Context& context) override;
    void reset() override;

    const std::vector<fx::ParamSpec>& getParamSpecs() const override { return specs(); }
    void setParameter (const juce::Identifier& paramId, double value) override;

private:
    std::atomic<float> roomSize { 0.65f };
    std::atomic<float> damping { 0.4f };
    std::atomic<float> width { 1.0f };
    std::atomic<float> preDelayMs { 20.0f };
    std::atomic<float> lowCutHz { 200.0f };
    std::atomic<float> highCutHz { 9000.0f };
    std::atomic<float> mix { 0.25f };
    std::atomic<bool> filterDirty { true };

    juce::Reverb reverb;
    fx::DelayLine preDelay;
    std::array<fx::Biquad, 2> lowCut, highCut;
    juce::AudioBuffer<float> wet;
    double sampleRateHz = 44100.0;
    int maxPreDelaySamples = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReverbEffect)
};
