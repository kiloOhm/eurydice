#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include "Ids.h"

// A lane holds piano-roll content once any note departs from the step grid:
// an off-grid start, a non-step length, or a pitch other than the channel root.
inline bool laneUsesPianoRoll (const juce::ValueTree& lane, int rootNote)
{
    for (const auto note : lane)
    {
        if (! note.hasType (ids::NOTE))                             continue;
        if ((int) note[ids::startTicks] % ids::ticksPerStep != 0)   return true;
        if ((int) note[ids::lengthTicks] != ids::ticksPerStep)      return true;
        if ((int) note[ids::key] != rootNote)                       return true;
    }
    return false;
}
