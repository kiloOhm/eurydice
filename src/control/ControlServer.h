#pragma once

#include <juce_core/juce_core.h>
#include "ControlDispatcher.h"

// Embedded control API: newline-delimited JSON-RPC 2.0 over a localhost TCP
// socket (default port 44890, override with EURYDICE_CONTROL_PORT). Every
// request is dispatched onto the message thread via ControlDispatcher.
// The stdio MCP server in mcp/ bridges AI clients to this.
class ControlServer : private juce::Thread
{
public:
    explicit ControlServer (AppServices&);
    ~ControlServer() override;

    int getPort() const { return port; }

private:
    void run() override;
    juce::String handleLine (const juce::String& line);

    AppServices& services;
    ControlDispatcher dispatcher { services };
    juce::StreamingSocket listener;
    int port = 44890;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlServer)
};
