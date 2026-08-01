#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "app/AppServices.h"
#include "app/Theme.h"
#include "engine/EngineSnapshot.h"

// Curve editor for one automation source. Left-drag moves points,
// double-click adds a point, right-click deletes one, vertical drag on a
// segment bends its tension (FL-style).
class AutomationEditor : public juce::Component
{
public:
    AutomationEditor (AppServices& s, juce::ValueTree automationTree, int clipLengthTicks)
        : services (s), automation (automationTree),
          lengthTicks (juce::jmax (clipLengthTicks, ids::ticksPerBar))
    {
        setSize (620, 260);
    }

    static void open (AppServices& s, juce::ValueTree automationTree, int clipLengthTicks)
    {
        struct Window : juce::DocumentWindow
        {
            explicit Window (const juce::String& title)
                : juce::DocumentWindow (title, theme::panelHeader, closeButton)
            {
                setUsingNativeTitleBar (true);
            }
            void closeButtonPressed() override { delete this; }
        };

        auto* window = new Window ("Automation: " + automationTree[ids::name].toString());
        window->setContentOwned (new AutomationEditor (s, automationTree, clipLengthTicks), true);
        window->centreWithSize (620, 260);
        window->setVisible (true);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::panelBg);
        auto area = curveArea();

        // grid: bars
        g.setColour (theme::outline);
        for (int t = 0; t <= lengthTicks; t += ids::ticksPerBar)
        {
            const int x = ticksToX (t);
            g.drawVerticalLine (x, (float) area.getY(), (float) area.getBottom());
        }
        for (int step = 0; step <= 4; ++step)
            g.drawHorizontalLine (valueToY ((float) step * 0.25f),
                                  (float) area.getX(), (float) area.getRight());

        // curve
        auto points = buildPoints();
        AutomationSnapshot snap;
        snap.points = points;

        juce::Path path;
        bool started = false;
        for (int x = area.getX(); x <= area.getRight(); x += 2)
        {
            const float v = snap.valueAt (xToTicks (x));
            const auto y = (float) valueToY (v);
            if (! started) { path.startNewSubPath ((float) x, y); started = true; }
            else           path.lineTo ((float) x, y);
        }
        g.setColour (theme::accent);
        g.strokePath (path, juce::PathStrokeType (2.0f));

        // points
        int index = 0;
        for (const auto point : automation)
        {
            if (! point.hasType (ids::POINT))
                continue;
            const int x = ticksToX ((int) point[ids::posTicks]);
            const int y = valueToY ((float) (double) point[ids::value]);
            g.setColour (index == draggedPoint ? juce::Colours::white : theme::secondary);
            g.fillEllipse ((float) x - 4, (float) y - 4, 8, 8);
            ++index;
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        draggedPoint = pointIndexAt (e.getPosition());

        if (e.mods.isPopupMenu())
        {
            if (draggedPoint >= 0 && numPoints() > 1)
                removePoint (draggedPoint);
            draggedPoint = -1;
            repaint();
            return;
        }

        if (e.getNumberOfClicks() == 2 && draggedPoint < 0)
        {
            addPoint (e.getPosition());
            return;
        }

        if (draggedPoint < 0)
        {
            // segment drag = tension edit; remember which segment
            tensionSegment = segmentLeftPointAt (e.getPosition().x);
            tensionStartY = e.getPosition().y;
            if (tensionSegment >= 0)
                tensionStart = (float) (double) getPoint (tensionSegment)[ids::tension];
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto& undo = services.project.getUndoManager();
        if (draggedPoint >= 0)
        {
            auto point = getPoint (draggedPoint);
            point.setProperty (ids::posTicks, juce::jlimit (0, lengthTicks,
                                   (int) xToTicks (e.getPosition().x)), &undo);
            point.setProperty (ids::value, juce::jlimit (0.0, 1.0,
                                   (double) yToValue (e.getPosition().y)), &undo);
            repaint();
        }
        else if (tensionSegment >= 0)
        {
            const float delta = (float) (tensionStartY - e.getPosition().y) / 80.0f;
            getPoint (tensionSegment).setProperty (ids::tension,
                juce::jlimit (-1.0f, 1.0f, tensionStart + delta), &undo);
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        draggedPoint = -1;
        tensionSegment = -1;
        repaint();
    }

private:
    juce::Rectangle<int> curveArea() const { return getLocalBounds().reduced (10); }
    int ticksToX (int t) const
    {
        const auto a = curveArea();
        return a.getX() + (int) ((double) t / lengthTicks * a.getWidth());
    }
    double xToTicks (int x) const
    {
        const auto a = curveArea();
        return juce::jlimit (0.0, (double) lengthTicks,
                             (double) (x - a.getX()) / a.getWidth() * lengthTicks);
    }
    int valueToY (float v) const
    {
        const auto a = curveArea();
        return a.getBottom() - (int) (v * (float) a.getHeight());
    }
    float yToValue (int y) const
    {
        const auto a = curveArea();
        return juce::jlimit (0.0f, 1.0f, (float) (a.getBottom() - y) / (float) a.getHeight());
    }

    std::vector<AutomationPoint> buildPoints() const
    {
        std::vector<AutomationPoint> points;
        for (const auto point : automation)
            if (point.hasType (ids::POINT))
                points.push_back ({ (double) (int) point[ids::posTicks],
                                    (float) (double) point[ids::value],
                                    (float) (double) point[ids::tension] });
        std::sort (points.begin(), points.end(),
                   [] (const AutomationPoint& a, const AutomationPoint& b)
                   { return a.posTicks < b.posTicks; });
        return points;
    }

    int numPoints() const
    {
        int n = 0;
        for (const auto point : automation)
            if (point.hasType (ids::POINT))
                ++n;
        return n;
    }

    juce::ValueTree getPoint (int index) const
    {
        int n = 0;
        for (const auto point : automation)
            if (point.hasType (ids::POINT) && n++ == index)
                return point;
        return {};
    }

    int pointIndexAt (juce::Point<int> pos) const
    {
        int index = 0;
        for (const auto point : automation)
        {
            if (! point.hasType (ids::POINT))
                continue;
            const int x = ticksToX ((int) point[ids::posTicks]);
            const int y = valueToY ((float) (double) point[ids::value]);
            if (pos.getDistanceFrom ({ x, y }) < 8)
                return index;
            ++index;
        }
        return -1;
    }

    // Left point of the segment under x (by sorted position); -1 if none.
    int segmentLeftPointAt (int x) const
    {
        const double ticks = xToTicks (x);
        int bestIndex = -1;
        double bestPos = -1.0;
        int index = 0;
        for (const auto point : automation)
        {
            if (! point.hasType (ids::POINT))
                continue;
            const double pos = (int) point[ids::posTicks];
            if (pos <= ticks && pos > bestPos)
            {
                bestPos = pos;
                bestIndex = index;
            }
            ++index;
        }
        return bestIndex;
    }

    void addPoint (juce::Point<int> pos)
    {
        juce::ValueTree point (ids::POINT);
        point.setProperty (ids::posTicks, (int) xToTicks (pos.x), nullptr);
        point.setProperty (ids::value, (double) yToValue (pos.y), nullptr);
        point.setProperty (ids::tension, 0.0, nullptr);
        automation.appendChild (point, &services.project.getUndoManager());
        repaint();
    }

    void removePoint (int index)
    {
        auto point = getPoint (index);
        if (point.isValid())
            automation.removeChild (point, &services.project.getUndoManager());
    }

    AppServices& services;
    juce::ValueTree automation;
    int lengthTicks;

    int draggedPoint = -1;
    int tensionSegment = -1;
    int tensionStartY = 0;
    float tensionStart = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationEditor)
};
