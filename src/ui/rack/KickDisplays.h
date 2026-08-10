#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "app/Theme.h"
#include "engine/Drive.h"
#include "engine/KickDsp.h"
#include "model/ChannelParams.h"
#include "model/Ids.h"
#include "model/KickChannel.h"
#include "model/KickEnvelope.h"
#include "ui/rack/SynthDisplays.h"

// The kick editor's pictures. Like the synth's, every one of them is measured
// from the code the engine runs — kickdsp for the layers and the envelopes,
// drive:: for the distortion curve, a real offline render for the output
// waveform and spectrum.
namespace kickdisplays
{
using synthdisplays::paintDisplayFrame;
using synthdisplays::strokeCurve;

// Seconds an envelope role spans, which is what its editor's time axis shows.
inline double envelopeSpanSeconds (const juce::ValueTree& channel, const juce::String& role)
{
    if (role == kickenv::pitchRole)
        return juce::jmax (0.001, (double) channel.getProperty (ids::kickPitchDecay, 0.035));
    return juce::jmax (0.01, (double) channel.getProperty (ids::kickAmpDecay, 0.5));
}

// What a role's envelope is worth that many seconds after it starts — the
// drawn curve if the channel carries one, otherwise the analytic decay the
// voice runs. A drawn curve holds its last value past its span; the analytic
// ones keep falling, exactly as the generator does.
inline float envelopeValueAt (const juce::ValueTree& channel, const juce::String& role,
                              double secondsIn)
{
    const double span = envelopeSpanSeconds (channel, role);
    const double u = juce::jmax (0.0, secondsIn) / span;

    if (const auto drawn = kickenv::read (channel, role); ! drawn.empty())
        return drawn.valueAt ((float) juce::jlimit (0.0, 1.0, u));

    if (role == kickenv::pitchRole)
        return (float) std::exp (-u);   // one time constant per span

    // The amp decay runs five time constants, blended towards linear as the
    // CURVE knob comes down.
    const double envShape = juce::jlimit (0.0, 1.0, (double) channel.getProperty (ids::envShape, 1.0));
    const double linear = juce::jmax (0.0, 1.0 - u);
    const double exponential = std::exp (-5.0 * u);
    return (float) (linear + envShape * (exponential - linear));
}

// The role's shape sampled across its own span, for the small module displays.
inline std::vector<float> effectiveEnvelope (const juce::ValueTree& channel,
                                             const juce::String& role, int points)
{
    const double span = envelopeSpanSeconds (channel, role);
    std::vector<float> out;
    out.reserve ((size_t) points);
    for (int i = 0; i < points; ++i)
        out.push_back (envelopeValueAt (channel, role,
                                        span * i / (double) juce::jmax (1, points - 1)));
    return out;
}

// ---------------------------------------------------------------------------
// A display that repaints when anything about the kick changes — every scalar
// parameter plus both envelopes, so drawn-curve edits refresh it too.
// ---------------------------------------------------------------------------
class KickDisplay : public juce::Component,
                    private juce::Timer
{
public:
    explicit KickDisplay (juce::ValueTree channelTree)
        : channel (std::move (channelTree))
    {
        snapshot (shown);
        startTimerHz (12);
    }

protected:
    double prop (const juce::Identifier& id, double fallback) const
    {
        return (double) channel.getProperty (id, fallback);
    }

    // Called when something moved; the base class repaints straight after.
    virtual void kickChanged() {}

    juce::ValueTree channel;

private:
    void timerCallback() override
    {
        std::vector<double> now;
        snapshot (now);
        if (now != shown)
        {
            shown = std::move (now);
            kickChanged();
            repaint();
        }
    }

    void snapshot (std::vector<double>& into) const
    {
        into.clear();
        for (const auto& descriptor : channelparams::kick())
            into.push_back ((double) channel.getProperty (descriptor.id, descriptor.defaultValue));
        for (const auto* role : { &kickenv::pitchRole, &kickenv::ampRole })
            for (const auto& point : kickenv::read (channel, *role).points)
            {
                into.push_back (point.pos);
                into.push_back (point.value);
                into.push_back (point.tension);
            }
    }

    std::vector<double> shown;
};

// ---------------------------------------------------------------------------
// Module displays
// ---------------------------------------------------------------------------

// One cycle of the body oscillator, with a tick where the start phase sits.
class BodyDisplay : public KickDisplay
{
public:
    using KickDisplay::KickDisplay;

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds();
        paintDisplayFrame (g, area);

        const float shape = (float) prop (ids::kickBodyShape, 0.0);
        const float harm  = (float) prop (ids::kickBodyHarm, 0.0);
        const float level = juce::jlimit (0.0f, 2.0f, (float) prop (ids::kickBodyLevel, 1.0));

        constexpr int points = 192;
        std::vector<float> curve;
        curve.reserve (points);
        for (int i = 0; i < points; ++i)
            curve.push_back (0.5f + 0.45f * juce::jlimit (-1.0f, 1.0f,
                                 kickdsp::body ((double) i / points, shape, harm) * level));
        strokeCurve (g, area, curve, theme::accent);

        const float phase = juce::jlimit (0.0f, 1.0f, (float) prop (ids::kickBodyPhase, 0.0));
        const auto inner = area.reduced (2);
        const float x = (float) inner.getX() + phase * (float) inner.getWidth();
        g.setColour (theme::secondary.withAlpha (0.6f));
        g.drawVerticalLine ((int) x, (float) inner.getY(), (float) inner.getBottom());
    }
};

// The effective amp envelope, drawn or analytic, with the hold plateau ahead
// of it and the punch spike on top — the shape a note is actually given.
class AmpDisplay : public KickDisplay
{
public:
    using KickDisplay::KickDisplay;

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds();
        paintDisplayFrame (g, area);

        const double hold = juce::jmax (0.0, prop (ids::kickHold, 0.0));
        const double decay = juce::jmax (0.01, prop (ids::kickAmpDecay, 0.5));
        const double total = hold + decay;
        const float punch = (float) prop (ids::kickPunch, 0.0);

        constexpr int points = 200;
        const auto shape = effectiveEnvelope (channel, kickenv::ampRole, points);

        std::vector<float> curve;
        curve.reserve (points);
        for (int i = 0; i < points; ++i)
        {
            const double t = (double) i / (points - 1) * total;
            float value = t < hold
                              ? 1.0f
                              : shape[(size_t) juce::jlimit (0, points - 1,
                                    (int) ((t - hold) / decay * (points - 1)))];
            if (punch > 0.0f)
                value *= 1.0f + 2.0f * punch * (float) std::exp (-t / 0.006 * 5.0);
            curve.push_back (juce::jmin (1.0f, value));
        }
        strokeCurve (g, area, curve, theme::accent);

        if (kickenv::isDrawn (channel, kickenv::ampRole))
        {
            g.setColour (theme::secondary.withAlpha (0.7f));
            g.setFont (theme::uiFont (8.5f, true));
            g.drawText ("DRAWN", area.reduced (5, 3), juce::Justification::topRight);
        }
    }
};

// The sub layer's sine under its own decay.
class SubDisplay : public KickDisplay
{
public:
    using KickDisplay::KickDisplay;

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds();
        paintDisplayFrame (g, area);

        const float level = (float) prop (ids::kickSubLevel, 0.0);
        constexpr int points = 220;
        std::vector<float> curve;
        curve.reserve (points);
        for (int i = 0; i < points; ++i)
        {
            const float u = (float) i / (points - 1);
            const float envelope = kickdsp::analyticDecay (u, 5.0f);
            curve.push_back (0.5f + 0.45f * level * envelope
                             * std::sin (u * 6.0f * juce::MathConstants<float>::twoPi));
        }
        strokeCurve (g, area, curve, level > 0.0f ? theme::secondary : theme::textFaint);

        g.setColour (theme::textFaint);
        g.setFont (theme::uiFont (8.5f, true));
        g.drawText (juce::String (prop (ids::kickSubTune, 0.0), 1) + " st",
                    area.reduced (5, 3), juce::Justification::topRight);
    }
};

// The click layer's waveform under its decay, over twice its decay time.
class ClickDisplay : public KickDisplay
{
public:
    using KickDisplay::KickDisplay;

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds();
        paintDisplayFrame (g, area);

        const int type = juce::jlimit (0, kickdsp::numClickTypes - 1,
                                       juce::roundToInt (prop (ids::kickClickType, 0.0)));
        const float level = (float) prop (ids::kickClickLevel, 0.3);
        const double decay = juce::jmax (0.0005, prop (ids::kickClickDecay, 0.004));
        const double freq = juce::jlimit (20.0, 12000.0, prop (ids::kickClickFreq, 1400.0));
        const double window = decay;

        static const char* names[] = { "TICK", "NOISE", "PULSE", "SAMPLE" };
        g.setColour (theme::textFaint);
        g.setFont (theme::uiFont (8.5f, true));
        g.drawText (names[type], area.reduced (5, 3), juce::Justification::topRight);

        if (type == (int) kickdsp::ClickType::sample)
        {
            const auto path = channel[ids::samplePath].toString();
            g.setColour (path.isEmpty() ? theme::textFaint : theme::secondary);
            g.setFont (theme::uiFont (9.5f));
            g.drawText (path.isEmpty() ? "drop a WAV on the CLICK box"
                                       : juce::File (path).getFileName(),
                        area.reduced (6), juce::Justification::centred);
            return;
        }

        // Drawn at full height whatever the level is: this display is about
        // the transient's shape, and the LEVEL knob already reads out its size.
        juce::Random rng { 0x51c7 };
        constexpr int points = 260;
        std::vector<float> curve;
        curve.reserve (points);
        for (int i = 0; i < points; ++i)
        {
            const double t = (double) i / (points - 1) * window;
            const float envelope = (float) std::exp (-5.0 * t / decay);
            const float sample = kickdsp::click (type, t * freq, rng.nextFloat() * 2.0f - 1.0f);
            curve.push_back (0.5f + 0.45f * envelope * sample);
        }
        strokeCurve (g, area, curve, level > 0.0f ? theme::accent : theme::textFaint);
    }
};

// The noise layer's tone control is a one-pole lowpass; this is its response.
class NoiseDisplay : public KickDisplay
{
public:
    using KickDisplay::KickDisplay;

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds();
        paintDisplayFrame (g, area);

        const float tone = juce::jlimit (0.02f, 1.0f, (float) prop (ids::kickNoiseTone, 0.4));
        const float level = (float) prop (ids::kickNoiseLevel, 0.12);

        constexpr int points = 120;
        constexpr double sampleRate = 44100.0;
        std::vector<float> curve;
        curve.reserve (points);
        for (int i = 0; i < points; ++i)
        {
            const double hz = 20.0 * std::pow (1000.0, (double) i / (points - 1));
            const double w = juce::MathConstants<double>::twoPi * hz / sampleRate;
            // y[n] = y[n-1] + tone*(x[n]-y[n-1])  ->  H(z) = tone / (1-(1-tone)z^-1)
            const double re = 1.0 - (1.0 - tone) * std::cos (w);
            const double im = (1.0 - tone) * std::sin (w);
            const double mag = tone / std::sqrt (re * re + im * im);
            curve.push_back (juce::jmap ((float) juce::Decibels::gainToDecibels (mag, -48.0),
                                         -48.0f, 3.0f, 0.0f, 1.0f));
        }
        strokeCurve (g, area, curve, level > 0.0f ? theme::secondary : theme::textFaint);
    }
};

// The waveshaper's transfer curve at the current amount and shape.
class DriveDisplay : public KickDisplay
{
public:
    using KickDisplay::KickDisplay;

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds();
        paintDisplayFrame (g, area);

        const float amount = juce::jlimit (0.0f, 1.0f, (float) prop (ids::drive, 0.25));
        const int curveIndex = juce::jlimit (0, drive::numCurves - 1,
                                             juce::roundToInt (prop (ids::driveCurve, 0.0)));

        constexpr int points = 160;
        std::vector<float> curve;
        curve.reserve (points);
        for (int i = 0; i < points; ++i)
        {
            const float x = -1.0f + 2.0f * (float) i / (points - 1);
            const float y = amount > 0.0f ? drive::process (x, amount, curveIndex) : x;
            curve.push_back (0.5f + 0.45f * juce::jlimit (-1.0f, 1.0f, y));
        }
        strokeCurve (g, area, curve, theme::accent);

        static const char* names[] = { "SOFT", "HARD", "FOLD" };
        g.setColour (theme::textFaint);
        g.setFont (theme::uiFont (8.5f, true));
        g.drawText (names[curveIndex], area.reduced (5, 3), juce::Justification::topRight);
    }
};

// The output EQ's magnitude response, read off the running biquads.
class EqDisplay : public KickDisplay
{
public:
    using KickDisplay::KickDisplay;

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds();
        paintDisplayFrame (g, area);

        kickdsp::ToneEq measured;
        measured.prepare (44100.0);
        measured.setSettings ({ (float) prop (ids::kickEqLowFreq, 90.0),
                                (float) prop (ids::kickEqLowGain, 0.0),
                                (float) prop (ids::kickEqMidFreq, 500.0),
                                (float) prop (ids::kickEqMidGain, 0.0),
                                (float) prop (ids::kickEqHighFreq, 4000.0),
                                (float) prop (ids::kickEqHighGain, 0.0) });

        constexpr int points = 128;
        std::vector<float> curve;
        curve.reserve (points);
        for (int i = 0; i < points; ++i)
        {
            const double hz = 20.0 * std::pow (1000.0, (double) i / (points - 1));
            curve.push_back (juce::jlimit (0.0f, 1.0f,
                juce::jmap ((float) measured.magnitudeDb (hz), -12.0f, 12.0f, 0.0f, 1.0f)));
        }

        g.setColour (theme::outlineLight.withAlpha (0.4f));
        g.drawHorizontalLine (area.getCentreY(), (float) area.getX() + 2,
                              (float) area.getRight() - 2);
        strokeCurve (g, area, curve, theme::accent);
    }
};

// Compressor plus limiter as one static input/output curve, in dB.
class OutDisplay : public KickDisplay
{
public:
    using KickDisplay::KickDisplay;

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds();
        paintDisplayFrame (g, area);

        const float comp = juce::jlimit (0.0f, 1.0f, (float) prop (ids::kickComp, 0.0));
        const float lim  = juce::jlimit (0.0f, 1.0f, (float) prop (ids::kickLimit, 0.0));
        const float out  = juce::Decibels::decibelsToGain ((float) prop (ids::kickOutput, 0.0));

        constexpr int points = 100;
        std::vector<float> curve;
        curve.reserve (points);
        for (int i = 0; i < points; ++i)
        {
            const float inDb = juce::jmap ((float) i / (points - 1), -48.0f, 0.0f);
            const float linear = juce::Decibels::decibelsToGain (inDb);
            // The compressor's own detector is settled here, so its gain is
            // just the static curve at this level.
            kickdsp::Compressor still;
            still.prepare (44100.0);
            float gain = 1.0f;
            for (int n = 0; n < 4; ++n)
                gain = still.gainFor (linear, comp);
            const float shaped = kickdsp::limit (linear * gain, lim) * out;
            curve.push_back (juce::jmap (juce::Decibels::gainToDecibels (shaped, -48.0f),
                                         -48.0f, 6.0f, 0.0f, 1.0f));
        }

        // Unity reference: what a fully bypassed chain would draw.
        std::vector<float> unity;
        unity.reserve (points);
        for (int i = 0; i < points; ++i)
            unity.push_back (juce::jmap (juce::jmap ((float) i / (points - 1), -48.0f, 0.0f),
                                         -48.0f, 6.0f, 0.0f, 1.0f));
        strokeCurve (g, area, unity, theme::outlineLight.withAlpha (0.4f));
        strokeCurve (g, area, curve, theme::accent);
    }
};

// ---------------------------------------------------------------------------
// Output: one hit rendered offline, as a waveform and a spectrum
// ---------------------------------------------------------------------------
class OutputDisplay : public KickDisplay,
                      public juce::SettableTooltipClient
{
public:
    explicit OutputDisplay (juce::ValueTree channelTree)
        : KickDisplay (std::move (channelTree))
    {
        rebuild();
    }

    // The outline is one peak per pixel column, so a resize needs a re-render.
    void resized() override { rebuild(); }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        paintDisplayFrame (g, area);
        auto inner = area.reduced (4);

        auto readout = inner.removeFromBottom (13);
        auto spectrumArea = inner.removeFromBottom (juce::jmax (28, inner.getHeight() / 2));
        auto waveArea = inner;

        // --- waveform ---
        if (! outline.empty())
        {
            const float midY = (float) waveArea.getCentreY();
            const float halfH = (float) waveArea.getHeight() * 0.46f;
            const float step = (float) waveArea.getWidth() / (float) outline.size();
            g.setColour (theme::accent.withAlpha (0.85f));
            for (size_t i = 0; i < outline.size(); ++i)
            {
                const float x = (float) waveArea.getX() + (float) i * step;
                const float h = juce::jmax (0.5f, outline[i] * halfH);
                g.fillRect (x, midY - h, juce::jmax (1.0f, step - 0.4f), h * 2.0f);
            }
            g.setColour (theme::outlineLight.withAlpha (0.35f));
            g.drawHorizontalLine (waveArea.getCentreY(), (float) waveArea.getX(),
                                  (float) waveArea.getRight());
        }

        // --- spectrum ---
        g.setColour (theme::outlineLight.withAlpha (0.25f));
        g.drawHorizontalLine (spectrumArea.getY(), (float) spectrumArea.getX(),
                              (float) spectrumArea.getRight());
        if (! spectrum.empty())
        {
            juce::Path path;
            path.startNewSubPath ((float) spectrumArea.getX(), (float) spectrumArea.getBottom());
            for (size_t i = 0; i < spectrum.size(); ++i)
            {
                const float x = (float) spectrumArea.getX()
                                + (float) i / (float) (spectrum.size() - 1)
                                      * (float) spectrumArea.getWidth();
                const float y = (float) spectrumArea.getBottom()
                                - spectrum[i] * (float) spectrumArea.getHeight();
                path.lineTo (x, y);
            }
            path.lineTo ((float) spectrumArea.getRight(), (float) spectrumArea.getBottom());
            path.closeSubPath();
            g.setColour (theme::secondary.withAlpha (0.30f));
            g.fillPath (path);
            g.setColour (theme::secondary);
            g.strokePath (path, juce::PathStrokeType (1.2f));

            // Decade marks: 100 Hz, 1 kHz, 10 kHz across the log axis.
            g.setColour (theme::textFaint.withAlpha (0.5f));
            g.setFont (theme::uiFont (8.0f));
            for (const double hz : { 100.0, 1000.0, 10000.0 })
            {
                const float u = (float) (std::log (hz / minHz) / std::log (maxHz / minHz));
                const int x = spectrumArea.getX() + juce::roundToInt (u * spectrumArea.getWidth());
                g.drawVerticalLine (x, (float) spectrumArea.getY(), (float) spectrumArea.getBottom());
                g.drawText (hz >= 1000.0 ? juce::String (hz / 1000.0, 0) + "k"
                                         : juce::String (hz, 0),
                            x + 2, spectrumArea.getBottom() - 11, 30, 10,
                            juce::Justification::centredLeft);
            }
        }

        g.setColour (theme::textDim);
        g.setFont (theme::uiFont (9.5f, true));
        g.drawText (summary, readout, juce::Justification::centredLeft);
    }

    // What the readout says, exposed so the editor's header can show the same
    // tuning the display measured.
    juce::String getSummary() const { return summary; }
    double getTunedFrequency() const { return tunedHz; }
    double getPeakDb() const { return peakDb; }

private:
    static constexpr double minHz = 20.0;
    static constexpr double maxHz = 20000.0;
    static constexpr double renderRate = 44100.0;
    static constexpr double maxDisplaySeconds = 3.0;

    void kickChanged() override { rebuild(); }

    void rebuild()
    {
        // A picture does not need the whole tail of a three-second 808, and
        // this re-runs on every knob move.
        const auto rendered = kickchannel::render (channel, renderRate, -1, 1.0f,
                                                   juce::jmin (maxDisplaySeconds,
                                                               kickchannel::lengthSeconds (channel)));
        const int numSamples = rendered.getNumSamples();
        if (numSamples <= 0)
            return;

        const float* data = rendered.getReadPointer (0);

        // Peak-per-bucket outline.
        const int buckets = juce::jlimit (48, 480, juce::jmax (48, getWidth()));
        outline.assign ((size_t) buckets, 0.0f);
        peakDb = -99.0;
        for (int b = 0; b < buckets; ++b)
        {
            const int start = (int) ((juce::int64) b * numSamples / buckets);
            const int end = juce::jmax (start + 1, (int) ((juce::int64) (b + 1) * numSamples / buckets));
            float peak = 0.0f;
            for (int i = start; i < juce::jmin (end, numSamples); ++i)
                peak = juce::jmax (peak, std::abs (data[i]));
            outline[(size_t) b] = juce::jmin (1.0f, peak);
        }
        peakDb = juce::Decibels::gainToDecibels (rendered.getMagnitude (0, numSamples), -99.0f);

        buildSpectrum (data, numSamples);

        // Tuning: read the tail, once the sweep has settled into the body note.
        const int tailStart = juce::jmin (numSamples - 1, (int) (renderRate * 0.06));
        tunedHz = kickdsp::dominantFrequency (data + tailStart, numSamples - tailStart, renderRate);

        summary = juce::String (peakDb, 1) + " dBFS   "
                  + (tunedHz > 0.0 ? juce::String (tunedHz, 1) + " Hz  "
                                         + juce::MidiMessage::getMidiNoteName (
                                               noteForFrequency (tunedHz), true, true, 4)
                                   : juce::String ("--"));
    }

    static int noteForFrequency (double hz)
    {
        return juce::jlimit (0, 127,
                             juce::roundToInt (69.0 + 12.0 * std::log2 (juce::jmax (1.0, hz) / 440.0)));
    }

    void buildSpectrum (const float* data, int numSamples)
    {
        constexpr int order = 12;
        constexpr int size = 1 << order;
        juce::dsp::FFT fft (order);

        std::vector<float> block ((size_t) size * 2, 0.0f);
        const int taken = juce::jmin (size, numSamples);
        for (int i = 0; i < taken; ++i)
        {
            const float window = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                         * (float) i / (float) (size - 1));
            block[(size_t) i] = data[i] * window;
        }
        fft.performFrequencyOnlyForwardTransform (block.data());

        constexpr int bins = 160;
        spectrum.assign ((size_t) bins, 0.0f);
        for (int b = 0; b < bins; ++b)
        {
            const double hz = minHz * std::pow (maxHz / minHz, (double) b / (bins - 1));
            const int bin = juce::jlimit (1, size / 2 - 1,
                                          (int) (hz / renderRate * size));
            const float db = juce::Decibels::gainToDecibels (block[(size_t) bin] / (float) (size / 4),
                                                             -90.0f);
            spectrum[(size_t) b] = juce::jlimit (0.0f, 1.0f, juce::jmap (db, -72.0f, 6.0f, 0.0f, 1.0f));
        }
    }

    std::vector<float> outline, spectrum;
    juce::String summary;
    double tunedHz = 0.0, peakDb = -99.0;
};
} // namespace kickdisplays
