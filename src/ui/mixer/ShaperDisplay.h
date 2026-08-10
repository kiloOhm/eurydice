#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "app/Theme.h"
#include "effects/ShaperEffect.h"
#include "model/Ids.h"
#include "model/ProjectModel.h"
#include "model/UndoGesture.h"

// The shaper's wave, drawn and edited in place. Gestures match the automation
// editor — drag a point to move it, double-click to add one, right-click to
// delete, drag a segment vertically to bend it — because it is the same curve
// law underneath, so a bend means the same thing in both places.
//
// Edits go onto the SLOT tree as the wave string, which is what carries them
// through undo and on to the audio thread; the playhead comes back the other
// way from the live instance, so the dot rides the sample the DSP is on.
class ShaperDisplay : public juce::Component,
                      private juce::ValueTree::Listener,
                      private juce::Timer
{
public:
    ShaperDisplay (ProjectModel& projectModel, juce::ValueTree slot,
                   std::shared_ptr<BuiltinEffect> liveInstance)
        : model (projectModel), slotTree (slot),
          live (std::dynamic_pointer_cast<ShaperEffect> (liveInstance))
    {
        slotTree.addListener (this);
        if (live != nullptr)
            startTimerHz (30);
    }

    ~ShaperDisplay() override { slotTree.removeListener (this); }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::sunken);
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 3.0f);

        const auto area = plotArea();
        const int target = storedInt (ids::fxTarget, 0);
        const float neutral = ShaperEffect::neutralValue (target);
        const auto wave = currentWave();

        drawGrid (g, area, neutral);

        // The curve, filled back to the neutral line so the shape reads as how
        // far from "leave it alone" it pushes, in whichever direction.
        juce::Path curve;
        const int resolution = juce::jmax (96, (int) area.getWidth());
        for (int i = 0; i <= resolution; ++i)
        {
            const float phase = (float) i / (float) resolution;
            const float x = area.getX() + phase * area.getWidth();
            const float y = yForValue (wave.valueAt (phase), area);
            if (i == 0) curve.startNewSubPath (x, y);
            else        curve.lineTo (x, y);
        }

        juce::Path fill (curve);
        fill.lineTo (area.getRight(), yForValue (neutral, area));
        fill.lineTo (area.getX(), yForValue (neutral, area));
        fill.closeSubPath();
        g.setColour (theme::accent.withAlpha (0.16f));
        g.fillPath (fill);

        g.setColour (theme::accent);
        g.strokePath (curve, juce::PathStrokeType (1.8f));

        for (int i = 0; i < wave.numPoints; ++i)
        {
            const auto& point = wave.points[(size_t) i];
            const float x = area.getX() + point.x * area.getWidth();
            const float y = yForValue (point.y, area);
            const bool lit = i == dragIndex || i == hoverIndex;
            g.setColour (lit ? juce::Colours::white : theme::secondary);
            g.fillEllipse (x - 3.5f, y - 3.5f, 7.0f, 7.0f);
        }

        drawPlayhead (g, area);
        drawLabels (g, target);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int under = pointIndexAt (e.getPosition());
        if (under != hoverIndex)
        {
            hoverIndex = under;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hoverIndex >= 0)
        {
            hoverIndex = -1;
            repaint();
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto wave = currentWave();
        const int index = pointIndexAt (e.getPosition());

        if (e.mods.isPopupMenu())
        {
            if (index >= 0 && wave.numPoints > 2)
            {
                const undoGesture::Scoped step (model, "Delete shaper point");
                wave.removePoint (index);
                writeWave (wave);
            }
            return;
        }

        if (e.getNumberOfClicks() == 2 && index < 0)
        {
            undoGesture::begin (model, "Add shaper point");
            dragIndex = wave.addPoint (snapX (positionToPhase (e.position.x, plotArea()), e.mods),
                                       valueAt (e.position.y, plotArea()));
            if (dragIndex >= 0)
                writeWave (wave);
            else
                undoGesture::end (model);   // the wave is full
            return;
        }

        if (index >= 0)
        {
            dragIndex = index;
            undoGesture::begin (model, "Move shaper point");
            return;
        }

        // Not on a point: a vertical drag here bends the segment under the mouse.
        tensionIndex = segmentIndexAt (e.position.x, wave);
        if (tensionIndex >= 0)
        {
            tensionStart = wave.points[(size_t) tensionIndex].tension;
            tensionStartY = e.position.y;
            undoGesture::begin (model, "Bend shaper curve");
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto area = plotArea();
        auto wave = currentWave();

        if (dragIndex >= 0 && dragIndex < wave.numPoints)
        {
            auto& point = wave.points[(size_t) dragIndex];
            // Clamped between its neighbours, so the wave stays sorted and the
            // point being dragged keeps its index for the rest of the gesture.
            const float lowest = dragIndex > 0 ? wave.points[(size_t) dragIndex - 1].x : 0.0f;
            const float highest = dragIndex + 1 < wave.numPoints
                                    ? wave.points[(size_t) dragIndex + 1].x : 1.0f;
            point.x = juce::jlimit (lowest, highest,
                                    snapX (positionToPhase (e.position.x, area), e.mods));
            point.y = valueAt (e.position.y, area);
            writeWave (wave);
        }
        else if (tensionIndex >= 0 && tensionIndex < wave.numPoints)
        {
            const float delta = (tensionStartY - e.position.y) / 80.0f;
            wave.points[(size_t) tensionIndex].tension = juce::jlimit (-1.0f, 1.0f, tensionStart + delta);
            writeWave (wave);
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragIndex >= 0 || tensionIndex >= 0)
            undoGesture::end (model);
        dragIndex = -1;
        tensionIndex = -1;
        repaint();
    }

private:
    juce::Rectangle<float> plotArea() const
    {
        return getLocalBounds().toFloat().reduced (8.0f, 14.0f);
    }

    static float yForValue (float value, juce::Rectangle<float> area)
    {
        return area.getBottom() - juce::jlimit (0.0f, 1.0f, value) * area.getHeight();
    }

    static float valueAt (float y, juce::Rectangle<float> area)
    {
        return juce::jlimit (0.0f, 1.0f, (area.getBottom() - y) / area.getHeight());
    }

    static float positionToPhase (float x, juce::Rectangle<float> area)
    {
        return juce::jlimit (0.0f, 1.0f, (x - area.getX()) / area.getWidth());
    }

    // Snaps to the grid unless shift asks for the exact position.
    float snapX (float phase, juce::ModifierKeys mods) const
    {
        const int steps = ShaperEffect::gridSteps (storedInt (ids::fxGrid, 7));
        if (steps <= 0 || mods.isShiftDown())
            return phase;
        return juce::jlimit (0.0f, 1.0f, std::round (phase * (float) steps) / (float) steps);
    }

    void drawGrid (juce::Graphics& g, juce::Rectangle<float> area, float neutral) const
    {
        const int steps = ShaperEffect::gridSteps (storedInt (ids::fxGrid, 7));
        if (steps > 1)
        {
            g.setColour (theme::outlineLight.withAlpha (0.14f));
            for (int i = 1; i < steps; ++i)
                g.drawVerticalLine ((int) (area.getX() + area.getWidth() * (float) i / (float) steps),
                                    area.getY(), area.getBottom());
        }

        // Quarters of the loop, so you can see where the beats fall even at a
        // fine grid.
        g.setColour (theme::outlineLight.withAlpha (0.3f));
        for (int q = 1; q < 4; ++q)
            g.drawVerticalLine ((int) (area.getX() + area.getWidth() * (float) q / 4.0f),
                                area.getY(), area.getBottom());

        g.setColour (theme::outlineLight.withAlpha (0.25f));
        g.drawHorizontalLine ((int) yForValue (0.0f, area), area.getX(), area.getRight());
        g.drawHorizontalLine ((int) yForValue (1.0f, area), area.getX(), area.getRight());

        // The line the depth knob collapses towards: on it, the target is
        // untouched.
        g.setColour (theme::outlineLight.withAlpha (0.55f));
        g.drawHorizontalLine ((int) yForValue (neutral, area), area.getX(), area.getRight());
    }

    void drawPlayhead (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        if (live == nullptr)
            return;

        const float phase = juce::jlimit (0.0f, 1.0f, live->getDisplayPhase());
        const float x = area.getX() + phase * area.getWidth();
        g.setColour (theme::record.withAlpha (0.75f));
        g.drawVerticalLine ((int) x, area.getY(), area.getBottom());

        // Where the modulation actually is — depth and smoothing included, so a
        // low depth shows the dot riding well inside the drawn shape.
        const float y = yForValue (live->getDisplayValue(), area);
        g.setColour (theme::record);
        g.fillEllipse (x - 3.0f, y - 3.0f, 6.0f, 6.0f);
    }

    void drawLabels (juce::Graphics& g, int target) const
    {
        const auto labels = ShaperEffect::axisLabels (target);
        g.setFont (theme::uiFont (9.0f));
        g.setColour (theme::textDim);
        g.drawText (labels.top, getLocalBounds().reduced (10, 2), juce::Justification::topLeft);
        g.drawText (labels.bottom, getLocalBounds().reduced (10, 2), juce::Justification::bottomLeft);

        if (live != nullptr)
        {
            const auto value = juce::String (live->getDisplayValue(), 2);
            g.drawText (value, getLocalBounds().reduced (10, 2), juce::Justification::topRight);
        }
    }

    int storedInt (const juce::Identifier& id, int fallback) const
    {
        return (int) std::lround ((double) slotTree.getProperty (id, (double) fallback));
    }

    fx::ShaperWave currentWave() const
    {
        return fx::ShaperWave::fromString (
            slotTree.getProperty (ids::fxWave, juce::String()).toString());
    }

    void writeWave (const fx::ShaperWave& wave)
    {
        slotTree.setProperty (ids::fxWave, wave.toString(), &model.getUndoManager());
    }

    int pointIndexAt (juce::Point<int> position) const
    {
        const auto area = plotArea();
        const auto wave = currentWave();
        for (int i = 0; i < wave.numPoints; ++i)
        {
            const auto& point = wave.points[(size_t) i];
            const juce::Point<float> at (area.getX() + point.x * area.getWidth(),
                                         yForValue (point.y, area));
            if (at.getDistanceFrom (position.toFloat()) < 8.0f)
                return i;
        }
        return -1;
    }

    // The point that owns the segment under x — its tension is what bends it.
    int segmentIndexAt (float x, const fx::ShaperWave& wave) const
    {
        if (wave.numPoints < 2)
            return -1;

        const float phase = positionToPhase (x, plotArea());
        int index = wave.numPoints - 1;   // before the first point: the wrap segment
        for (int i = 0; i < wave.numPoints; ++i)
        {
            if (wave.points[(size_t) i].x > phase)
                break;
            index = i;
        }
        return index;
    }

    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&) override
    {
        if (tree == slotTree)
            repaint();
    }

    void timerCallback() override { repaint(); }

    ProjectModel& model;
    juce::ValueTree slotTree;
    std::shared_ptr<ShaperEffect> live;

    int dragIndex = -1;
    int hoverIndex = -1;
    int tensionIndex = -1;
    float tensionStart = 0.0f;
    float tensionStartY = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShaperDisplay)
};
