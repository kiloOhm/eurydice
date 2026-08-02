#pragma once

#include <array>
#include <atomic>
#include "BuiltinEffect.h"
#include "Biquad.h"
#include "DelayLine.h"

// Tempo-synced stereo delay with a filtered feedback path and a ping-pong
// mode. Delay time is smoothed so a division change glides instead of clicking.
class DelayEffect : public BuiltinEffect
{
public:
    DelayEffect() = default;

    static const juce::String& identifier();
    static const juce::String& displayName();
    static const std::vector<fx::ParamSpec>& specs();

    void prepare (double sampleRate, int maxBlockSize) override;
    void process (juce::AudioBuffer<float>& stereoBus, int numSamples, const Context& context) override;
    void reset() override;

    const std::vector<fx::ParamSpec>& getParamSpecs() const override { return specs(); }
    void setParameter (const juce::Identifier& paramId, double value) override;

private:
    std::atomic<float> feedback { 0.4f };
    std::atomic<float> mix { 0.3f };
    std::atomic<float> hpFreq { 200.0f };
    std::atomic<float> lpFreq { 6000.0f };
    std::atomic<int> division { 8 };   // 1/4
    std::atomic<bool> pingPong { false };
    std::atomic<bool> filterDirty { true };

    fx::DelayLine line;
    std::array<fx::Biquad, 2> feedbackHp, feedbackLp;
    double sampleRateHz = 44100.0;
    float smoothedDelay = 0.0f;
    int maxDelaySamples = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DelayEffect)
};
