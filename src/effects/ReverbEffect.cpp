#include "ReverbEffect.h"
#include "model/Ids.h"

namespace
{
constexpr double maxPreDelayMs = 250.0;
}

const juce::String& ReverbEffect::identifier()
{
    static const juce::String id ("builtin:reverb");
    return id;
}

const juce::String& ReverbEffect::displayName()
{
    static const juce::String name ("Reverb");
    return name;
}

const std::vector<fx::ParamSpec>& ReverbEffect::specs()
{
    static const std::vector<fx::ParamSpec> s {
        { ids::fxSize,     "Size",     0.0,     1.0, 1.0,    0.65, {},    2 },
        { ids::fxDamping,  "Damp",     0.0,     1.0, 1.0,    0.4,  {},    2 },
        { ids::fxWidth,    "Width",    0.0,     1.0, 1.0,    1.0,  {},    2 },
        { ids::fxPreDelay, "Pre",      0.0,   250.0, 0.5,   20.0,  " ms", 0 },
        { ids::fxHpFreq,   "Low cut", 20.0,  2000.0, 0.3,  200.0,  " Hz", 0 },
        { ids::fxLpFreq,   "Hi cut", 500.0, 20000.0, 0.3, 9000.0,  " Hz", 0 },
        { ids::fxMix,      "Mix",      0.0,     1.0, 1.0,    0.25, {},    2 },
    };
    return s;
}

void ReverbEffect::prepare (double sampleRate, int maxBlockSize)
{
    sampleRateHz = sampleRate;
    reverb.setSampleRate (sampleRate);
    maxPreDelaySamples = (int) (maxPreDelayMs * 0.001 * sampleRate) + 2;
    preDelay.prepare (2, maxPreDelaySamples);
    wet.setSize (2, juce::jmax (32, maxBlockSize));
    filterDirty.store (true, std::memory_order_relaxed);
    reset();
}

void ReverbEffect::reset()
{
    reverb.reset();
    preDelay.reset();
    for (auto& f : lowCut)  f.reset();
    for (auto& f : highCut) f.reset();
    wet.clear();
}

void ReverbEffect::setParameter (const juce::Identifier& paramId, double value)
{
    if (paramId == ids::fxSize)          roomSize.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxDamping)  damping.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxWidth)    width.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxPreDelay) preDelayMs.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxMix)      mix.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxHpFreq)
    {
        lowCutHz.store ((float) value, std::memory_order_relaxed);
        filterDirty.store (true, std::memory_order_relaxed);
    }
    else if (paramId == ids::fxLpFreq)
    {
        highCutHz.store ((float) value, std::memory_order_relaxed);
        filterDirty.store (true, std::memory_order_relaxed);
    }
}

void ReverbEffect::process (juce::AudioBuffer<float>& stereoBus, int numSamples, const Context& context)
{
    juce::ignoreUnused (context);
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (2, stereoBus.getNumChannels());
    if (numCh < 2 || numSamples < 1 || numSamples > wet.getNumSamples())
        return;

    if (filterDirty.exchange (false, std::memory_order_relaxed))
        for (int ch = 0; ch < 2; ++ch)
        {
            lowCut[(size_t) ch].setHighPass (sampleRateHz, lowCutHz.load (std::memory_order_relaxed), 0.707);
            highCut[(size_t) ch].setLowPass (sampleRateHz, highCutHz.load (std::memory_order_relaxed), 0.707);
        }

    juce::Reverb::Parameters params;
    params.roomSize = juce::jlimit (0.0f, 1.0f, roomSize.load (std::memory_order_relaxed));
    params.damping = juce::jlimit (0.0f, 1.0f, damping.load (std::memory_order_relaxed));
    params.width = juce::jlimit (0.0f, 1.0f, width.load (std::memory_order_relaxed));
    params.wetLevel = 1.0f;
    params.dryLevel = 0.0f;
    params.freezeMode = 0.0f;
    reverb.setParameters (params);

    const auto preSamples = (float) juce::jlimit (
        1.0, (double) maxPreDelaySamples,
        (double) preDelayMs.load (std::memory_order_relaxed) * 0.001 * sampleRateHz);

    for (int i = 0; i < numSamples; ++i)
    {
        for (int ch = 0; ch < 2; ++ch)
            wet.setSample (ch, i, preDelay.read (ch, preSamples));
        for (int ch = 0; ch < 2; ++ch)
            preDelay.write (ch, stereoBus.getSample (ch, i));
        preDelay.advance();
    }

    reverb.processStereo (wet.getWritePointer (0), wet.getWritePointer (1), numSamples);

    const bool lowCutActive = lowCutHz.load (std::memory_order_relaxed) > 20.5f;
    const bool highCutActive = highCutHz.load (std::memory_order_relaxed) < 19500.0f;
    const auto gains = fx::mixGains (mix.load (std::memory_order_relaxed));

    for (int ch = 0; ch < 2; ++ch)
    {
        auto* dryData = stereoBus.getWritePointer (ch);
        auto* wetData = wet.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            float w = wetData[i];
            if (lowCutActive)
                w = lowCut[(size_t) ch].processSample (w);
            if (highCutActive)
                w = highCut[(size_t) ch].processSample (w);
            dryData[i] = dryData[i] * gains.dry + w * gains.wet;
        }
    }
}
