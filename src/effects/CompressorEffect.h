#pragma once

#include <array>
#include <atomic>
#include "BuiltinEffect.h"
#include "Biquad.h"

// Feed-forward peak compressor with a soft knee and an optional external
// sidechain taken from another mixer insert — the pumping that defines these
// genres. Detection runs in dB so the ballistics behave the same at any level.
class CompressorEffect : public BuiltinEffect
{
public:
    CompressorEffect() = default;

    static const juce::String& identifier();
    static const juce::String& displayName();
    static const std::vector<fx::ParamSpec>& specs();

    void prepare (double sampleRate, int maxBlockSize) override;
    void process (juce::AudioBuffer<float>& stereoBus, int numSamples, const Context& context) override;
    void reset() override;

    int getSidechainInsert() const noexcept override
    {
        return sidechainInsert.load (std::memory_order_relaxed);
    }

    const std::vector<fx::ParamSpec>& getParamSpecs() const override { return specs(); }
    void setParameter (const juce::Identifier& paramId, double value) override;

    // Gain reduction of the last processed block, in dB (0 = none). For meters
    // and tests.
    float getGainReductionDb() const noexcept
    {
        return lastReductionDb.load (std::memory_order_relaxed);
    }

    // Static transfer curve: gain reduction in dB (>= 0) for an input level,
    // given threshold/ratio/knee. process() uses this same function per
    // sample, so the editor's plotted curve is the real one.
    static float gainReductionDbFor (float levelDb, float thresholdDb,
                                     float ratio, float kneeDb) noexcept
    {
        const float knee = juce::jmax (0.01f, kneeDb);
        const float slope = 1.0f - 1.0f / juce::jmax (1.0f, ratio);
        const float over = levelDb - thresholdDb;
        if (2.0f * over > knee)
            return slope * over;
        if (2.0f * over > -knee)
        {
            const float t = over + knee * 0.5f;
            return slope * t * t / (2.0f * knee);
        }
        return 0.0f;
    }

    // Writes the one-click "duck this bus from <source>" preset onto a slot:
    // compressor id, pumping ballistics (fast attack, deep ratio, ~100 ms
    // release) and the sidechain source. Shared by the mixer menu and tests.
    static void configureDuckSlot (juce::ValueTree slot, int sourceInsert, juce::UndoManager*);

private:
    std::atomic<float> thresholdDb { -12.0f };
    std::atomic<float> ratio { 2.5f };
    std::atomic<float> attackMs { 8.0f };
    std::atomic<float> releaseMs { 120.0f };
    std::atomic<float> kneeDb { 6.0f };
    std::atomic<float> makeupDb { 2.5f };
    std::atomic<float> scHpFreq { 20.0f };
    std::atomic<float> mix { 1.0f };
    std::atomic<int> sidechainInsert { -1 };
    std::atomic<bool> filterDirty { true };
    std::atomic<float> lastReductionDb { 0.0f };

    std::array<fx::Biquad, 2> scHighPass;
    juce::AudioBuffer<float> dry;
    double sampleRateHz = 44100.0;
    float envelopeDb = 0.0f;   // current gain reduction, in dB (>= 0)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompressorEffect)
};
