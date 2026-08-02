#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// Per-note pan rides the note stream as a CC10 immediately before each
// sequenced note-on (the engine emits one for every note, centre included, so
// the generators' latch never carries stale state into preview notes).
// Plugins receive the same CC and interpret it as standard MIDI pan.
namespace notepan
{
constexpr int controller = 10;

inline int toController (float pan) noexcept
{
    return juce::jlimit (0, 127, juce::roundToInt ((pan + 1.0f) * 63.5f));
}

inline float fromController (int value) noexcept
{
    return juce::jlimit (-1.0f, 1.0f, (float) value / 63.5f - 1.0f);
}

// Balance law, matching the engine's channel/insert pan: centre is unity on
// both channels (so existing material is untouched) and panning attenuates
// the far side only.
inline void gains (float pan, float& left, float& right) noexcept
{
    const float p = juce::jlimit (-1.0f, 1.0f, pan);
    left  = juce::jmin (1.0f, 1.0f - p);
    right = juce::jmin (1.0f, 1.0f + p);
}
} // namespace notepan
