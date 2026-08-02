#include "TestHelpers.h"
#include "plugins/PluginGenerator.h"

// Hosting tests use Apple Audio Units from the machine's plugin database
// (populated by a scan from the app). They skip cleanly when the database
// is empty, so CI without a scan still passes.
namespace
{
void pumpUntil (const std::function<bool()>& condition, int timeoutMs)
{
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (! condition() && juce::Time::getMillisecondCounter() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
}

std::optional<juce::PluginDescription> findEffect (PluginManager& plugins, const juce::String& name)
{
    for (const auto& d : plugins.getEffects())
        if (d.name.containsIgnoreCase (name))
            return d;
    return {};
}
}

TEST (PluginHosting, EffectPoolLoadsAndProcesses)
{
    test::EngineFixture fx;
    const auto delay = findEffect (fx.plugins, "AUDelay");
    if (! delay)
        GTEST_SKIP() << "plugin database empty — run a scan from the app first";

    const auto pluginId = delay->createIdentifierString();
    EXPECT_TRUE (fx.plugins.findByIdentifier (pluginId).has_value());

    // First call kicks off async creation and returns null.
    EXPECT_EQ (fx.effects.getReady (0, 0, pluginId, {}), nullptr);
    pumpUntil ([&fx] { return fx.effects.peek (0, 0) != nullptr; }, 15000);

    auto hosted = fx.effects.getReady (0, 0, pluginId, {});
    ASSERT_NE (hosted, nullptr);
    EXPECT_EQ (hosted->getDescription().name, delay->name);

    // Process an impulse through it; a delay must not explode or hard-mute.
    hosted->prepare (test::kSampleRate, test::kBlockSize);
    juce::AudioBuffer<float> bus (2, test::kBlockSize);
    bus.clear();
    bus.setSample (0, 0, 1.0f);
    bus.setSample (1, 0, 1.0f);
    hosted->process (bus, test::kBlockSize, {});
    EXPECT_TRUE (std::isfinite (bus.getMagnitude (0, 0, test::kBlockSize)));

    // State round-trip.
    const auto state = hosted->getStateBase64();
    EXPECT_FALSE (state.isEmpty());
    hosted->setStateFromBase64 (state);

    // Replacing the slot with a different plugin id evicts the old entry.
    fx.effects.remove (0, 0);
    EXPECT_EQ (fx.effects.peek (0, 0), nullptr);
}

TEST (PluginHosting, EffectInChainRendersInEngine)
{
    test::EngineFixture fx;
    const auto delay = findEffect (fx.plugins, "AUDelay");
    if (! delay)
        GTEST_SKIP() << "plugin database empty — run a scan from the app first";

    juce::ValueTree slot (ids::SLOT);
    slot.setProperty (ids::slotIndex, 0, nullptr);
    slot.setProperty (ids::pluginId, delay->createIdentifierString(), nullptr);
    fx.model.getInsert (0).appendChild (slot, nullptr);
    fx.sync.rebuildNow();

    pumpUntil ([&fx]
    {
        auto snap = fx.engine.getPendingSnapshot();
        return ! snap->inserts[0].effects.empty();
    }, 15000);

    auto snap = fx.engine.getPendingSnapshot();
    ASSERT_EQ ((int) snap->inserts[0].effects.size(), 1);

    auto out = fx.renderFromStart (8192);   // default beat through the delay
    EXPECT_GT (test::rmsOf (out, 0, 8192), 1.0e-4f);
}

TEST (PluginHosting, UnknownPluginIdYieldsNothing)
{
    test::EngineFixture fx;
    EXPECT_EQ (fx.effects.getReady (1, 0, "Format-Fake-00000000-0", {}), nullptr);
    EXPECT_EQ (fx.effects.peek (1, 0), nullptr);
    EXPECT_FALSE (fx.plugins.findByIdentifier ("Format-Fake-00000000-0").has_value());
}

TEST (PluginHosting, PluginChannelGeneratorCreated)
{
    test::EngineFixture fx;
    juce::PluginDescription instrument;
    bool haveInstrument = false;
    for (const auto& d : fx.plugins.getInstruments())
    {
        instrument = d;
        haveInstrument = true;
        break;
    }
    if (! haveInstrument)
        GTEST_SKIP() << "no instruments in plugin database";

    auto channel = fx.model.addChannel ("plugin", instrument.name);
    channel.setProperty (ids::pluginId, instrument.createIdentifierString(), nullptr);
    fx.sync.rebuildNow();

    auto generator = fx.generators.getOrCreate (channel);
    ASSERT_NE (generator, nullptr);

    auto* pluginGen = dynamic_cast<PluginGenerator*> (generator.get());
    ASSERT_NE (pluginGen, nullptr);

    pumpUntil ([pluginGen] { return pluginGen->getPlugin() != nullptr; }, 15000);
    ASSERT_NE (pluginGen->getPlugin(), nullptr);

    // Render a note through the hosted instrument; must stay finite.
    juce::AudioBuffer<float> out (2, test::kBlockSize);
    out.clear();
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
    pluginGen->render (out, midi);
    EXPECT_TRUE (std::isfinite (out.getMagnitude (0, 0, test::kBlockSize)));
}

TEST (PluginHosting, ScanListsAndPersistence)
{
    PluginManager manager;
    // These exercise the list/query paths regardless of database contents.
    const auto instruments = manager.getInstruments();
    const auto fxList = manager.getEffects();
    for (const auto& d : instruments)
        EXPECT_TRUE (d.isInstrument);
    for (const auto& d : fxList)
        EXPECT_FALSE (d.isInstrument);
    EXPECT_FALSE (manager.isScanning());
    EXPECT_TRUE (PluginManager::getAppDataDir().isDirectory());
}
