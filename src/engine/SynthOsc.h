#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

// The synth's single-cycle oscillator, shared by the audio thread and the
// editor's waveform preview so the plotted wave IS the rendered wave.
namespace synthosc
{
// Continuous morph axis: -2 sine, -1 triangle, 0 saw, 1 square. The legacy
// oscShape range (0..1, saw to square) keeps its exact meaning.
inline constexpr float morphMin = -2.0f, morphMax = 1.0f;

inline float polyBlep (double t, double dt)
{
    if (t < dt)
    {
        const double x = t / dt;
        return (float) (x + x - x * x - 1.0);
    }
    if (t > 1.0 - dt)
    {
        const double x = (t - 1.0) / dt;
        return (float) (x * x + x + x + 1.0);
    }
    return 0.0f;
}

// Piecewise-linear phase bend — the classic phase-distortion warp. warp 0 is
// the identity, so unwarped output matches the pre-warp synth bit for bit.
inline double bendPhase (double phase, float warp)
{
    if (warp <= 0.0f)
        return phase;
    const double mid = 0.5 - 0.45 * (double) warp;
    return phase < mid ? phase * (0.5 / mid)
                       : 0.5 + (phase - mid) * (0.5 / (1.0 - mid));
}

inline float sine (double ph)
{
    return (float) std::sin (ph * juce::MathConstants<double>::twoPi);
}

inline float triangle (double ph)
{
    return (float) (ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph);
}

inline float saw (double ph, double dt)
{
    return (float) (2.0 * ph - 1.0) - polyBlep (ph, dt);
}

// The square's warp is pulse width instead of a bend.
inline float square (double ph, double dt, float warp)
{
    const double width = 0.5 - 0.45 * (double) juce::jlimit (0.0f, 1.0f, warp);
    float s = ph < width ? 1.0f : -1.0f;
    s += polyBlep (ph, dt);
    s -= polyBlep (std::fmod (ph + (1.0 - width), 1.0), dt);
    return s;
}

// The synth's resonance-knob-to-Q mapping, shared with the filter response
// display so the plotted curve is the filter that runs.
inline float resonanceToQ (float resonance)
{
    return juce::jmap (resonance, 0.707f, 8.0f);
}

// One oscillator sample. dt is cycles per sample (for the polyBLEP edges);
// previews pass 1/points.
inline float sample (float morph, float warp, double phase, double dt)
{
    morph = juce::jlimit (morphMin, morphMax, morph);
    const double bent = bendPhase (phase, warp);

    if (morph >= 0.0f)
        return saw (bent, dt) * (1.0f - morph) + square (phase, dt, warp) * morph;
    if (morph >= -1.0f)
        return triangle (bent) * (-morph) + saw (bent, dt) * (1.0f + morph);
    return sine (bent) * (-1.0f - morph) + triangle (bent) * (2.0f + morph);
}
} // namespace synthosc
