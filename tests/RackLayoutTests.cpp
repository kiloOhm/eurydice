#include <gtest/gtest.h>
#include "app/AppServices.h"
#include "ui/rack/ChannelRackPanel.h"
#include "ui/rack/ChannelRow.h"

namespace
{
juce::Viewport* viewportOf (juce::Component& panel)
{
    for (auto* child : panel.getChildren())
        if (auto* viewport = dynamic_cast<juce::Viewport*> (child))
            return viewport;
    return nullptr;
}

int widthForSteps (int steps)
{
    return ChannelRow::fixedLeftWidth + steps * ChannelRow::stepWidth + 8;
}
}

TEST (RackLayout, LoadedPatternLengthReachesTheRowContainer)
{
    // A loaded 32-step project left the row container at the 16-step
    // fallback width computed before the patterns arrived, clipping the rows
    // until some unrelated event resized it ("resizing the frame still cuts
    // off the content").
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getNonexistentChildFile ("eurytest-rack", ".eury");
    {
        AppServices author (false);
        author.project.getPattern (0).setProperty (ids::lengthTicks,
                                                   32 * ids::ticksPerStep, nullptr);
        ASSERT_TRUE (author.saveProject (file));
    }

    AppServices services (false);
    ChannelRackPanel panel (services);
    panel.setSize (700, 400);   // narrower than 32 steps: the viewport scrolls

    ASSERT_TRUE (services.project.loadFromFile (file));

    auto* viewport = viewportOf (panel);
    ASSERT_NE (viewport, nullptr);
    EXPECT_EQ (viewport->getViewedComponent()->getWidth(), widthForSteps (32))
        << "row container kept the pre-load width";

    file.deleteFile();
}
