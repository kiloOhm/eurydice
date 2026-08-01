#include <gtest/gtest.h>
#include "ui/common/DockZones.h"

using docking::Zone;
using juce::Point;
using juce::Rectangle;

namespace
{
const Rectangle<int> kDesktop (0, 0, 1000, 800);
constexpr docking::Config kConfig {};   // edge 28, corner 140
}

TEST (DockZones, CentreIsNoZone)
{
    EXPECT_EQ (docking::zoneForPointer ({ 500, 400 }, kDesktop), Zone::none);
}

TEST (DockZones, EdgesGiveHalves)
{
    EXPECT_EQ (docking::zoneForPointer ({ 5, 400 }, kDesktop), Zone::left);
    EXPECT_EQ (docking::zoneForPointer ({ 995, 400 }, kDesktop), Zone::right);
    EXPECT_EQ (docking::zoneForPointer ({ 500, 5 }, kDesktop), Zone::top);
    EXPECT_EQ (docking::zoneForPointer ({ 500, 795 }, kDesktop), Zone::bottom);
}

TEST (DockZones, CornersGiveQuarters)
{
    EXPECT_EQ (docking::zoneForPointer ({ 5, 5 }, kDesktop), Zone::topLeft);
    EXPECT_EQ (docking::zoneForPointer ({ 995, 5 }, kDesktop), Zone::topRight);
    EXPECT_EQ (docking::zoneForPointer ({ 5, 795 }, kDesktop), Zone::bottomLeft);
    EXPECT_EQ (docking::zoneForPointer ({ 995, 795 }, kDesktop), Zone::bottomRight);
}

TEST (DockZones, CornerBandBeatsEdge)
{
    // Down the left edge but still inside the top corner band.
    EXPECT_EQ (docking::zoneForPointer ({ 5, 100 }, kDesktop), Zone::topLeft);
    // Past the corner band, it is a plain left half.
    EXPECT_EQ (docking::zoneForPointer ({ 5, 300 }, kDesktop), Zone::left);
}

TEST (DockZones, JustInsideAndOutsideTheMargin)
{
    EXPECT_EQ (docking::zoneForPointer ({ 28, 400 }, kDesktop), Zone::left);
    EXPECT_EQ (docking::zoneForPointer ({ 29, 400 }, kDesktop), Zone::none);
}

TEST (DockZones, HalvesTileTheDesktopExactly)
{
    const auto left = docking::boundsForZone (Zone::left, kDesktop);
    const auto right = docking::boundsForZone (Zone::right, kDesktop);
    EXPECT_EQ (left.getRight(), right.getX()) << "no gap or overlap";
    EXPECT_EQ (left.getUnion (right), kDesktop) << "together they fill the desktop";

    const auto top = docking::boundsForZone (Zone::top, kDesktop);
    const auto bottom = docking::boundsForZone (Zone::bottom, kDesktop);
    EXPECT_EQ (top.getBottom(), bottom.getY());
    EXPECT_EQ (top.getUnion (bottom), kDesktop);
}

TEST (DockZones, QuartersTileTheDesktopExactly)
{
    const auto tl = docking::boundsForZone (Zone::topLeft, kDesktop);
    const auto tr = docking::boundsForZone (Zone::topRight, kDesktop);
    const auto bl = docking::boundsForZone (Zone::bottomLeft, kDesktop);
    const auto br = docking::boundsForZone (Zone::bottomRight, kDesktop);

    EXPECT_EQ (tl.getUnion (tr).getUnion (bl).getUnion (br), kDesktop);
    EXPECT_TRUE (tl.getIntersection (tr).isEmpty());
    EXPECT_TRUE (tl.getIntersection (bl).isEmpty());
    EXPECT_TRUE (br.getIntersection (tr).isEmpty());
}

TEST (DockZones, OddSizesLoseNoPixels)
{
    // Odd width and height: integer halving must not leave a one-pixel seam.
    const Rectangle<int> odd (0, 0, 1001, 801);
    const auto left = docking::boundsForZone (Zone::left, odd);
    const auto right = docking::boundsForZone (Zone::right, odd);
    EXPECT_EQ (left.getUnion (right), odd);

    const auto top = docking::boundsForZone (Zone::top, odd);
    const auto bottom = docking::boundsForZone (Zone::bottom, odd);
    EXPECT_EQ (top.getUnion (bottom), odd);
}

TEST (DockZones, RespectsDesktopOrigin)
{
    // The desktop sits below the transport bar and right of the browser.
    const Rectangle<int> offset (240, 44, 1000, 800);
    EXPECT_EQ (docking::zoneForPointer ({ 245, 400 }, offset), Zone::left);
    EXPECT_EQ (docking::zoneForPointer ({ 700, 400 }, offset), Zone::none);

    const auto left = docking::boundsForZone (Zone::left, offset);
    EXPECT_EQ (left.getX(), 240);
    EXPECT_EQ (left.getY(), 44);
}

TEST (DockZones, Complements)
{
    EXPECT_EQ (docking::complementOf (Zone::left), Zone::right);
    EXPECT_EQ (docking::complementOf (Zone::topLeft), Zone::bottomRight);
    EXPECT_EQ (docking::complementOf (Zone::none), Zone::none);

    // A zone and its complement must tile the desktop.
    for (auto zone : { Zone::left, Zone::right, Zone::top, Zone::bottom })
    {
        const auto a = docking::boundsForZone (zone, kDesktop);
        const auto b = docking::boundsForZone (docking::complementOf (zone), kDesktop);
        EXPECT_EQ (a.getUnion (b), kDesktop) << docking::zoneName (zone);
    }
}

TEST (DockZones, NoneHasEmptyBounds)
{
    EXPECT_TRUE (docking::boundsForZone (Zone::none, kDesktop).isEmpty());
}
