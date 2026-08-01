#pragma once

#include <juce_core/juce_core.h>
#include "app/AppServices.h"

// The full control-API surface, callable directly (tests) or via the
// JSON-RPC socket (ControlServer). Runs on the message thread.
class ControlDispatcher
{
public:
    explicit ControlDispatcher (AppServices& s) : services (s) {}

    struct ControlError { juce::String message; };

    // Returns the JSON-RPC "result" value; throws ControlError on failure.
    juce::var dispatch (const juce::String& method, const juce::var& params);

private:
    juce::var channelToVar (const juce::ValueTree&) const;
    juce::ValueTree requireChannel (const juce::var& params);
    juce::ValueTree requirePattern (const juce::var& params);

    AppServices& services;
};
