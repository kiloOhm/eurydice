#pragma once

#include <algorithm>
#include <cmath>
#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>
#include "effects/Biquad.h"

// The kick instrument's DSP, split out of the generator so the editor's
// displays can run the identical code the engine runs — the envelope curves,
// the layer waveforms and the EQ response drawn in the editor are measurements
// of the sound, not illustrations of it.
namespace kickdsp
{
// ---------------------------------------------------------------------------
// Breakpoint envelope
// ---------------------------------------------------------------------------

// One node of a drawn envelope. `pos` is 0..1 of the envelope's span, `value`
// is 0..1, `tension` bends the segment reaching the NEXT point — the same
// curve law the automation clips use (EngineSnapshot::valueAt), so a curve
// drawn here behaves like a curve drawn there.
struct Point
{
    float pos = 0.0f;
    float value = 0.0f;
    float tension = 0.0f;
};

// A drawn envelope. Empty means "no envelope stored": the generator then falls
// back to its analytic decay, which is what every project saved before the
// curve editor existed contains.
struct Envelope
{
    std::vector<Point> points;

    bool empty() const noexcept { return points.size() < 2; }

    // Value at 0..1 of the span. Flat outside the first and last point.
    float valueAt (float u) const noexcept
    {
        if (points.empty())
            return 0.0f;
        if (points.size() == 1 || u <= points.front().pos)
            return points.front().value;
        if (u >= points.back().pos)
            return points.back().value;

        for (size_t i = 1; i < points.size(); ++i)
        {
            const auto& a = points[i - 1];
            const auto& b = points[i];
            if (u <= b.pos)
            {
                const float span = juce::jmax (1.0e-6f, b.pos - a.pos);
                float t = (u - a.pos) / span;
                // tension > 0 falls fast then slow, < 0 slow then fast.
                t = std::pow (t, (float) std::pow (4.0, (double) -a.tension));
                return a.value + (b.value - a.value) * t;
            }
        }
        return points.back().value;
    }

    // Keeps the list usable after an edit: sorted, in range, ends pinned.
    void tidy()
    {
        std::stable_sort (points.begin(), points.end(),
                          [] (const Point& a, const Point& b) { return a.pos < b.pos; });
        for (auto& p : points)
        {
            p.pos     = juce::jlimit (0.0f, 1.0f, p.pos);
            p.value   = juce::jlimit (0.0f, 1.0f, p.value);
            p.tension = juce::jlimit (-1.0f, 1.0f, p.tension);
        }
        if (! points.empty())
        {
            points.front().pos = 0.0f;
            points.back().pos = 1.0f;
        }
    }
};

// The analytic fallback: the exponential the kick has always used, expressed
// on the same 0..1 span so the display can draw both modes the same way.
// `fall` is how many time constants the span covers.
inline float analyticDecay (float u, float fall) noexcept
{
    return std::exp (-fall * juce::jlimit (0.0f, 1.0f, u));
}

// ---------------------------------------------------------------------------
// Layers
// ---------------------------------------------------------------------------

// Triangle through (0,0), (0.25,1), (0.75,-1) so it lines up with the sine it
// is blended against.
inline float triangle (double phase) noexcept
{
    const double t = 4.0 * phase;
    if (t < 1.0) return (float) t;
    if (t < 3.0) return (float) (2.0 - t);
    return (float) (t - 4.0);
}

// The body oscillator. `shape` morphs sine to triangle; `harm` bends the phase
// with the wave's own value (self-FM), which grows the harmonics that make a
// kick read as "hard" rather than "round".
inline float body (double phase, float shape, float harm) noexcept
{
    phase -= std::floor (phase);
    if (harm > 0.0f)
    {
        phase += harm * 0.35 * std::sin (juce::MathConstants<double>::twoPi * phase);
        phase -= std::floor (phase);
    }
    const float sine = std::sin ((float) (phase * juce::MathConstants<double>::twoPi));
    return sine + shape * (triangle (phase) - sine);
}

enum class ClickType
{
    tick = 0,     // a short sine burst — the classic Kick click
    noise = 1,    // band-limited noise snap
    pulse = 2,    // hard square edge, the most aggressive of the three
    sample = 3    // whatever WAV the click layer was pointed at
};

inline constexpr int numClickTypes = 4;

// One sample of the synthesised click layer at a phase in cycles.
inline float click (int type, double phase, float noiseValue) noexcept
{
    phase -= std::floor (phase);
    switch (type)
    {
        case (int) ClickType::noise:
            return noiseValue;
        case (int) ClickType::pulse:
            return phase < 0.5 ? 1.0f : -1.0f;
        default:
            return std::sin ((float) (phase * juce::MathConstants<double>::twoPi));
    }
}

// ---------------------------------------------------------------------------
// Output chain
// ---------------------------------------------------------------------------

// Low shelf / bell / high shelf, the three moves a kick actually needs: weight,
// the 300–800 Hz box, and the top of the click.
struct ToneEq
{
    struct Settings
    {
        float lowFreq = 90.0f,   lowGain = 0.0f;
        float midFreq = 500.0f,  midGain = 0.0f;
        float highFreq = 4000.0f, highGain = 0.0f;

        bool isFlat() const noexcept
        {
            return std::abs (lowGain) < 0.01f && std::abs (midGain) < 0.01f
                   && std::abs (highGain) < 0.01f;
        }
    };

    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        reset();
    }

    void reset() noexcept
    {
        for (auto& channel : filters)
            for (auto& f : channel)
                f.reset();
    }

    void setSettings (const Settings& s) noexcept
    {
        if (juce::exactlyEqual (s.lowFreq, current.lowFreq)
            && juce::exactlyEqual (s.lowGain, current.lowGain)
            && juce::exactlyEqual (s.midFreq, current.midFreq)
            && juce::exactlyEqual (s.midGain, current.midGain)
            && juce::exactlyEqual (s.highFreq, current.highFreq)
            && juce::exactlyEqual (s.highGain, current.highGain))
            return;   // the coefficients already match: nothing to recompute
        current = s;
        for (auto& channel : filters)
        {
            channel[0].setLowShelf  (sampleRate, s.lowFreq, 0.707, s.lowGain);
            channel[1].setPeak      (sampleRate, s.midFreq, 1.1, s.midGain);
            channel[2].setHighShelf (sampleRate, s.highFreq, 0.707, s.highGain);
        }
    }

    float processSample (int channel, float x) noexcept
    {
        auto& f = filters[(size_t) juce::jlimit (0, 1, channel)];
        return f[2].processSample (f[1].processSample (f[0].processSample (x)));
    }

    // Magnitude in dB at a frequency, straight off the running coefficients.
    double magnitudeDb (double freqHz) const noexcept
    {
        const double w = juce::MathConstants<double>::twoPi * freqHz / sampleRate;
        const auto& f = filters[0];
        const double mag = f[0].magnitudeAt (w) * f[1].magnitudeAt (w) * f[2].magnitudeAt (w);
        return juce::Decibels::gainToDecibels (mag, -60.0);
    }

    Settings settings() const noexcept { return current; }

private:
    std::array<std::array<fx::Biquad, 3>, 2> filters;
    Settings current { 0.0f, -999.0f, 0.0f, -999.0f, 0.0f, -999.0f };   // forces the first setup
    double sampleRate = 44100.0;
};

// One-knob compressor: amount picks the threshold and ratio together, so it
// goes from untouched to visibly flattened without a control surface of its
// own. Peak detection on the louder channel keeps the stereo image intact.
struct Compressor
{
    void prepare (double sr) noexcept
    {
        attackCoef  = (float) std::exp (-1.0 / (0.0015 * sr));   // 1.5 ms
        releaseCoef = (float) std::exp (-1.0 / (0.12 * sr));     // 120 ms
        reset();
    }

    void reset() noexcept { envelope = 0.0f; }

    // Returns the gain to apply to both channels for this sample.
    float gainFor (float peak, float amount) noexcept
    {
        const float coef = peak > envelope ? attackCoef : releaseCoef;
        envelope = peak + coef * (envelope - peak);
        if (amount <= 0.0f || envelope <= 0.0f)
            return 1.0f;

        const float thresholdDb = -2.0f - 26.0f * amount;
        const float ratio = 1.0f + 9.0f * amount;
        const float levelDb = juce::Decibels::gainToDecibels (envelope, -100.0f);
        if (levelDb <= thresholdDb)
            return 1.0f;

        const float compressedDb = thresholdDb + (levelDb - thresholdDb) / ratio;
        // Make-up returns most of what the threshold took away.
        const float makeupDb = -thresholdDb * (1.0f - 1.0f / ratio) * 0.6f;
        return juce::Decibels::decibelsToGain (compressedDb - levelDb + makeupDb);
    }

private:
    float envelope = 0.0f;
    float attackCoef = 0.0f, releaseCoef = 0.0f;
};

// Soft-knee limiter on the way out. Amount raises the drive into a tanh knee
// pinned at the ceiling, so it loudens and catches overs in one stage.
inline float limit (float x, float amount, float ceiling = 0.98f) noexcept
{
    if (amount <= 0.0f)
        return x;
    const float driven = x * (1.0f + amount * 3.0f);
    return ceiling * std::tanh (driven / ceiling);
}

// ---------------------------------------------------------------------------
// Analysis, shared by the editor's output display and the tests
// ---------------------------------------------------------------------------

// Dominant frequency of a window, by parabolic interpolation over an
// autocorrelation — steady enough to read the tuning of a kick's tail.
//
// The signal is box-averaged down to a few times maxHz and the window capped
// to a fraction of a second first. At full rate over a long tail the
// correlation is an O(samples x lags) sum that takes a fifth of a second, and
// the editor's render display re-runs this every time a knob moves.
inline double dominantFrequency (const float* data, int numSamples, double sampleRate,
                                 double minHz = 20.0, double maxHz = 400.0)
{
    constexpr double analysisSeconds = 0.4;
    const int factor = juce::jmax (1, (int) (sampleRate / (6.0 * maxHz)));
    const double rate = sampleRate / factor;

    const int available = numSamples / factor;
    const int n = juce::jmin (available, (int) (analysisSeconds * rate));
    if (n < 8)
        return 0.0;

    std::vector<double> signal ((size_t) n, 0.0);
    for (int i = 0; i < n; ++i)
    {
        double sum = 0.0;
        for (int k = 0; k < factor; ++k)
            sum += data[i * factor + k];
        signal[(size_t) i] = sum / factor;
    }

    const int minLag = juce::jmax (2, (int) (rate / maxHz));
    const int maxLag = juce::jmin (n / 2, (int) (rate / minHz));
    if (maxLag <= minLag)
        return 0.0;

    double best = 0.0;
    int bestLag = 0;
    std::vector<double> correlation ((size_t) (maxLag + 2), 0.0);
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double sum = 0.0;
        for (int i = 0; i + lag < n; ++i)
            sum += signal[(size_t) i] * signal[(size_t) (i + lag)];
        correlation[(size_t) lag] = sum;
        if (sum > best)
        {
            best = sum;
            bestLag = lag;
        }
    }
    if (bestLag <= 0)
        return 0.0;

    double lag = bestLag;
    if (bestLag > minLag && bestLag < maxLag)
    {
        const double a = correlation[(size_t) bestLag - 1];
        const double b = correlation[(size_t) bestLag];
        const double c = correlation[(size_t) bestLag + 1];
        const double denominator = a - 2.0 * b + c;
        if (std::abs (denominator) > 1.0e-12)
            lag += 0.5 * (a - c) / denominator;
    }
    return rate / juce::jmax (1.0, lag);
}
} // namespace kickdsp
