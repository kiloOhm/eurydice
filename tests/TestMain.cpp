#include <gtest/gtest.h>
#include <juce_events/juce_events.h>

int main (int argc, char** argv)
{
    // Makes this thread the JUCE message thread; required by the model/engine
    // sync layers, which assert message-thread access.
    juce::ScopedJuceInitialiser_GUI juceInit;

    ::testing::InitGoogleTest (&argc, argv);
    return RUN_ALL_TESTS();
}
