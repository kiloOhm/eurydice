#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "app/Theme.h"
#include "effects/RetroEffect.h"
#include "model/Ids.h"
#include "model/ProjectModel.h"
#include "model/UndoGesture.h"

// The Retro rack: one row per module — an LED that switches it in and a slider
// that sets how much of it there is — over the Magnitude slider that scales
// every one of those amounts at once. The rows sit in the same order as the
// signal chain and as the knob groups underneath, so the editor reads top to
// bottom in the order the audio is degraded.
//
// The rack edits the same slot properties the generic grid would have drawn
// (they are marked drawnByDisplay(), so the grid leaves them alone), which is
// what keeps every drag undoable and automatable without a second code path.
class RetroRackDisplay : public juce::Component,
                         private juce::ValueTree::Listener,
                         private juce::Timer
{
public:
    RetroRackDisplay (ProjectModel& projectModel, juce::ValueTree slot,
                      std::shared_ptr<BuiltinEffect> liveInstance)
        : model (projectModel), slotTree (slot),
          live (std::dynamic_pointer_cast<RetroEffect> (liveInstance))
    {
        slotTree.addListener (this);
        if (live != nullptr)
            startTimerHz (30);
    }

    ~RetroRackDisplay() override { slotTree.removeListener (this); }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::sunken);
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 3.0f);

        for (int m = 0; m < RetroEffect::numModules; ++m)
            paintModule (g, m);

        paintMagnitude (g);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int module = moduleAt (e.getPosition());

        if (module >= 0 && ledBounds (module).expanded (3.0f).contains (e.position))
        {
            const auto& info = RetroEffect::modules()[(size_t) module];
            const undoGesture::Scoped step (model, "Toggle " + info.name);
            slotTree.setProperty (*info.enableId, stored (*info.enableId, 0.0) >= 0.5 ? 0.0 : 1.0,
                                  &model.getUndoManager());
            return;
        }

        if (magnitudeSlider().contains (e.position))
        {
            dragging = &ids::fxMagnitude;
            undoGesture::begin (model, "Retro magnitude");
            dragTo (e.position);
            return;
        }

        if (module >= 0 && sliderBounds (module).contains (e.position))
        {
            const auto& info = RetroEffect::modules()[(size_t) module];
            dragging = info.amountId;
            dragBar = sliderBounds (module);
            undoGesture::begin (model, info.name + " amount");
            dragTo (e.position);
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging != nullptr)
            dragTo (e.position);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging != nullptr)
            undoGesture::end (model);
        dragging = nullptr;
    }

    // A double click on an amount is the fastest way back to "none of this".
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int module = moduleAt (e.getPosition());
        if (module < 0 || ! sliderBounds (module).contains (e.position))
            return;

        const auto& info = RetroEffect::modules()[(size_t) module];
        const undoGesture::Scoped step (model, info.name + " amount");
        slotTree.setProperty (*info.amountId, 0.0, &model.getUndoManager());
    }

private:
    static constexpr float rowHeight = 22.0f;
    static constexpr float labelWidth = 54.0f;
    static constexpr float readoutWidth = 34.0f;
    static constexpr float magnitudeHeight = 30.0f;

    // Each module keeps one colour across its row, its LED and its meter. All
    // six come off the channel palette rather than the greys, so a lit LED can
    // never be mistaken for a dark one.
    static juce::Colour colourFor (int module)
    {
        static const int palette[RetroEffect::numModules] { 4, 1, 3, 5, 6, 0 };
        const int index = palette[juce::jlimit (0, RetroEffect::numModules - 1, module)];
        return theme::channelPalette[(size_t) index].colour;
    }

    juce::Rectangle<float> rackArea() const
    {
        return getLocalBounds().toFloat().reduced (7.0f, 6.0f);
    }

    juce::Rectangle<float> rowBounds (int module) const
    {
        auto area = rackArea();
        return { area.getX(), area.getY() + (float) module * rowHeight, area.getWidth(), rowHeight };
    }

    juce::Rectangle<float> ledBounds (int module) const
    {
        return rowBounds (module).withWidth (10.0f).withSizeKeepingCentre (10.0f, 10.0f);
    }

    juce::Rectangle<float> sliderBounds (int module) const
    {
        auto row = rowBounds (module).reduced (0.0f, 5.0f);
        row.removeFromLeft (labelWidth + 14.0f);
        row.removeFromRight (readoutWidth);
        return row;
    }

    juce::Rectangle<float> magnitudeSlider() const
    {
        auto area = rackArea();
        auto strip = area.removeFromBottom (magnitudeHeight).reduced (0.0f, 6.0f);
        strip.removeFromLeft (labelWidth + 14.0f);
        strip.removeFromRight (readoutWidth);
        return strip;
    }

    int moduleAt (juce::Point<int> position) const
    {
        for (int m = 0; m < RetroEffect::numModules; ++m)
            if (rowBounds (m).contains (position.toFloat()))
                return m;
        return -1;
    }

    double stored (const juce::Identifier& id, double fallback) const
    {
        return (double) slotTree.getProperty (id, fallback);
    }

    void dragTo (juce::Point<float> position)
    {
        if (dragging == nullptr)
            return;

        const auto bar = dragging == &ids::fxMagnitude ? magnitudeSlider() : dragBar;
        const float value = juce::jlimit (0.0f, 1.0f,
                                          (position.x - bar.getX()) / juce::jmax (1.0f, bar.getWidth()));
        slotTree.setProperty (*dragging, (double) value, &model.getUndoManager());
    }

    // A filled bar with the value written beside it: the same read as a mixer
    // fader, which is what these amounts are.
    static void paintBar (juce::Graphics& g, juce::Rectangle<float> bar, float value,
                          juce::Colour colour, bool enabled)
    {
        g.setColour (theme::outlineLight.withAlpha (0.25f));
        g.fillRoundedRectangle (bar, 2.0f);

        if (value > 0.0f)
        {
            auto filled = bar.withWidth (juce::jmax (2.0f, bar.getWidth() * value));
            g.setColour (enabled ? colour.withAlpha (0.85f) : theme::textFaint.withAlpha (0.4f));
            g.fillRoundedRectangle (filled, 2.0f);
        }
    }

    void paintModule (juce::Graphics& g, int module) const
    {
        const auto& info = RetroEffect::modules()[(size_t) module];
        const bool on = stored (*info.enableId, 0.0) >= 0.5;
        const auto amount = (float) stored (*info.amountId, 0.0);
        const auto colour = colourFor (module);

        const auto led = ledBounds (module);
        g.setColour (on ? colour : theme::ledOff);
        g.fillEllipse (led);
        if (on)
        {
            g.setColour (colour.withAlpha (0.3f));
            g.drawEllipse (led.expanded (2.0f), 1.0f);
        }

        auto row = rowBounds (module);
        row.removeFromLeft (14.0f);
        g.setColour (on ? theme::textPrimary : theme::textFaint);
        g.setFont (theme::uiFont (10.0f, on));
        g.drawText (info.name, row.removeFromLeft (labelWidth), juce::Justification::centredLeft);

        const auto bar = sliderBounds (module);
        paintBar (g, bar, amount, colour, on);
        paintActivity (g, bar, module, on);

        g.setColour (on ? theme::textDim : theme::textFaint);
        g.setFont (theme::uiFont (9.0f));
        g.drawText (juce::String (juce::roundToInt (amount * 100.0f)),
                    rowBounds (module).removeFromRight (readoutWidth),
                    juce::Justification::centredRight);
    }

    // What the module is doing right now, drawn inside its own bar: the wobble's
    // speed offset as a needle, the drops' current dip and the noise floor as a
    // brighter overlay. Only the three modules that have something to show.
    void paintActivity (juce::Graphics& g, juce::Rectangle<float> bar, int module, bool on) const
    {
        if (live == nullptr || ! on)
            return;

        if (module == (int) RetroEffect::Module::wobble)
        {
            const float offset = juce::jlimit (-1.0f, 1.0f, live->getWobbleOffset());
            const float x = bar.getCentreX() + offset * bar.getWidth() * 0.5f;
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.fillRect (x - 0.75f, bar.getY(), 1.5f, bar.getHeight());
        }
        else if (module == (int) RetroEffect::Module::drops)
        {
            const float depth = juce::jlimit (0.0f, 1.0f, live->getDropDepth());
            if (depth > 0.01f)
            {
                g.setColour (juce::Colours::white.withAlpha (0.55f));
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * depth), 2.0f);
            }
        }
        else if (module == (int) RetroEffect::Module::noise)
        {
            const float level = juce::jlimit (0.0f, 1.0f, live->getNoiseLevel() * 4.0f);
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.fillRoundedRectangle (bar.withWidth (juce::jmax (1.0f, bar.getWidth() * level)), 2.0f);
        }
    }

    void paintMagnitude (juce::Graphics& g) const
    {
        auto area = rackArea();
        auto strip = area.removeFromBottom (magnitudeHeight);

        g.setColour (theme::outlineLight.withAlpha (0.4f));
        g.drawHorizontalLine ((int) strip.getY(), strip.getX(), strip.getRight());

        const auto value = (float) stored (ids::fxMagnitude, 1.0);
        auto label = strip.reduced (0.0f, 4.0f);
        g.setColour (theme::textPrimary);
        g.setFont (theme::uiFont (9.5f, true));
        g.drawText ("MAGNITUDE", label.removeFromLeft (labelWidth + 14.0f),
                    juce::Justification::centredLeft);

        const auto bar = magnitudeSlider();
        paintBar (g, bar, value, theme::accent, true);

        g.setColour (theme::textDim);
        g.setFont (theme::uiFont (9.0f));
        g.drawText (juce::String (juce::roundToInt (value * 100.0f)),
                    strip.removeFromRight (readoutWidth), juce::Justification::centredRight);
    }

    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&) override
    {
        if (tree == slotTree)
            repaint();
    }

    void timerCallback() override { repaint(); }

    ProjectModel& model;
    juce::ValueTree slotTree;
    std::shared_ptr<RetroEffect> live;

    const juce::Identifier* dragging = nullptr;
    juce::Rectangle<float> dragBar;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RetroRackDisplay)
};
