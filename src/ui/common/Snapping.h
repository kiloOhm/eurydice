#pragma once

#include <juce_graphics/juce_graphics.h>
#include <vector>

// Magnetic snapping for floating panels: pull edges onto nearby guide lines
// (the desktop edges and other panels' edges) while moving or resizing.
// Pure geometry, so it can be tested without a UI.
namespace snapping
{
struct Edges
{
    bool stretchingTop = false;
    bool stretchingLeft = false;
    bool stretchingBottom = false;
    bool stretchingRight = false;

    bool stretchingHorizontally() const { return stretchingLeft || stretchingRight; }
    bool stretchingVertically() const   { return stretchingTop || stretchingBottom; }
};

// Offset that pulls `value` onto the nearest line, or 0 if none is close enough.
inline int offsetToNearestLine (int value, const std::vector<int>& lines, int threshold)
{
    int best = 0;
    int bestDistance = threshold + 1;
    for (int line : lines)
    {
        const int distance = std::abs (line - value);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = line - value;
        }
    }
    return bestDistance <= threshold ? best : 0;
}

// Adjusts `bounds` in place. While resizing only the dragged edge moves;
// while moving the whole rectangle translates by whichever edge snaps first.
inline void apply (juce::Rectangle<int>& bounds,
                   const std::vector<int>& xLines,
                   const std::vector<int>& yLines,
                   int threshold,
                   Edges edges = {})
{
    if (edges.stretchingLeft)
    {
        if (const int d = offsetToNearestLine (bounds.getX(), xLines, threshold); d != 0)
            bounds.setLeft (bounds.getX() + d);
    }
    else if (edges.stretchingRight)
    {
        if (const int d = offsetToNearestLine (bounds.getRight(), xLines, threshold); d != 0)
            bounds.setRight (bounds.getRight() + d);
    }
    else
    {
        const int left = offsetToNearestLine (bounds.getX(), xLines, threshold);
        const int right = offsetToNearestLine (bounds.getRight(), xLines, threshold);
        const int offset = (left != 0 && (right == 0 || std::abs (left) <= std::abs (right)))
                               ? left : right;
        if (offset != 0)
            bounds.translate (offset, 0);
    }

    if (edges.stretchingTop)
    {
        if (const int d = offsetToNearestLine (bounds.getY(), yLines, threshold); d != 0)
            bounds.setTop (bounds.getY() + d);
    }
    else if (edges.stretchingBottom)
    {
        if (const int d = offsetToNearestLine (bounds.getBottom(), yLines, threshold); d != 0)
            bounds.setBottom (bounds.getBottom() + d);
    }
    else
    {
        const int top = offsetToNearestLine (bounds.getY(), yLines, threshold);
        const int bottom = offsetToNearestLine (bounds.getBottom(), yLines, threshold);
        const int offset = (top != 0 && (bottom == 0 || std::abs (top) <= std::abs (bottom)))
                               ? top : bottom;
        if (offset != 0)
            bounds.translate (0, offset);
    }
}
} // namespace snapping
