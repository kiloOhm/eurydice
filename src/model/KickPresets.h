#pragma once

#include <vector>
#include <juce_data_structures/juce_data_structures.h>
#include "Ids.h"
#include "engine/KickDsp.h"

// The kick channel's factory bank, browsed by category in the editor. A preset
// is an absolute starting point: applying one writes every kick parameter,
// falling back to the descriptor default for anything it does not name, so
// switching presets can never leave a stray knob from the last one behind.
//
// Deliberately left alone by a preset load: the channel's root note (the kick
// is tuned to the track, not to the preset) and its click sample path.
namespace kickpresets
{
struct Preset
{
    juce::String name;
    juce::String category;
    std::vector<std::pair<juce::Identifier, double>> values;
    // Empty envelopes mean the analytic decay; presets that want a drawn shape
    // carry their points here.
    kickdsp::Envelope pitchEnvelope {};
    kickdsp::Envelope ampEnvelope {};
};

const std::vector<Preset>& all();

// Categories in bank order, deduplicated.
juce::StringArray categories();

const Preset* find (const juce::String& name);

// Writes a preset onto a kick channel in the caller's undo transaction.
void apply (juce::ValueTree channel, const Preset&, juce::UndoManager*);
} // namespace kickpresets
