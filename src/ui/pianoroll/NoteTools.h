#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>
#include "model/Ids.h"
#include "model/LaneUtils.h"

// Note-editing maths for the piano roll: rolls/ratchets, chop, glue, strum.
// Pure functions over a plain note struct, so they can be tested without a UI.
namespace notetools
{
struct Note
{
    int key = 60;
    int startTicks = 0;
    int lengthTicks = ids::ticksPerStep;
    double velocity = 0.78;
    double pan = 0.0;
};

// Notes lifted out of a roll, waiting to be dropped somewhere else. Starts are
// relative to the earliest note, so pasting only has to decide where the block
// lands — in any lane, in any pattern.
struct Clipboard
{
    std::vector<Note> notes;
    int spanTicks = 0;     // earliest start -> latest end
    int originTicks = 0;   // where the block last sat, for keyboard pastes

    bool isEmpty() const { return notes.empty(); }
};

// Velocity shape across a roll. Ratchets need the ramp to read as one gesture.
enum class Ramp { flat, rising, falling };

inline constexpr double minVelocity = 0.01;

// How many pieces a note of `lengthTicks` splits into at `divisionTicks`.
// Anything shorter than one division stays whole.
inline int rollCount (int lengthTicks, int divisionTicks)
{
    if (divisionTicks <= 0 || lengthTicks <= 0)
        return 1;
    return std::max (1, lengthTicks / divisionTicks);
}

// Velocity of piece `index` of `count`, scaled from `base` by `depth` (0..1):
// rising starts at base * (1 - depth) and ends at base, falling is reversed.
inline double rampVelocity (double base, int index, int count, Ramp ramp, double depth)
{
    if (ramp == Ramp::flat || count < 2)
        return std::clamp (base, minVelocity, 1.0);

    const double d = std::clamp (depth, 0.0, 1.0);
    const double position = (double) index / (double) (count - 1);
    const double factor = ramp == Ramp::rising ? (1.0 - d) + d * position
                                               : 1.0 - d * position;
    return std::clamp (base * factor, minVelocity, 1.0);
}

// Replaces one note with `rollCount` evenly spaced pieces filling its original
// span. Piece boundaries are computed from the span so rounding never leaves a
// gap or overshoots the end.
inline std::vector<Note> roll (const Note& source, int divisionTicks, Ramp ramp, double depth = 0.5)
{
    const int count = rollCount (source.lengthTicks, divisionTicks);
    if (count < 2)
        return { source };

    std::vector<Note> result;
    result.reserve ((size_t) count);

    const auto span = (std::int64_t) source.lengthTicks;
    for (int i = 0; i < count; ++i)
    {
        const auto from = (int) (span * i / count);
        const auto to   = (int) (span * (i + 1) / count);

        Note piece = source;
        piece.startTicks = source.startTicks + from;
        piece.lengthTicks = to - from;
        piece.velocity = rampVelocity (source.velocity, i, count, ramp, depth);
        result.push_back (piece);
    }
    return result;
}

inline std::vector<Note> rollAll (const std::vector<Note>& notes, int divisionTicks,
                                  Ramp ramp, double depth = 0.5)
{
    std::vector<Note> result;
    for (const auto& note : notes)
    {
        auto pieces = roll (note, divisionTicks, ramp, depth);
        result.insert (result.end(), pieces.begin(), pieces.end());
    }
    return result;
}

// Splits a note at the absolute grid lines of `divisionTicks` that fall inside
// it, so pieces line up with the grid even when the note starts off-grid.
inline std::vector<Note> chop (const Note& source, int divisionTicks)
{
    if (divisionTicks <= 0 || source.lengthTicks <= 0)
        return { source };

    const int end = source.startTicks + source.lengthTicks;
    std::vector<Note> result;

    int from = source.startTicks;
    while (from < end)
    {
        const int nextLine = (from / divisionTicks + 1) * divisionTicks;
        const int to = std::min (nextLine, end);

        Note piece = source;
        piece.startTicks = from;
        piece.lengthTicks = to - from;
        result.push_back (piece);
        from = to;
    }
    return result;
}

inline std::vector<Note> chopAll (const std::vector<Note>& notes, int divisionTicks)
{
    std::vector<Note> result;
    for (const auto& note : notes)
    {
        auto pieces = chop (note, divisionTicks);
        result.insert (result.end(), pieces.begin(), pieces.end());
    }
    return result;
}

// Merges notes that touch or overlap on the same key into one note spanning
// them; the earliest note of each run supplies velocity and pan. Notes on
// different keys, and runs separated by more than `gapTicks`, stay apart.
// Output is ordered by key, then start.
inline std::vector<Note> glue (std::vector<Note> notes, int gapTicks = 0)
{
    std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b)
    {
        if (a.key != b.key)
            return a.key < b.key;
        if (a.startTicks != b.startTicks)
            return a.startTicks < b.startTicks;
        return a.lengthTicks < b.lengthTicks;
    });

    std::vector<Note> result;
    for (const auto& note : notes)
    {
        if (! result.empty())
        {
            auto& run = result.back();
            const int runEnd = run.startTicks + run.lengthTicks;
            if (run.key == note.key && note.startTicks <= runEnd + gapTicks)
            {
                const int end = std::max (runEnd, note.startTicks + note.lengthTicks);
                run.lengthTicks = end - run.startTicks;
                continue;
            }
        }
        result.push_back (note);
    }
    return result;
}

// Offsets notes progressively for chord strums and humanising: the earliest,
// lowest note stays put and each following one shifts by another `offsetTicks`.
// Input order is preserved so callers can map results back onto their notes.
inline std::vector<Note> strum (std::vector<Note> notes, int offsetTicks)
{
    std::vector<size_t> order (notes.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;

    std::stable_sort (order.begin(), order.end(), [&notes] (size_t a, size_t b)
    {
        if (notes[a].startTicks != notes[b].startTicks)
            return notes[a].startTicks < notes[b].startTicks;
        return notes[a].key < notes[b].key;
    });

    for (size_t step = 0; step < order.size(); ++step)
    {
        auto& note = notes[order[step]];
        note.startTicks = std::max (0, note.startTicks + (int) step * offsetTicks);
    }
    return notes;
}

// Re-exported from the model layer so piano-roll code has one obvious home
// for it; the single definition lives in model/LaneUtils.h.
using ::laneUsesPianoRoll;
} // namespace notetools
