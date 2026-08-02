#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "app/Theme.h"
#include "effects/CompressorEffect.h"
#include "effects/EqEffect.h"
#include "effects/FilterEffect.h"
#include "model/Ids.h"

// Displays for the built-in effect editors. They ask the *effects* for their
// response (EqEffect/FilterEffect::magnitudeAt, CompressorEffect::
// gainReductionDbFor), so the plots can't drift from the DSP.

// Log-frequency magnitude curve, 20 Hz..20 kHz over ±24 dB. An offline
// instance of the effect is fed the slot's values; the slot listener keeps it
// in sync while knobs move.
class ResponseCurveDisplay : public juce::Component,
                             private juce::ValueTree::Listener
{
public:
    ResponseCurveDisplay (juce::ValueTree slot, const std::vector<fx::ParamSpec>& specList,
                          std::unique_ptr<BuiltinEffect> analysisEffect,
                          std::function<double (BuiltinEffect&, double)> magnitudeFn)
        : slotTree (slot), specs (specList),
          analysis (std::move (analysisEffect)), magnitude (std::move (magnitudeFn))
    {
        analysis->prepare (44100.0, 512);
        syncFromSlot();
        slotTree.addListener (this);
    }

    ~ResponseCurveDisplay() override { slotTree.removeListener (this); }

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (theme::sunken);
        g.fillRoundedRectangle (area, 3.0f);

        // Grid: decades and octave-ish lines, 0/±12 dB.
        g.setColour (theme::outlineLight.withAlpha (0.25f));
        for (const double f : { 50.0, 100.0, 500.0, 1000.0, 5000.0, 10000.0 })
        {
            const float x = xForFreq (f, area);
            g.drawVerticalLine ((int) x, area.getY() + 2, area.getBottom() - 2);
        }
        for (const double db : { -12.0, 12.0 })
            g.drawHorizontalLine ((int) yForDb (db, area), area.getX() + 2, area.getRight() - 2);
        g.setColour (theme::outlineLight.withAlpha (0.5f));
        g.drawHorizontalLine ((int) yForDb (0.0, area), area.getX() + 2, area.getRight() - 2);

        juce::Path curve;
        const int steps = juce::jmax (32, getWidth() / 2);
        for (int i = 0; i <= steps; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, i / (double) steps);   // 20..20k
            const double db = juce::Decibels::gainToDecibels (
                juce::jmax (1.0e-6, magnitude (*analysis, f)));
            const float x = xForFreq (f, area);
            const float y = yForDb (juce::jlimit (-30.0, 30.0, db), area);
            if (i == 0) curve.startNewSubPath (x, y);
            else        curve.lineTo (x, y);
        }
        g.setColour (theme::accent);
        g.strokePath (curve, juce::PathStrokeType (1.6f));
    }

private:
    static float xForFreq (double f, juce::Rectangle<float> area)
    {
        const double t = std::log (f / 20.0) / std::log (1000.0);   // 0..1 over 20..20k
        return area.getX() + (float) t * area.getWidth();
    }

    static float yForDb (double db, juce::Rectangle<float> area)
    {
        return area.getCentreY() - (float) (db / 24.0) * area.getHeight() * 0.5f;
    }

    void syncFromSlot()
    {
        for (const auto& spec : specs)
            analysis->setParameter (spec.id,
                                    (double) slotTree.getProperty (spec.id, spec.defaultValue));
    }

    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&) override
    {
        if (tree == slotTree)
        {
            syncFromSlot();
            repaint();
        }
    }

    juce::ValueTree slotTree;
    const std::vector<fx::ParamSpec>& specs;
    std::unique_ptr<BuiltinEffect> analysis;
    std::function<double (BuiltinEffect&, double)> magnitude;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResponseCurveDisplay)
};

// Compressor: the static transfer curve (in → out, dB) from the slot's
// threshold/ratio/knee/makeup, with the live instance's gain reduction shown
// as a moving dot on the curve and a meter bar on the right.
class CompressorDisplay : public juce::Component,
                          private juce::ValueTree::Listener,
                          private juce::Timer
{
public:
    CompressorDisplay (juce::ValueTree slot, std::shared_ptr<BuiltinEffect> liveInstance)
        : slotTree (slot),
          live (std::dynamic_pointer_cast<CompressorEffect> (liveInstance))
    {
        slotTree.addListener (this);
        if (live != nullptr)
            startTimerHz (30);
    }

    ~CompressorDisplay() override { slotTree.removeListener (this); }

    void paint (juce::Graphics& g) override
    {
        auto full = getLocalBounds().toFloat().reduced (1.0f);
        auto meter = full.removeFromRight (18.0f);
        full.removeFromRight (4.0f);
        const auto area = full;

        g.setColour (theme::sunken);
        g.fillRoundedRectangle (area, 3.0f);
        g.fillRoundedRectangle (meter, 3.0f);

        const auto threshold = (float) (double) slotTree.getProperty (ids::fxThreshold, -12.0);
        const auto ratio     = (float) (double) slotTree.getProperty (ids::fxRatio, 2.5);
        const auto knee      = (float) (double) slotTree.getProperty (ids::fxKnee, 6.0);
        const auto makeup    = (float) (double) slotTree.getProperty (ids::fxMakeup, 2.5);

        // Unity diagonal + threshold mark.
        g.setColour (theme::outlineLight.withAlpha (0.3f));
        g.drawLine (area.getX(), area.getBottom(), area.getRight(), area.getY(), 1.0f);
        const float tx = xForDb (threshold, area);
        g.drawVerticalLine ((int) tx, area.getY() + 2, area.getBottom() - 2);

        // Transfer curve through the real per-sample function.
        juce::Path curve;
        const int steps = juce::jmax (32, (int) area.getWidth() / 2);
        for (int i = 0; i <= steps; ++i)
        {
            const float inDb = rangeDb * ((float) i / (float) steps) - rangeDb;   // -60..0
            const float outDb = inDb + makeup
                              - CompressorEffect::gainReductionDbFor (inDb, threshold, ratio, knee);
            const float x = xForDb (inDb, area);
            const float y = yForDb (outDb, area);
            if (i == 0) curve.startNewSubPath (x, y);
            else        curve.lineTo (x, y);
        }
        g.setColour (theme::accent);
        g.strokePath (curve, juce::PathStrokeType (1.6f));

        // Live gain reduction: meter bar (0..24 dB downwards) and label.
        if (live != nullptr)
        {
            const float gr = live->getGainReductionDb();
            const float norm = juce::jlimit (0.0f, 1.0f, gr / 24.0f);
            auto lit = meter.reduced (2.0f);
            lit = lit.removeFromTop (lit.getHeight() * norm);
            g.setColour (theme::record.withAlpha (0.9f));
            g.fillRect (lit);

            g.setColour (theme::textDim);
            g.setFont (theme::uiFont (9.0f));
            g.drawText (juce::String (gr, 1), area.reduced (4.0f, 2.0f),
                        juce::Justification::topRight);
        }
    }

private:
    static constexpr float rangeDb = 60.0f;

    static float xForDb (float db, juce::Rectangle<float> area)
    {
        return area.getX() + (db + rangeDb) / rangeDb * area.getWidth();
    }

    static float yForDb (float db, juce::Rectangle<float> area)
    {
        return area.getY() + (-db / rangeDb) * area.getHeight();
    }

    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&) override
    {
        if (tree == slotTree)
            repaint();
    }

    void timerCallback() override
    {
        // Only the meter moves at rest; repainting the strip is cheap.
        repaint();
    }

    juce::ValueTree slotTree;
    std::shared_ptr<CompressorEffect> live;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompressorDisplay)
};
