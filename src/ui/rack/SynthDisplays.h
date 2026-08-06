#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "app/Theme.h"
#include "engine/SynthOsc.h"
#include "model/Ids.h"

// The synth editor's module displays. Everything drawn here is measured from
// the same DSP the engine runs — a real StateVariableTPTFilter for the
// response curve, a real juce::ADSR for the envelopes, synthosc for the
// wave — so the pictures are the sound, not an artist's impression.
namespace synthdisplays
{
// Magnitude (dB) of the synth's filter at the given frequencies, measured by
// pushing an impulse through the identical filter configuration.
inline std::vector<float> filterResponseDb (int type, float cutoffHz, float resonance,
                                            const std::vector<float>& freqsHz,
                                            double sampleRate = 44100.0)
{
    juce::dsp::StateVariableTPTFilter<float> filter;
    filter.prepare ({ sampleRate, 8192, 1 });
    filter.setType (type == 1 ? juce::dsp::StateVariableTPTFilterType::bandpass
                  : type == 2 ? juce::dsp::StateVariableTPTFilterType::highpass
                              : juce::dsp::StateVariableTPTFilterType::lowpass);
    filter.setCutoffFrequency (juce::jlimit (40.0f, 18000.0f, cutoffHz));
    filter.setResonance (synthosc::resonanceToQ (juce::jlimit (0.0f, 1.0f, resonance)));

    constexpr int n = 8192;
    std::vector<float> impulseResponse ((size_t) n);
    for (int i = 0; i < n; ++i)
        impulseResponse[(size_t) i] = filter.processSample (0, i == 0 ? 1.0f : 0.0f);

    std::vector<float> out;
    out.reserve (freqsHz.size());
    for (const float hz : freqsHz)
    {
        // Goertzel over the impulse response = the transfer magnitude there.
        const double w = 2.0 * juce::MathConstants<double>::pi * hz / sampleRate;
        const double coeff = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double s0 = impulseResponse[(size_t) i] + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const double mag = std::sqrt (juce::jmax (0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2));
        out.push_back (juce::Decibels::gainToDecibels ((float) mag, -60.0f));
    }
    return out;
}

// The envelope a note actually gets: a real juce::ADSR run through attack and
// decay, a sustain hold, then release, resampled to `points` values 0..1.
inline std::vector<float> envelopeShape (float attack, float decay, float sustain,
                                         float release, int points)
{
    const double attackDecay = juce::jmax (0.001, (double) attack + decay);
    const double hold = juce::jmax (0.05, 0.35 * (attackDecay + release));
    const double total = attackDecay + hold + (double) release + 1.0e-3;
    const double rate = (double) points / total;

    juce::ADSR envelope;
    envelope.setSampleRate (rate);
    envelope.setParameters ({ attack, decay, sustain, release });
    envelope.noteOn();

    const int releaseAt = (int) ((attackDecay + hold) * rate);
    std::vector<float> out;
    out.reserve ((size_t) points);
    for (int i = 0; i < points; ++i)
    {
        if (i == releaseAt)
            envelope.noteOff();
        out.push_back (envelope.getNextSample());
    }
    return out;
}

// ---------------------------------------------------------------------------

// Shared look for the module displays.
inline void paintDisplayFrame (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (theme::sunken);
    g.fillRoundedRectangle (area.toFloat(), 3.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (area.toFloat(), 3.0f, 1.0f);
}

inline void strokeCurve (juce::Graphics& g, juce::Rectangle<int> area,
                         const std::vector<float>& normalised, juce::Colour colour)
{
    if (normalised.size() < 2)
        return;
    juce::Path path;
    const auto inner = area.reduced (2);
    for (size_t i = 0; i < normalised.size(); ++i)
    {
        const float x = (float) inner.getX()
                        + (float) i / (float) (normalised.size() - 1) * (float) inner.getWidth();
        const float y = (float) inner.getBottom()
                        - juce::jlimit (0.0f, 1.0f, normalised[i]) * (float) inner.getHeight();
        if (i == 0)
            path.startNewSubPath (x, y);
        else
            path.lineTo (x, y);
    }
    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (1.6f));
}

// Base for the tree-watching displays: polls the properties it draws from and
// repaints when any change (knobs, automation, undo — all reach the tree).
class TreeDisplay : public juce::Component,
                    private juce::Timer
{
public:
    TreeDisplay (juce::ValueTree channelTree, std::vector<juce::Identifier> watchedIds)
        : channel (std::move (channelTree)), watched (std::move (watchedIds))
    {
        snapshotValues (shown);
        startTimerHz (15);
    }

protected:
    double prop (const juce::Identifier& id, double fallback) const
    {
        return (double) channel.getProperty (id, fallback);
    }

    juce::ValueTree channel;

private:
    void timerCallback() override
    {
        std::vector<double> now;
        snapshotValues (now);
        if (now != shown)
        {
            shown = std::move (now);
            repaint();
        }
    }

    void snapshotValues (std::vector<double>& into) const
    {
        into.clear();
        for (const auto& id : watched)
            into.push_back ((double) channel.getProperty (id, 0.0));
    }

    std::vector<juce::Identifier> watched;
    std::vector<double> shown;
};

// One cycle of oscillator 1 (morph + warp).
class OscDisplay : public TreeDisplay
{
public:
    explicit OscDisplay (juce::ValueTree channelTree)
        : TreeDisplay (std::move (channelTree), { ids::oscShape, ids::oscWarp }) {}

    void paint (juce::Graphics& g) override
    {
        paintDisplayFrame (g, getLocalBounds());
        const float morph = (float) prop (ids::oscShape, 0.0);
        const float warp  = (float) prop (ids::oscWarp, 0.0);

        constexpr int points = 192;
        std::vector<float> curve;
        curve.reserve (points);
        for (int i = 0; i < points; ++i)
            curve.push_back (0.5f + 0.45f * synthosc::sample (morph, warp,
                                                              (double) i / points, 1.0 / points));
        strokeCurve (g, getLocalBounds(), curve, theme::accent);
    }
};

// Filter magnitude response, log frequency, -60..+18 dB.
class FilterResponseDisplay : public TreeDisplay
{
public:
    explicit FilterResponseDisplay (juce::ValueTree channelTree)
        : TreeDisplay (std::move (channelTree), { ids::filterType, ids::cutoff, ids::resonance }) {}

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds();
        paintDisplayFrame (g, area);

        constexpr int points = 96;
        std::vector<float> freqs;
        freqs.reserve (points);
        for (int i = 0; i < points; ++i)
            freqs.push_back (40.0f * std::pow (18000.0f / 40.0f, (float) i / (points - 1)));

        const auto db = filterResponseDb (juce::roundToInt (prop (ids::filterType, 0.0)),
                                          (float) prop (ids::cutoff, 4000.0),
                                          (float) prop (ids::resonance, 0.3), freqs);
        std::vector<float> curve;
        curve.reserve (db.size());
        for (const float d : db)
            curve.push_back (juce::jmap (d, -60.0f, 18.0f, 0.0f, 1.0f));

        // 0 dB reference line.
        g.setColour (theme::outlineLight.withAlpha (0.4f));
        const int zeroY = area.getBottom() - 2
                          - juce::roundToInt (juce::jmap (0.0f, -60.0f, 18.0f, 0.0f, 1.0f)
                                              * (float) (area.getHeight() - 4));
        g.drawHorizontalLine (zeroY, (float) area.getX() + 2, (float) area.getRight() - 2);

        strokeCurve (g, area, curve, theme::accent);
    }
};

// ADSR shape as a note plays it.
class EnvelopeDisplay : public TreeDisplay
{
public:
    EnvelopeDisplay (juce::ValueTree channelTree,
                     juce::Identifier attackId, juce::Identifier decayId,
                     juce::Identifier sustainId, juce::Identifier releaseId,
                     juce::Colour curveColour, double defaultSustain)
        : TreeDisplay (std::move (channelTree), { attackId, decayId, sustainId, releaseId }),
          aId (attackId), dId (decayId), sId (sustainId), rId (releaseId),
          colour (curveColour), sustainDefault (defaultSustain) {}

    void paint (juce::Graphics& g) override
    {
        paintDisplayFrame (g, getLocalBounds());
        const auto curve = envelopeShape ((float) prop (aId, 0.004), (float) prop (dId, 0.25),
                                          (float) prop (sId, sustainDefault),
                                          (float) prop (rId, 0.08), 160);
        strokeCurve (g, getLocalBounds(), curve, colour);
    }

private:
    juce::Identifier aId, dId, sId, rId;
    juce::Colour colour;
    double sustainDefault;
};

// Two cycles of the LFO at its current depth, plus the destination readout.
class LfoDisplay : public TreeDisplay
{
public:
    explicit LfoDisplay (juce::ValueTree channelTree)
        : TreeDisplay (std::move (channelTree), { ids::lfoAmount, ids::lfoTarget }) {}

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds();
        paintDisplayFrame (g, area);

        const float amount = (float) prop (ids::lfoAmount, 0.0);
        constexpr int points = 160;
        std::vector<float> curve;
        curve.reserve (points);
        for (int i = 0; i < points; ++i)
            curve.push_back (0.5f + 0.45f * amount
                             * (float) std::sin (2.0 * juce::MathConstants<double>::twoPi
                                                 * i / (points - 1)));
        strokeCurve (g, area, curve, theme::secondary);

        static const char* destNames[] = { "CUT", "PITCH", "WARP", "PAN" };
        const int dest = juce::jlimit (0, 3, juce::roundToInt (prop (ids::lfoTarget, 0.0)));
        g.setColour (theme::textFaint);
        g.setFont (theme::uiFont (9.0f, true));
        g.drawText (destNames[dest], area.reduced (6, 3), juce::Justification::topRight);
    }
};
} // namespace synthdisplays
