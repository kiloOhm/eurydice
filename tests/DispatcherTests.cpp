#include <gtest/gtest.h>
#include "control/ControlDispatcher.h"
#include "TestHelpers.h"

// These use the full AppServices stack (opens the default audio device when
// available; falls back gracefully when not).
namespace
{
struct DispatcherFixture : ::testing::Test
{
    AppServices services { false };   // no audio device in tests
    ControlDispatcher dispatcher { services };

    juce::var call (const juce::String& method, const juce::String& paramsJson = "{}")
    {
        return dispatcher.dispatch (method, juce::JSON::parse (paramsJson));
    }

    int firstChannelId()
    {
        return services.project.getChannel (0)[ids::id];
    }
};

#define EXPECT_CONTROL_ERROR(expr)                                  \
    EXPECT_THROW ((expr), ControlDispatcher::ControlError)
}

TEST_F (DispatcherFixture, PingPongs)
{
    EXPECT_EQ (call ("ping").toString(), "pong");
}

TEST_F (DispatcherFixture, UnknownMethodThrows)
{
    EXPECT_CONTROL_ERROR (call ("nope.nothing"));
}

TEST_F (DispatcherFixture, StateGetShape)
{
    const auto state = call ("state.get");
    EXPECT_DOUBLE_EQ ((double) state["tempo"], 140.0);
    EXPECT_EQ (state["channels"].getArray()->size(), 4);
    EXPECT_EQ (state["patterns"].getArray()->size(), 1);
    EXPECT_EQ ((int) state["ticksPerBar"], 3840);
}

TEST_F (DispatcherFixture, TransportSetAndPlayStop)
{
    call ("transport.set", R"({"tempo": 90.5, "swing": 0.4, "songMode": true})");
    EXPECT_DOUBLE_EQ (services.project.getTempo(), 90.5);
    EXPECT_DOUBLE_EQ (services.project.getSwing(), 0.4);
    EXPECT_TRUE (services.project.isSongMode());

    call ("transport.play");
    EXPECT_TRUE (services.engine.isPlaying());
    call ("transport.stop");
    EXPECT_FALSE (services.engine.isPlaying());
}

TEST_F (DispatcherFixture, TransportSetLoopRange)
{
    call ("transport.set", R"({"loopStart": 3840, "loopEnd": 15360, "loopEnabled": true})");
    EXPECT_EQ (services.project.getLoopStart(), 3840);
    EXPECT_EQ (services.project.getLoopEnd(), 15360);
    EXPECT_TRUE (services.project.isLoopEnabled());

    // One end at a time leaves the other where it was.
    call ("transport.set", R"({"loopEnd": 7680})");
    EXPECT_EQ (services.project.getLoopStart(), 3840);
    EXPECT_EQ (services.project.getLoopEnd(), 7680);

    const auto state = call ("state.get");
    EXPECT_EQ ((int) state["loopStart"], 3840);
    EXPECT_EQ ((int) state["loopEnd"], 7680);
    EXPECT_TRUE ((bool) state["loopEnabled"]);
}

TEST_F (DispatcherFixture, ChannelAddSetRemove)
{
    const auto added = call ("channel.add", R"({"type": "synth", "name": "Lead"})");
    const int chId = added["id"];
    ASSERT_TRUE (services.project.getChannelById (chId).isValid());

    call ("channel.set", R"({"channelId": )" + juce::String (chId)
                             + R"(, "volume": 0.5, "pan": -0.5, "insert": 3})");
    auto channel = services.project.getChannelById (chId);
    EXPECT_DOUBLE_EQ ((double) channel[ids::volume], 0.5);
    EXPECT_EQ ((int) channel[ids::insertIndex], 3);

    call ("channel.remove", R"({"channelId": )" + juce::String (chId) + "}");
    EXPECT_FALSE (services.project.getChannelById (chId).isValid());

    EXPECT_CONTROL_ERROR (call ("channel.add", R"({"type": "theremin", "name": "x"})"));
    EXPECT_CONTROL_ERROR (call ("channel.set", R"({"channelId": 99999})"));
}

TEST_F (DispatcherFixture, NotesSetGetClear)
{
    const auto chId = juce::String (firstChannelId());
    call ("notes.set", R"({"channelId": )" + chId
              + R"(, "notes": [{"key": 60, "start": 0, "length": 240},
                               {"key": 62, "start": 240, "length": 240, "velocity": 0.5}]})");

    auto notes = call ("notes.get", R"({"channelId": )" + chId + "}");
    ASSERT_EQ (notes.getArray()->size(), 2);
    EXPECT_EQ ((int) notes[1]["key"], 62);
    EXPECT_DOUBLE_EQ ((double) notes[1]["velocity"], 0.5);

    call ("notes.clear", R"({"channelId": )" + chId + "}");
    EXPECT_EQ (call ("notes.get", R"({"channelId": )" + chId + "}").getArray()->size(), 0);
}

TEST_F (DispatcherFixture, PatternCreateAndSelect)
{
    const auto created = call ("pattern.create", R"({"name": "P2", "lengthTicks": 7680})");
    const int patId = created["id"];
    call ("pattern.select", R"({"patternId": )" + juce::String (patId) + "}");
    EXPECT_EQ ((int) services.project.getRoot()[ids::activePattern], patId);
    EXPECT_CONTROL_ERROR (call ("pattern.select", R"({"patternId": 424242})"));
}

TEST_F (DispatcherFixture, PatternCloneCopiesNotes)
{
    const int sourceId = services.project.getRoot()[ids::activePattern];
    const auto chId = juce::String (firstChannelId());

    const auto cloned = call ("pattern.clone", R"({"patternId": )" + juce::String (sourceId) + "}");
    const int copyId = cloned["id"];
    EXPECT_NE (copyId, sourceId);
    EXPECT_EQ (services.project.numPatterns(), 2);

    auto copiedNotes = call ("notes.get", R"({"patternId": )" + juce::String (copyId)
                                              + R"(, "channelId": )" + chId + "}");
    EXPECT_EQ (copiedNotes.getArray()->size(), 4);

    // The copy is independent: clearing it leaves the original alone.
    call ("notes.clear", R"({"patternId": )" + juce::String (copyId)
              + R"(, "channelId": )" + chId + "}");
    EXPECT_EQ (call ("notes.get", R"({"patternId": )" + juce::String (sourceId)
                         + R"(, "channelId": )" + chId + "}").getArray()->size(), 4);

    EXPECT_CONTROL_ERROR (call ("pattern.clone", R"({"patternId": 424242})"));
}

TEST_F (DispatcherFixture, PatternRemoveDropsClipsAndKeepsTheLastPattern)
{
    const int firstId = services.project.getRoot()[ids::activePattern];
    const int secondId = call ("pattern.create", R"({"name": "P2"})")["id"];

    call ("playlist.addClip", R"({"track": 0, "patternId": )" + juce::String (secondId)
                                  + R"(, "start": 0})");
    ASSERT_EQ (call ("playlist.get")[0]["clips"].getArray()->size(), 1);

    call ("pattern.remove", R"({"patternId": )" + juce::String (secondId) + "}");
    EXPECT_FALSE (services.project.getPatternById (secondId).isValid());
    EXPECT_EQ (call ("playlist.get")[0]["clips"].getArray()->size(), 0);
    EXPECT_EQ ((int) services.project.getRoot()[ids::activePattern], firstId);

    // The last pattern cannot go, and unknown ids are rejected.
    EXPECT_CONTROL_ERROR (call ("pattern.remove", R"({"patternId": )" + juce::String (firstId) + "}"));
    EXPECT_CONTROL_ERROR (call ("pattern.remove", R"({"patternId": 424242})"));
    EXPECT_EQ (services.project.numPatterns(), 1);
}

TEST_F (DispatcherFixture, PlaylistAddAndClear)
{
    const int patId = services.project.getRoot()[ids::activePattern];
    call ("playlist.addClip", R"({"track": 2, "patternId": )" + juce::String (patId)
                                  + R"(, "start": 3840})");
    auto tracks = call ("playlist.get");
    EXPECT_EQ (tracks[2]["clips"].getArray()->size(), 1);

    call ("playlist.clear");
    tracks = call ("playlist.get");
    EXPECT_EQ (tracks[2]["clips"].getArray()->size(), 0);

    EXPECT_CONTROL_ERROR (call ("playlist.addClip",
        R"({"track": 999, "patternId": )" + juce::String (patId) + "}"));
}

TEST_F (DispatcherFixture, MixerRoutingAndValidation)
{
    call ("mixer.setInsert", R"({"insert": 1, "volume": 0.4, "name": "Drums"})");
    auto insert = services.project.getInsert (1);
    EXPECT_DOUBLE_EQ ((double) insert[ids::volume], 0.4);
    EXPECT_EQ (insert[ids::name].toString(), "Drums");

    call ("mixer.addSend", R"({"from": 1, "to": 2, "level": 0.6})");
    const auto mixer = call ("mixer.get");
    EXPECT_EQ (mixer[1]["sends"].getArray()->size(), 2);   // default master + new

    EXPECT_CONTROL_ERROR (call ("mixer.addSend", R"({"from": 1, "to": 999})"));
    EXPECT_CONTROL_ERROR (call ("mixer.setEffect",
        R"({"insert": 1, "slot": 0, "pluginId": "not-a-plugin"})"));
}

TEST_F (DispatcherFixture, AutomationCreateAndSetPoints)
{
    const auto created = call ("automation.create",
        R"({"targetType": "channel", "targetId": )" + juce::String (firstChannelId())
            + R"(, "paramId": "volume", "name": "vol", "initialValue": 0.8})");
    const int autoId = created["id"];

    call ("automation.setPoints", R"({"automationId": )" + juce::String (autoId)
              + R"(, "points": [{"pos": 0, "value": 1.0}, {"pos": 3840, "value": 0.0, "tension": 0.5}]})");

    auto automation = services.project.getAutomationById (autoId);
    ASSERT_EQ (automation.getNumChildren(), 2);
    EXPECT_DOUBLE_EQ ((double) automation.getChild (1)[ids::tension], 0.5);

    EXPECT_CONTROL_ERROR (call ("automation.create", R"({"targetType": "nothing"})"));
    EXPECT_CONTROL_ERROR (call ("automation.setPoints", R"({"automationId": 777})"));
}

TEST_F (DispatcherFixture, ProjectSaveLoadViaDispatch)
{
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getNonexistentChildFile ("eurytest-dispatch", ".eury");
    call ("transport.set", R"({"tempo": 155})");
    call ("project.save", R"({"path": ")" + file.getFullPathName() + R"("})");
    ASSERT_TRUE (file.existsAsFile());

    call ("transport.set", R"({"tempo": 100})");
    call ("project.load", R"({"path": ")" + file.getFullPathName() + R"("})");
    EXPECT_DOUBLE_EQ (services.project.getTempo(), 155.0);

    call ("project.new");
    EXPECT_DOUBLE_EQ (services.project.getTempo(), 140.0);

    EXPECT_CONTROL_ERROR (call ("project.load", R"({"path": "/nope/missing.eury"})"));
    file.deleteFile();
}

TEST_F (DispatcherFixture, RenderExportViaDispatch)
{
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getNonexistentChildFile ("eurytest-dispatchrender", ".wav");
    const auto result = call ("render.export",
        R"({"path": ")" + file.getFullPathName() + R"(", "tailSeconds": 0.1})");
    ASSERT_EQ (result["files"].getArray()->size(), 1);
    EXPECT_TRUE (file.existsAsFile());
    EXPECT_GT (file.getSize(), 1000);
    file.deleteFile();
}

TEST_F (DispatcherFixture, MetersAndPluginsList)
{
    const auto meters = call ("meters.get");
    EXPECT_EQ (meters["inserts"].getArray()->size(), 33);
    EXPECT_TRUE (call ("plugins.list").isArray());
}

TEST_F (DispatcherFixture, AudioClipAddViaDispatch)
{
    const auto tone = test::makeToneFile (0.5);
    const auto result = call ("playlist.addAudioClip",
        R"({"track": 0, "path": ")" + tone.getFullPathName()
            + R"(", "start": 0, "lengthTicks": 3840, "stretchToFit": true})");
    EXPECT_GT ((double) result["stretchRatio"], 1.0);

    EXPECT_CONTROL_ERROR (call ("playlist.addAudioClip",
        R"({"track": 0, "path": "/missing.wav"})"));
    tone.deleteFile();
}

TEST_F (DispatcherFixture, EachCallIsItsOwnUndoStep)
{
    auto channel = services.project.getChannel (0);
    channel.setProperty (ids::volume, 0.5, nullptr);
    channel.setProperty (ids::pan, 0.0, nullptr);
    services.project.getUndoManager().clearUndoHistory();

    const auto id = juce::String (firstChannelId());
    call ("channel.set", R"({"channelId": )" + id + R"(, "volume": 0.9})");
    call ("channel.set", R"({"channelId": )" + id + R"(, "pan": -0.4})");

    int steps = 0;
    while (services.project.getUndoManager().undo())
        ++steps;
    EXPECT_EQ (steps, 2);
    EXPECT_DOUBLE_EQ ((double) channel[ids::volume], 0.5);
    EXPECT_DOUBLE_EQ ((double) channel[ids::pan], 0.0);
}
