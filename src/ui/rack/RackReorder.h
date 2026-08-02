#pragma once

#include <juce_core/juce_core.h>

// Pure geometry for the channel-rack drag-reorder, kept out of the components
// so it can be unit-tested (same idea as model/LaneUtils.h).
namespace rackreorder
{
// Maps a drag position to the index the dragged row should land on: the row
// under the cursor is the drop target, positions past either end clamp. Each
// row occupies rowPitch px starting at index * rowPitch.
inline int dropIndexForY (int yInContainer, int numRows, int rowPitch)
{
    if (numRows <= 0 || rowPitch <= 0)
        return 0;
    return juce::jlimit (0, numRows - 1, yInContainer / rowPitch);
}

// Y of the insertion indicator line. ValueTree::moveChild removes then
// re-inserts, so a row dragged down lands *below* the target row and a row
// dragged up lands above it — the line matches where the row will end up.
inline int indicatorYForDrop (int dropIndex, int sourceIndex, int rowPitch, int rowHeight)
{
    return dropIndex <= sourceIndex ? dropIndex * rowPitch
                                    : dropIndex * rowPitch + rowHeight;
}
} // namespace rackreorder
