#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include "Ids.h"

// Model helpers for the drum-machine channel. Pads are PAD children of the
// CHANNEL tree, ordered by pad index. The grid shape (padRows x padCols) is an
// editor concern only: every pad keeps playing whatever the grid looks like,
// so shrinking the grid never loses sounds or note mappings.
namespace drumpads
{
inline constexpr int maxPads = 64;
inline constexpr int maxGridSide = 8;
inline constexpr int defaultBaseNote = 36;   // C1, where FPC/MPC pads start

inline int padCount (const juce::ValueTree& channel)
{
    int n = 0;
    for (const auto& child : channel)
        if (child.hasType (ids::PAD))
            ++n;
    return n;
}

inline juce::ValueTree getPad (const juce::ValueTree& channel, int index)
{
    int n = 0;
    for (const auto& child : channel)
        if (child.hasType (ids::PAD) && n++ == index)
            return child;
    return {};
}

// Pad 0 sits bottom-left and indices run left-to-right, bottom-to-top — the
// layout every pad controller ships with.
inline int padIndexForCell (int column, int visualRow, int rows, int cols)
{
    return (rows - 1 - visualRow) * cols + column;
}

inline int gridRows (const juce::ValueTree& channel)
{
    return juce::jlimit (1, maxGridSide, (int) channel.getProperty (ids::padRows, 4));
}

inline int gridCols (const juce::ValueTree& channel)
{
    return juce::jlimit (1, maxGridSide, (int) channel.getProperty (ids::padCols, 4));
}

inline int baseNote (const juce::ValueTree& channel)
{
    return juce::jlimit (0, 127, (int) channel.getProperty (ids::padBaseNote, defaultBaseNote));
}

// Grows the pad list to at least `count` pads; new pads get consecutive notes
// above the base note. Never removes pads.
inline void ensurePadCount (juce::ValueTree channel, int count, juce::UndoManager* undo)
{
    const int base = baseNote (channel);
    for (int i = padCount (channel); i < juce::jmin (count, maxPads); ++i)
    {
        juce::ValueTree pad (ids::PAD);
        pad.setProperty (ids::name, "Pad " + juce::String (i + 1), undo);
        pad.setProperty (ids::key, juce::jlimit (0, 127, base + i), undo);
        pad.setProperty (ids::volume, 0.9, undo);
        pad.setProperty (ids::pan, 0.0, undo);
        pad.setProperty (ids::tune, 0.0, undo);
        pad.setProperty (ids::choke, 0, undo);
        channel.appendChild (pad, undo);
    }
}

// Renumbers every pad from the base note in pad order, for matching a
// controller that just sends consecutive notes.
inline void autoMapNotes (juce::ValueTree channel, juce::UndoManager* undo)
{
    const int base = baseNote (channel);
    int i = 0;
    for (auto child : channel)
        if (child.hasType (ids::PAD))
            child.setProperty (ids::key, juce::jlimit (0, 127, base + i++), undo);
}

// A fresh drum channel: 4x4 grid, the first pads filled with the synthesised
// kit so the channel makes sound before any sample is loaded.
inline void initialiseDrumChannel (juce::ValueTree channel, juce::UndoManager* undo)
{
    channel.setProperty (ids::padRows, 4, undo);
    channel.setProperty (ids::padCols, 4, undo);
    channel.setProperty (ids::padBaseNote, defaultBaseNote, undo);
    ensurePadCount (channel, 16, undo);

    const std::pair<const char*, const char*> kit[] = {
        { "Kick", "kick" }, { "Snare", "snare" }, { "Clap", "clap" }, { "Hat", "hat" } };
    for (int i = 0; i < 4; ++i)
    {
        auto pad = getPad (channel, i);
        pad.setProperty (ids::name, kit[i].first, undo);
        pad.setProperty (ids::synthDrum, kit[i].second, undo);
    }
}

inline juce::String noteName (int key)
{
    return juce::MidiMessage::getMidiNoteName (key, true, true, 4);
}
} // namespace drumpads
