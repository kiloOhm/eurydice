#include "FilterEffect.h"
#include "model/Ids.h"

const juce::String& FilterEffect::identifier()
{
    static const juce::String id ("builtin:filter");
    return id;
}

const juce::String& FilterEffect::displayName()
{
    static const juce::String name ("Filter");
    return name;
}

const std::vector<fx::ParamSpec>& FilterEffect::specs()
{
    static const std::vector<fx::ParamSpec> s {
        { ids::fxFilterType,  "Type",   0.0,     3.0, 1.0,    0.0, {},    0, { "LP", "HP", "BP" } },
        { ids::fxCutoff,      "Cutoff", 20.0, 20000.0, 0.25, 1200.0, " Hz", 0 },
        { ids::fxResonance,   "Reso",   0.0,     1.0, 1.0,    0.4, {},    2 },
        { ids::fxEnvAmount,   "Env",   -1.0,     1.0, 1.0,    0.0, {},    2 },
        { ids::fxEnvAttack,   "Atk",    0.5,   200.0, 0.4,    5.0, " ms", 1 },
        { ids::fxEnvRelease,  "Rel",    5.0,  2000.0, 0.4,  120.0, " ms", 0 },
        { ids::fxLfoAmount,   "LFO",   -1.0,     1.0, 1.0,    0.0, {},    2 },
        { ids::fxLfoRate,     "Rate",   0.0,    14.0, 1.0,   12.0, {},    0, fx::syncDivisionNames() },
        { ids::fxLfoShape,    "Shape",  0.0,     3.0, 1.0,    0.0, {},    0, { "Sine", "Tri", "Saw", "Square" } },
        { ids::fxMix,         "Mix",    0.0,     1.0, 1.0,    1.0, {},    2 },
    };
    return s;
}

float FilterEffect::lfoValue (int shape, double phase) noexcept
{
    const double p = phase - std::floor (phase);
    switch ((Shape) shape)
    {
        case Shape::triangle: return (float) (p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p);
        case Shape::sawDown:  return (float) (1.0 - 2.0 * p);
        case Shape::square:   return p < 0.5 ? 1.0f : -1.0f;
        case Shape::sine:
        default:              return (float) std::sin (juce::MathConstants<double>::twoPi * p);
    }
}

void FilterEffect::prepare (double sampleRate, int maxBlockSize)
{
    sampleRateHz = sampleRate;
    const int block = juce::jmax (32, maxBlockSize);

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) block, 2 };
    filter.prepare (spec);
    dry.setSize (2, block);
    reset();
}

void FilterEffect::reset()
{
    filter.reset();
    dry.clear();
    envelope = 0.0f;
    lfoPhase = 0.0;
}

void FilterEffect::setParameter (const juce::Identifier& paramId, double value)
{
    if (paramId == ids::fxCutoff)           cutoffHz.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxResonance)   resonance.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxEnvAmount)   envAmount.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxEnvAttack)   envAttackMs.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxEnvRelease)  envReleaseMs.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxLfoAmount)   lfoAmount.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxMix)         mix.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxFilterType)  mode.store ((int) std::lround (value), std::memory_order_relaxed);
    else if (paramId == ids::fxLfoRate)     lfoDivision.store ((int) std::lround (value), std::memory_order_relaxed);
    else if (paramId == ids::fxLfoShape)    lfoShape.store ((int) std::lround (value), std::memory_order_relaxed);
}

void FilterEffect::processControlBlock (juce::AudioBuffer<float>& bus, int start, int numSamples)
{
    const int numCh = juce::jmin (2, bus.getNumChannels());
    for (int i = 0; i < numSamples; ++i)
        for (int ch = 0; ch < numCh; ++ch)
            bus.setSample (ch, start + i, filter.processSample (ch, bus.getSample (ch, start + i)));
}

void FilterEffect::process (juce::AudioBuffer<float>& stereoBus, int numSamples, const Context& context)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (2, stereoBus.getNumChannels());
    if (numCh < 1 || numSamples < 1 || numSamples > dry.getNumSamples())
        return;

    for (int ch = 0; ch < numCh; ++ch)
        dry.copyFrom (ch, 0, stereoBus, ch, 0, numSamples);

    switch ((Mode) juce::jlimit (0, 2, mode.load (std::memory_order_relaxed)))
    {
        case Mode::highPass: filter.setType (juce::dsp::StateVariableTPTFilterType::highpass); break;
        case Mode::bandPass: filter.setType (juce::dsp::StateVariableTPTFilterType::bandpass); break;
        case Mode::lowPass:
        default:             filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass); break;
    }
    const float reso = juce::jlimit (0.0f, 1.0f, resonance.load (std::memory_order_relaxed));
    filter.setResonance (0.5f + reso * reso * 15.0f);

    const double quarters = fx::syncDivisionQuarters (lfoDivision.load (std::memory_order_relaxed));
    const double periodSeconds = quarters * 60.0 / juce::jmax (1.0, context.tempo);
    lfoIncrement = 1.0 / juce::jmax (1.0, periodSeconds * sampleRateHz);
    if (context.playing)
        lfoPhase = context.ppqPosition / juce::jmax (1.0e-6, quarters);

    const int shape = juce::jlimit (0, 3, lfoShape.load (std::memory_order_relaxed));
    const float lfoDepth = lfoAmount.load (std::memory_order_relaxed);
    const float envDepth = envAmount.load (std::memory_order_relaxed);
    const float baseCutoff = cutoffHz.load (std::memory_order_relaxed);

    const double blockSeconds = (double) controlBlock / sampleRateHz;
    const float attackCoeff = (float) std::exp (-blockSeconds
        / juce::jmax (1.0e-4, (double) envAttackMs.load (std::memory_order_relaxed) * 0.001));
    const float releaseCoeff = (float) std::exp (-blockSeconds
        / juce::jmax (1.0e-4, (double) envReleaseMs.load (std::memory_order_relaxed) * 0.001));

    for (int start = 0; start < numSamples; start += controlBlock)
    {
        const int len = juce::jmin (controlBlock, numSamples - start);

        float peak = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            peak = juce::jmax (peak, dry.getMagnitude (ch, start, len));
        const float coeff = peak > envelope ? attackCoeff : releaseCoeff;
        envelope = peak + coeff * (envelope - peak);

        modOctaves = envDepth * 5.0f * juce::jmin (1.0f, envelope)
                     + lfoDepth * 5.0f * lfoValue (shape, lfoPhase);
        filter.setCutoffFrequency (juce::jlimit (20.0f, (float) (sampleRateHz * 0.45),
                                                 baseCutoff * std::pow (2.0f, modOctaves)));

        processControlBlock (stereoBus, start, len);
        lfoPhase += lfoIncrement * len;
    }

    const auto gains = fx::mixGains (mix.load (std::memory_order_relaxed));
    if (gains.dry > 0.0f)
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* data = stereoBus.getWritePointer (ch);
            const auto* dryData = dry.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
                data[i] = data[i] * gains.wet + dryData[i] * gains.dry;
        }
}
