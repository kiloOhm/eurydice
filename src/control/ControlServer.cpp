#include "ControlServer.h"

namespace
{
juce::var getOr (const juce::var& params, const char* key, const juce::var& fallback)
{
    if (auto* obj = params.getDynamicObject())
        if (obj->hasProperty (key))
            return obj->getProperty (key);
    return fallback;
}

juce::var makeObj (std::initializer_list<std::pair<juce::String, juce::var>> fields)
{
    auto* obj = new juce::DynamicObject();
    for (auto& [k, v] : fields)
        obj->setProperty (k, v);
    return juce::var (obj);
}
}

ControlServer::ControlServer (AppServices& s)
    : juce::Thread ("ControlServer"), services (s)
{
    const auto envPort = juce::SystemStats::getEnvironmentVariable ("EURYDICE_CONTROL_PORT", "");
    if (envPort.isNotEmpty())
        port = envPort.getIntValue();

    if (listener.createListener (port, "127.0.0.1"))
    {
        std::cout << "CONTROL_LISTENING " << port << "\n" << std::flush;
        startThread();
    }
    else
        std::cout << "CONTROL_BIND_FAILED " << port << "\n" << std::flush;
}

ControlServer::~ControlServer()
{
    signalThreadShouldExit();
    listener.close();
    stopThread (4000);
}

void ControlServer::run()
{
    while (! threadShouldExit())
    {
        std::unique_ptr<juce::StreamingSocket> client (listener.waitForNextConnection());
        if (client == nullptr)
            continue;

        juce::MemoryBlock buffer;
        char chunk[8192];

        while (! threadShouldExit() && client->isConnected())
        {
            const int ready = client->waitUntilReady (true, 250);
            if (ready < 0)
                break;
            if (ready == 0)
                continue;

            const int n = client->read (chunk, sizeof (chunk), false);
            if (n <= 0)
                break;
            buffer.append (chunk, (size_t) n);

            // Process complete lines.
            for (;;)
            {
                auto* data = static_cast<const char*> (buffer.getData());
                const auto size = buffer.getSize();
                const auto* nl = static_cast<const char*> (memchr (data, '\n', size));
                if (nl == nullptr)
                    break;

                const auto lineLen = (size_t) (nl - data);
                const juce::String line = juce::String::fromUTF8 (data, (int) lineLen);
                buffer.removeSection (0, lineLen + 1);

                if (line.trim().isEmpty())
                    continue;

                juce::String response;
                juce::WaitableEvent done;
                juce::MessageManager::callAsync ([this, line, &response, &done]
                {
                    response = handleLine (line);
                    done.signal();
                });
                if (! done.wait (180000))   // long enough for offline renders
                    response = R"({"jsonrpc":"2.0","id":null,"error":{"code":-32000,"message":"timeout"}})";

                response += "\n";
                client->write (response.toRawUTF8(), (int) response.getNumBytesAsUTF8());
            }
        }
    }
}

juce::String ControlServer::handleLine (const juce::String& line)
{
    juce::var request = juce::JSON::parse (line);
    const juce::var id = getOr (request, "id", {});

    auto errorResponse = [&id] (int code, const juce::String& message)
    {
        return juce::JSON::toString (makeObj ({ { "jsonrpc", "2.0" }, { "id", id },
            { "error", makeObj ({ { "code", code }, { "message", message } }) } }), true);
    };

    if (! request.isObject())
        return errorResponse (-32700, "parse error");

    const auto method = getOr (request, "method", "").toString();
    if (method.isEmpty())
        return errorResponse (-32600, "missing method");

    try
    {
        const juce::var result = dispatcher.dispatch (method, getOr (request, "params", {}));
        return juce::JSON::toString (makeObj ({ { "jsonrpc", "2.0" }, { "id", id },
                                                { "result", result } }), true);
    }
    catch (const ControlDispatcher::ControlError& e)
    {
        return errorResponse (-32001, e.message);
    }
}

