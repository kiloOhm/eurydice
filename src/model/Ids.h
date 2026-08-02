#pragma once

#include <juce_data_structures/juce_data_structures.h>

// ValueTree schema. Time is integer ticks at 960 PPQ (assume 4/4 for v1):
// one 16th-note step = 240 ticks, one bar = 3840 ticks.
namespace ids
{
#define DECLARE_ID(name) inline const juce::Identifier name (#name);

// Tree types
DECLARE_ID (PROJECT)
DECLARE_ID (CHANNELS)
DECLARE_ID (CHANNEL)
DECLARE_ID (PATTERNS)
DECLARE_ID (PATTERN)
DECLARE_ID (LANE)          // one channel's notes inside a pattern
DECLARE_ID (NOTE)
DECLARE_ID (PLAYLIST)
DECLARE_ID (TRACK)
DECLARE_ID (CLIP)
DECLARE_ID (MIXER)
DECLARE_ID (INSERT)
DECLARE_ID (SLOT)          // effect slot in a mixer insert
DECLARE_ID (SENDS)
DECLARE_ID (SEND)
DECLARE_ID (AUTOMATIONS)
DECLARE_ID (AUTOMATION)    // an automation source (target param + curve)
DECLARE_ID (POINT)

// Common properties
DECLARE_ID (id)
DECLARE_ID (name)
DECLARE_ID (colour)

// Project
DECLARE_ID (tempo)
DECLARE_ID (swing)         // 0..1 global swing
DECLARE_ID (songMode)
DECLARE_ID (loopStart)       // transport loop range, in ticks
DECLARE_ID (loopEnd)
DECLARE_ID (loopEnabled)
DECLARE_ID (activePattern)   // id of the pattern selected in the rack
DECLARE_ID (selectedChannel) // id of the channel focused for piano roll / typing keys

// Channel
DECLARE_ID (type)          // "sampler" | "synth" | "plugin"
DECLARE_ID (volume)        // 0..1 linear gain scale (1 = unity-ish)
DECLARE_ID (pan)           // -1..1
DECLARE_ID (mute)
DECLARE_ID (solo)
DECLARE_ID (insertIndex)   // target mixer insert (0 = master)
DECLARE_ID (samplePath)
DECLARE_ID (rootNote)
DECLARE_ID (pluginId)      // plugin identifier string from scan DB
DECLARE_ID (pluginState)   // base64 blob

// Generator parameters (sampler + synth), stored flat on the CHANNEL.
DECLARE_ID (attack)        // seconds
DECLARE_ID (decay)
DECLARE_ID (sustain)       // 0..1
DECLARE_ID (release)
DECLARE_ID (cutoff)        // Hz
DECLARE_ID (resonance)     // 0..1
DECLARE_ID (oneShot)       // sampler: ignore note-offs
DECLARE_ID (osc2Detune)    // synth: cents
DECLARE_ID (osc2Mix)       // synth: 0..1
DECLARE_ID (oscShape)      // synth: 0 = saw, 1 = square
DECLARE_ID (filterEnvAmt)  // synth: 0..1

// Pattern
DECLARE_ID (lengthTicks)
DECLARE_ID (channelId)     // on LANE
DECLARE_ID (editedWith)    // on LANE: "steps" | "pianoroll"

// Note (also used as a "step": steps are just notes on the grid)
DECLARE_ID (key)           // MIDI note number
DECLARE_ID (startTicks)
DECLARE_ID (velocity)      // 0..1
DECLARE_ID (notePan)       // -1..1

// Playlist clip
DECLARE_ID (clipType)      // "pattern" | "audio" | "automation"
DECLARE_ID (patternId)
DECLARE_ID (audioPath)
DECLARE_ID (audioOffsetTicks)   // trim start inside source
DECLARE_ID (stretchRatio)       // 1 = natural speed
DECLARE_ID (automationId)
DECLARE_ID (trackIndex)
DECLARE_ID (muted)

// Mixer
DECLARE_ID (destInsert)
DECLARE_ID (level)
DECLARE_ID (slotIndex)
DECLARE_ID (bypass)

// Automation
DECLARE_ID (targetType)    // "channel" | "insert" | "plugin"
DECLARE_ID (targetId)
DECLARE_ID (paramId)
DECLARE_ID (posTicks)      // on POINT (relative to clip start)
DECLARE_ID (value)         // 0..1 normalised
DECLARE_ID (tension)       // -1..1 curve tension to next point

// Autosave. Set on the root of a recovery copy only, never on a saved project.
DECLARE_ID (recoveryOf)    // path of the project the copy shadows; empty = untitled

#undef DECLARE_ID

inline constexpr int ticksPerQuarter = 960;
inline constexpr int ticksPerStep    = 240;   // 16th
inline constexpr int ticksPerBar     = 3840;  // 4/4
} // namespace ids
