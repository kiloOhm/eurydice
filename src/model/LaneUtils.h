#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include "Ids.h"

// Whether a lane should be presented as piano-roll content (a note graph in
// the rack, and ghost notes in the piano roll) rather than as on/off steps.
//
// The editors record which one last touched a lane, because the shape of the
// notes alone cannot distinguish "drawn in the piano roll" from "a step whose
// pitch was nudged in the rack graph lane" — both produce a note off the root.
// The heuristic below is only the fallback for lanes that predate the flag.
namespace lanes
{
inline void markEditedWithSteps (juce::ValueTree lane)
{
    if (lane.isValid())
        lane.setProperty (ids::editedWith, "steps", nullptr);
}

inline void markEditedWithPianoRoll (juce::ValueTree lane)
{
    if (lane.isValid())
        lane.setProperty (ids::editedWith, "pianoroll", nullptr);
}

inline bool usesPianoRoll (const juce::ValueTree& lane, int rootNote)
{
    if (lane.hasProperty (ids::editedWith))
        return lane[ids::editedWith].toString() == "pianoroll";

    // Fallback: any note that departs from the plain step grid.
    for (const auto note : lane)
    {
        if (! note.hasType (ids::NOTE))                             continue;
        if ((int) note[ids::startTicks] % ids::ticksPerStep != 0)   return true;
        if ((int) note[ids::lengthTicks] != ids::ticksPerStep)      return true;
        if ((int) note[ids::key] != rootNote)                       return true;
    }
    return false;
}
} // namespace lanes

// Kept as a free function so existing call sites read naturally.
inline bool laneUsesPianoRoll (const juce::ValueTree& lane, int rootNote)
{
    return lanes::usesPianoRoll (lane, rootNote);
}
