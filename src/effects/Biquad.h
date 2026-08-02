#pragma once

#include <cmath>
#include <juce_core/juce_core.h>

namespace fx
{
// Transposed direct-form II biquad with RBJ cookbook coefficients. Deliberately
// allocation-free (unlike juce::dsp::IIR::Coefficients, which is heap
// allocated and reference counted) so filters can be retuned on the audio
// thread whenever a parameter moves.
struct Biquad
{
    void reset() noexcept { z1 = 0.0f; z2 = 0.0f; }

    float processSample (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setPassthrough() noexcept
    {
        b0 = 1.0f; b1 = 0.0f; b2 = 0.0f; a1 = 0.0f; a2 = 0.0f;
    }

    // |H(e^jw)| of the current coefficients; w in radians per sample. Feeds
    // the editors' response curves, so what is drawn is what runs.
    double magnitudeAt (double w) const noexcept
    {
        const double c1 = std::cos (w),       s1 = std::sin (w);
        const double c2 = std::cos (2.0 * w), s2 = std::sin (2.0 * w);
        const double nr = b0 + b1 * c1 + b2 * c2, ni = b1 * s1 + b2 * s2;
        const double dr = 1.0 + a1 * c1 + a2 * c2, di = a1 * s1 + a2 * s2;
        return std::sqrt ((nr * nr + ni * ni) / juce::jmax (1.0e-24, dr * dr + di * di));
    }

    void setLowPass (double sampleRate, double freq, double q) noexcept
    {
        const auto c = common (sampleRate, freq, q);
        const double b1n = 1.0 - c.cosw;
        set (b1n * 0.5, b1n, b1n * 0.5, c);
    }

    void setHighPass (double sampleRate, double freq, double q) noexcept
    {
        const auto c = common (sampleRate, freq, q);
        const double b1n = -(1.0 + c.cosw);
        set ((1.0 + c.cosw) * 0.5, b1n, (1.0 + c.cosw) * 0.5, c);
    }

    void setBandPass (double sampleRate, double freq, double q) noexcept
    {
        const auto c = common (sampleRate, freq, q);
        set (c.alpha, 0.0, -c.alpha, c);
    }

    void setNotch (double sampleRate, double freq, double q) noexcept
    {
        const auto c = common (sampleRate, freq, q);
        set (1.0, -2.0 * c.cosw, 1.0, c);
    }

    void setPeak (double sampleRate, double freq, double q, double gainDb) noexcept
    {
        const auto c = common (sampleRate, freq, q);
        const double a = std::pow (10.0, gainDb / 40.0);
        setRaw (1.0 + c.alpha * a, -2.0 * c.cosw, 1.0 - c.alpha * a,
                1.0 + c.alpha / a, -2.0 * c.cosw, 1.0 - c.alpha / a);
    }

    void setLowShelf (double sampleRate, double freq, double q, double gainDb) noexcept
    {
        const auto c = common (sampleRate, freq, q);
        const double a = std::pow (10.0, gainDb / 40.0);
        const double s = 2.0 * std::sqrt (a) * c.alpha;
        setRaw (a * ((a + 1.0) - (a - 1.0) * c.cosw + s),
                2.0 * a * ((a - 1.0) - (a + 1.0) * c.cosw),
                a * ((a + 1.0) - (a - 1.0) * c.cosw - s),
                (a + 1.0) + (a - 1.0) * c.cosw + s,
                -2.0 * ((a - 1.0) + (a + 1.0) * c.cosw),
                (a + 1.0) + (a - 1.0) * c.cosw - s);
    }

    void setHighShelf (double sampleRate, double freq, double q, double gainDb) noexcept
    {
        const auto c = common (sampleRate, freq, q);
        const double a = std::pow (10.0, gainDb / 40.0);
        const double s = 2.0 * std::sqrt (a) * c.alpha;
        setRaw (a * ((a + 1.0) + (a - 1.0) * c.cosw + s),
                -2.0 * a * ((a - 1.0) + (a + 1.0) * c.cosw),
                a * ((a + 1.0) + (a - 1.0) * c.cosw - s),
                (a + 1.0) - (a - 1.0) * c.cosw + s,
                2.0 * ((a - 1.0) - (a + 1.0) * c.cosw),
                (a + 1.0) - (a - 1.0) * c.cosw - s);
    }

private:
    struct Common { double cosw = 0.0; double alpha = 0.0; };

    static Common common (double sampleRate, double freq, double q) noexcept
    {
        const double nyquist = sampleRate * 0.5;
        const double f = juce::jlimit (10.0, nyquist * 0.995, freq);
        const double w = juce::MathConstants<double>::twoPi * f / sampleRate;
        return { std::cos (w), std::sin (w) / (2.0 * juce::jmax (0.05, q)) };
    }

    void set (double B0, double B1, double B2, const Common& c) noexcept
    {
        setRaw (B0, B1, B2, 1.0 + c.alpha, -2.0 * c.cosw, 1.0 - c.alpha);
    }

    void setRaw (double B0, double B1, double B2, double A0, double A1, double A2) noexcept
    {
        const double inv = 1.0 / A0;
        b0 = (float) (B0 * inv);
        b1 = (float) (B1 * inv);
        b2 = (float) (B2 * inv);
        a1 = (float) (A1 * inv);
        a2 = (float) (A2 * inv);
    }

    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;
};
} // namespace fx
