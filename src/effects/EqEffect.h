#pragma once

#include <array>
#include <atomic>
#include "BuiltinEffect.h"
#include "Biquad.h"

// Four fully parametric bands (bell / low shelf / high shelf / notch) plus
// 24 dB/oct high- and low-pass sweeps. Coefficients are recomputed on the
// audio thread when a parameter moves, which fx::Biquad makes allocation-free.
class EqEffect : public BuiltinEffect
{
public:
    enum class BandType { bell = 0, lowShelf, highShelf, notch, off };

    static constexpr int numBands = 4;

    EqEffect() = default;

    static const juce::String& identifier();
    static const juce::String& displayName();
    static const std::vector<fx::ParamSpec>& specs();

    void prepare (double sampleRate, int maxBlockSize) override;
    void process (juce::AudioBuffer<float>& stereoBus, int numSamples, const Context& context) override;
    void reset() override;

    const std::vector<fx::ParamSpec>& getParamSpecs() const override { return specs(); }
    void setParameter (const juce::Identifier& paramId, double value) override;

private:
    struct BandParams
    {
        std::atomic<int> type { (int) BandType::bell };
        std::atomic<float> freq { 1000.0f };
        std::atomic<float> gainDb { 0.0f };
        std::atomic<float> q { 1.0f };
    };

    void updateCoefficients();

    std::array<BandParams, numBands> bands;
    std::atomic<float> hpFreq { 20.0f };
    std::atomic<float> lpFreq { 20000.0f };
    std::atomic<bool> dirty { true };

    // [channel][band] plus two cascaded stages each for the HP and LP sweeps.
    std::array<std::array<fx::Biquad, numBands>, 2> bandFilters;
    std::array<std::array<fx::Biquad, 2>, 2> hpFilters, lpFilters;
    bool hpActive = false, lpActive = false;
    std::array<bool, numBands> bandActive {};

    double sampleRateHz = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqEffect)
};
