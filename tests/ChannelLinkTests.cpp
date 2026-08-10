#include "TestHelpers.h"
#include "model/ChannelLinks.h"

// Bundled channels play the same MIDI track, so one part drives two sounds.

TEST (ChannelLinks, IndependentChannelsHaveNoLeader)
{
    test::EngineFixture fx;
    auto a = fx.model.addChannel ("synth", "A");

    EXPECT_FALSE (channellinks::isFollower (a));
    EXPECT_EQ (channellinks::leaderOf (a), 0);

    const auto targets = channellinks::playbackTargets (fx.model.channels(), (int) a[ids::id]);
    ASSERT_EQ ((int) targets.size(), 1);
    EXPECT_EQ (targets[0], (int) a[ids::id]);
}

TEST (ChannelLinks, FollowerIsListedAsAPlaybackTargetOfItsLeader)
{
    test::EngineFixture fx;
    auto lead = fx.model.addChannel ("synth", "Lead");
    auto layer = fx.model.addChannel ("synth", "Layer");
    layer.setProperty (ids::linkedTo, (int) lead[ids::id], nullptr);

    EXPECT_TRUE (channellinks::isFollower (layer));

    const auto targets = channellinks::playbackTargets (fx.model.channels(), (int) lead[ids::id]);
    ASSERT_EQ ((int) targets.size(), 2);
    EXPECT_EQ (targets[0], (int) lead[ids::id]);
    EXPECT_EQ (targets[1], (int) layer[ids::id]);

    // The follower on its own sounds only itself; its notes live on the leader.
    const auto followerTargets = channellinks::playbackTargets (fx.model.channels(),
                                                               (int) layer[ids::id]);
    EXPECT_EQ ((int) followerTargets.size(), 1);
}

TEST (ChannelLinks, ChainsAndSelfLinksAreRefused)
{
    test::EngineFixture fx;
    auto a = fx.model.addChannel ("synth", "A");
    auto b = fx.model.addChannel ("synth", "B");
    auto c = fx.model.addChannel ("synth", "C");
    const int aId = a[ids::id], bId = b[ids::id], cId = c[ids::id];

    const auto channels = fx.model.channels();
    EXPECT_FALSE (channellinks::canLink (channels, aId, aId)) << "a channel cannot follow itself";
    EXPECT_TRUE (channellinks::canLink (channels, bId, aId));

    b.setProperty (ids::linkedTo, aId, nullptr);

    // C must not chain onto B, which is already following A.
    EXPECT_FALSE (channellinks::canLink (channels, cId, bId)) << "chains must be refused";
    EXPECT_TRUE (channellinks::canLink (channels, cId, aId)) << "a second follower is fine";

    // A leads a bundle, so it must not become someone's follower.
    EXPECT_FALSE (channellinks::canLink (channels, aId, cId));

    // Unlinking is always allowed.
    EXPECT_TRUE (channellinks::canLink (channels, bId, 0));
}

// The real test: does the bundled channel actually make sound off the
// leader's notes?
TEST (ChannelLinks, BundledChannelSoundsTheLeadersNotes)
{
    test::EngineFixture fx;
    auto lead = fx.model.addChannel ("synth", "Lead");
    auto layer = fx.model.addChannel ("synth", "Layer");

    // Silence the default kick/hat channels so we measure only these two.
    for (int i = 0; i < fx.model.numChannels(); ++i)
    {
        auto ch = fx.model.getChannel (i);
        if ((int) ch[ids::id] != (int) lead[ids::id] && (int) ch[ids::id] != (int) layer[ids::id])
            ch.setProperty (ids::mute, true, nullptr);
    }

    auto pattern = fx.model.getPattern (0);
    auto laneTree = fx.model.getOrCreateLane (pattern, (int) lead[ids::id]);
    fx.model.addNote (laneTree, 60, 0, ids::ticksPerStep * 4);

    // Leader alone.
    fx.sync.rebuildNow();
    const auto soloRms = test::rmsOf (fx.renderFromStart (22050), 0, 22050);
    EXPECT_GT (soloRms, 0.0f) << "the leader itself made no sound";

    // Now bundle the layer onto it: the same part, two voices.
    layer.setProperty (ids::linkedTo, (int) lead[ids::id], nullptr);
    fx.sync.rebuildNow();
    const auto bundledRms = test::rmsOf (fx.renderFromStart (22050), 0, 22050);

    EXPECT_GT (bundledRms, soloRms * 1.2f)
        << "bundling did not add the second channel's voice";
}

TEST (ChannelLinks, FollowersOwnLaneGoesDormant)
{
    test::EngineFixture fx;
    auto lead = fx.model.addChannel ("synth", "Lead");
    auto layer = fx.model.addChannel ("synth", "Layer");

    for (int i = 0; i < fx.model.numChannels(); ++i)
    {
        auto ch = fx.model.getChannel (i);
        if ((int) ch[ids::id] != (int) lead[ids::id] && (int) ch[ids::id] != (int) layer[ids::id])
            ch.setProperty (ids::mute, true, nullptr);
    }

    auto pattern = fx.model.getPattern (0);
    // Only the follower has notes; the leader's lane is empty.
    auto followerLane = fx.model.getOrCreateLane (pattern, (int) layer[ids::id]);
    fx.model.addNote (followerLane, 60, 0, ids::ticksPerStep * 4);

    layer.setProperty (ids::linkedTo, (int) lead[ids::id], nullptr);
    fx.sync.rebuildNow();

    EXPECT_NEAR (test::rmsOf (fx.renderFromStart (22050), 0, 22050), 0.0f, 1.0e-6f)
        << "a bundled channel should take its part from the leader, not its own lane";
}

// Deleting the leader must not silence the channel that followed it.
TEST (ChannelLinks, DanglingLinkFallsBackToTheChannelsOwnLane)
{
    test::EngineFixture fx;
    auto layer = fx.model.addChannel ("synth", "Layer");

    for (int i = 0; i < fx.model.numChannels(); ++i)
    {
        auto ch = fx.model.getChannel (i);
        if ((int) ch[ids::id] != (int) layer[ids::id])
            ch.setProperty (ids::mute, true, nullptr);
    }

    auto pattern = fx.model.getPattern (0);
    auto lane = fx.model.getOrCreateLane (pattern, (int) layer[ids::id]);
    fx.model.addNote (lane, 60, 0, ids::ticksPerStep * 4);

    // A leader id that no longer exists.
    layer.setProperty (ids::linkedTo, 999999, nullptr);
    fx.sync.rebuildNow();

    EXPECT_GT (test::rmsOf (fx.renderFromStart (22050), 0, 22050), 0.0f)
        << "a dangling bundle link silenced the channel";
}
