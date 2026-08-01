#include <gtest/gtest.h>
#include "engine/EngineSnapshot.h"

namespace
{
AutomationSnapshot makeRamp()
{
    AutomationSnapshot snapshot;
    snapshot.points = { { 0.0, 0.0f, 0.0f }, { 1000.0, 1.0f, 0.0f } };
    return snapshot;
}
}

TEST (AutomationValueAt, EmptyPointsReturnsZero)
{
    AutomationSnapshot snapshot;
    EXPECT_FLOAT_EQ (snapshot.valueAt (123.0), 0.0f);
}

TEST (AutomationValueAt, SinglePointIsConstant)
{
    AutomationSnapshot snapshot;
    snapshot.points = { { 500.0, 0.42f, 0.0f } };
    EXPECT_FLOAT_EQ (snapshot.valueAt (0.0), 0.42f);
    EXPECT_FLOAT_EQ (snapshot.valueAt (500.0), 0.42f);
    EXPECT_FLOAT_EQ (snapshot.valueAt (9999.0), 0.42f);
}

TEST (AutomationValueAt, ClampsBeforeAndAfter)
{
    auto snapshot = makeRamp();
    EXPECT_FLOAT_EQ (snapshot.valueAt (-100.0), 0.0f);
    EXPECT_FLOAT_EQ (snapshot.valueAt (5000.0), 1.0f);
}

TEST (AutomationValueAt, LinearMidpoint)
{
    auto snapshot = makeRamp();
    EXPECT_NEAR (snapshot.valueAt (500.0), 0.5f, 1.0e-4f);
    EXPECT_NEAR (snapshot.valueAt (250.0), 0.25f, 1.0e-4f);
}

TEST (AutomationValueAt, TensionBendsCurve)
{
    auto fast = makeRamp();
    fast.points[0].tension = 1.0f;    // fast rise
    auto slow = makeRamp();
    slow.points[0].tension = -1.0f;   // slow rise

    const float linearMid = makeRamp().valueAt (500.0);
    EXPECT_GT (fast.valueAt (500.0), linearMid + 0.1f);
    EXPECT_LT (slow.valueAt (500.0), linearMid - 0.1f);

    // Endpoints unaffected by tension.
    EXPECT_NEAR (fast.valueAt (0.0), 0.0f, 1.0e-4f);
    EXPECT_NEAR (fast.valueAt (1000.0), 1.0f, 1.0e-4f);
}

TEST (AutomationValueAt, MultiSegment)
{
    AutomationSnapshot snapshot;
    snapshot.points = { { 0.0, 0.0f, 0.0f }, { 100.0, 1.0f, 0.0f }, { 200.0, 0.5f, 0.0f } };
    EXPECT_NEAR (snapshot.valueAt (100.0), 1.0f, 1.0e-4f);
    EXPECT_NEAR (snapshot.valueAt (150.0), 0.75f, 1.0e-4f);
    EXPECT_NEAR (snapshot.valueAt (200.0), 0.5f, 1.0e-4f);
}
