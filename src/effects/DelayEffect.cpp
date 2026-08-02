#include "DelayEffect.h"
#include "model/Ids.h"

namespace
{
constexpr double maxDelaySeconds = 8.0;
}

const juce::String& DelayEffect::identifier()
{
    static const juce::String id ("builtin:delay");
    return id;
}

const juce::String& DelayEffect::displayName()
{
    static const juce::String name ("Delay");
    return name;
}

const std::vector<fx::ParamSpec>& DelayEffect::specs()
{
    static const std::vector<fx::ParamSpec> s {
        { ids::fxDivision, "Time",   0.0,    14.0, 1.0,     8.0, {},    0, fx::syncDivisionNames() },
        { ids::fxFeedback, "Fdbk",   0.0,    0.95, 1.0,     0.4, {},    2 },
        { ids::fxPingPong, "Mode",   0.0,     1.0, 1.0,     0.0, {},    0, { "Mono", "Ping-pong" } },
        { ids::fxHpFreq,   "HP",    20.0,  2000.0, 0.3,   200.0, " Hz", 0 },
        { ids::fxLpFreq,   "LP",   200.0, 20000.0, 0.3,  6000.0, " Hz", 0 },
        { ids::fxMix,      "Mix",    0.0,     1.0, 1.0,     0.3, {},    2 },
    };
    return s;
}

void DelayEffect::prepare (double sampleRate, int maxBlockSize)
{
    juce::ignoreUnused (maxBlockSize);
    sampleRateHz = sampleRate;
    maxDelaySamples = (int) (maxDelaySeconds * sampleRate);
    line.prepare (2, maxDelaySamples);
    filterDirty.store (true, std::memory_order_relaxed);
    reset();
}

void DelayEffect::reset()
{
    line.reset();
    for (auto& f : feedbackHp) f.reset();
    for (auto& f : feedbackLp) f.reset();
    smoothedDelay = -1.0f;   // snap to the target on the next block
}

void DelayEffect::setParameter (const juce::Identifier& paramId, double value)
{
    if (paramId == ids::fxFeedback)      feedback.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxMix)      mix.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxDivision) division.store ((int) std::lround (value), std::memory_order_relaxed);
    else if (paramId == ids::fxPingPong) pingPong.store (value >= 0.5, std::memory_order_relaxed);
    else if (paramId == ids::fxHpFreq)
    {
        hpFreq.store ((float) value, std::memory_order_relaxed);
        filterDirty.store (true, std::memory_order_relaxed);
    }
    else if (paramId == ids::fxLpFreq)
    {
        lpFreq.store ((float) value, std::memory_order_relaxed);
        filterDirty.store (true, std::memory_order_relaxed);
    }
}

void DelayEffect::process (juce::AudioBuffer<float>& stereoBus, int numSamples, const Context& context)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (2, stereoBus.getNumChannels());
    if (numCh < 2 || numSamples < 1)
        return;

    if (filterDirty.exchange (false, std::memory_order_relaxed))
        for (int ch = 0; ch < 2; ++ch)
        {
            feedbackHp[(size_t) ch].setHighPass (sampleRateHz, hpFreq.load (std::memory_order_relaxed), 0.707);
            feedbackLp[(size_t) ch].setLowPass (sampleRateHz, lpFreq.load (std::memory_order_relaxed), 0.707);
        }

    const bool hpActive = hpFreq.load (std::memory_order_relaxed) > 20.5f;
    const bool lpActive = lpFreq.load (std::memory_order_relaxed) < 19500.0f;

    const double quarters = fx::syncDivisionQuarters (division.load (std::memory_order_relaxed));
    const double seconds = quarters * 60.0 / juce::jmax (1.0, context.tempo);
    const auto target = (float) juce::jlimit (1.0, (double) maxDelaySamples, seconds * sampleRateHz);
    if (smoothedDelay < 0.0f)
        smoothedDelay = target;

    // ~40 ms glide, so retuning the division pitches the tail instead of clicking.
    const auto glide = (float) std::exp (-1.0 / (0.04 * sampleRateHz));

    const float fb = juce::jlimit (0.0f, 0.95f, feedback.load (std::memory_order_relaxed));
    const bool cross = pingPong.load (std::memory_order_relaxed);
    const auto gains = fx::mixGains (mix.load (std::memory_order_relaxed));

    auto* left = stereoBus.getWritePointer (0);
    auto* right = stereoBus.getWritePointer (1);

    for (int i = 0; i < numSamples; ++i)
    {
        smoothedDelay = target + glide * (smoothedDelay - target);

        float tap[2] { line.read (0, smoothedDelay), line.read (1, smoothedDelay) };
        for (int ch = 0; ch < 2; ++ch)
        {
            if (hpActive)
                tap[ch] = feedbackHp[(size_t) ch].processSample (tap[ch]);
            if (lpActive)
                tap[ch] = feedbackLp[(size_t) ch].processSample (tap[ch]);
        }

        const float inL = left[i];
        const float inR = right[i];
        line.write (0, inL + (cross ? tap[1] : tap[0]) * fb);
        line.write (1, inR + (cross ? tap[0] : tap[1]) * fb);
        line.advance();

        left[i]  = inL * gains.dry + tap[0] * gains.wet;
        right[i] = inR * gains.dry + tap[1] * gains.wet;
    }
}
