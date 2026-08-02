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
    // Heading this knob sits under in the editor. Empty continues the previous
    // section, so the table also defines the layout.
    juce::String section;

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

// The drive curve is an index, not a continuous value, so the section caption
// spells the choices out — a rotary can only show the number.
inline const juce::String driveSection { "DRIVE   0 SOFT   1 HARD   2 FOLD" };

inline const std::vector<Descriptor>& sampler()
{
    static const std::vector<Descriptor> table {
        { ids::rootNote,      "ROOT",  { 0.0, 127.0, 1.0 },          60.0,    {},    0, false, "SAMPLE" },
        { ids::sampleStart,   "START", { 0.0, 1.0 },                 0.0,     {},    3, true,  {} },
        { ids::sampleEnd,     "END",   { 0.0, 1.0 },                 1.0,     {},    3, true,  {} },
        { ids::pitchEnvDepth, "DEPTH", { -48.0, 48.0 },              0.0,     " st", 1, true,  "PITCH ENV" },
        { ids::pitchEnvDecay, "PDEC",  { 0.001, 2.0, 0.0, 0.35 },    0.08,    " s",  3, true,  {} },
        { ids::attack,        "ATT",   { 0.0, 2.0, 0.0, 0.35 },      0.001,   " s",  3, true,  "AMP" },
        { ids::decay,         "DEC",   { 0.0, 4.0, 0.0, 0.35 },      0.0,     " s",  2, true,  {} },
        { ids::sustain,       "SUS",   { 0.0, 1.0 },                 1.0,     {},    2, true,  {} },
        { ids::release,       "REL",   { 0.0, 4.0, 0.0, 0.35 },      0.02,    " s",  2, true,  {} },
        { ids::envShape,      "SHAPE", { 0.0, 1.0 },                 0.0,     {},    2, true,  {} },
        { ids::cutoff,        "CUT",   { 40.0, 20000.0, 0.0, 0.28 }, 20000.0, " Hz", 0, true,  "FILTER" },
        { ids::resonance,     "RES",   { 0.0, 1.0 },                 0.0,     {},    2, true,  {} },
        { ids::drive,         "DRIVE", { 0.0, 1.0 },                 0.0,     {},    2, true,  driveSection },
        { ids::driveCurve,    "CURVE", { 0.0, 2.0, 1.0 },            0.0,     {},    0, true,  {} },
    };
    return table;
}

inline const std::vector<Descriptor>& kick()
{
    static const std::vector<Descriptor> table {
        { ids::kickStartFreq,  "START", { 40.0, 800.0, 0.0, 0.4 },  240.0, " Hz", 0, true, "BODY" },
        { ids::kickEndFreq,    "END",   { 20.0, 200.0, 0.0, 0.5 },  48.0,  " Hz", 0, true, {} },
        { ids::kickPitchDecay, "PDEC",  { 0.002, 0.5, 0.0, 0.35 },  0.035, " s",  3, true, {} },
        { ids::kickAmpDecay,   "ADEC",  { 0.02, 3.0, 0.0, 0.35 },   0.5,   " s",  2, true, {} },
        { ids::kickBodyShape,  "SHAPE", { 0.0, 1.0 },               0.0,   {},    2, true, {} },
        { ids::kickClickLevel, "LEVEL", { 0.0, 1.0 },               0.3,   {},    2, true, "CLICK" },
        { ids::kickClickDecay, "DECAY", { 0.0005, 0.05, 0.0, 0.4 }, 0.004, " s",  4, true, {} },
        { ids::kickNoiseLevel, "LEVEL", { 0.0, 1.0 },               0.12,  {},    2, true, "NOISE" },
        { ids::kickNoiseDecay, "DECAY", { 0.002, 0.5, 0.0, 0.35 },  0.02,  " s",  3, true, {} },
        { ids::drive,          "DRIVE", { 0.0, 1.0 },               0.25,  {},    2, true, driveSection },
        { ids::driveCurve,     "CURVE", { 0.0, 2.0, 1.0 },          0.0,   {},    0, true, {} },
        { ids::envShape,       "ENV",   { 0.0, 1.0 },               1.0,   {},    2, true, {} },
    };
    return table;
}

inline const std::vector<Descriptor>& synth()
{
    static const std::vector<Descriptor> table {
        { ids::oscShape,     "SHAPE",  { 0.0, 1.0 },                 0.0,    {},     2, true, "OSCILLATORS" },
        { ids::osc2Detune,   "DETUNE", { -50.0, 50.0 },              7.0,    " ct",  1, true, {} },
        { ids::osc2Mix,      "OSC2",   { 0.0, 1.0 },                 0.35,   {},     2, true, {} },
        { ids::cutoff,       "CUT",    { 40.0, 18000.0, 0.0, 0.28 }, 4000.0, " Hz",  0, true, "FILTER" },
        { ids::resonance,    "RES",    { 0.0, 1.0 },                 0.3,    {},     2, true, {} },
        { ids::filterEnvAmt, "ENV",    { 0.0, 1.0 },                 0.35,   {},     2, true, {} },
        { ids::attack,       "ATT",    { 0.0, 2.0, 0.0, 0.35 },      0.004,  " s",   3, true, "ENVELOPE" },
        { ids::decay,        "DEC",    { 0.0, 4.0, 0.0, 0.35 },      0.25,   " s",   2, true, {} },
        { ids::sustain,      "SUS",    { 0.0, 1.0 },                 0.7,    {},     2, true, {} },
        { ids::release,      "REL",    { 0.0, 4.0, 0.0, 0.35 },      0.08,   " s",   2, true, {} },
    };
    return table;
}

inline const std::vector<Descriptor>& forChannelType (const juce::String& channelType)
{
    static const std::vector<Descriptor> none;
    if (channelType == "sampler") return sampler();
    if (channelType == "synth")   return synth();
    if (channelType == "kick")    return kick();
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
