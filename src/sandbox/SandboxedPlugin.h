#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SharedAudioRing.h"
#include "effects/Effect.h"

// DAW-side handle to a plugin running in an EurydiceHelper process.
//
// The audio thread only ever touches the shared-memory ring (copy in, copy
// out, one sem_post) — it never blocks on the child. If the child stalls or
// dies, process() outputs silence and the supervisor (message-thread calls to
// isAlive()) reports the death so the UI can offer a restart. A crashing
// plugin therefore costs its own sound, not the DAW.
//
// Control (load/state/editor) runs over the child's stdin/stdout as JSON
// lines, message thread only, with bounded waits.
class SandboxedPlugin : public Effect
{
public:
    SandboxedPlugin() = default;
    ~SandboxedPlugin() override;

    // Message thread. Spawns the helper and loads the plugin (empty pluginId
    // with testGain=true runs the helper's gain mode — used by tests).
    // Returns false if the helper could not start or the plugin failed to
    // load; `error` then says why.
    bool launch (const juce::String& pluginId, double sampleRate, int blockSize,
                 const juce::String& initialStateBase64, juce::String& error,
                 bool testGain = false);

    // Where the helper binary lives. Checks EURYDICE_HELPER_PATH, then next
    // to the running executable, then the standard build location.
    static juce::File findHelperBinary();

    // ---- Effect (audio thread) ----
    void prepare (double sampleRate, int maxBlockSize) override;
    void process (juce::AudioBuffer<float>& stereoBus, int numSamples, const Context& context) override;
    void reset() override {}

    // RT-safe: queues a normalised parameter change for the child's next block.
    void setParameter (int parameterIndex, float normalisedValue);

    // ---- supervision / control (message thread) ----
    bool isAlive();
    juce::int64 overrunCount() const { return overruns.load (std::memory_order_relaxed); }
    juce::String getStateBase64();
    void setStateFromBase64 (const juce::String& base64);
    void showEditor (const juce::String& title);
    juce::String getName() const { return pluginName; }

    // Parameter names reported by the child at load (first 128); the
    // automation menu lists these and setParameter targets their indices.
    const juce::StringArray& getParamNames() const { return paramNames; }

    // Re-prepares the child after a device/sample-rate change.
    void prepareChild (double sampleRate, int blockSize);

    void shutdown();   // polite quit, then SIGKILL after a grace period

    // Tests need the pid to simulate a plugin crash (SIGKILL the helper).
    pid_t getChildPid() const { return childPid; }

private:
    juce::var sendCommand (const juce::var& command, int timeoutMs);
    bool writeLine (const juce::String& line);
    juce::String readLine (int timeoutMs);

    sandbox::SharedAudioRing ring;
    juce::String pluginName;
    juce::StringArray paramNames;
    pid_t childPid = -1;
    int toChildFd = -1;     // we write commands here
    int fromChildFd = -1;   // we read replies here
    juce::String readBuffer;

    juce::int64 seq = 0;                      // audio thread only
    std::atomic<juce::int64> overruns { 0 };
    std::atomic<bool> childGone { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SandboxedPlugin)
};
