#include "SaturatorEffect.h"
#include "model/Ids.h"

namespace
{
const juce::Identifier* satTypeIds[]  { &ids::fxSatType1,  &ids::fxSatType2,  &ids::fxSatType3 };
const juce::Identifier* satDriveIds[] { &ids::fxSatDrive1, &ids::fxSatDrive2, &ids::fxSatDrive3 };
const juce::Identifier* satLevelIds[] { &ids::fxSatLevel1, &ids::fxSatLevel2, &ids::fxSatLevel3 };
}

const juce::String& SaturatorEffect::identifier()
{
    static const juce::String id ("builtin:saturator");
    return id;
}

const juce::String& SaturatorEffect::displayName()
{
    static const juce::String name ("Saturator");
    return name;
}

const juce::StringArray& SaturatorEffect::styleNames()
{
    static const juce::StringArray names { "Clean", "Tape", "Tube", "Amp", "Fold", "Rectify" };
    return names;
}

const std::vector<fx::ParamSpec>& SaturatorEffect::specs()
{
    static const std::vector<fx::ParamSpec> s = []
    {
        std::vector<fx::ParamSpec> list {
            { ids::fxBands,   "Bands",    0.0,    2.0, 1.0,    0.0, {},    0, { "1", "2", "3" } },
            { ids::fxCrossLo, "Split Lo", 40.0, 2000.0, 0.4, 150.0, " Hz", 0 },
            { ids::fxCrossHi, "Split Hi", 500.0, 12000.0, 0.4, 2500.0, " Hz", 0 },
        };

        static const char* bandNames[] = { "Low", "Mid", "High" };
        for (int b = 0; b < maxBands; ++b)
        {
            const auto n = juce::String (b + 1);
            // Each band gets its own row in the editor.
            list.push_back ({ *satTypeIds[b], bandNames[b], 0.0, 5.0, 1.0, 1.0,
                              {}, 0, styleNames(), false, true });
            list.push_back ({ *satDriveIds[b], "Drive " + n,   0.0, 36.0, 1.0, 6.0, " dB", 1 });
            list.push_back ({ *satLevelIds[b], "Level " + n, -24.0, 12.0, 1.0, 0.0, " dB", 1 });
        }

        list.push_back ({ ids::fxOversample, "OS", 0.0, 3.0, 1.0, 1.0,
                          {}, 0, { "1x", "2x", "4x", "8x" }, false, true });
        list.push_back ({ ids::fxOutput, "Out", -24.0, 12.0, 1.0, 0.0, " dB", 1 });
        list.push_back ({ ids::fxMix,    "Mix",   0.0,  1.0, 1.0, 1.0, {},    2 });
        return list;
    }();
    return s;
}

const std::vector<fx::BuiltinPreset>& SaturatorEffect::presets()
{
    struct Band { double style, drive, level; };
    auto make = [] (juce::String name, double bands, double lo, double hi,
                    Band b1, Band b2, Band b3, double os, double out)
    {
        return fx::BuiltinPreset { std::move (name),
            { { ids::fxBands, bands }, { ids::fxCrossLo, lo }, { ids::fxCrossHi, hi },
              { ids::fxSatType1, b1.style }, { ids::fxSatDrive1, b1.drive }, { ids::fxSatLevel1, b1.level },
              { ids::fxSatType2, b2.style }, { ids::fxSatDrive2, b2.drive }, { ids::fxSatLevel2, b2.level },
              { ids::fxSatType3, b3.style }, { ids::fxSatDrive3, b3.drive }, { ids::fxSatLevel3, b3.level },
              { ids::fxOversample, os }, { ids::fxOutput, out } } };
    };

    // Style indices: 0 Clean, 1 Tape, 2 Tube, 3 Amp, 4 Fold, 5 Rectify.
    static const std::vector<fx::BuiltinPreset> p {
        make ("Gentle Glue",   0, 150, 2500, { 0, 12, 0 }, { 0, 6, 0 },  { 0, 6, 0 },   1,  0),
        make ("Warm Tape",     0, 150, 2500, { 1,  8, 0 }, { 1, 6, 0 },  { 1, 6, 0 },   1,  0),
        make ("Tube Glow",     0, 150, 2500, { 2, 10, 0 }, { 2, 6, 0 },  { 2, 6, 0 },   1,  0),
        make ("Bass Warmer",   2, 120, 2500, { 2, 12, 0 }, { 0, 3, 0 },  { 0, 0, 0 },   1,  0),
        make ("Vocal Exciter", 2, 200, 3500, { 0,  0, 0 }, { 1, 6, 0 },  { 3, 14, -3 }, 2,  0),
        make ("Drum Smash",    1, 150, 2500, { 1, 14, 0 }, { 3, 12, 0 }, { 0, 6, 0 },   2, -2),
        make ("Lo-Fi Fold",    0, 150, 2500, { 4, 16, 0 }, { 0, 6, 0 },  { 0, 6, 0 },   2, -4),
        make ("Broken Radio",  2, 300, 3000, { 0,  0, -12 }, { 5, 20, 2 }, { 4, 10, -6 }, 2, -3),
    };
    return p;
}

float SaturatorEffect::shapeSample (int style, float x) noexcept
{
    switch ((Style) style)
    {
        case Style::tape:
            return std::tanh (x);

        case Style::tube:
        {
            // Biased soft clip: the halves saturate at different points, which
            // is what puts even harmonics in. Normalised to stay inside [-1, 1].
            constexpr float bias = 0.2f;
            constexpr float offset = 0.19737532f;   // tanh (bias)
            return (std::tanh (x + bias) - offset) * (1.0f / (1.0f + offset));
        }

        case Style::amp:
        {
            // Exponential clip: bends earlier than tanh but eases into the
            // ceiling, like a driven amp stage.
            const float m = 1.0f - std::exp (-std::abs (x));
            return x < 0.0f ? -m : m;
        }

        case Style::fold:
        {
            // Triangle wavefold: identity inside [-1, 1], folding back beyond.
            const float p = x + 1.0f;
            const float q = p - 4.0f * std::floor (p * 0.25f);
            return q < 2.0f ? q - 1.0f : 3.0f - q;
        }

        case Style::rectify:
            // Halves get very different slopes, so strong even harmonics plus
            // a DC component the per-band blocker removes afterwards.
            return std::tanh (0.8f * x + 0.5f * std::abs (x));

        case Style::clean:
        default:
            // Algebraic soft clip: unity slope and almost linear until close
            // to full scale, then a gentle bend into the ceiling.
            return x / std::cbrt (1.0f + std::abs (x * x * x));
    }
}

void SaturatorEffect::prepare (double sampleRate, int maxBlockSize)
{
    sampleRateHz = sampleRate;
    const int block = juce::jmax (32, maxBlockSize);

    latencySamples[0] = 0;
    for (int b = 0; b < maxBands; ++b)
    {
        for (size_t i = 0; i < oversamplers[(size_t) b].size(); ++i)
        {
            auto os = std::make_unique<juce::dsp::Oversampling<float>> (
                2, i + 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
            os->initProcessing ((size_t) block);
            latencySamples[i + 1] = (int) std::lround (os->getLatencyInSamples());
            oversamplers[(size_t) b][i] = std::move (os);
        }
        bandBuffers[(size_t) b].setSize (2, block);
    }

    dry.setSize (2, block);
    dryDelay.prepare (2, 512);
    activeOversample = -1;
    activeBands = -1;
    dirty.store (true, std::memory_order_relaxed);
    reset();
}

void SaturatorEffect::reset()
{
    for (auto& bandOs : oversamplers)
        for (auto& os : bandOs)
            if (os != nullptr)
                os->reset();
    for (auto& split : splits)
        split.reset();
    for (int b = 0; b < maxBands; ++b)
        for (int ch = 0; ch < 2; ++ch)
            dcX[b][ch] = dcY[b][ch] = 0.0f;
    dryDelay.reset();
    dry.clear();
}

void SaturatorEffect::setParameter (const juce::Identifier& paramId, double value)
{
    if (paramId == ids::fxBands)            bandsIndex.store ((int) std::lround (value), std::memory_order_relaxed);
    else if (paramId == ids::fxCrossLo)     crossLoHz.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxCrossHi)     crossHiHz.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxOversample)  oversampleIndex.store ((int) std::lround (value), std::memory_order_relaxed);
    else if (paramId == ids::fxOutput)      outputDb.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxMix)         mix.store ((float) value, std::memory_order_relaxed);
    else
    {
        for (int b = 0; b < maxBands; ++b)
        {
            auto& band = bandParams[(size_t) b];
            if (paramId == *satTypeIds[b])       band.style.store ((int) std::lround (value), std::memory_order_relaxed);
            else if (paramId == *satDriveIds[b]) band.driveDb.store ((float) value, std::memory_order_relaxed);
            else if (paramId == *satLevelIds[b]) band.levelDb.store ((float) value, std::memory_order_relaxed);
        }
    }
    dirty.store (true, std::memory_order_relaxed);
}

void SaturatorEffect::updateCrossovers()
{
    for (auto& split : splits)
        split.setFrequencies (sampleRateHz,
                              crossLoHz.load (std::memory_order_relaxed),
                              crossHiHz.load (std::memory_order_relaxed));
}

void SaturatorEffect::splitBands (const juce::AudioBuffer<float>& input, int numCh,
                                  int numSamples, int numBands)
{
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto& split = splits[(size_t) ch];
        const auto* in = input.getReadPointer (ch);
        float* bands[maxBands];
        for (int b = 0; b < numBands; ++b)
            bands[b] = bandBuffers[(size_t) b].getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            float out[maxBands] {};
            split.processSample (in[i], out, numBands);
            for (int b = 0; b < numBands; ++b)
                bands[b][i] = out[b];
        }
    }
}

void SaturatorEffect::shapeBand (int band, int numCh, int numSamples, int osIndex)
{
    auto& params = bandParams[(size_t) band];
    const int style = juce::jlimit (0, styleNames().size() - 1,
                                    params.style.load (std::memory_order_relaxed));
    const float driveGain = juce::Decibels::decibelsToGain (params.driveDb.load (std::memory_order_relaxed));
    const float levelGain = juce::Decibels::decibelsToGain (params.levelDb.load (std::memory_order_relaxed));

    auto& buffer = bandBuffers[(size_t) band];
    juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) numCh,
                                        0, (size_t) numSamples);
    block.multiplyBy (driveGain);

    if (osIndex == 0)
    {
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
                data[i] = shapeSample (style, data[i]);
        }
    }
    else if (auto* os = oversamplers[(size_t) band][(size_t) osIndex - 1].get())
    {
        auto upBlock = os->processSamplesUp (block);
        for (size_t ch = 0; ch < upBlock.getNumChannels(); ++ch)
        {
            auto* data = upBlock.getChannelPointer (ch);
            for (size_t i = 0; i < upBlock.getNumSamples(); ++i)
                data[i] = shapeSample (style, data[i]);
        }
        os->processSamplesDown (block);
    }

    // ~5 Hz one-pole DC blocker: the asymmetric styles leave a DC offset the
    // next stage shouldn't see, then the band trim is folded in on top.
    const float r = 1.0f - (float) (juce::MathConstants<double>::twoPi * 5.0 / sampleRateHz);
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        float x1 = dcX[band][ch];
        float y1 = dcY[band][ch];
        for (int i = 0; i < numSamples; ++i)
        {
            const float x = data[i];
            y1 = x - x1 + r * y1;
            x1 = x;
            data[i] = y1 * levelGain;
        }
        dcX[band][ch] = x1;
        dcY[band][ch] = y1;
    }
}

void SaturatorEffect::process (juce::AudioBuffer<float>& stereoBus, int numSamples, const Context& context)
{
    juce::ignoreUnused (context);
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (2, stereoBus.getNumChannels());
    if (numCh < 1 || numSamples < 1 || numSamples > dry.getNumSamples())
        return;

    const int numBands = 1 + juce::jlimit (0, maxBands - 1, bandsIndex.load (std::memory_order_relaxed));
    const int osIndex = juce::jlimit (0, 3, oversampleIndex.load (std::memory_order_relaxed));

    if (osIndex != activeOversample || numBands != activeBands)
    {
        activeOversample = osIndex;
        activeBands = numBands;
        reset();
    }
    if (dirty.exchange (false, std::memory_order_relaxed))
        updateCrossovers();

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

    if (numBands == 1)
    {
        for (int ch = 0; ch < numCh; ++ch)
            bandBuffers[0].copyFrom (ch, 0, stereoBus, ch, 0, numSamples);
    }
    else
    {
        splitBands (stereoBus, numCh, numSamples, numBands);
    }

    for (int b = 0; b < numBands; ++b)
        shapeBand (b, numCh, numSamples, osIndex);

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* out = stereoBus.getWritePointer (ch);
        const auto* dryData = dry.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            float wet = bandBuffers[0].getSample (ch, i);
            for (int b = 1; b < numBands; ++b)
                wet += bandBuffers[(size_t) b].getSample (ch, i);
            out[i] = wet * outGain * gains.wet + dryData[i] * gains.dry;
        }
    }
}
