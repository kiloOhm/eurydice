#include <gtest/gtest.h>
#include "model/ProjectModel.h"
#include "ui/rack/ChannelRow.h"

// The swing-knob drag preview: odd step cells shift right by swing * half a
// step. Verified at the pixel level so the paint path itself is what's pinned.
namespace
{
// Horizontal centroid of the bright ("on"-cell) pixels inside a band.
float litCentroidX (const juce::Image& img, int xMin, int xMax)
{
    double sum = 0.0, weight = 0.0;
    for (int y = 0; y < img.getHeight(); ++y)
        for (int x = xMin; x < juce::jmin (xMax, img.getWidth()); ++x)
        {
            const auto c = img.getPixelAt (x, y);
            if (c.getBrightness() > 0.5f)
            {
                sum += x;
                weight += 1.0;
            }
        }
    return weight > 0.0 ? (float) (sum / weight) : -1.0f;
}
}

TEST (SwingPreview, OddStepCellShiftsByHalfAStep)
{
    ProjectModel model;
    ChannelRow row (model, model.getChannel (0));
    row.setPattern (model.getPattern (0));
    row.setSize (ChannelRow::fixedLeftWidth + 16 * ChannelRow::stepWidth + 20,
                 ChannelRow::rowHeight);
    row.resized();

    // A single note on step 1 — an odd step, so the preview moves it. The
    // stock beat is cleared so the centroid tracks just this one cell.
    auto lane = model.getOrCreateLane (model.getPattern (0),
                                       model.getChannel (0)[ids::id]);
    for (int i = lane.getNumChildren(); --i >= 0;)
        lane.removeChild (i, nullptr);
    model.addNote (lane, 60, 1 * ids::ticksPerStep, ids::ticksPerStep);

    // Only search the band where cells 0..3 live, well clear of the knobs.
    const int xMin = ChannelRow::fixedLeftWidth;
    const int xMax = xMin + 4 * ChannelRow::stepWidth;

    auto before = row.createComponentSnapshot (row.getLocalBounds());
    const float restingX = litCentroidX (before, xMin, xMax);
    ASSERT_GT (restingX, 0.0f) << "no lit cell found without preview";

    row.setSwingPreview (1.0f);
    auto during = row.createComponentSnapshot (row.getLocalBounds());
    const float swungX = litCentroidX (during, xMin, xMax);
    ASSERT_GT (swungX, 0.0f) << "no lit cell found during preview";

    // Full swing delays an odd step by half a step; the ghost outline drags
    // the centroid a little left of the full 13 px, hence the tolerance.
    const float shift = swungX - restingX;
    EXPECT_GT (shift, ChannelRow::stepWidth * 0.30f)
        << "odd cell did not move under the swing preview";
    EXPECT_LT (shift, ChannelRow::stepWidth * 0.60f);

    // Ending the preview puts the cell back exactly.
    row.setSwingPreview (-1.0f);
    auto after = row.createComponentSnapshot (row.getLocalBounds());
    EXPECT_NEAR (litCentroidX (after, xMin, xMax), restingX, 0.5f);
}

TEST (SwingPreview, EvenStepCellStaysPut)
{
    ProjectModel model;
    ChannelRow row (model, model.getChannel (0));
    row.setPattern (model.getPattern (0));
    row.setSize (ChannelRow::fixedLeftWidth + 16 * ChannelRow::stepWidth + 20,
                 ChannelRow::rowHeight);
    row.resized();

    auto lane = model.getOrCreateLane (model.getPattern (0),
                                       model.getChannel (0)[ids::id]);
    for (int i = lane.getNumChildren(); --i >= 0;)
        lane.removeChild (i, nullptr);
    model.addNote (lane, 60, 0, ids::ticksPerStep);   // step 0: on the beat

    const int xMin = ChannelRow::fixedLeftWidth;
    const int xMax = xMin + 4 * ChannelRow::stepWidth;

    auto before = row.createComponentSnapshot (row.getLocalBounds());
    row.setSwingPreview (1.0f);
    auto during = row.createComponentSnapshot (row.getLocalBounds());

    EXPECT_NEAR (litCentroidX (during, xMin, xMax),
                 litCentroidX (before, xMin, xMax), 0.5f);
}
