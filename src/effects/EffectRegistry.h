#pragma once

#include <memory>
#include <vector>
#include "BuiltinEffect.h"

namespace fx
{
struct BuiltinEntry
{
    juce::String id;      // "builtin:clipper" — stored as the slot's pluginId
    juce::String name;
    const std::vector<ParamSpec>& specs;
};

// Menu order: the effects a track actually needs first.
const std::vector<BuiltinEntry>& builtinEffects();

bool isBuiltinId (const juce::String& pluginId);
const BuiltinEntry* findBuiltin (const juce::String& pluginId);
std::unique_ptr<BuiltinEffect> createBuiltin (const juce::String& pluginId);
} // namespace fx
