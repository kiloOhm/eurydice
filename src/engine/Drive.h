#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// Shared waveshaping stage for the kick-design generators. Every curve is
// bounded to +/-1 so the stage clips instead of running away as drive comes up.
namespace drive
{
enum class Curve
{
    soft = 0,   // tanh saturation
    hard = 1,   // hard clip
    fold = 2    // triangle foldback
};

inline constexpr int numCurves = 3;

// amount is 0..1 and maps to 1x..32x of pre-gain.
inline float process (float x, float amount, int curve) noexcept
{
    const float driven = x * (1.0f + amount * 31.0f);

    switch (curve)
    {
        case (int) Curve::hard:
            return juce::jlimit (-1.0f, 1.0f, driven);

        case (int) Curve::fold:
        {
            // Triangle of period 4 through (0,0), (1,1), (3,-1).
            float m = std::fmod (driven + 1.0f, 4.0f);
            if (m < 0.0f)
                m += 4.0f;
            return 1.0f - std::abs (m - 2.0f);
        }

        default:
            return std::tanh (driven);
    }
}

// Applies the stage in place across one segment of a generator's own buffer.
inline void processBlock (juce::AudioBuffer<float>& buffer, int from, int to,
                          float amount, int curve) noexcept
{
    if (amount <= 0.0f)
        return;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        for (int i = from; i < to; ++i)
            data[i] = process (data[i], amount, curve);
    }
}

// Blends a 0..1 envelope value from linear towards a snappier exponential-ish
// curve. shape 0 leaves the envelope untouched.
inline float shapeEnvelope (float envValue, float shape) noexcept
{
    if (shape <= 0.0f)
        return envValue;
    return envValue + shape * (envValue * envValue * envValue - envValue);
}
} // namespace drive
