#pragma once

#include <juce_graphics/juce_graphics.h>

// Windows/Visual-Studio style docking: dragging a panel near an edge or corner
// of the desktop targets a region that the panel will fill on drop — halves at
// the edges, quarters at the corners. Pure geometry so it can be tested.
namespace docking
{
enum class Zone
{
    none,
    left, right, top, bottom,
    topLeft, topRight, bottomLeft, bottomRight
};

struct Config
{
    int edgeMargin = 28;     // how close to an edge before that edge's zone arms
    int cornerMargin = 140;  // how close to a corner before it becomes a quarter
};

// Which zone the pointer is currently over, or Zone::none when it is not near
// an edge. Corners win over edges so quarters stay reachable.
inline Zone zoneForPointer (juce::Point<int> pointer,
                            juce::Rectangle<int> desktop,
                            Config config = {})
{
    if (! desktop.expanded (config.edgeMargin).contains (pointer))
        return Zone::none;

    const bool nearLeft   = pointer.x - desktop.getX() <= config.edgeMargin;
    const bool nearRight  = desktop.getRight() - pointer.x <= config.edgeMargin;
    const bool nearTop    = pointer.y - desktop.getY() <= config.edgeMargin;
    const bool nearBottom = desktop.getBottom() - pointer.y <= config.edgeMargin;

    const bool inTopBand    = pointer.y - desktop.getY() <= config.cornerMargin;
    const bool inBottomBand = desktop.getBottom() - pointer.y <= config.cornerMargin;
    const bool inLeftBand   = pointer.x - desktop.getX() <= config.cornerMargin;
    const bool inRightBand  = desktop.getRight() - pointer.x <= config.cornerMargin;

    if (nearLeft  && inTopBand)    return Zone::topLeft;
    if (nearLeft  && inBottomBand) return Zone::bottomLeft;
    if (nearRight && inTopBand)    return Zone::topRight;
    if (nearRight && inBottomBand) return Zone::bottomRight;
    if (nearTop    && inLeftBand)  return Zone::topLeft;
    if (nearTop    && inRightBand) return Zone::topRight;
    if (nearBottom && inLeftBand)  return Zone::bottomLeft;
    if (nearBottom && inRightBand) return Zone::bottomRight;

    if (nearLeft)   return Zone::left;
    if (nearRight)  return Zone::right;
    if (nearTop)    return Zone::top;
    if (nearBottom) return Zone::bottom;

    return Zone::none;
}

// The rectangle a panel occupies once dropped in a zone. Halves and quarters
// are computed so that complementary zones tile the desktop exactly, with no
// overlapping or leftover pixels from integer division.
inline juce::Rectangle<int> boundsForZone (Zone zone, juce::Rectangle<int> desktop)
{
    const int halfWidth = desktop.getWidth() / 2;
    const int halfHeight = desktop.getHeight() / 2;
    const int x = desktop.getX();
    const int y = desktop.getY();
    const int w = desktop.getWidth();
    const int h = desktop.getHeight();

    switch (zone)
    {
        case Zone::left:        return { x, y, halfWidth, h };
        case Zone::right:       return { x + halfWidth, y, w - halfWidth, h };
        case Zone::top:         return { x, y, w, halfHeight };
        case Zone::bottom:      return { x, y + halfHeight, w, h - halfHeight };
        case Zone::topLeft:     return { x, y, halfWidth, halfHeight };
        case Zone::topRight:    return { x + halfWidth, y, w - halfWidth, halfHeight };
        case Zone::bottomLeft:  return { x, y + halfHeight, halfWidth, h - halfHeight };
        case Zone::bottomRight: return { x + halfWidth, y + halfHeight,
                                         w - halfWidth, h - halfHeight };
        case Zone::none:
        default:                return {};
    }
}

// The zone that fills the space a newly docked panel does not occupy, so an
// existing occupant can be pushed aside instead of being covered up.
inline Zone complementOf (Zone zone)
{
    switch (zone)
    {
        case Zone::left:        return Zone::right;
        case Zone::right:       return Zone::left;
        case Zone::top:         return Zone::bottom;
        case Zone::bottom:      return Zone::top;
        case Zone::topLeft:     return Zone::bottomRight;
        case Zone::topRight:    return Zone::bottomLeft;
        case Zone::bottomLeft:  return Zone::topRight;
        case Zone::bottomRight: return Zone::topLeft;
        case Zone::none:
        default:                return Zone::none;
    }
}

inline const char* zoneName (Zone zone)
{
    switch (zone)
    {
        case Zone::left:        return "left";
        case Zone::right:       return "right";
        case Zone::top:         return "top";
        case Zone::bottom:      return "bottom";
        case Zone::topLeft:     return "topLeft";
        case Zone::topRight:    return "topRight";
        case Zone::bottomLeft:  return "bottomLeft";
        case Zone::bottomRight: return "bottomRight";
        case Zone::none:
        default:                return "none";
    }
}
} // namespace docking
