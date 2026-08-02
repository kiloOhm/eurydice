#include <gtest/gtest.h>
#include <juce_events/juce_events.h>
#include <thread>
#include <signal.h>
#include "sandbox/SharedAudioRing.h"
#include "sandbox/SandboxedPlugin.h"
#include "plugins/PluginManager.h"

// The ring itself, in-process: a "child" thread plays the helper's role over
// the same mapping the "DAW" side created.
TEST (SandboxRing, GainRoundTripsWithOneBlockLatency)
{
    sandbox::SharedAudioRing daw, child;
    ASSERT_TRUE (daw.create ("eurtest1"));
    ASSERT_TRUE (child.open ("eurtest1"));

    std::atomic<bool> run { true };
    std::thread worker ([&child, &run]
    {
        while (run.load())
        {
            const auto seq = child.waitForInput();
            if (! run.load() || seq < 0)
                continue;
            const int n = child.header()->inputLen[seq & 1].load();
            for (int ch = 0; ch < sandbox::RingHeader::maxChannels; ++ch)
            {
                const float* in = child.inputSlot (seq, ch);
                float* out = child.outputSlot (seq, ch);
                for (int i = 0; i < n; ++i)
                    out[i] = in[i] * 0.5f;
            }
            child.publishOutput (seq);
        }
    });

    constexpr int blockSize = 256;
    daw.header()->blockSize.store (blockSize);
    std::vector<float> left (blockSize), right (blockSize), outL (blockSize), outR (blockSize);

    for (juce::int64 seq = 0; seq < 16; ++seq)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            left[(size_t) i]  = (float) seq + (float) i * 0.001f;
            right[(size_t) i] = -left[(size_t) i];
        }
        const float* ins[2] = { left.data(), right.data() };
        daw.publishInput (seq, ins, 2, blockSize);

        // Give the worker a moment, as the real engine's block cadence would.
        for (int spin = 0; spin < 1000 && daw.header()->outputSeq.load() < seq; ++spin)
            std::this_thread::sleep_for (std::chrono::microseconds (50));

        float* outs[2] = { outL.data(), outR.data() };
        ASSERT_TRUE (daw.readOutput (seq, outs, 2, blockSize)) << "seq " << seq;
        EXPECT_FLOAT_EQ (outL[0], ((float) seq) * 0.5f);
        EXPECT_FLOAT_EQ (outR[10], -((float) seq + 0.010f) * 0.5f);
    }

    // No child response yet for a future block: readOutput refuses.
    float* outs[2] = { outL.data(), outR.data() };
    EXPECT_FALSE (daw.readOutput (99, outs, 2, blockSize));

    run.store (false);
    daw.kick();
    worker.join();
}

namespace
{
// The full pipeline against the real helper binary in --test-gain mode.
struct HelperFixture
{
    SandboxedPlugin plugin;
    bool available = false;
    juce::String error;

    HelperFixture()
    {
        if (! SandboxedPlugin::findHelperBinary().existsAsFile())
            return;
        available = plugin.launch ({}, 44100.0, 256, {}, error, true);
    }
};

float processBlocks (SandboxedPlugin& plugin, float sampleValue, int blocks, int blockSize = 256)
{
    juce::AudioBuffer<float> bus (2, blockSize);
    float lastFirstSample = 0.0f;
    for (int b = 0; b < blocks; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (bus.getWritePointer (ch), sampleValue, blockSize);
        plugin.process (bus, blockSize, {});
        lastFirstSample = bus.getSample (0, 0);
        // Real engine blocks arrive at audio-callback pace; give the child a
        // beat so this test measures behaviour, not raw scheduling luck.
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }
    return lastFirstSample;
}
}

TEST (SandboxHelper, StreamsAudioThroughTheChildProcess)
{
    HelperFixture fx;
    if (! fx.available)
        GTEST_SKIP() << "helper unavailable: " << fx.error;

    // After a few blocks of steady input the (one-block-late) output must be
    // the child's half-gain result.
    const float out = processBlocks (fx.plugin, 0.8f, 8);
    EXPECT_NEAR (out, 0.4f, 1.0e-6f);
    EXPECT_TRUE (fx.plugin.isAlive());
    fx.plugin.shutdown();
}

TEST (SandboxHelper, SurvivesChildCrashWithSilence)
{
    HelperFixture fx;
    if (! fx.available)
        GTEST_SKIP() << "helper unavailable: " << fx.error;

    ASSERT_NEAR (processBlocks (fx.plugin, 0.8f, 8), 0.4f, 1.0e-6f);

    // Simulate the plugin crashing: kill the helper outright.
    ASSERT_TRUE (fx.plugin.isAlive());
    ASSERT_GT (fx.plugin.getChildPid(), 0);
    ::kill (fx.plugin.getChildPid(), SIGKILL);

    // The DAW side must keep running and produce silence, never hang.
    const float out = processBlocks (fx.plugin, 0.8f, 20);
    EXPECT_FLOAT_EQ (out, 0.0f);
    EXPECT_FALSE (fx.plugin.isAlive());
    fx.plugin.shutdown();   // safe on a dead child
}

TEST (SandboxHelper, HostsARealPluginOutOfProcess)
{
    if (! SandboxedPlugin::findHelperBinary().existsAsFile())
        GTEST_SKIP() << "helper unavailable";

    // Find an AU delay in the shared plugin database; skip cleanly without one.
    PluginManager manager;
    juce::String delayId;
    for (const auto& d : manager.getEffects())
        if (d.name.containsIgnoreCase ("AUDelay"))
            delayId = d.createIdentifierString();
    if (delayId.isEmpty())
        GTEST_SKIP() << "plugin database empty — run a scan from the app first";

    SandboxedPlugin plugin;
    juce::String error;
    ASSERT_TRUE (plugin.launch (delayId, 44100.0, 256, {}, error)) << error;
    EXPECT_TRUE (plugin.getName().containsIgnoreCase ("AUDelay"));

    // An impulse through a delay must come back non-silent and finite.
    juce::AudioBuffer<float> bus (2, 256);
    float peak = 0.0f;
    for (int b = 0; b < 40; ++b)
    {
        bus.clear();
        if (b == 0)
        {
            bus.setSample (0, 0, 1.0f);
            bus.setSample (1, 0, 1.0f);
        }
        plugin.process (bus, 256, {});
        peak = juce::jmax (peak, bus.getMagnitude (0, 0, 256));
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }
    EXPECT_GT (peak, 1.0e-4f) << "the delayed impulse never came back";
    EXPECT_TRUE (std::isfinite (peak));

    // State survives the process boundary.
    const auto state = plugin.getStateBase64();
    EXPECT_FALSE (state.isEmpty());
    plugin.setStateFromBase64 (state);
    EXPECT_TRUE (plugin.isAlive());
    plugin.shutdown();
}

#include "TestHelpers.h"

TEST (SandboxPool, SandboxedEffectRendersInTheEngineAndSurvivesCrash)
{
    if (! SandboxedPlugin::findHelperBinary().existsAsFile())
        GTEST_SKIP() << "helper unavailable";

    test::EngineFixture fx;
    juce::String delayId;
    for (const auto& d : fx.plugins.getEffects())
        if (d.name.containsIgnoreCase ("AUDelay"))
            delayId = d.createIdentifierString();
    if (delayId.isEmpty())
        GTEST_SKIP() << "plugin database empty — run a scan from the app first";

    // Sandbox mode on: the slot loads out of process through the normal path.
    fx.effects.setSandboxEnabled (true);
    juce::ValueTree slot (ids::SLOT);
    slot.setProperty (ids::slotIndex, 0, nullptr);
    slot.setProperty (ids::pluginId, delayId, nullptr);
    fx.model.getInsert (0).appendChild (slot, nullptr);
    fx.sync.rebuildNow();

    const auto deadline = juce::Time::getMillisecondCounter() + 20000;
    while (fx.effects.peekSandboxed (0, 0) == nullptr
           && juce::Time::getMillisecondCounter() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    auto sandboxed = fx.effects.peekSandboxed (0, 0);
    ASSERT_NE (sandboxed, nullptr) << "sandboxed load timed out";
    EXPECT_EQ (fx.effects.peek (0, 0), nullptr) << "must not also load in-process";
    fx.sync.rebuildNow();
    ASSERT_EQ ((int) fx.engine.getPendingSnapshot()->inserts[0].effects.size(), 1);

    // The default beat through the sandboxed delay must be audible. The child
    // needs wall-clock time to process, so render in paced slices.
    fx.engine.stop();
    fx.engine.setPositionTicks (0.0);
    fx.engine.play();
    juce::AudioBuffer<float> out (2, test::kBlockSize);
    float peak = 0.0f;
    for (int b = 0; b < 60; ++b)
    {
        float* ptrs[2] = { out.getWritePointer (0), out.getWritePointer (1) };
        fx.engine.processBlockOffline (ptrs, 2, test::kBlockSize);
        peak = juce::jmax (peak, out.getMagnitude (0, 0, test::kBlockSize));
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }
    EXPECT_GT (peak, 1.0e-4f);

    // Crash the helper: the engine keeps rendering (the slot goes silent),
    // health-check flags it, and a restart brings the plugin back.
    ::kill (sandboxed->getChildPid(), SIGKILL);
    std::this_thread::sleep_for (std::chrono::milliseconds (100));   // SIGKILL is async
    for (int b = 0; b < 20; ++b)
    {
        float* ptrs[2] = { out.getWritePointer (0), out.getWritePointer (1) };
        fx.engine.processBlockOffline (ptrs, 2, test::kBlockSize);
    }
    EXPECT_TRUE (fx.effects.checkHealth());
    EXPECT_TRUE (fx.effects.isCrashed (0, 0));
    fx.sync.rebuildNow();
    EXPECT_EQ ((int) fx.engine.getPendingSnapshot()->inserts[0].effects.size(), 0)
        << "a crashed slot must drop out of the chain";

    fx.effects.restartSandboxed (0, 0, {});
    const auto deadline2 = juce::Time::getMillisecondCounter() + 20000;
    while (fx.effects.peekSandboxed (0, 0) == nullptr
           && juce::Time::getMillisecondCounter() < deadline2)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    ASSERT_NE (fx.effects.peekSandboxed (0, 0), nullptr) << "restart timed out";
    EXPECT_FALSE (fx.effects.isCrashed (0, 0));
    EXPECT_TRUE (fx.effects.peekSandboxed (0, 0)->isAlive());
    fx.engine.stop();
}

#include "sandbox/SandboxedGenerator.h"

TEST (SandboxPool, SandboxedInstrumentPlaysNotesAndSurvivesCrash)
{
    if (! SandboxedPlugin::findHelperBinary().existsAsFile())
        GTEST_SKIP() << "helper unavailable";

    test::EngineFixture fx;
    juce::String instrumentId;
    for (const auto& d : fx.plugins.getInstruments())
    {
        instrumentId = d.createIdentifierString();
        break;
    }
    if (instrumentId.isEmpty())
        GTEST_SKIP() << "no instruments in plugin database";

    fx.generators.setSandboxEnabled (true);
    auto channel = fx.model.addChannel ("plugin", "SandboxSynth");
    channel.setProperty (ids::pluginId, instrumentId, nullptr);

    // A whole-bar note so the paced render below has something to play.
    auto pattern = fx.model.getPattern (0);
    auto lane = fx.model.getOrCreateLane (pattern, channel[ids::id]);
    fx.model.addNote (lane, 48, 0, 16 * ids::ticksPerStep);
    fx.sync.rebuildNow();

    auto generator = fx.generators.getOrCreate (channel);
    auto* sandboxGen = dynamic_cast<SandboxedGenerator*> (generator.get());
    ASSERT_NE (sandboxGen, nullptr) << "sandbox mode must create a SandboxedGenerator";

    const auto deadline = juce::Time::getMillisecondCounter() + 30000;
    while (sandboxGen->getPlugin() == nullptr
           && juce::Time::getMillisecondCounter() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    auto sandboxed = sandboxGen->getPlugin();
    ASSERT_NE (sandboxed, nullptr) << "sandboxed instrument load timed out";
    EXPECT_TRUE (sandboxed->isInstrument());

    const auto pacedPeak = [&fx] (int blocks)
    {
        juce::AudioBuffer<float> out (2, test::kBlockSize);
        float peak = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            float* ptrs[2] = { out.getWritePointer (0), out.getWritePointer (1) };
            fx.engine.processBlockOffline (ptrs, 2, test::kBlockSize);
            peak = juce::jmax (peak, out.getMagnitude (0, 0, test::kBlockSize));
            std::this_thread::sleep_for (std::chrono::milliseconds (2));
        }
        return peak;
    };

    fx.engine.stop();
    fx.engine.setPositionTicks (0.0);
    fx.engine.play();
    EXPECT_GT (pacedPeak (60), 1.0e-4f) << "sandboxed instrument made no sound";

    // Crash it: the engine keeps running, the channel goes silent (allow the
    // default kick/hat channels' output — mute them for a clean reading).
    for (int i = 0; i < 4; ++i)
        fx.model.getChannel (i).setProperty (ids::mute, true, nullptr);
    fx.sync.rebuildNow();

    ::kill (sandboxed->getChildPid(), SIGKILL);
    std::this_thread::sleep_for (std::chrono::milliseconds (100));
    EXPECT_TRUE (fx.generators.checkSandboxHealth());
    EXPECT_TRUE (fx.generators.isSandboxCrashed ((int) channel[ids::id]));
    EXPECT_LT (pacedPeak (20), 1.0e-6f) << "crashed instrument must be silent";

    // Restart brings it back through the same channel.
    fx.generators.restartSandboxed (channel);
    const auto deadline2 = juce::Time::getMillisecondCounter() + 30000;
    while ((sandboxGen->getPlugin() == nullptr || ! sandboxGen->getPlugin()->isAlive())
           && juce::Time::getMillisecondCounter() < deadline2)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    ASSERT_NE (sandboxGen->getPlugin(), nullptr) << "restart timed out";
    EXPECT_FALSE (fx.generators.isSandboxCrashed ((int) channel[ids::id]));

    // Sound returns at the next note-on: the note that was held when the old
    // child died is gone with it, so rewind to fire it again.
    fx.engine.stop();
    fx.engine.setPositionTicks (0.0);
    fx.engine.play();
    EXPECT_GT (pacedPeak (60), 1.0e-4f) << "restarted instrument made no sound";
    fx.engine.stop();
}
