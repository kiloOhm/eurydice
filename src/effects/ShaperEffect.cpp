#include "ShaperEffect.h"
#include "SaturatorEffect.h"
#include "engine/NotePan.h"
#include "model/Ids.h"

namespace
{
// Steps per loop offered by the editor's snapping grid, matching gridNames().
const int gridStepCounts[] = { 0, 2, 3, 4, 6, 8, 12, 16, 24, 32 };

juce::String waveText (std::initializer_list<fx::WavePoint> list)
{
    fx::ShaperWave wave;
    for (const auto& point : list)
        if (wave.numPoints < fx::ShaperWave::maxPoints)
            wave.points[(size_t) wave.numPoints++] = point;
    wave.sort();
    return wave.toString();
}

// A square gate: `steps` even divisions per loop, each open for `duty` of its
// length. Two points per edge keeps the transitions vertical, and the smoothing
// knob is what decides how hard they actually land.
juce::String gateText (int steps, float duty, float low = 0.0f, float high = 1.0f)
{
    fx::ShaperWave wave;
    const float step = 1.0f / (float) steps;
    for (int i = 0; i < steps && wave.numPoints + 2 <= fx::ShaperWave::maxPoints; ++i)
    {
        const float start = (float) i * step;
        wave.points[(size_t) wave.numPoints++] = { start, high, 0.0f };
        wave.points[(size_t) wave.numPoints++] = { start + step * duty, low, 0.0f };
    }
    return wave.toString();
}
} // namespace

ShaperEffect::ShaperEffect()
{
    wave = fx::ShaperWave::defaultWave();
    for (int i = 0; i < tableSize; ++i)
        tables[0][(size_t) i] = wave.valueAt ((float) i / (float) tableSize);
    liveTable.store (0, std::memory_order_release);
}

const juce::String& ShaperEffect::identifier()
{
    static const juce::String id ("builtin:shaper");
    return id;
}

const juce::String& ShaperEffect::displayName()
{
    static const juce::String name ("Shaper");
    return name;
}

const juce::StringArray& ShaperEffect::targetNames()
{
    static const juce::StringArray names { "Volume", "Pan", "Width", "Low Pass", "High Pass", "Drive" };
    return names;
}

const juce::StringArray& ShaperEffect::bandNames()
{
    static const juce::StringArray names { "Full", "Low", "Mid", "High" };
    return names;
}

const juce::StringArray& ShaperEffect::gridNames()
{
    static const juce::StringArray names { "Off", "2", "3", "4", "6", "8", "12", "16", "24", "32" };
    return names;
}

int ShaperEffect::gridSteps (int index) noexcept
{
    const int count = (int) (sizeof (gridStepCounts) / sizeof (int));
    return gridStepCounts[juce::jlimit (0, count - 1, index)];
}

float ShaperEffect::neutralValue (int target) noexcept
{
    switch ((Target) juce::jlimit (0, 5, target))
    {
        case Target::pan:
        case Target::width:    return 0.5f;   // centred / unchanged width
        case Target::drive:    return 0.0f;   // clean
        case Target::volume:
        case Target::lowPass:
        case Target::highPass:
        default:               return 1.0f;   // unity / wide open
    }
}

ShaperEffect::AxisLabels ShaperEffect::axisLabels (int target)
{
    switch ((Target) juce::jlimit (0, 5, target))
    {
        case Target::pan:      return { "right", "left" };
        case Target::width:    return { "wide", "mono" };
        case Target::lowPass:  return { "open", "dark" };
        case Target::highPass: return { "open", "thin" };
        case Target::drive:    return { "hot", "clean" };
        case Target::volume:
        default:               return { "0 dB", "silent" };
    }
}

const std::vector<fx::ParamSpec>& ShaperEffect::specs()
{
    static const std::vector<fx::ParamSpec> s {
        { ids::fxTarget,    "Target", 0.0,   5.0, 1.0,   0.0, {},    0, targetNames() },
        { ids::fxLfoAmount, "Depth",  0.0,   1.0, 1.0,   1.0, {},    2 },
        { ids::fxSmooth,    "Smooth", 0.0,  50.0, 0.5,   3.0, " ms", 1 },
        { ids::fxInvert,    "Invert", 0.0,   1.0, 1.0,   0.0, {},    0, { "Off", "On" } },
        { ids::fxLfoSync,   "Mode",   0.0,   1.0, 1.0,   1.0, {},    0, { "Hz", "Sync" } },
        { ids::fxLfoRate,   "Length", 0.0,  14.0, 1.0,   8.0, {},    0, fx::syncDivisionNames() },
        { ids::fxLfoHz,     "Rate",   0.02, 20.0, 0.3,   1.0, " Hz", 2 },

        { ids::fxPhase,     "Phase",  0.0, 360.0, 1.0,   0.0,
          juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")), 0, {}, false, true },
        { ids::fxResonance, "Reso",   0.0,   1.0, 1.0,   0.2, {},    2 },
        { ids::fxBand,      "Band",   0.0,   3.0, 1.0,   0.0, {},    0, bandNames() },
        { ids::fxCrossLo,   "Split Lo", 40.0, 2000.0, 0.4, 150.0, " Hz", 0 },
        { ids::fxCrossHi,   "Split Hi", 500.0, 12000.0, 0.4, 2500.0, " Hz", 0 },
        { ids::fxGrid,      "Grid",   0.0,   9.0, 1.0,   7.0, {},    0, gridNames() },
        { ids::fxMix,       "Mix",    0.0,   1.0, 1.0,   1.0, {},    2 },
    };
    return s;
}

const std::vector<fx::BuiltinPreset>& ShaperEffect::presets()
{
    // Division indices follow fx::syncDivisionNames(): 2 = 1/16, 5 = 1/8,
    // 8 = 1/4, 11 = 1/2, 12 = 1/1. Target indices follow targetNames().
    auto make = [] (juce::String name, double target, double div, double depth,
                    double smooth, juce::String points, double band = 0.0)
    {
        return fx::BuiltinPreset { std::move (name),
            { { ids::fxTarget, target },   { ids::fxLfoSync, 1.0 },
              { ids::fxLfoRate, div },     { ids::fxLfoAmount, depth },
              { ids::fxSmooth, smooth },   { ids::fxInvert, 0.0 },
              { ids::fxPhase, 0.0 },       { ids::fxBand, band },
              { ids::fxWave, std::move (points) } } };
    };

    static const std::vector<fx::BuiltinPreset> p {
        make ("Sidechain Pump", 0, 8, 1.0, 4.0,
              waveText ({ { 0.0f, 0.0f, 0.55f }, { 0.45f, 1.0f }, { 1.0f, 1.0f } })),
        make ("Deep Duck 1/2", 0, 11, 1.0, 6.0,
              waveText ({ { 0.0f, 0.0f, 0.7f }, { 0.3f, 1.0f }, { 1.0f, 1.0f } })),
        make ("Gate 1/8", 0, 12, 1.0, 1.0, gateText (8, 0.5f)),
        make ("Trance Gate 1/16", 0, 12, 1.0, 2.0, gateText (16, 0.55f)),
        make ("Stutter 1/16", 0, 8, 1.0, 0.5, gateText (4, 0.35f)),
        make ("Half-Time Chop", 0, 12, 1.0, 2.0,
              waveText ({ { 0.0f, 1.0f }, { 0.25f, 1.0f }, { 0.25f, 0.0f },
                          { 0.5f, 0.0f }, { 0.5f, 1.0f }, { 0.75f, 1.0f },
                          { 0.75f, 0.0f }, { 1.0f, 0.0f } })),
        make ("Tremolo 1/8", 0, 5, 0.6, 8.0,
              waveText ({ { 0.0f, 1.0f, -0.3f }, { 0.5f, 0.3f, 0.3f }, { 1.0f, 1.0f } })),
        make ("Rumble Gate (Low)", 0, 12, 1.0, 2.0, gateText (16, 0.5f), 1.0),
        make ("Hat Chop (High)", 0, 12, 1.0, 1.0, gateText (8, 0.4f), 3.0),
        make ("Auto Pan 1/2", 1, 11, 0.8, 10.0,
              waveText ({ { 0.0f, 0.0f, 0.0f }, { 0.5f, 1.0f }, { 1.0f, 0.0f } })),
        make ("Ping Pong 1/8", 1, 5, 1.0, 1.0, gateText (2, 0.5f)),
        make ("Width Breathe", 2, 12, 0.8, 20.0,
              waveText ({ { 0.0f, 0.15f, 0.0f }, { 0.5f, 1.0f }, { 1.0f, 0.15f } })),
        make ("Filter Sweep 1/1", 3, 12, 1.0, 8.0,
              waveText ({ { 0.0f, 0.15f, -0.4f }, { 0.75f, 1.0f }, { 1.0f, 1.0f } })),
        make ("Wobble 1/8", 3, 5, 0.9, 2.0,
              waveText ({ { 0.0f, 1.0f, 0.0f }, { 0.5f, 0.2f }, { 1.0f, 1.0f } })),
        make ("Riser 4 Bars", 4, 14, 1.0, 12.0,
              waveText ({ { 0.0f, 1.0f, 0.5f }, { 1.0f, 0.1f } })),
        make ("Drive Pulse 1/4", 5, 8, 1.0, 3.0,
              waveText ({ { 0.0f, 1.0f, 0.6f }, { 0.35f, 0.0f }, { 1.0f, 0.0f } })),
    };
    return p;
}

void ShaperEffect::prepare (double sampleRate, int maxBlockSize)
{
    sampleRateHz = sampleRate;
    const int block = juce::jmax (32, maxBlockSize);

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) block, 2 };
    filter.prepare (spec);

    for (auto& buffer : bandBuffers)
        buffer.setSize (2, block);
    dry.setSize (2, block);

    crossoverDirty.store (true, std::memory_order_relaxed);
    publishWave();   // the sample rate does not change the table, but a fresh
                     // instance must have one before the first block
    reset();
}

void ShaperEffect::reset()
{
    filter.reset();
    for (auto& split : splits)
        split.reset();
    for (auto& buffer : bandBuffers)
        buffer.clear();
    dry.clear();
    lfoPhase = 0.0;
    smoothedValue = neutralValue (targetIndex.load (std::memory_order_relaxed));
}

void ShaperEffect::setParameter (const juce::Identifier& paramId, double value)
{
    if (paramId == ids::fxTarget)          targetIndex.store ((int) std::lround (value), std::memory_order_relaxed);
    else if (paramId == ids::fxBand)       bandIndex.store ((int) std::lround (value), std::memory_order_relaxed);
    else if (paramId == ids::fxLfoAmount)  depthAmount.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxSmooth)     smoothMs.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxResonance)  resonance.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxPhase)      phaseDeg.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxMix)        mix.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxLfoHz)      rateHz.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxLfoRate)    division.store ((int) std::lround (value), std::memory_order_relaxed);
    else if (paramId == ids::fxLfoSync)    synced.store (value >= 0.5, std::memory_order_relaxed);
    else if (paramId == ids::fxInvert)     invert.store (value >= 0.5, std::memory_order_relaxed);
    else if (paramId == ids::fxCrossLo)  { crossLoHz.store ((float) value, std::memory_order_relaxed);
                                           crossoverDirty.store (true, std::memory_order_relaxed); }
    else if (paramId == ids::fxCrossHi)  { crossHiHz.store ((float) value, std::memory_order_relaxed);
                                           crossoverDirty.store (true, std::memory_order_relaxed); }
    // fxGrid is an editor aid; the DSP has no use for it.
}

void ShaperEffect::applyExtraState (const juce::ValueTree& slot)
{
    const auto text = slot.getProperty (ids::fxWave, juce::String()).toString();
    if (text == appliedWaveText)
        return;   // every snapshot rebuild lands here; only real edits cost a table

    appliedWaveText = text;
    wave = fx::ShaperWave::fromString (text);
    publishWave();
}

void ShaperEffect::publishWave()
{
    auto& table = tables[(size_t) nextTable];
    for (int i = 0; i < tableSize; ++i)
        table[(size_t) i] = wave.valueAt ((float) i / (float) tableSize);

    liveTable.store (nextTable, std::memory_order_release);
    nextTable = (nextTable + 1) % numTables;
}

float ShaperEffect::lookup (const Table& table, double phase) noexcept
{
    const double p = phase - std::floor (phase);
    const double scaled = p * (double) tableSize;
    const int i0 = juce::jlimit (0, tableSize - 1, (int) scaled);
    const int i1 = (i0 + 1) % tableSize;
    const auto frac = (float) (scaled - (double) i0);
    return table[(size_t) i0] + (table[(size_t) i1] - table[(size_t) i0]) * frac;
}

float ShaperEffect::nextValue (const Table& table, double increment, double offset,
                               float neutral, float depth, bool inverted, float glide) noexcept
{
    float target = lookup (table, lfoPhase + offset);
    if (inverted)
        target = 1.0f - target;
    target = neutral + depth * (target - neutral);

    smoothedValue = target + glide * (smoothedValue - target);
    lfoPhase += increment;
    return smoothedValue;
}

void ShaperEffect::applyGainStage (juce::AudioBuffer<float>& buffer, int numCh, int index,
                                   Target target, float value) noexcept
{
    switch (target)
    {
        case Target::pan:
        {
            if (numCh < 2)
                return;
            // Same balance law as channel and insert pan (notepan::gains), so a
            // pan curve behaves like automating the pan knob.
            float left = 1.0f, right = 1.0f;
            notepan::gains (2.0f * value - 1.0f, left, right);
            buffer.setSample (0, index, buffer.getSample (0, index) * left);
            buffer.setSample (1, index, buffer.getSample (1, index) * right);
            break;
        }

        case Target::width:
        {
            if (numCh < 2)
                return;
            const float l = buffer.getSample (0, index);
            const float r = buffer.getSample (1, index);
            const float mid = 0.5f * (l + r);
            const float side = 0.5f * (l - r) * (2.0f * value);
            buffer.setSample (0, index, mid + side);
            buffer.setSample (1, index, mid - side);
            break;
        }

        case Target::drive:
        {
            // The wave both pushes the input harder into the shaper and
            // crossfades the result in, so a value of zero is exactly the
            // input. Deliberately not oversampled — this is the modulated
            // colour-and-movement kind of drive; the Saturator is where hard
            // clipping with an oversampled path belongs.
            const float amount = juce::jlimit (0.0f, 1.0f, value);
            if (amount <= 0.0f)
                return;
            const float gain = 1.0f + amount * 3.0f;              // up to +12 dB
            const float norm = 1.0f / std::tanh (gain);
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float x = buffer.getSample (ch, index);
                const float shaped = SaturatorEffect::shapeSample (
                    (int) SaturatorEffect::Style::tape, x * gain) * norm;
                buffer.setSample (ch, index, x + amount * (shaped - x));
            }
            break;
        }

        case Target::volume:
        default:
            for (int ch = 0; ch < numCh; ++ch)
                buffer.setSample (ch, index, buffer.getSample (ch, index) * value);
            break;
    }
}

void ShaperEffect::filterControlBlock (juce::AudioBuffer<float>& buffer, int numCh,
                                       int start, int len, Target target, float value) noexcept
{
    // Nine octaves of travel from wide open: a wave at the bottom of the display
    // lands the low pass near 40 Hz and the high pass near 10 kHz.
    constexpr float octaves = 9.0f;
    const float closed = juce::jlimit (0.0f, 1.0f, 1.0f - value);
    const float cutoff = target == Target::lowPass
                           ? 20000.0f * std::pow (2.0f, -octaves * closed)
                           : 20.0f * std::pow (2.0f, octaves * closed);

    filter.setCutoffFrequency (juce::jlimit (20.0f, (float) (sampleRateHz * 0.45), cutoff));

    for (int i = 0; i < len; ++i)
        for (int ch = 0; ch < numCh; ++ch)
            buffer.setSample (ch, start + i,
                              filter.processSample (ch, buffer.getSample (ch, start + i)));
}

void ShaperEffect::process (juce::AudioBuffer<float>& stereoBus, int numSamples, const Context& context)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (2, stereoBus.getNumChannels());
    if (numCh < 1 || numSamples < 1 || numSamples > dry.getNumSamples())
        return;

    const auto target = (Target) juce::jlimit (0, 5, targetIndex.load (std::memory_order_relaxed));
    const auto band = (Band) juce::jlimit (0, 3, bandIndex.load (std::memory_order_relaxed));
    const float depth = juce::jlimit (0.0f, 1.0f, depthAmount.load (std::memory_order_relaxed));
    const bool inverted = invert.load (std::memory_order_relaxed);
    const float neutral = neutralValue ((int) target);
    const auto gains = fx::mixGains (mix.load (std::memory_order_relaxed));
    const auto& table = tables[(size_t) liveTable.load (std::memory_order_acquire)];

    double increment;
    if (synced.load (std::memory_order_relaxed))
    {
        const double quarters = fx::syncDivisionQuarters (division.load (std::memory_order_relaxed));
        const double periodSeconds = quarters * 60.0 / juce::jmax (1.0, context.tempo);
        increment = 1.0 / juce::jmax (1.0, periodSeconds * sampleRateHz);
        if (context.playing)   // lock the loop to the timeline so it sits on the bar
            lfoPhase = context.ppqPosition / juce::jmax (1.0e-6, quarters);
    }
    else
    {
        increment = juce::jmax (0.0f, rateHz.load (std::memory_order_relaxed)) / sampleRateHz;
    }

    const bool split = band != Band::full;
    if ((int) target != activeTarget || split != activeSplit)
    {
        activeTarget = (int) target;
        activeSplit = split;
        filter.reset();
        for (auto& channel : splits)
            channel.reset();
        smoothedValue = neutral;   // or the glide would sweep in from the old target's neutral
    }

    const double offset = (double) phaseDeg.load (std::memory_order_relaxed) / 360.0;
    const double smoothSeconds = juce::jmax (1.0e-5, (double) smoothMs.load (std::memory_order_relaxed) * 0.001);
    const auto glide = (float) std::exp (-1.0 / (smoothSeconds * sampleRateHz));

    for (int ch = 0; ch < numCh; ++ch)
        dry.copyFrom (ch, 0, stereoBus, ch, 0, numSamples);

    if (target == Target::lowPass || target == Target::highPass)
    {
        filter.setType (target == Target::lowPass ? juce::dsp::StateVariableTPTFilterType::lowpass
                                                  : juce::dsp::StateVariableTPTFilterType::highpass);
        const float reso = juce::jlimit (0.0f, 1.0f, resonance.load (std::memory_order_relaxed));
        filter.setResonance (0.5f + reso * reso * 15.0f);   // matches FilterEffect's law
    }

    // Only one band is shaped; the others are split off, kept aside and summed
    // back afterwards. The crossover's allpass compensation is what lets them
    // rejoin flat.
    if (split)
    {
        if (crossoverDirty.exchange (false, std::memory_order_relaxed))
            for (auto& channel : splits)
                channel.setFrequencies (sampleRateHz,
                                        crossLoHz.load (std::memory_order_relaxed),
                                        crossHiHz.load (std::memory_order_relaxed));

        for (int ch = 0; ch < numCh; ++ch)
        {
            const auto* in = dry.getReadPointer (ch);
            auto& channel = splits[(size_t) ch];
            for (int i = 0; i < numSamples; ++i)
            {
                float bands[3] {};
                channel.processSample (in[i], bands, 3);
                for (int b = 0; b < 3; ++b)
                    bandBuffers[(size_t) b].setSample (ch, i, bands[b]);
            }
        }
    }

    auto& shaped = split ? bandBuffers[(size_t) ((int) band - 1)] : stereoBus;

    for (int start = 0; start < numSamples; start += controlBlock)
    {
        const int len = juce::jmin (controlBlock, numSamples - start);

        if (target == Target::lowPass || target == Target::highPass)
        {
            // Retuning the filter costs a tan(), so the wave is sampled once
            // per control block; the glide covers the steps between.
            const float value = nextValue (table, increment, offset, neutral, depth, inverted, glide);
            filterControlBlock (shaped, numCh, start, len, target, value);
            for (int i = 1; i < len; ++i)
                nextValue (table, increment, offset, neutral, depth, inverted, glide);
        }
        else
        {
            for (int i = 0; i < len; ++i)
            {
                const float value = nextValue (table, increment, offset, neutral, depth, inverted, glide);
                applyGainStage (shaped, numCh, start + i, target, value);
            }
        }
    }

    displayPhase.store ((float) (lfoPhase - std::floor (lfoPhase)), std::memory_order_relaxed);
    displayValue.store (smoothedValue, std::memory_order_relaxed);
    lfoPhase -= std::floor (lfoPhase);

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* out = stereoBus.getWritePointer (ch);
        const auto* dryData = dry.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            const float wet = split ? bandBuffers[0].getSample (ch, i)
                                      + bandBuffers[1].getSample (ch, i)
                                      + bandBuffers[2].getSample (ch, i)
                                    : out[i];
            out[i] = wet * gains.wet + dryData[i] * gains.dry;
        }
    }
}
