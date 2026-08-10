#pragma once

#include <array>
#include <atomic>
#include <juce_dsp/juce_dsp.h>
#include "BuiltinEffect.h"
#include "EffectRegistry.h"
#include "Biquad.h"
#include "DelayLine.h"

// Algorithmic reverb in the spirit of the classic 70s/80s digital units: an
// 8-line feedback delay network with a Householder matrix, slow sine
// modulation on every line (the lush, chorused tail), input diffusion
// allpasses, decay in seconds via the RT60 gain formula, and one-pole
// high-frequency damping inside the loop. A mode choice retunes the tank
// (room / chamber / plate / hall / cathedral) and a colour choice limits the
// bandwidth and deepens the modulation the way the vintage boxes did.
class ReverbEffect : public BuiltinEffect
{
public:
    ReverbEffect() = default;

    static const juce::String& identifier();
    static const juce::String& displayName();
    static const std::vector<fx::ParamSpec>& specs();
    static const std::vector<fx::BuiltinPreset>& presets();

    void prepare (double sampleRate, int maxBlockSize) override;
    void process (juce::AudioBuffer<float>& stereoBus, int numSamples, const Context& context) override;
    void reset() override;

    const std::vector<fx::ParamSpec>& getParamSpecs() const override { return specs(); }
    void setParameter (const juce::Identifier& paramId, double value) override;

    static constexpr int numLines = 8;

private:
    // Schroeder allpass with a fixed-length buffer; coefficient may move at
    // runtime (it only changes the diffusion, never the length).
    struct Allpass
    {
        void prepare (int lengthSamples)
        {
            buffer.assign ((size_t) juce::jmax (1, lengthSamples), 0.0f);
            pos = 0;
        }

        void reset() { std::fill (buffer.begin(), buffer.end(), 0.0f); pos = 0; }

        float processSample (float x, float g) noexcept
        {
            const float d = buffer[(size_t) pos];
            const float v = x + g * d;
            buffer[(size_t) pos] = v;
            if (++pos >= (int) buffer.size())
                pos = 0;
            return d - g * v;
        }

        std::vector<float> buffer;
        int pos = 0;
    };

    // One-pole lowpass, the classic in-loop damping element.
    struct OnePole
    {
        void reset() noexcept { z = 0.0f; }
        float processSample (float x, float a) noexcept { z = x + a * (z - x); return z; }
        float z = 0.0f;
    };

    // DC blocker so decades-long decays can't accumulate offset in the tank.
    struct DcBlock
    {
        void reset() noexcept { x1 = 0.0f; y1 = 0.0f; }
        float processSample (float x, float r) noexcept
        {
            const float y = x - x1 + r * y1;
            x1 = x; y1 = y;
            return y;
        }
        float x1 = 0.0f, y1 = 0.0f;
    };

    std::atomic<float> decaySeconds { 2.6f };
    std::atomic<float> size { 0.6f };
    std::atomic<float> preDelayMs { 12.0f };
    std::atomic<float> modRateHz { 1.0f };
    std::atomic<float> modDepth { 0.5f };
    std::atomic<float> dampFreqHz { 7500.0f };
    std::atomic<float> width { 1.0f };
    std::atomic<float> lowCutHz { 120.0f };
    std::atomic<float> highCutHz { 16000.0f };
    std::atomic<float> mix { 0.25f };
    std::atomic<int> mode { 3 };
    std::atomic<int> color { 1 };
    std::atomic<bool> filterDirty { true };

    fx::DelayLine preDelay;
    fx::DelayLine tank;                                    // one channel per line
    std::array<std::array<Allpass, 3>, 2> diffusers;       // [channel][stage]
    std::array<OnePole, numLines> damping;
    std::array<DcBlock, numLines> dcBlockers;
    std::array<OnePole, 2> bandwidth;                      // colour bandwidth limit
    std::array<fx::Biquad, 2> lowCut, highCut;
    juce::AudioBuffer<float> wet;

    std::array<double, numLines> lfoPhase {};
    std::array<float, numLines> smoothedLength {};
    bool lengthsPrimed = false;

    double sampleRateHz = 44100.0;
    int maxPreDelaySamples = 0;
    int maxLineSamples = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReverbEffect)
};
