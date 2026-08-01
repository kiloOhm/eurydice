#include <gtest/gtest.h>
#include <thread>
#include "control/ControlServer.h"
#include "TestHelpers.h"

// Exercises the real TCP framing layer: connect, send JSON-RPC lines
// (including malformed ones), read framed responses.
namespace
{
constexpr int kTestPort = 44777;

struct SocketFixture : ::testing::Test
{
    SocketFixture()
    {
        setenv ("EURYDICE_CONTROL_PORT", std::to_string (kTestPort).c_str(), 1);
        server = std::make_unique<ControlServer> (services);
    }

    ~SocketFixture() override
    {
        unsetenv ("EURYDICE_CONTROL_PORT");
    }

    // Runs the client I/O on a worker thread while pumping the message loop
    // (the server dispatches onto the message thread).
    juce::StringArray roundTrip (const juce::StringArray& requests)
    {
        juce::StringArray responses;
        std::atomic<bool> done { false };

        std::thread client ([&]
        {
            juce::StreamingSocket socket;
            if (! socket.connect ("127.0.0.1", kTestPort, 3000))
            {
                done = true;
                return;
            }
            for (const auto& request : requests)
            {
                const auto line = request + "\n";
                socket.write (line.toRawUTF8(), (int) line.getNumBytesAsUTF8());

                juce::String received;
                char c = 0;
                while (received.length() < 1 << 20)
                {
                    if (socket.waitUntilReady (true, 5000) <= 0)
                        break;
                    if (socket.read (&c, 1, true) != 1 || c == '\n')
                        break;
                    received += juce::String::charToString ((juce::juce_wchar) (juce::uint8) c);
                }
                responses.add (received);
            }
            done = true;
        });

        while (! done)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
        client.join();
        return responses;
    }

    AppServices services { false };
    std::unique_ptr<ControlServer> server;
};
}

TEST_F (SocketFixture, PingOverSocket)
{
    auto responses = roundTrip ({ R"({"jsonrpc":"2.0","id":7,"method":"ping"})" });
    ASSERT_EQ (responses.size(), 1);
    const auto parsed = juce::JSON::parse (responses[0]);
    EXPECT_EQ (parsed["result"].toString(), "pong");
    EXPECT_EQ ((int) parsed["id"], 7);
}

TEST_F (SocketFixture, MalformedAndErrorRequests)
{
    auto responses = roundTrip ({
        "this is not json",
        R"({"jsonrpc":"2.0","id":1})",
        R"({"jsonrpc":"2.0","id":2,"method":"no.such.method"})",
    });
    ASSERT_EQ (responses.size(), 3);
    EXPECT_EQ ((int) juce::JSON::parse (responses[0])["error"]["code"], -32700);
    EXPECT_EQ ((int) juce::JSON::parse (responses[1])["error"]["code"], -32600);
    EXPECT_EQ ((int) juce::JSON::parse (responses[2])["error"]["code"], -32001);
}

TEST_F (SocketFixture, StatefulSequenceOverSocket)
{
    auto responses = roundTrip ({
        R"({"jsonrpc":"2.0","id":1,"method":"transport.set","params":{"tempo":99}})",
        R"({"jsonrpc":"2.0","id":2,"method":"state.get"})",
    });
    ASSERT_EQ (responses.size(), 2);
    EXPECT_DOUBLE_EQ ((double) juce::JSON::parse (responses[1])["result"]["tempo"], 99.0);
}
