#pragma once

#include <memory>
#include <vector>
#include "BuiltinEffect.h"

namespace fx
{
// A named starting point for a built-in effect: parameter values the editor
// writes onto the SLOT tree in one undoable step. Values deliberately never
// include fxMix, so loading a preset can't blow up a send level.
struct BuiltinPreset
{
    juce::String name;
    std::vector<std::pair<juce::Identifier, double>> values;
};

struct BuiltinEntry
{
    juce::String id;      // "builtin:clipper" — stored as the slot's pluginId
    juce::String name;
    const std::vector<ParamSpec>& specs;
    const std::vector<BuiltinPreset>* presets = nullptr;
};

// Menu order: the effects a track actually needs first.
const std::vector<BuiltinEntry>& builtinEffects();

bool isBuiltinId (const juce::String& pluginId);
const BuiltinEntry* findBuiltin (const juce::String& pluginId);
std::unique_ptr<BuiltinEffect> createBuiltin (const juce::String& pluginId);
} // namespace fx
