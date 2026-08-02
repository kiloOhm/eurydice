#include "ClipperEffect.h"
#include "model/Ids.h"

const juce::String& ClipperEffect::identifier()
{
    static const juce::String id ("builtin:clipper");
    return id;
}

const juce::String& ClipperEffect::displayName()
{
    static const juce::String name ("Clipper");
    return name;
}

const std::vector<fx::ParamSpec>& ClipperEffect::specs()
{
    static const std::vector<fx::ParamSpec> s {
        { ids::fxDrive,      "Drive",  0.0, 48.0, 1.0,  6.0, " dB", 1 },
        { ids::fxCurve,      "Curve",  0.0,  3.0, 1.0,  1.0, {},    0, { "Soft", "Hard", "Tube", "Fold" } },
        { ids::fxOversample, "OS",     0.0,  3.0, 1.0,  1.0, {},    0, { "1x", "2x", "4x", "8x" } },
        { ids::fxOutput,     "Out",  -24.0, 12.0, 1.0,  0.0, " dB", 1 },
        { ids::fxMix,        "Mix",    0.0,  1.0, 1.0,  1.0, {},    2 },
    };
    return s;
}

float ClipperEffect::shapeSample (int curve, float x) noexcept
{
    switch ((Curve) curve)
    {
        case Curve::hard:
            return juce::jlimit (-1.0f, 1.0f, x);

        case Curve::tube:
        {
            // Biased soft clip: the halves saturate at different points, which
            // is what puts even harmonics in. Normalised to stay inside [-1, 1].
            constexpr float bias = 0.15f;
            constexpr float offset = 0.14888503f;   // tanh (bias)
            return (std::tanh (x + bias) - offset) * (1.0f / (1.0f + offset));
        }

        case Curve::fold:
        {
            // Triangle wavefold: identity inside [-1, 1], folding back beyond.
            const float p = x + 1.0f;
            const float q = p - 4.0f * std::floor (p * 0.25f);
            return q < 2.0f ? q - 1.0f : 3.0f - q;
        }

        case Curve::soft:
        default:
            return std::tanh (x);
    }
}

void ClipperEffect::prepare (double sampleRate, int maxBlockSize)
{
    juce::ignoreUnused (sampleRate);
    const int block = juce::jmax (32, maxBlockSize);

    latencySamples[0] = 0;
    for (size_t i = 0; i < oversamplers.size(); ++i)
    {
        auto os = std::make_unique<juce::dsp::Oversampling<float>> (
            2, i + 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
        os->initProcessing ((size_t) block);
        latencySamples[i + 1] = (int) std::lround (os->getLatencyInSamples());
        oversamplers[i] = std::move (os);
    }

    dry.setSize (2, block);
    dryDelay.prepare (2, 512);
    activeOversample = -1;
    reset();
}

void ClipperEffect::reset()
{
    for (auto& os : oversamplers)
        if (os != nullptr)
            os->reset();
    dryDelay.reset();
    dry.clear();
}

void ClipperEffect::setParameter (const juce::Identifier& paramId, double value)
{
    if (paramId == ids::fxDrive)            driveDb.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxOutput)      outputDb.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxMix)         mix.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxCurve)       curveIndex.store ((int) std::lround (value), std::memory_order_relaxed);
    else if (paramId == ids::fxOversample)  oversampleIndex.store ((int) std::lround (value), std::memory_order_relaxed);
}

void ClipperEffect::process (juce::AudioBuffer<float>& stereoBus, int numSamples, const Context& context)
{
    juce::ignoreUnused (context);
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (2, stereoBus.getNumChannels());
    if (numCh < 1 || numSamples < 1 || numSamples > dry.getNumSamples())
        return;

    const int osIndex = juce::jlimit (0, 3, oversampleIndex.load (std::memory_order_relaxed));
    if (osIndex != activeOversample)
    {
        activeOversample = osIndex;
        if (osIndex > 0 && oversamplers[(size_t) osIndex - 1] != nullptr)
            oversamplers[(size_t) osIndex - 1]->reset();
        dryDelay.reset();
    }

    const int curve = juce::jlimit (0, 3, curveIndex.load (std::memory_order_relaxed));
    const float driveGain = juce::Decibels::decibelsToGain (driveDb.load (std::memory_order_relaxed));
    const float outGain = juce::Decibels::decibelsToGain (outputDb.load (std::memory_order_relaxed));
    const auto gains = fx::mixGains (mix.load (std::memory_order_relaxed));
    const auto delay = (float) latencySamples[(size_t) osIndex];

    // The dry path is delayed by the oversampler's latency so a partial mix
    // stays phase-aligned instead of comb-filtering.
    for (int i = 0; i < numSamples; ++i)
    {
        for (int ch = 0; ch < numCh; ++ch)
        {
            dryDelay.write (ch, stereoBus.getSample (ch, i));
            dry.setSample (ch, i, dryDelay.read (ch, delay));
        }
        dryDelay.advance();
    }

    if (osIndex == 0)
    {
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* data = stereoBus.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
                data[i] = shapeSample (curve, data[i] * driveGain);
        }
    }
    else if (auto* os = oversamplers[(size_t) osIndex - 1].get())
    {
        juce::dsp::AudioBlock<float> block (stereoBus.getArrayOfWritePointers(), (size_t) numCh,
                                            0, (size_t) numSamples);
        block.multiplyBy (driveGain);

        auto upBlock = os->processSamplesUp (block);
        for (size_t ch = 0; ch < upBlock.getNumChannels(); ++ch)
        {
            auto* data = upBlock.getChannelPointer (ch);
            for (size_t i = 0; i < upBlock.getNumSamples(); ++i)
                data[i] = shapeSample (curve, data[i]);
        }
        os->processSamplesDown (block);
    }

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* data = stereoBus.getWritePointer (ch);
        const auto* dryData = dry.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
            data[i] = data[i] * outGain * gains.wet + dryData[i] * gains.dry;
    }
}
