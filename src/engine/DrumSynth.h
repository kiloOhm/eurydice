#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// The synthesised fallback drums, shared by the sampler channel (default
// channels with no sample) and the drum machine (its starter kit). Renders a
// stereo buffer at the device rate; deterministic so a re-render after a
// device change sounds identical.
namespace drumsynth
{
inline juce::AudioBuffer<float> render (const juce::String& kind, double sr)
{
    juce::AudioBuffer<float> out;
    juce::Random rng (0x5eed);
    const auto lower = kind.toLowerCase();

    auto makeBuffer = [&] (double seconds)
    {
        out.setSize (2, (int) (seconds * sr));
        out.clear();
    };

    if (lower.contains ("kick"))
    {
        makeBuffer (0.40);
        double phase = 0.0;
        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            const double t = i / sr;
            const double freq = 50.0 + 110.0 * std::exp (-t * 22.0);
            phase += freq * juce::MathConstants<double>::twoPi / sr;
            const float env = (float) std::exp (-t * 9.0);
            const float click = (float) (std::exp (-t * 400.0) * 0.4 * (rng.nextFloat() * 2.0f - 1.0f));
            const float s = (float) std::sin (phase) * env * 0.9f + click;
            out.setSample (0, i, s);
            out.setSample (1, i, s);
        }
    }
    else if (lower.contains ("snare"))
    {
        makeBuffer (0.25);
        double phase = 0.0;
        float noiseLP = 0.0f;
        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            const double t = i / sr;
            phase += 185.0 * juce::MathConstants<double>::twoPi / sr;
            const float tone  = (float) (std::sin (phase) * std::exp (-t * 30.0) * 0.5);
            const float white = rng.nextFloat() * 2.0f - 1.0f;
            noiseLP += 0.35f * (white - noiseLP);   // tame the top end a bit
            const float noise = (white - noiseLP * 0.5f) * (float) std::exp (-t * 18.0) * 0.55f;
            const float s = tone + noise;
            out.setSample (0, i, s);
            out.setSample (1, i, s);
        }
    }
    else if (lower.contains ("clap"))
    {
        makeBuffer (0.30);
        float lp = 0.0f;
        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            const double t = i / sr;
            // Three quick bursts then a tail, like layered claps.
            double burstEnv = 0.0;
            for (int b = 0; b < 3; ++b)
                burstEnv = juce::jmax (burstEnv, std::exp (-(juce::jmax (0.0, t - 0.011 * b)) * 90.0)
                                                    * (t >= 0.011 * b ? 1.0 : 0.0));
            burstEnv = juce::jmax (burstEnv, std::exp (-t * 11.0) * 0.5);
            const float white = rng.nextFloat() * 2.0f - 1.0f;
            lp += 0.25f * (white - lp);
            const float band = white - lp;   // rough bandpass
            const float s = band * (float) burstEnv * 0.8f;
            out.setSample (0, i, s);
            out.setSample (1, i, s);
        }
    }
    else   // hat / anything else: short bright noise
    {
        makeBuffer (0.09);
        float lp = 0.0f;
        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            const double t = i / sr;
            const float white = rng.nextFloat() * 2.0f - 1.0f;
            lp += 0.55f * (white - lp);
            const float highpassed = white - lp;
            const float s = highpassed * (float) std::exp (-t * 55.0) * 0.6f;
            out.setSample (0, i, s);
            out.setSample (1, i, s);
        }
    }

    return out;
}
} // namespace drumsynth
