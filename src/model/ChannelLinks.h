#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <vector>
#include "Ids.h"

// Channel bundling: a channel can be linked to another so both play the same
// MIDI track, which is how you layer two sounds off one part. The follower
// keeps its own generator, mixer routing and knobs — only the notes are
// shared.
//
// The link is one level deep on purpose: a follower can never be a leader, so
// there are no chains to resolve and no cycles to guard against at playback
// time. Everything here works off the CHANNELS tree, so the engine snapshot,
// live input and the rack UI all agree on who plays what.
namespace channellinks
{
// The channel a follower takes its notes from, or 0 when it is independent.
inline int leaderOf (const juce::ValueTree& channel)
{
    return (int) channel.getProperty (ids::linkedTo, 0);
}

inline bool isFollower (const juce::ValueTree& channel)
{
    return leaderOf (channel) != 0;
}

// Channels following `leaderId`. `channels` is the CHANNELS tree.
inline std::vector<int> followersOf (const juce::ValueTree& channels, int leaderId)
{
    std::vector<int> out;
    if (leaderId == 0)
        return out;

    for (const auto channel : channels)
        if (channel.hasType (ids::CHANNEL) && leaderOf (channel) == leaderId)
            out.push_back ((int) channel[ids::id]);
    return out;
}

// Every channel a note written against `channelId` should sound on: the
// channel itself, plus anything bundled to it. A follower sounds only itself
// (its notes live on the leader's lane, which is handled from that end).
inline std::vector<int> playbackTargets (const juce::ValueTree& channels, int channelId)
{
    std::vector<int> out { channelId };
    for (const int follower : followersOf (channels, channelId))
        out.push_back (follower);
    return out;
}

// Linking is refused when it would build a chain: the target must not itself
// be a follower, and the channel must not already lead someone.
inline bool canLink (const juce::ValueTree& channels, int channelId, int leaderId)
{
    if (leaderId == 0)
        return true;   // unlinking is always allowed
    if (channelId == leaderId)
        return false;

    for (const auto channel : channels)
    {
        if (! channel.hasType (ids::CHANNEL))
            continue;
        const int id = (int) channel[ids::id];
        if (id == leaderId && isFollower (channel))
            return false;   // would chain onto another leader
        if (leaderOf (channel) == channelId)
            return false;   // this channel already leads a bundle
    }
    return true;
}
} // namespace channellinks
