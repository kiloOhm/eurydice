#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <juce_dsp/juce_dsp.h>
#include "BuiltinEffect.h"
#include "DelayLine.h"

// Drive + waveshaper with selectable oversampling. Built for kick clipping:
// hammering a 50 Hz kick into a hard curve folds its harmonics past Nyquist,
// so the oversampled path is the default.
class ClipperEffect : public BuiltinEffect
{
public:
    enum class Curve { soft = 0, hard, tube, fold };

    ClipperEffect() = default;

    static const juce::String& identifier();
    static const juce::String& displayName();
    static const std::vector<fx::ParamSpec>& specs();

    // Waveshaper for one sample of already-driven input. Static so the curves
    // can be inspected directly from tests.
    static float shapeSample (int curve, float x) noexcept;

    void prepare (double sampleRate, int maxBlockSize) override;
    void process (juce::AudioBuffer<float>& stereoBus, int numSamples, const Context& context) override;
    void reset() override;

    const std::vector<fx::ParamSpec>& getParamSpecs() const override { return specs(); }
    void setParameter (const juce::Identifier& paramId, double value) override;

private:
    std::atomic<float> driveDb { 6.0f };
    std::atomic<float> outputDb { 0.0f };
    std::atomic<float> mix { 1.0f };
    std::atomic<int> curveIndex { 1 };
    std::atomic<int> oversampleIndex { 1 };

    // Index 0..2 == 2x / 4x / 8x; 1x needs no oversampler at all.
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 3> oversamplers;
    std::array<int, 4> latencySamples {};
    int activeOversample = -1;

    juce::AudioBuffer<float> dry;
    fx::DelayLine dryDelay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClipperEffect)
};
