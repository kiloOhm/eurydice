#include "TestHelpers.h"

using test::EngineFixture;

TEST (Snapshot, BasicShapeMatchesModel)
{
    EngineFixture fx;
    auto snap = fx.engine.getPendingSnapshot();
    ASSERT_NE (snap, nullptr);
    EXPECT_EQ ((int) snap->channels.size(), 4);
    EXPECT_EQ ((int) snap->patterns.size(), 1);
    EXPECT_EQ ((int) snap->inserts.size(), 9);
    EXPECT_DOUBLE_EQ (snap->tempo, 140.0);
    EXPECT_EQ (snap->activePatternIndex, 0);

    // Sampler generators were created for the default channels.
    EXPECT_NE (snap->channels[0].generator, nullptr);
}

TEST (Snapshot, NotesAreSortedByStart)
{
    EngineFixture fx;
    auto pattern = fx.model.getPattern (0);
    auto lane = fx.model.getOrCreateLane (pattern, fx.model.getChannel (1)[ids::id]);
    fx.model.addNote (lane, 60, 960, 240);
    fx.model.addNote (lane, 61, 0, 240);
    fx.model.addNote (lane, 62, 480, 240);
    fx.sync.rebuildNow();

    auto snap = fx.engine.getPendingSnapshot();
    const auto& notes = snap->patterns[0].notes;
    for (size_t i = 1; i < notes.size(); ++i)
        EXPECT_LE (notes[i - 1].startTicks, notes[i].startTicks);
}

TEST (Snapshot, SoloWinsOverMute)
{
    EngineFixture fx;
    fx.model.getChannel (0).setProperty (ids::solo, true, nullptr);
    fx.model.getChannel (1).setProperty (ids::mute, false, nullptr);
    fx.sync.rebuildNow();

    auto snap = fx.engine.getPendingSnapshot();
    EXPECT_TRUE (snap->channels[0].audible);
    EXPECT_FALSE (snap->channels[1].audible);   // not soloed -> silent
}

TEST (Snapshot, MuteWithoutSolo)
{
    EngineFixture fx;
    fx.model.getChannel (2).setProperty (ids::mute, true, nullptr);
    fx.sync.rebuildNow();

    auto snap = fx.engine.getPendingSnapshot();
    EXPECT_TRUE (snap->channels[0].audible);
    EXPECT_FALSE (snap->channels[2].audible);
}

TEST (Snapshot, SendTopologyOrdersSourcesFirst)
{
    EngineFixture fx;
    // insert 1 -> insert 2 (plus both default-route to master)
    auto sends1 = fx.model.getInsert (1).getChildWithName (ids::SENDS);
    juce::ValueTree send (ids::SEND);
    send.setProperty (ids::destInsert, 2, nullptr);
    send.setProperty (ids::level, 1.0, nullptr);
    sends1.appendChild (send, nullptr);
    fx.sync.rebuildNow();

    auto snap = fx.engine.getPendingSnapshot();
    const auto& order = snap->insertOrder;
    const auto posOf = [&order] (int insert)
    {
        return std::find (order.begin(), order.end(), insert) - order.begin();
    };
    EXPECT_LT (posOf (1), posOf (2));
    EXPECT_EQ (order.back(), 0);   // master last
    EXPECT_EQ ((int) order.size(), 9);
}

TEST (Snapshot, SendCycleFallsBackToIndexOrder)
{
    EngineFixture fx;
    auto addSend = [&fx] (int from, int to)
    {
        juce::ValueTree send (ids::SEND);
        send.setProperty (ids::destInsert, to, nullptr);
        send.setProperty (ids::level, 1.0, nullptr);
        fx.model.getInsert (from).getChildWithName (ids::SENDS).appendChild (send, nullptr);
    };
    addSend (1, 2);
    addSend (2, 1);   // cycle
    fx.sync.rebuildNow();

    auto snap = fx.engine.getPendingSnapshot();
    EXPECT_EQ ((int) snap->insertOrder.size(), 9);    // everything still processed
    EXPECT_EQ (snap->insertOrder.back(), 0);
}

TEST (Snapshot, PatternClipsResolveAndSetSongLength)
{
    EngineFixture fx;
    const int patId = fx.model.getRoot()[ids::activePattern];
    auto clip = fx.model.addPlaylistClip ("pattern", 0, 3840, 3840);
    clip.setProperty (ids::patternId, patId, nullptr);
    fx.sync.rebuildNow();

    auto snap = fx.engine.getPendingSnapshot();
    ASSERT_EQ ((int) snap->clips.size(), 1);
    EXPECT_EQ (snap->clips[0].patternIndex, 0);
    EXPECT_EQ (snap->songLengthTicks, 7680);
}

TEST (Snapshot, MutedClipsAndTracksExcluded)
{
    EngineFixture fx;
    const int patId = fx.model.getRoot()[ids::activePattern];

    auto clip = fx.model.addPlaylistClip ("pattern", 0, 0, 3840);
    clip.setProperty (ids::patternId, patId, nullptr);
    clip.setProperty (ids::muted, true, nullptr);

    auto clip2 = fx.model.addPlaylistClip ("pattern", 1, 0, 3840);
    clip2.setProperty (ids::patternId, patId, nullptr);
    fx.model.playlist().getChild (1).setProperty (ids::mute, true, nullptr);

    fx.sync.rebuildNow();
    EXPECT_EQ ((int) fx.engine.getPendingSnapshot()->clips.size(), 0);
}

TEST (Snapshot, AutomationClipResolves)
{
    EngineFixture fx;
    const int chId = fx.model.getChannel (0)[ids::id];
    auto automation = fx.model.addAutomation ("channel", chId, "volume", "vol", 0.5);
    auto clip = fx.model.addPlaylistClip ("automation", 0, 0, 3840);
    clip.setProperty (ids::automationId, (int) automation[ids::id], nullptr);
    fx.sync.rebuildNow();

    auto snap = fx.engine.getPendingSnapshot();
    ASSERT_EQ ((int) snap->automations.size(), 1);
    EXPECT_EQ (snap->automations[0].kind, AutomationSnapshot::Kind::channelVolume);
    EXPECT_EQ (snap->automations[0].channelIndex, 0);
    ASSERT_EQ ((int) snap->clips.size(), 1);
    EXPECT_EQ (snap->clips[0].type, ClipSnapshot::Type::automation);
}

TEST (Snapshot, AudioClipStretchDoublesLength)
{
    EngineFixture fx;
    const auto tone = test::makeToneFile (1.0);
    ASSERT_TRUE (tone.existsAsFile());

    auto natural = fx.audioClips.getStretched (tone.getFullPathName(), 1.0);
    auto doubled = fx.audioClips.getStretched (tone.getFullPathName(), 2.0);
    ASSERT_NE (natural, nullptr);
    ASSERT_NE (doubled, nullptr);
    EXPECT_NEAR (natural->getNumSamples(), 44100, 50);
    EXPECT_NEAR (doubled->getNumSamples(), 88200, 2000);   // RB may pad slightly

    // Cache returns the same buffer next time.
    EXPECT_EQ (fx.audioClips.getStretched (tone.getFullPathName(), 2.0).get(), doubled.get());
    tone.deleteFile();
}
