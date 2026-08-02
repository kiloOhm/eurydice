#include "CompressorEffect.h"
#include "model/Ids.h"

const juce::String& CompressorEffect::identifier()
{
    static const juce::String id ("builtin:compressor");
    return id;
}

const juce::String& CompressorEffect::displayName()
{
    static const juce::String name ("Compressor");
    return name;
}

const std::vector<fx::ParamSpec>& CompressorEffect::specs()
{
    static const std::vector<fx::ParamSpec> s {
        // Defaults tuned so a fresh instance is roughly unity RMS on a busy
        // bus (gentle 2.5:1 over a soft knee, makeup covering the reduction);
        // the old -18/4:1/no-makeup start cost a kick bus ~5 dB.
        { ids::fxThreshold, "Thresh", -60.0,    0.0, 1.0,  -12.0, " dB", 1 },
        { ids::fxRatio,     "Ratio",    1.0,   20.0, 0.4,    2.5, ":1",  1 },
        { ids::fxAttack,    "Atk",      0.1,  200.0, 0.35,   8.0, " ms", 1 },
        { ids::fxRelease,   "Rel",      5.0, 1000.0, 0.4,  120.0, " ms", 0 },
        { ids::fxKnee,      "Knee",     0.0,   24.0, 1.0,    6.0, " dB", 1 },
        { ids::fxMakeup,    "Makeup", -12.0,   24.0, 1.0,    2.5, " dB", 1 },
        { ids::fxScHpFreq,  "SC HP",   20.0, 2000.0, 0.3,   20.0, " Hz", 0 },
        { ids::fxMix,       "Mix",      0.0,    1.0, 1.0,    1.0, {},    2 },
        { ids::fxSidechain, "Sidechain", -1.0, 32.0, 1.0,   -1.0, {},    0, {}, true },
    };
    return s;
}

void CompressorEffect::prepare (double sampleRate, int maxBlockSize)
{
    sampleRateHz = sampleRate;
    dry.setSize (2, juce::jmax (32, maxBlockSize));
    filterDirty.store (true, std::memory_order_relaxed);
    reset();
}

void CompressorEffect::reset()
{
    for (auto& f : scHighPass)
        f.reset();
    dry.clear();
    envelopeDb = 0.0f;
    lastReductionDb.store (0.0f, std::memory_order_relaxed);
}

void CompressorEffect::setParameter (const juce::Identifier& paramId, double value)
{
    if (paramId == ids::fxThreshold)      thresholdDb.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxRatio)     ratio.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxAttack)    attackMs.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxRelease)   releaseMs.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxKnee)      kneeDb.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxMakeup)    makeupDb.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxMix)       mix.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxSidechain) sidechainInsert.store ((int) std::lround (value), std::memory_order_relaxed);
    else if (paramId == ids::fxScHpFreq)
    {
        scHpFreq.store ((float) value, std::memory_order_relaxed);
        filterDirty.store (true, std::memory_order_relaxed);
    }
}

void CompressorEffect::process (juce::AudioBuffer<float>& stereoBus, int numSamples, const Context& context)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (2, stereoBus.getNumChannels());
    if (numCh < 1 || numSamples < 1 || numSamples > dry.getNumSamples())
        return;

    if (filterDirty.exchange (false, std::memory_order_relaxed))
        for (auto& f : scHighPass)
            f.setHighPass (sampleRateHz, scHpFreq.load (std::memory_order_relaxed), 0.707);

    const bool scActive = scHpFreq.load (std::memory_order_relaxed) > 20.5f;
    const auto* detectSource = &stereoBus;
    if (context.sidechain != nullptr && context.sidechain->getNumSamples() >= numSamples
        && context.sidechain->getNumChannels() >= 1)
        detectSource = context.sidechain;
    const int detectCh = juce::jmin (numCh, detectSource->getNumChannels());

    for (int ch = 0; ch < numCh; ++ch)
        dry.copyFrom (ch, 0, stereoBus, ch, 0, numSamples);

    const float threshold = thresholdDb.load (std::memory_order_relaxed);
    const float knee = juce::jmax (0.01f, kneeDb.load (std::memory_order_relaxed));
    const float slope = 1.0f / juce::jmax (1.0f, ratio.load (std::memory_order_relaxed)) - 1.0f;
    const float makeup = juce::Decibels::decibelsToGain (makeupDb.load (std::memory_order_relaxed));
    const auto gains = fx::mixGains (mix.load (std::memory_order_relaxed));

    const float attackCoeff = (float) std::exp (-1.0
        / juce::jmax (1.0, (double) attackMs.load (std::memory_order_relaxed) * 0.001 * sampleRateHz));
    const float releaseCoeff = (float) std::exp (-1.0
        / juce::jmax (1.0, (double) releaseMs.load (std::memory_order_relaxed) * 0.001 * sampleRateHz));

    float maxReduction = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        float detector = 0.0f;
        for (int ch = 0; ch < detectCh; ++ch)
        {
            float s = detectSource->getSample (ch, i);
            if (scActive)
                s = scHighPass[(size_t) ch].processSample (s);
            detector = juce::jmax (detector, std::abs (s));
        }

        const float levelDb = juce::Decibels::gainToDecibels (detector, -120.0f);
        const float over = levelDb - threshold;

        float targetReduction = 0.0f;
        if (2.0f * over > knee)
            targetReduction = -slope * over;
        else if (2.0f * over > -knee)
        {
            const float t = over + knee * 0.5f;
            targetReduction = -slope * t * t / (2.0f * knee);
        }

        const float coeff = targetReduction > envelopeDb ? attackCoeff : releaseCoeff;
        envelopeDb = targetReduction + coeff * (envelopeDb - targetReduction);
        maxReduction = juce::jmax (maxReduction, envelopeDb);

        const float gain = juce::Decibels::decibelsToGain (-envelopeDb) * makeup;
        for (int ch = 0; ch < numCh; ++ch)
            stereoBus.setSample (ch, i, stereoBus.getSample (ch, i) * gain * gains.wet
                                            + dry.getSample (ch, i) * gains.dry);
    }

    lastReductionDb.store (maxReduction, std::memory_order_relaxed);
}

void CompressorEffect::configureDuckSlot (juce::ValueTree slot, int sourceInsert,
                                          juce::UndoManager* undo)
{
    slot.setProperty (ids::pluginId, identifier(), undo);
    slot.setProperty (ids::pluginState, juce::String(), nullptr);
    BuiltinEffect::writeDefaults (slot, specs(), undo);
    slot.setProperty (ids::fxThreshold, -30.0, undo);
    slot.setProperty (ids::fxRatio,       8.0, undo);
    slot.setProperty (ids::fxAttack,      0.5, undo);
    slot.setProperty (ids::fxRelease,   100.0, undo);
    slot.setProperty (ids::fxKnee,        3.0, undo);
    slot.setProperty (ids::fxMakeup,      0.0, undo);
    slot.setProperty (ids::fxSidechain, sourceInsert, undo);
}
