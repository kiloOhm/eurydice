#include <gtest/gtest.h>
#include "ui/common/Snapping.h"

using juce::Rectangle;

namespace
{
// A desktop 1000x800 with one panel occupying x 400..700, y 100..300.
const std::vector<int> kXLines { 0, 1000, 400, 700 };
const std::vector<int> kYLines { 0, 800, 100, 300 };
constexpr int kThreshold = 12;
}

TEST (Snapping, NearestLineOffset)
{
    EXPECT_EQ (snapping::offsetToNearestLine (5, { 0, 100 }, 12), -5);
    EXPECT_EQ (snapping::offsetToNearestLine (95, { 0, 100 }, 12), 5);
    EXPECT_EQ (snapping::offsetToNearestLine (50, { 0, 100 }, 12), 0) << "too far to snap";
    EXPECT_EQ (snapping::offsetToNearestLine (100, { 0, 100 }, 12), 0) << "already on the line";
}

TEST (Snapping, MoveSnapsLeftEdgeToDesktopEdge)
{
    Rectangle<int> bounds (7, 400, 200, 150);
    snapping::apply (bounds, kXLines, kYLines, kThreshold);
    EXPECT_EQ (bounds.getX(), 0);
    EXPECT_EQ (bounds.getWidth(), 200) << "moving must not resize";
    EXPECT_EQ (bounds.getY(), 400) << "nothing to snap to vertically";
}

TEST (Snapping, MoveSnapsRightEdgeToDesktopEdge)
{
    Rectangle<int> bounds (795, 400, 200, 150);   // right edge at 995
    snapping::apply (bounds, kXLines, kYLines, kThreshold);
    EXPECT_EQ (bounds.getRight(), 1000);
    EXPECT_EQ (bounds.getWidth(), 200);
}

TEST (Snapping, MoveSnapsFlushAgainstAnotherPanel)
{
    // Dragging so our right edge is near the other panel's left edge (400).
    Rectangle<int> bounds (200, 400, 194, 150);   // right edge at 394
    snapping::apply (bounds, kXLines, kYLines, kThreshold);
    EXPECT_EQ (bounds.getRight(), 400) << "should butt up against the neighbour";
}

TEST (Snapping, MoveAlignsTopsWithNeighbour)
{
    Rectangle<int> bounds (50, 106, 200, 150);
    snapping::apply (bounds, kXLines, kYLines, kThreshold);
    EXPECT_EQ (bounds.getY(), 100) << "tops should line up";
    EXPECT_EQ (bounds.getHeight(), 150);
}

TEST (Snapping, MoveLeavesDistantPanelAlone)
{
    Rectangle<int> bounds (200, 500, 100, 100);
    const auto before = bounds;
    snapping::apply (bounds, kXLines, kYLines, kThreshold);
    EXPECT_EQ (bounds, before);
}

TEST (Snapping, ClosestEdgeWins)
{
    // Left edge 8 from 0; right edge 10 from 400 (at 390). Left is closer.
    Rectangle<int> bounds (8, 500, 382, 100);
    snapping::apply (bounds, kXLines, kYLines, kThreshold);
    EXPECT_EQ (bounds.getX(), 0);
}

TEST (Snapping, ResizeMovesOnlyTheDraggedEdge)
{
    Rectangle<int> bounds (100, 400, 294, 150);   // right edge at 394
    snapping::Edges edges;
    edges.stretchingRight = true;
    snapping::apply (bounds, kXLines, kYLines, kThreshold, edges);

    EXPECT_EQ (bounds.getRight(), 400) << "dragged edge snaps";
    EXPECT_EQ (bounds.getX(), 100) << "opposite edge must stay put";
    EXPECT_EQ (bounds.getWidth(), 300) << "resize should change the width";
}

TEST (Snapping, ResizeBottomEdge)
{
    Rectangle<int> bounds (500, 400, 200, 294);   // bottom at 694
    snapping::Edges edges;
    edges.stretchingBottom = true;
    snapping::apply (bounds, kXLines, kYLines, kThreshold, edges);
    EXPECT_EQ (bounds.getY(), 400) << "top stays";

    // Nothing near 694, so it should be untouched.
    EXPECT_EQ (bounds.getBottom(), 694);
}

TEST (Snapping, ResizeLeftEdgeKeepsRightFixed)
{
    Rectangle<int> bounds (694, 400, 200, 150);   // left edge near 700
    snapping::Edges edges;
    edges.stretchingLeft = true;
    snapping::apply (bounds, kXLines, kYLines, kThreshold, edges);

    EXPECT_EQ (bounds.getX(), 700);
    EXPECT_EQ (bounds.getRight(), 894) << "right edge must not move";
}

TEST (Snapping, EmptyLinesAreSafe)
{
    Rectangle<int> bounds (5, 5, 100, 100);
    const auto before = bounds;
    snapping::apply (bounds, {}, {}, kThreshold);
    EXPECT_EQ (bounds, before);
}
