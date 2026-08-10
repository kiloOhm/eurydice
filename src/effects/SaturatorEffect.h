#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <juce_dsp/juce_dsp.h>
#include "Biquad.h"
#include "BuiltinEffect.h"
#include "DelayLine.h"
#include "EffectRegistry.h"

// Multiband saturation in the spirit of the big multiband saturators: the bus
// splits into up to three bands through Linkwitz-Riley crossovers, each band
// drives its own waveshaper style, and with clean settings the bands sum back
// to a flat (allpass) response. Shaping runs oversampled so the hard styles
// don't splash aliasing across the other bands' spectrum.
class SaturatorEffect : public BuiltinEffect
{
public:
    static constexpr int maxBands = 3;

    enum class Style { clean = 0, tape, tube, amp, fold, rectify };

    SaturatorEffect() = default;

    static const juce::String& identifier();
    static const juce::String& displayName();
    static const std::vector<fx::ParamSpec>& specs();
    static const std::vector<fx::BuiltinPreset>& presets();
    static const juce::StringArray& styleNames();

    // Waveshaper for one sample of already-driven input. Static so the
    // editor's band display and the tests draw the exact curves that run.
    static float shapeSample (int style, float x) noexcept;

    void prepare (double sampleRate, int maxBlockSize) override;
    void process (juce::AudioBuffer<float>& stereoBus, int numSamples, const Context& context) override;
    void reset() override;

    const std::vector<fx::ParamSpec>& getParamSpecs() const override { return specs(); }
    void setParameter (const juce::Identifier& paramId, double value) override;

private:
    void updateCrossovers();
    void splitBands (const juce::AudioBuffer<float>& input, int numCh, int numSamples, int numBands);
    void shapeBand (int band, int numCh, int numSamples, int osIndex);

    struct BandParams
    {
        std::atomic<int> style { 1 };
        std::atomic<float> driveDb { 6.0f };
        std::atomic<float> levelDb { 0.0f };
    };

    std::array<BandParams, maxBands> bandParams;
    std::atomic<int> bandsIndex { 0 };        // 0..2 == 1..3 bands
    std::atomic<float> crossLoHz { 150.0f };
    std::atomic<float> crossHiHz { 2500.0f };
    std::atomic<int> oversampleIndex { 1 };
    std::atomic<float> outputDb { 0.0f };
    std::atomic<float> mix { 1.0f };
    std::atomic<bool> dirty { true };

    double sampleRateHz = 44100.0;

    // Crossover tree per channel: LR4 split at the low crossover, the top half
    // split again at the high crossover, and the low band pushed through the
    // matching allpass so all three bands still sum flat.
    struct ChannelSplit
    {
        fx::Biquad lowLp[2], lowHp[2], midLp[2], midHp[2], lowAp;

        void reset()
        {
            for (int s = 0; s < 2; ++s)
            {
                lowLp[s].reset(); lowHp[s].reset();
                midLp[s].reset(); midHp[s].reset();
            }
            lowAp.reset();
        }
    };
    std::array<ChannelSplit, 2> splits;
    int activeBands = -1;

    // One oversampler per band per factor (index 0..2 == 2x/4x/8x). All bands
    // run the same factor, so their latencies always match.
    std::array<std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 3>, maxBands> oversamplers;
    std::array<int, 4> latencySamples {};
    int activeOversample = -1;

    // Per band per channel DC blockers: the asymmetric styles rectify.
    float dcX[maxBands][2] {};
    float dcY[maxBands][2] {};

    std::array<juce::AudioBuffer<float>, maxBands> bandBuffers;
    juce::AudioBuffer<float> dry;
    fx::DelayLine dryDelay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SaturatorEffect)
};
