#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <juce_core/juce_core.h>

namespace fx
{
// One breakpoint of a drawn shaper wave. x is the position in the loop and y
// the modulation value, both 0..1; tension bends the segment towards the NEXT
// point using the same law as an automation lane, so a bend drawn here feels
// like a bend drawn there.
struct WavePoint
{
    float x = 0.0f;
    float y = 1.0f;
    float tension = 0.0f;
};

// A loop of breakpoints: the wave a shaper reads instead of picking an LFO
// shape. Stored on the SLOT tree as one compact string, so it rides undo,
// project save and load like every other effect property.
//
// The loop always closes — the segment after the last point wraps round to the
// first — which is what lets any drawing repeat cleanly. Two points at the same
// x are an instant step, so a gate can drop in a single sample.
struct ShaperWave
{
    static constexpr int maxPoints = 32;

    std::array<WavePoint, maxPoints> points {};
    int numPoints = 0;

    // Value at a phase in cycles; anything outside 0..1 wraps.
    float valueAt (float phase) const noexcept
    {
        if (numPoints <= 0)
            return 1.0f;
        if (numPoints == 1)
            return points[0].y;

        const float p = phase - std::floor (phase);

        int left = -1;
        for (int i = 0; i < numPoints; ++i)
        {
            if (points[(size_t) i].x > p)
                break;
            left = i;
        }

        // Before the first point or after the last: both sit on the segment
        // that wraps from the last point round to the first.
        if (left < 0)
            return segment (numPoints - 1, 0, p + 1.0f);
        if (left == numPoints - 1)
            return segment (left, 0, p);
        return segment (left, left + 1, p);
    }

    void sort() noexcept
    {
        std::stable_sort (points.begin(), points.begin() + numPoints,
                          [] (const WavePoint& a, const WavePoint& b) { return a.x < b.x; });
    }

    // Adds a point and returns its index, or -1 when the wave is full.
    int addPoint (float x, float y) noexcept
    {
        if (numPoints >= maxPoints)
            return -1;

        const WavePoint point { juce::jlimit (0.0f, 1.0f, x), juce::jlimit (0.0f, 1.0f, y), 0.0f };
        int index = 0;
        while (index < numPoints && points[(size_t) index].x <= point.x)
            ++index;

        for (int i = numPoints; i > index; --i)
            points[(size_t) i] = points[(size_t) i - 1];
        points[(size_t) index] = point;
        ++numPoints;
        return index;
    }

    // Removes a point, keeping the two the wave needs to be a shape at all.
    void removePoint (int index) noexcept
    {
        if (index < 0 || index >= numPoints || numPoints <= 2)
            return;
        for (int i = index; i + 1 < numPoints; ++i)
            points[(size_t) i] = points[(size_t) i + 1];
        --numPoints;
    }

    juce::String toString() const
    {
        juce::String text;
        for (int i = 0; i < numPoints; ++i)
        {
            const auto& p = points[(size_t) i];
            if (i > 0)
                text << '|';
            text << juce::String (p.x, 4) << ',' << juce::String (p.y, 4)
                 << ',' << juce::String (p.tension, 3);
        }
        return text;
    }

    // Parses "x,y,tension|x,y,tension|…". Anything unreadable — including the
    // empty string a fresh slot has — comes back as the default wave, so the
    // effect and its editor never have to special-case a missing property.
    static ShaperWave fromString (const juce::String& text)
    {
        ShaperWave wave;
        for (const auto& token : juce::StringArray::fromTokens (text, "|", ""))
        {
            if (wave.numPoints >= maxPoints)
                break;
            const auto parts = juce::StringArray::fromTokens (token.trim(), ",", "");
            if (parts.size() < 2)
                continue;
            wave.points[(size_t) wave.numPoints++] =
                { juce::jlimit (0.0f, 1.0f, parts[0].getFloatValue()),
                  juce::jlimit (0.0f, 1.0f, parts[1].getFloatValue()),
                  parts.size() > 2 ? juce::jlimit (-1.0f, 1.0f, parts[2].getFloatValue()) : 0.0f };
        }

        if (wave.numPoints < 2)
            return defaultWave();
        wave.sort();
        return wave;
    }

    // What a fresh Shaper starts on: silence on the beat, curving back up to
    // unity by the middle and holding there until the loop drops it again —
    // the sidechain pump, which is what most people reach for first.
    static ShaperWave defaultWave()
    {
        ShaperWave wave;
        wave.points[0] = { 0.0f,  0.0f, 0.55f };
        wave.points[1] = { 0.45f, 1.0f, 0.0f };
        wave.points[2] = { 1.0f,  1.0f, 0.0f };
        wave.numPoints = 3;
        return wave;
    }

private:
    // Interpolates from point `a` to point `b`, where `b` may be the first
    // point reached by wrapping (its x then reads one cycle later).
    float segment (int a, int b, float p) const noexcept
    {
        const auto& from = points[(size_t) a];
        const auto& to = points[(size_t) b];
        const float endX = b <= a ? to.x + 1.0f : to.x;
        const float span = endX - from.x;
        if (span <= 1.0e-6f)
            return to.y;

        float u = juce::jlimit (0.0f, 1.0f, (p - from.x) / span);
        // tension > 0 moves fast then eases, < 0 the other way round; same
        // exponent as AutomationSnapshot::valueAt.
        u = std::pow (u, (float) std::pow (4.0, -(double) from.tension));
        return from.y + (to.y - from.y) * u;
    }
};
} // namespace fx
