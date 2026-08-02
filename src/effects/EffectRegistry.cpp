#include "EffectRegistry.h"
#include "ClipperEffect.h"
#include "CompressorEffect.h"
#include "DelayEffect.h"
#include "EqEffect.h"
#include "FilterEffect.h"
#include "ReverbEffect.h"

namespace fx
{
const std::vector<BuiltinEntry>& builtinEffects()
{
    static const std::vector<BuiltinEntry> entries {
        { ClipperEffect::identifier(),    ClipperEffect::displayName(),    ClipperEffect::specs() },
        { CompressorEffect::identifier(), CompressorEffect::displayName(), CompressorEffect::specs() },
        { FilterEffect::identifier(),     FilterEffect::displayName(),     FilterEffect::specs() },
        { EqEffect::identifier(),         EqEffect::displayName(),         EqEffect::specs() },
        { DelayEffect::identifier(),      DelayEffect::displayName(),      DelayEffect::specs() },
        { ReverbEffect::identifier(),     ReverbEffect::displayName(),     ReverbEffect::specs() },
    };
    return entries;
}

bool isBuiltinId (const juce::String& pluginId)
{
    return pluginId.startsWith ("builtin:");
}

const BuiltinEntry* findBuiltin (const juce::String& pluginId)
{
    for (const auto& entry : builtinEffects())
        if (entry.id == pluginId)
            return &entry;
    return nullptr;
}

std::unique_ptr<BuiltinEffect> createBuiltin (const juce::String& pluginId)
{
    if (pluginId == ClipperEffect::identifier())    return std::make_unique<ClipperEffect>();
    if (pluginId == CompressorEffect::identifier()) return std::make_unique<CompressorEffect>();
    if (pluginId == FilterEffect::identifier())     return std::make_unique<FilterEffect>();
    if (pluginId == EqEffect::identifier())         return std::make_unique<EqEffect>();
    if (pluginId == DelayEffect::identifier())      return std::make_unique<DelayEffect>();
    if (pluginId == ReverbEffect::identifier())     return std::make_unique<ReverbEffect>();
    return nullptr;
}
} // namespace fx
