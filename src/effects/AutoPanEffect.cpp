#include "AutoPanEffect.h"
#include "model/Ids.h"

const juce::String& AutoPanEffect::identifier()
{
    static const juce::String id ("builtin:autopan");
    return id;
}

const juce::String& AutoPanEffect::displayName()
{
    static const juce::String name ("Auto Pan");
    return name;
}

const std::vector<fx::ParamSpec>& AutoPanEffect::specs()
{
    static const std::vector<fx::ParamSpec> s {
        { ids::fxLfoShape,  "Shape",  0.0,   3.0, 1.0,    0.0, {},    0, { "Sine", "Tri", "Saw", "Square" } },
        { ids::fxLfoSync,   "Mode",   0.0,   1.0, 1.0,    1.0, {},    0, { "Hz", "Sync" } },
        { ids::fxLfoHz,     "Rate",   0.02, 50.0, 0.3,    1.0, " Hz", 2 },
        { ids::fxLfoRate,   "Sync",   0.0,  14.0, 1.0,    8.0, {},    0, fx::syncDivisionNames() },
        { ids::fxPhase,     "Phase",  0.0, 360.0, 1.0,  180.0,
          juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")), 0, {}, false, true },
        { ids::fxLfoAmount, "Amount", 0.0,   1.0, 1.0,    0.5, {},    2 },
        { ids::fxMix,       "Mix",    0.0,   1.0, 1.0,    1.0, {},    2 },
    };
    return s;
}

const std::vector<fx::BuiltinPreset>& AutoPanEffect::presets()
{
    auto make = [] (juce::String name, double shp, double sync, double hz,
                    double div, double phase, double amt)
    {
        return fx::BuiltinPreset { std::move (name),
            { { ids::fxLfoShape, shp }, { ids::fxLfoSync, sync },
              { ids::fxLfoHz, hz },     { ids::fxLfoRate, div },
              { ids::fxPhase, phase },  { ids::fxLfoAmount, amt } } };
    };

    // Division indices follow fx::syncDivisionNames(): 2 = 1/16, 5 = 1/8,
    // 8 = 1/4, 11 = 1/2.
    static const std::vector<fx::BuiltinPreset> p {
        make ("Autopan 1/4",  0, 1, 1.0,  8, 180, 1.0),
        make ("Autopan 1/2",  0, 1, 1.0, 11, 180, 0.8),
        make ("Slow Drift",   0, 0, 0.2,  8, 180, 0.7),
        make ("Wide Wobble",  1, 0, 3.0,  8, 180, 0.5),
        make ("Amp Tremolo",  0, 0, 5.5,  8,   0, 0.6),
        make ("Tremolo 1/8",  1, 1, 1.0,  5,   0, 0.7),
        make ("Chopper 1/16", 3, 1, 1.0,  2,   0, 1.0),
        make ("Gate 1/8",     3, 1, 1.0,  5,   0, 1.0),
        make ("Pump 1/4",     2, 1, 1.0,  8,   0, 0.9),
    };
    return p;
}

float AutoPanEffect::channelGain (int shape, double phase, float amount) noexcept
{
    const double p = phase - std::floor (phase);

    double dip;   // 0 = full level, 1 = fully attenuated; every cycle starts loud
    switch ((Shape) shape)
    {
        case Shape::triangle: dip = p < 0.5 ? 2.0 * p : 2.0 - 2.0 * p; break;
        case Shape::sawDown:  dip = p; break;
        case Shape::square:   dip = p < 0.5 ? 0.0 : 1.0; break;
        case Shape::sine:
        default:              dip = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * p); break;
    }
    return 1.0f - amount * (float) dip;
}

void AutoPanEffect::prepare (double sampleRate, int maxBlockSize)
{
    juce::ignoreUnused (maxBlockSize);
    sampleRateHz = sampleRate;
    reset();
}

void AutoPanEffect::reset()
{
    lfoPhase = 0.0;
    smoothedGain[0] = smoothedGain[1] = 1.0f;
}

void AutoPanEffect::setParameter (const juce::Identifier& paramId, double value)
{
    if (paramId == ids::fxLfoAmount)     amount.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxLfoHz)    rateHz.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxPhase)    phaseDeg.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxMix)      mix.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxLfoShape) shape.store ((int) std::lround (value), std::memory_order_relaxed);
    else if (paramId == ids::fxLfoRate)  division.store ((int) std::lround (value), std::memory_order_relaxed);
    else if (paramId == ids::fxLfoSync)  synced.store (value >= 0.5, std::memory_order_relaxed);
}

void AutoPanEffect::process (juce::AudioBuffer<float>& stereoBus, int numSamples, const Context& context)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (2, stereoBus.getNumChannels());
    if (numCh < 2 || numSamples < 1)
        return;

    double increment;
    if (synced.load (std::memory_order_relaxed))
    {
        const double quarters = fx::syncDivisionQuarters (division.load (std::memory_order_relaxed));
        const double periodSeconds = quarters * 60.0 / juce::jmax (1.0, context.tempo);
        increment = 1.0 / juce::jmax (1.0, periodSeconds * sampleRateHz);
        if (context.playing)   // lock to the timeline so the pan lands on the beat
            lfoPhase = context.ppqPosition / juce::jmax (1.0e-6, quarters);
    }
    else
    {
        increment = juce::jmax (0.0f, rateHz.load (std::memory_order_relaxed)) / sampleRateHz;
    }

    const int shp = juce::jlimit (0, 3, shape.load (std::memory_order_relaxed));
    const float depth = juce::jlimit (0.0f, 1.0f, amount.load (std::memory_order_relaxed));
    const double offset = (double) phaseDeg.load (std::memory_order_relaxed) / 360.0;
    const auto gains = fx::mixGains (mix.load (std::memory_order_relaxed));

    // ~0.5 ms gain glide: inaudible on the smooth shapes, turns square edges
    // into click-free ramps.
    const auto glide = (float) std::exp (-1.0 / (0.0005 * sampleRateHz));

    auto* left = stereoBus.getWritePointer (0);
    auto* right = stereoBus.getWritePointer (1);

    for (int i = 0; i < numSamples; ++i)
    {
        const float targetL = channelGain (shp, lfoPhase, depth);
        const float targetR = channelGain (shp, lfoPhase + offset, depth);
        smoothedGain[0] = targetL + glide * (smoothedGain[0] - targetL);
        smoothedGain[1] = targetR + glide * (smoothedGain[1] - targetR);

        left[i]  *= gains.dry + gains.wet * smoothedGain[0];
        right[i] *= gains.dry + gains.wet * smoothedGain[1];
        lfoPhase += increment;
    }
    lfoPhase -= std::floor (lfoPhase);
}
