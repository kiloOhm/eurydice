#include <gtest/gtest.h>
#include "engine/AutomationRecorder.h"

using autorec::Options;
using autorec::Pass;
using autorec::Point;

namespace
{
std::vector<int> positionsOf (const std::vector<Point>& points)
{
    std::vector<int> out;
    out.reserve (points.size());
    for (const auto& point : points)
        out.push_back (point.posTicks);
    return out;
}

// Spacing wide enough that nothing is dropped for being too close, so a test
// can isolate the collinear rule.
Options spacedOptions (int spacing = 1)
{
    Options options;
    options.minSpacingTicks = spacing;
    return options;
}

bool isOneOf (double value, double a, double b)
{
    return std::abs (value - a) < 1.0e-9 || std::abs (value - b) < 1.0e-9;
}
}

// ---------------- thinning ----------------

TEST (AutomationThin, EmptyStaysEmpty)
{
    EXPECT_TRUE (autorec::thin ({}).empty());
}

TEST (AutomationThin, DropsInteriorPointsOnAStraightRamp)
{
    std::vector<Point> samples;
    for (int i = 0; i <= 10; ++i)
        samples.push_back ({ i * 100, i * 0.1, 0.0 });

    const auto thinned = autorec::thin (samples, spacedOptions());
    ASSERT_EQ (thinned.size(), 2u);
    EXPECT_EQ (thinned.front().posTicks, 0);
    EXPECT_EQ (thinned.back().posTicks, 1000);
    EXPECT_NEAR (thinned.back().value, 1.0, 1.0e-9);
}

TEST (AutomationThin, KeepsTheCorneryOfAnElbow)
{
    // Up to 1.0 at the midpoint, back down to 0: the elbow carries the shape.
    std::vector<Point> samples;
    for (int i = 0; i <= 10; ++i)
        samples.push_back ({ i * 100, i * 0.2, 0.0 });
    for (int i = 1; i <= 10; ++i)
        samples.push_back ({ 1000 + i * 100, 2.0 - i * 0.2, 0.0 });

    const auto thinned = autorec::thin (samples, spacedOptions());
    EXPECT_EQ (positionsOf (thinned), (std::vector<int> { 0, 1000, 2000 }));
}

TEST (AutomationThin, KeepsPointsThatLeaveTheTolerance)
{
    Options options = spacedOptions();
    options.collinearTolerance = 0.001;

    std::vector<Point> samples { { 0, 0.0, 0.0 }, { 100, 0.5, 0.0 }, { 200, 0.02, 0.0 } };
    EXPECT_EQ (autorec::thin (samples, options).size(), 3u);

    // The same wobble inside a loose tolerance collapses.
    options.collinearTolerance = 0.6;
    EXPECT_EQ (autorec::thin (samples, options).size(), 2u);
}

TEST (AutomationThin, EnforcesMinimumSpacing)
{
    Options options;
    options.minSpacingTicks = 50;
    options.collinearTolerance = 0.0;   // only spacing may drop anything

    // Alternating values so no three points are ever collinear.
    std::vector<Point> samples;
    for (int i = 0; i <= 20; ++i)
        samples.push_back ({ i * 10, i % 2 == 0 ? 0.0 : 1.0, 0.0 });

    const auto thinned = autorec::thin (samples, options);
    for (size_t i = 1; i + 1 < thinned.size(); ++i)
        EXPECT_GE (thinned[i].posTicks - thinned[i - 1].posTicks, 50);
    EXPECT_LT (thinned.size(), samples.size());
}

TEST (AutomationThin, AlwaysKeepsTheFinalValue)
{
    Options options;
    options.minSpacingTicks = 1000;
    options.collinearTolerance = 0.0;

    const std::vector<Point> samples { { 0, 0.0, 0.0 }, { 5, 0.4, 0.0 }, { 9, 0.9, 0.0 } };
    const auto thinned = autorec::thin (samples, options);
    ASSERT_FALSE (thinned.empty());
    EXPECT_EQ (thinned.back().posTicks, 9);
    EXPECT_NEAR (thinned.back().value, 0.9, 1.0e-9);
}

// ---------------- range replacement ----------------

TEST (AutomationReplaceRange, RemovesEveryStalePointInsideTheRange)
{
    const std::vector<Point> existing {
        { 0, 0.1, 0.0 }, { 500, 0.2, 0.0 }, { 1000, 0.3, 0.0 },
        { 1500, 0.4, 0.0 }, { 2000, 0.5, 0.0 } };
    const std::vector<Point> run { { 600, 0.9, 0.0 }, { 1400, 0.8, 0.0 } };

    const auto merged = autorec::replaceRange (existing, run, 500, 1500);
    EXPECT_EQ (positionsOf (merged), (std::vector<int> { 0, 600, 1400, 2000 }));
}

TEST (AutomationReplaceRange, RangeBoundsAreInclusive)
{
    const std::vector<Point> existing { { 100, 0.1, 0.0 }, { 200, 0.2, 0.0 } };
    const auto merged = autorec::replaceRange (existing, { { 150, 0.9, 0.0 } }, 100, 200);
    EXPECT_EQ (positionsOf (merged), (std::vector<int> { 150 }));
}

TEST (AutomationReplaceRange, LeavesTheCurveOutsideTheRangeAlone)
{
    const std::vector<Point> existing { { 0, 0.1, 0.5 }, { 4000, 0.9, -0.5 } };
    const auto merged = autorec::replaceRange (existing, { { 1000, 0.4, 0.0 } }, 900, 1100);

    ASSERT_EQ (merged.size(), 3u);
    EXPECT_NEAR (merged.front().tension, 0.5, 1.0e-9);
    EXPECT_NEAR (merged.back().tension, -0.5, 1.0e-9);
}

// ---------------- passes ----------------

TEST (AutomationPass, RecordsAThinnedCurveOverAFlatSource)
{
    Pass pass (std::vector<Point> { { 0, 0.5, 0.0 }, { 4000, 0.5, 0.0 } }, spacedOptions (30));

    for (int tick = 1000; tick <= 2000; tick += 40)
        pass.addSample (tick, 0.5 + (tick - 1000) * 0.0004);   // straight ramp
    pass.finish();

    const auto merged = pass.merged();
    // The ramp collapses to its endpoints; the flat points outside survive.
    EXPECT_EQ (positionsOf (merged), (std::vector<int> { 0, 1000, 2000, 4000 }));
    EXPECT_NEAR (merged[2].value, 0.9, 1.0e-6);
}

TEST (AutomationPass, SecondPassReplacesTheFirstInsteadOfInterleaving)
{
    Pass first (std::vector<Point> { { 0, 0.0, 0.0 } }, spacedOptions (30));
    for (int tick = 1000; tick <= 2000; tick += 100)
        first.addSample (tick, tick % 200 == 0 ? 0.2 : 0.8);   // zig-zag, nothing collinear
    first.finish();
    const auto afterFirst = first.merged();
    ASSERT_GT (afterFirst.size(), 3u);

    Pass second (afterFirst, spacedOptions (30));
    for (int tick = 1000; tick <= 2000; tick += 500)
        second.addSample (tick, 0.5);
    second.finish();

    const auto afterSecond = second.merged();
    for (const auto& point : afterSecond)
        if (point.posTicks >= 1000 && point.posTicks <= 2000)
            EXPECT_NEAR (point.value, 0.5, 1.0e-9) << "stale point left at " << point.posTicks;
    EXPECT_EQ (afterSecond.front().posTicks, 0);
}

TEST (AutomationPass, LoopWrapStartsANewRunAndKeepsBothStretches)
{
    Pass pass (std::vector<Point> { { 0, 0.0, 0.0 }, { 4000, 0.0, 0.0 } }, spacedOptions (30));

    // First lap: 0 -> 1900 climbing. Then the transport wraps to 0 and the
    // second lap holds a different value over the front of the range.
    for (int tick = 0; tick <= 1900; tick += 100)
        pass.addSample (tick, tick % 200 == 0 ? 0.1 : 0.6);
    for (int tick = 0; tick <= 900; tick += 100)
        pass.addSample (tick, tick % 200 == 0 ? 0.9 : 0.4);
    pass.finish();

    const auto merged = pass.merged();
    ASSERT_FALSE (merged.empty());

    // Sorted, no duplicate positions, and the far end of the source survives.
    for (size_t i = 1; i < merged.size(); ++i)
        EXPECT_LT (merged[i - 1].posTicks, merged[i].posTicks);
    EXPECT_EQ (merged.back().posTicks, 4000);

    // The second lap owns 0..900; the first lap still owns 1000..1900.
    for (const auto& point : merged)
    {
        if (point.posTicks <= 900)
            EXPECT_TRUE (isOneOf (point.value, 0.9, 0.4))
                << "first-lap value survived the second lap at " << point.posTicks;
        else if (point.posTicks <= 1900)
            EXPECT_TRUE (isOneOf (point.value, 0.1, 0.6));
    }
}

TEST (AutomationPass, TrailingSampleIsCommittedByFinish)
{
    Pass pass ({}, spacedOptions (500));
    pass.addSample (0, 0.0);
    EXPECT_FALSE (pass.addSample (100, 0.7)) << "too close to commit on its own";

    pass.finish();
    const auto merged = pass.merged();
    ASSERT_EQ (merged.size(), 2u);
    EXPECT_EQ (merged.back().posTicks, 100);
    EXPECT_NEAR (merged.back().value, 0.7, 1.0e-9);
}

TEST (AutomationPass, MergedIsUsableBeforeFinish)
{
    Pass pass ({}, spacedOptions (500));
    pass.addSample (0, 0.2);
    pass.addSample (100, 0.8);   // still pending

    const auto merged = pass.merged();
    ASSERT_EQ (merged.size(), 2u);
    EXPECT_NEAR (merged.back().value, 0.8, 1.0e-9);
}

TEST (AutomationPass, EmptyPassLeavesTheCurveUntouched)
{
    const std::vector<Point> before { { 0, 0.25, 0.0 }, { 960, 0.75, 0.0 } };
    Pass pass (before);
    pass.finish();

    EXPECT_TRUE (pass.isEmpty());
    EXPECT_EQ (positionsOf (pass.merged()), positionsOf (before));
}
