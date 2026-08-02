#pragma once

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

// Turns the stream of values a control emits while it is being performed into
// an automation curve: thins the samples down to the points that carry shape,
// and splices each recorded run back over whatever the curve held there
// before. Pure maths over plain structs, so the write-arm behaviour can be
// tested without a UI or a running transport.
namespace autorec
{
struct Point
{
    int    posTicks = 0;
    double value = 0.0;
    double tension = 0.0;
};

struct Options
{
    // Samples arrive at UI rate, so without a floor on the spacing a two-bar
    // move would leave hundreds of points behind.
    int    minSpacingTicks = 30;
    double collinearTolerance = 0.004;
};

// True when `b` sits on the straight line from `a` to `c`, so dropping it
// leaves the curve within `tolerance` of where it was.
inline bool isCollinear (const Point& a, const Point& b, const Point& c, double tolerance)
{
    const auto span = (double) (c.posTicks - a.posTicks);
    if (span <= 0.0)
        return true;
    const double u = (double) (b.posTicks - a.posTicks) / span;
    const double expected = a.value + (c.value - a.value) * u;
    return std::abs (b.value - expected) <= tolerance;
}

// Removes the last-but-one point for as long as it adds nothing to the shape.
// Run after every append so a straight move never accumulates points.
inline void collapseTail (std::vector<Point>& points, const Options& options)
{
    while (points.size() >= 3
           && isCollinear (points[points.size() - 3], points[points.size() - 2],
                           points.back(), options.collinearTolerance))
        points.erase (points.end() - 2);
}

// Batch form of the same thinning: drops samples that arrive sooner than the
// minimum spacing and interior points that lie on a straight run. The first
// and last sample always survive, so the recorded gesture keeps its endpoints.
inline std::vector<Point> thin (const std::vector<Point>& samples, const Options& options = {})
{
    std::vector<Point> out;
    out.reserve (samples.size());

    for (const auto& sample : samples)
    {
        if (! out.empty() && sample.posTicks - out.back().posTicks < options.minSpacingTicks)
            continue;
        out.push_back (sample);
        collapseTail (out, options);
    }

    if (! samples.empty() && out.back().posTicks != samples.back().posTicks)
    {
        out.push_back (samples.back());
        collapseTail (out, options);
    }
    return out;
}

// Splices `run` into `existing`, dropping every existing point inside
// [fromTicks, toTicks]. Without this a second pass over the same stretch would
// interleave with the first instead of replacing it.
inline std::vector<Point> replaceRange (const std::vector<Point>& existing,
                                        const std::vector<Point>& run,
                                        int fromTicks, int toTicks)
{
    std::vector<Point> out;
    out.reserve (existing.size() + run.size());

    for (const auto& point : existing)
        if (point.posTicks < fromTicks || point.posTicks > toTicks)
            out.push_back (point);

    out.insert (out.end(), run.begin(), run.end());
    std::stable_sort (out.begin(), out.end(),
                      [] (const Point& a, const Point& b) { return a.posTicks < b.posTicks; });
    return out;
}

// One write pass over a single parameter. Samples arrive in transport order; a
// backwards jump means the transport looped, which closes the current run and
// opens a new one so each pass replaces only the stretch it actually covered.
struct Pass
{
    Pass() = default;
    explicit Pass (std::vector<Point> curveBeforePass, Options passOptions = {})
        : options (passOptions), before (std::move (curveBeforePass)) {}

    // Returns true when the merged curve changed and is worth writing out.
    bool addSample (int posTicks, double value)
    {
        if (runs.empty() || posTicks < runs.back().to)
        {
            if (! runs.empty())
                closeRun (runs.back());
            runs.push_back ({ posTicks, posTicks, {}, {}, false });
        }

        auto& run = runs.back();
        run.to = posTicks;

        if (! run.points.empty()
            && posTicks - run.points.back().posTicks < options.minSpacingTicks)
        {
            run.pending = { posTicks, value, 0.0 };
            run.hasPending = true;
            return false;
        }

        run.points.push_back ({ posTicks, value, 0.0 });
        run.hasPending = false;
        collapseTail (run.points, options);
        return true;
    }

    // Commits the trailing sample so the pass ends on the value the control
    // actually settled at. Idempotent.
    void finish()
    {
        if (! runs.empty())
            closeRun (runs.back());
    }

    const std::vector<Point>& curveBeforePass() const { return before; }
    bool isEmpty() const { return runs.empty(); }

    // The curve as it stands, whether or not finish() has run yet.
    std::vector<Point> merged() const
    {
        auto out = before;
        for (const auto& run : runs)
        {
            auto points = run.points;
            if (run.hasPending)
            {
                points.push_back (run.pending);
                collapseTail (points, options);
            }
            if (points.empty())
                continue;
            out = replaceRange (out, points, run.from,
                                std::max (run.to, points.back().posTicks));
        }
        return out;
    }

private:
    struct Run
    {
        int from = 0;
        int to = 0;
        std::vector<Point> points;
        Point pending;
        bool hasPending = false;
    };

    void closeRun (Run& run)
    {
        if (! run.hasPending)
            return;
        run.points.push_back (run.pending);
        run.hasPending = false;
        collapseTail (run.points, options);
    }

    Options options;
    std::vector<Point> before;
    std::vector<Run> runs;
};
} // namespace autorec
