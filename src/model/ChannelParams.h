#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include "Ids.h"

// The knobs the built-in generators expose. One table feeds the channel
// editors (which draw them) and the automation layer (which records and plays
// them back), so a knob can never exist in one and be missing from the other.
namespace channelparams
{
struct Descriptor
{
    juce::Identifier id;
    juce::String caption;
    juce::NormalisableRange<double> range;
    double defaultValue = 0.0;
    juce::String suffix;
    int decimals = 2;
    // Automation reaches the audio thread as a float atomic; rootNote is an
    // integer the generator only re-reads on a snapshot rebuild.
    bool automatable = true;

    juce::NormalisableRange<float> floatRange() const
    {
        return { (float) range.start, (float) range.end, (float) range.interval,
                 (float) range.skew, range.symmetricSkew };
    }

    double toNormalised (double value) const
    {
        return range.convertTo0to1 (juce::jlimit (range.start, range.end, value));
    }
};

inline const std::vector<Descriptor>& sampler()
{
    static const std::vector<Descriptor> table {
        { ids::rootNote,  "ROOT", { 0.0, 127.0, 1.0 },          60.0,    {},     0, false },
        { ids::attack,    "ATT",  { 0.0, 2.0, 0.0, 0.35 },      0.001,   " s",   3 },
        { ids::decay,     "DEC",  { 0.0, 4.0, 0.0, 0.35 },      0.0,     " s",   2 },
        { ids::sustain,   "SUS",  { 0.0, 1.0 },                 1.0,     {},     2 },
        { ids::release,   "REL",  { 0.0, 4.0, 0.0, 0.35 },      0.02,    " s",   2 },
        { ids::cutoff,    "CUT",  { 40.0, 20000.0, 0.0, 0.28 }, 20000.0, " Hz",  0 },
        { ids::resonance, "RES",  { 0.0, 1.0 },                 0.0,     {},     2 },
    };
    return table;
}

inline const std::vector<Descriptor>& synth()
{
    static const std::vector<Descriptor> table {
        { ids::oscShape,     "SHAPE",  { 0.0, 1.0 },                 0.0,    {},     2 },
        { ids::osc2Detune,   "DETUNE", { -50.0, 50.0 },              7.0,    " ct",  1 },
        { ids::osc2Mix,      "OSC2",   { 0.0, 1.0 },                 0.35,   {},     2 },
        { ids::cutoff,       "CUT",    { 40.0, 18000.0, 0.0, 0.28 }, 4000.0, " Hz",  0 },
        { ids::resonance,    "RES",    { 0.0, 1.0 },                 0.3,    {},     2 },
        { ids::filterEnvAmt, "ENV",    { 0.0, 1.0 },                 0.35,   {},     2 },
        { ids::attack,       "ATT",    { 0.0, 2.0, 0.0, 0.35 },      0.004,  " s",   3 },
        { ids::decay,        "DEC",    { 0.0, 4.0, 0.0, 0.35 },      0.25,   " s",   2 },
        { ids::sustain,      "SUS",    { 0.0, 1.0 },                 0.7,    {},     2 },
        { ids::release,      "REL",    { 0.0, 4.0, 0.0, 0.35 },      0.08,   " s",   2 },
    };
    return table;
}

inline const std::vector<Descriptor>& forChannelType (const juce::String& channelType)
{
    static const std::vector<Descriptor> none;
    if (channelType == "sampler") return sampler();
    if (channelType == "synth")   return synth();
    return none;
}

inline const Descriptor* find (const juce::String& channelType, const juce::String& paramId)
{
    for (const auto& descriptor : forChannelType (channelType))
        if (descriptor.id.toString() == paramId)
            return &descriptor;
    return nullptr;
}
} // namespace channelparams
