#include "SandboxedPlugin.h"

#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>

extern char** environ;

SandboxedPlugin::~SandboxedPlugin()
{
    shutdown();
}

juce::File SandboxedPlugin::findHelperBinary()
{
    const auto override = juce::SystemStats::getEnvironmentVariable ("EURYDICE_HELPER_PATH", "");
    if (override.isNotEmpty() && juce::File (override).existsAsFile())
        return { override };

    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);

    // Installed layout: Eurydice.app/Contents/Helpers/EurydiceHelper.app/...
    auto installed = exe.getParentDirectory().getSiblingFile ("Helpers")
                        .getChildFile ("EurydiceHelper.app/Contents/MacOS/EurydiceHelper");
    if (installed.existsAsFile())
        return installed;

    // Dev layout: both artefact dirs sit under the same build directory.
    for (auto dir = exe.getParentDirectory(); dir != juce::File(); dir = dir.getParentDirectory())
    {
        auto candidate = dir.getChildFile ("EurydiceHelper_artefacts");
        if (candidate.isDirectory())
        {
            auto matches = candidate.findChildFiles (juce::File::findFiles, true, "EurydiceHelper");
            for (const auto& match : matches)
                if (match.getParentDirectory().getFileName() == "MacOS")
                    return match;
        }
    }
    return {};
}

bool SandboxedPlugin::launch (const juce::String& pluginId, double sampleRate, int blockSize,
                              const juce::String& initialStateBase64, juce::String& error,
                              bool testGain)
{
    const auto helper = findHelperBinary();
    if (! helper.existsAsFile())
    {
        error = "EurydiceHelper binary not found";
        return false;
    }

    // Short unique ring name (macOS caps POSIX IPC names at 31 chars).
    const auto ringName = "eur" + juce::String::toHexString ((juce::int64) juce::Random::getSystemRandom().nextInt64())
                                      .removeCharacters ("-").substring (0, 12);
    if (! ring.create (ringName))
    {
        error = "could not create shared memory ring";
        return false;
    }

    int inPipe[2] = { -1, -1 }, outPipe[2] = { -1, -1 };   // [read, write]
    if (::pipe (inPipe) != 0 || ::pipe (outPipe) != 0)
    {
        error = "pipe() failed";
        return false;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init (&actions);
    posix_spawn_file_actions_adddup2 (&actions, inPipe[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2 (&actions, outPipe[1], STDOUT_FILENO);
    for (int fd : { inPipe[0], inPipe[1], outPipe[0], outPipe[1] })
        posix_spawn_file_actions_addclose (&actions, fd);

    const auto helperPath = helper.getFullPathName().toStdString();
    const auto ringArg = ringName.toStdString();
    std::vector<char*> argv;
    argv.push_back (const_cast<char*> (helperPath.c_str()));
    argv.push_back (const_cast<char*> ("--ring"));
    argv.push_back (const_cast<char*> (ringArg.c_str()));
    if (testGain)
        argv.push_back (const_cast<char*> ("--test-gain"));
    argv.push_back (nullptr);

    const int rc = ::posix_spawn (&childPid, helperPath.c_str(), &actions, nullptr,
                                  argv.data(), environ);
    posix_spawn_file_actions_destroy (&actions);

    ::close (inPipe[0]);
    ::close (outPipe[1]);
    toChildFd = inPipe[1];
    fromChildFd = outPipe[0];

    if (rc != 0)
    {
        error = "posix_spawn failed: " + juce::String (strerror (rc));
        childPid = -1;
        return false;
    }

    // Handshake: the helper prints a ready event once the ring is mapped.
    const auto ready = juce::JSON::parse (readLine (10000));
    if (! (bool) ready["ok"])
    {
        error = "helper did not become ready";
        shutdown();
        return false;
    }

    auto* load = new juce::DynamicObject();
    load->setProperty ("cmd", "load");
    load->setProperty ("pluginId", pluginId);
    load->setProperty ("sampleRate", sampleRate);
    load->setProperty ("blockSize", blockSize);
    const auto loaded = sendCommand (juce::var (load), 30000);   // first AU load can be slow
    if (! (bool) loaded["ok"])
    {
        error = "plugin load failed: " + loaded["error"].toString();
        shutdown();
        return false;
    }
    pluginName = loaded["name"].toString();

    if (initialStateBase64.isNotEmpty())
        setStateFromBase64 (initialStateBase64);

    if (testGain)
    {
        auto* prep = new juce::DynamicObject();
        prep->setProperty ("cmd", "prepare");
        prep->setProperty ("sampleRate", sampleRate);
        prep->setProperty ("blockSize", blockSize);
        sendCommand (juce::var (prep), 5000);
    }
    return true;
}

// ---------------- Effect (audio thread) ----------------

void SandboxedPlugin::prepare (double, int)
{
    // The helper prepared at launch/load; per-block lengths ride the ring, so
    // nothing to renegotiate here without blocking the caller.
}

void SandboxedPlugin::process (juce::AudioBuffer<float>& stereoBus, int numSamples, const Context&)
{
    if (! ring.isOpen() || childGone.load (std::memory_order_relaxed))
    {
        stereoBus.clear (0, numSamples);
        return;
    }

    const int n = juce::jmin (numSamples, sandbox::SharedAudioRing::maxBlock);
    const float* ins[2] = { stereoBus.getReadPointer (0),
                            stereoBus.getNumChannels() > 1 ? stereoBus.getReadPointer (1)
                                                           : stereoBus.getReadPointer (0) };
    ring.publishInput (seq, ins, 2, n);

    // One block behind by design: read the previous block's result.
    float* outs[2] = { stereoBus.getWritePointer (0),
                       stereoBus.getWritePointer (juce::jmin (1, stereoBus.getNumChannels() - 1)) };
    if (! ring.readOutput (seq - 1, outs, juce::jmin (2, stereoBus.getNumChannels()), n))
    {
        stereoBus.clear (0, n);
        if (seq > 0)
            overruns.fetch_add (1, std::memory_order_relaxed);
    }
    ++seq;
}

void SandboxedPlugin::setParameter (int parameterIndex, float normalisedValue)
{
    if (! ring.isOpen())
        return;
    auto* head = ring.header();
    const int slot = head->paramCount.load (std::memory_order_relaxed);
    if (slot >= sandbox::RingHeader::maxParamEvents)
        return;   // burst overflow: drop rather than block
    head->paramEvents[slot] = { parameterIndex, normalisedValue };
    head->paramCount.store (slot + 1, std::memory_order_release);
}

// ---------------- supervision / control ----------------

bool SandboxedPlugin::isAlive()
{
    if (childPid <= 0 || childGone.load())
        return false;

    int status = 0;
    const auto reaped = ::waitpid (childPid, &status, WNOHANG);
    if (reaped == childPid || (reaped < 0 && errno == ECHILD))
    {
        childGone.store (true);
        return false;
    }
    return true;
}

juce::String SandboxedPlugin::getStateBase64()
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("cmd", "getState");
    return sendCommand (juce::var (obj), 5000)["state"].toString();
}

void SandboxedPlugin::setStateFromBase64 (const juce::String& base64)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("cmd", "setState");
    obj->setProperty ("state", base64);
    sendCommand (juce::var (obj), 5000);
}

void SandboxedPlugin::showEditor (const juce::String& title)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("cmd", "editor");
    obj->setProperty ("title", title);
    sendCommand (juce::var (obj), 5000);
}

void SandboxedPlugin::shutdown()
{
    if (childPid > 0 && ! childGone.load())
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("cmd", "quit");
        writeLine (juce::JSON::toString (juce::var (obj), true));

        // Grace period, then force. waitpid also reaps the zombie.
        for (int i = 0; i < 50; ++i)
        {
            int status = 0;
            if (::waitpid (childPid, &status, WNOHANG) == childPid)
            {
                childPid = -1;
                break;
            }
            juce::Thread::sleep (10);
        }
        if (childPid > 0)
        {
            ::kill (childPid, SIGKILL);
            int status = 0;
            ::waitpid (childPid, &status, 0);
            childPid = -1;
        }
    }
    childGone.store (true);

    for (int* fd : { &toChildFd, &fromChildFd })
        if (*fd >= 0)
        {
            ::close (*fd);
            *fd = -1;
        }
    ring.close();
}

// ---------------- pipe plumbing ----------------

bool SandboxedPlugin::writeLine (const juce::String& line)
{
    if (toChildFd < 0)
        return false;
    const auto data = line + "\n";
    const auto* bytes = data.toRawUTF8();
    size_t remaining = strlen (bytes);
    while (remaining > 0)
    {
        const auto written = ::write (toChildFd, bytes, remaining);
        if (written <= 0)
            return false;
        bytes += written;
        remaining -= (size_t) written;
    }
    return true;
}

juce::String SandboxedPlugin::readLine (int timeoutMs)
{
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (true)
    {
        if (const int nl = readBuffer.indexOfChar ('\n'); nl >= 0)
        {
            const auto line = readBuffer.substring (0, nl);
            readBuffer = readBuffer.substring (nl + 1);
            return line;
        }

        const auto now = juce::Time::getMillisecondCounter();
        if (now >= deadline || fromChildFd < 0)
            return {};

        struct pollfd pfd { fromChildFd, POLLIN, 0 };
        if (::poll (&pfd, 1, (int) (deadline - now)) <= 0)
            return {};
        if ((pfd.revents & POLLIN) == 0)
            return {};   // hangup: child died

        char chunk[4096];
        const auto got = ::read (fromChildFd, chunk, sizeof (chunk));
        if (got <= 0)
            return {};
        readBuffer += juce::String::fromUTF8 (chunk, (int) got);
    }
}

juce::var SandboxedPlugin::sendCommand (const juce::var& command, int timeoutMs)
{
    if (! writeLine (juce::JSON::toString (command, true)))
        return {};
    return juce::JSON::parse (readLine (timeoutMs));
}
