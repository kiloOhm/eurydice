#include <gtest/gtest.h>
#include "ui/rack/RackReorder.h"

using rackreorder::dropIndexForY;
using rackreorder::indicatorYForDrop;

namespace
{
constexpr int rowHeight = 30;
constexpr int pitch = rowHeight + 2;
}

TEST (RackReorder, DropIndexTargetsRowUnderCursor)
{
    EXPECT_EQ (dropIndexForY (0, 4, pitch), 0);
    EXPECT_EQ (dropIndexForY (pitch - 1, 4, pitch), 0);
    EXPECT_EQ (dropIndexForY (pitch, 4, pitch), 1);
    EXPECT_EQ (dropIndexForY (2 * pitch + 5, 4, pitch), 2);
}

TEST (RackReorder, DropIndexClampsPastTheEnds)
{
    EXPECT_EQ (dropIndexForY (-1, 4, pitch), 0);
    EXPECT_EQ (dropIndexForY (-500, 4, pitch), 0);
    EXPECT_EQ (dropIndexForY (4 * pitch, 4, pitch), 3);
    EXPECT_EQ (dropIndexForY (9999, 4, pitch), 3);
}

TEST (RackReorder, DropIndexToleratesDegenerateInput)
{
    EXPECT_EQ (dropIndexForY (100, 0, pitch), 0);
    EXPECT_EQ (dropIndexForY (100, 4, 0), 0);
}

// moveChild semantics: dragging up inserts above the target row, dragging
// down inserts below it, so the indicator sits where the row will land.
TEST (RackReorder, IndicatorSitsAboveWhenMovingUpBelowWhenMovingDown)
{
    EXPECT_EQ (indicatorYForDrop (0, 2, pitch, rowHeight), 0);
    EXPECT_EQ (indicatorYForDrop (1, 2, pitch, rowHeight), pitch);
    EXPECT_EQ (indicatorYForDrop (3, 2, pitch, rowHeight), 3 * pitch + rowHeight);
    EXPECT_EQ (indicatorYForDrop (2, 2, pitch, rowHeight), 2 * pitch);   // no-op drop
}
