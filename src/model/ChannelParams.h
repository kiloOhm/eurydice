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

// The click layer's source is an index like the drive curve, so its caption
// carries the legend a rotary cannot.
inline const juce::String clickSection { "CLICK   0 TICK   1 NOISE   2 PULSE   3 SAMPLE" };

inline const std::vector<Descriptor>& kick()
{
    static const std::vector<Descriptor> table {
        { ids::rootNote,       "ROOT",  { 0.0, 127.0, 1.0 },          60.0,   {},    0, false, "TUNE" },
        { ids::kickStartFreq,  "FROM",  { 40.0, 2000.0, 0.0, 0.4 },   240.0,  " Hz", 0, true,  "BODY" },
        { ids::kickEndFreq,    "TO",    { 20.0, 400.0, 0.0, 0.5 },    48.0,   " Hz", 1, true,  {} },
        { ids::kickPitchDecay, "TIME",  { 0.001, 2.0, 0.0, 0.3 },     0.035,  " s",  3, true,  {} },
        { ids::kickBodyShape,  "SHAPE", { 0.0, 1.0 },                 0.0,    {},    2, true,  {} },
        { ids::kickBodyHarm,   "HARM",  { 0.0, 1.0 },                 0.0,    {},    2, true,  {} },
        { ids::kickBodyPhase,  "PHASE", { 0.0, 1.0 },                 0.0,    {},    2, true,  {} },
        { ids::kickBodyLevel,  "LEVEL", { 0.0, 2.0 },                 1.0,    {},    2, true,  {} },
        { ids::kickAmpDecay,   "DECAY", { 0.02, 4.0, 0.0, 0.35 },     0.5,    " s",  2, true,  "AMP" },
        { ids::kickHold,       "HOLD",  { 0.0, 0.5, 0.0, 0.4 },       0.0,    " s",  3, true,  {} },
        { ids::envShape,       "CURVE", { 0.0, 1.0 },                 1.0,    {},    2, true,  {} },
        { ids::kickPunch,      "PUNCH", { 0.0, 1.0 },                 0.0,    {},    2, true,  {} },
        { ids::kickSubLevel,   "LEVEL", { 0.0, 1.0 },                 0.0,    {},    2, true,  "SUB" },
        { ids::kickSubTune,    "TUNE",  { -24.0, 24.0 },              0.0,    " st", 1, true,  {} },
        { ids::kickSubDecay,   "DECAY", { 0.01, 3.0, 0.0, 0.35 },     0.4,    " s",  2, true,  {} },
        { ids::kickClickLevel, "LEVEL", { 0.0, 1.0 },                 0.3,    {},    2, true,  clickSection },
        { ids::kickClickDecay, "DECAY", { 0.0005, 2.0, 0.0, 0.25 },   0.004,  " s",  4, true,  {} },
        { ids::kickClickFreq,  "FREQ",  { 20.0, 12000.0, 0.0, 0.3 },  1400.0, " Hz", 0, true,  {} },
        { ids::kickClickType,  "TYPE",  { 0.0, 3.0, 1.0 },            0.0,    {},    0, false, {} },
        { ids::kickNoiseLevel, "LEVEL", { 0.0, 1.0 },                 0.12,   {},    2, true,  "NOISE" },
        { ids::kickNoiseDecay, "DECAY", { 0.001, 1.0, 0.0, 0.3 },     0.02,   " s",  3, true,  {} },
        { ids::kickNoiseTone,  "TONE",  { 0.02, 1.0 },                0.4,    {},    2, true,  {} },
        { ids::drive,          "DRIVE", { 0.0, 1.0 },                 0.25,   {},    2, true,  driveSection },
        // An index, not a continuous value: a curve sweeping it would step
        // through unrelated shapes, so it stays off the automation list.
        { ids::driveCurve,     "CURVE", { 0.0, 2.0, 1.0 },            0.0,    {},    0, false, {} },
        { ids::kickEqLowFreq,  "LO Hz", { 20.0, 500.0, 0.0, 0.5 },    90.0,   " Hz", 0, true,  "EQ" },
        { ids::kickEqLowGain,  "LO",    { -18.0, 18.0 },              0.0,    " dB", 1, true,  {} },
        { ids::kickEqMidFreq,  "MID Hz",{ 100.0, 6000.0, 0.0, 0.35 }, 500.0,  " Hz", 0, true,  {} },
        { ids::kickEqMidGain,  "MID",   { -18.0, 18.0 },              0.0,    " dB", 1, true,  {} },
        { ids::kickEqHighFreq, "HI Hz", { 1000.0, 16000.0, 0.0, 0.35 }, 4000.0, " Hz", 0, true, {} },
        { ids::kickEqHighGain, "HI",    { -18.0, 18.0 },              0.0,    " dB", 1, true,  {} },
        { ids::kickComp,       "COMP",  { 0.0, 1.0 },                 0.0,    {},    2, true,  "OUT" },
        { ids::kickLimit,      "LIMIT", { 0.0, 1.0 },                 0.0,    {},    2, true,  {} },
        { ids::kickOutput,     "GAIN",  { -24.0, 12.0 },              0.0,    " dB", 1, true,  {} },
    };
    return table;
}

inline const std::vector<Descriptor>& synth()
{
    static const std::vector<Descriptor> table {
        { ids::oscShape,     "MORPH",  { -2.0, 1.0 },                0.0,    {},     2, true, "OSC   MORPH: -2 SIN  -1 TRI  0 SAW  1 SQR" },
        { ids::oscWarp,      "WARP",   { 0.0, 1.0 },                 0.0,    {},     2, true, {} },
        { ids::osc2Semi,     "SEMI",   { -24.0, 24.0, 1.0 },         0.0,    " st",  0, true, {} },
        { ids::osc2Detune,   "FINE",   { -50.0, 50.0 },              7.0,    " ct",  1, true, {} },
        { ids::osc2Mix,      "OSC2",   { 0.0, 1.0 },                 0.35,   {},     2, true, {} },
        { ids::unisonVoices, "VOICES", { 1.0, 7.0, 1.0 },            1.0,    {},     0, true, "UNISON" },
        { ids::unisonDetune, "DETUNE", { 0.0, 50.0 },                18.0,   " ct",  1, true, {} },
        { ids::unisonWidth,  "WIDTH",  { 0.0, 1.0 },                 0.7,    {},     2, true, {} },
        { ids::subLevel,     "SUB",    { 0.0, 1.0 },                 0.0,    {},     2, true, "LAYERS" },
        { ids::noiseLevel,   "NOISE",  { 0.0, 1.0 },                 0.0,    {},     2, true, {} },
        { ids::filterType,   "TYPE",   { 0.0, 2.0, 1.0 },            0.0,    {},     0, true, "FILTER   0 LP   1 BP   2 HP" },
        { ids::cutoff,       "CUT",    { 40.0, 18000.0, 0.0, 0.28 }, 4000.0, " Hz",  0, true, {} },
        { ids::resonance,    "RES",    { 0.0, 1.0 },                 0.3,    {},     2, true, {} },
        { ids::filterKey,    "KEY",    { 0.0, 1.0 },                 0.0,    {},     2, true, {} },
        { ids::filterEnvAmt, "ENV",    { -1.0, 1.0 },                0.35,   {},     2, true, {} },
        { ids::fenvAttack,   "ATT",    { 0.0, 2.0, 0.0, 0.35 },      0.004,  " s",   3, true, "FILTER ENV" },
        { ids::fenvDecay,    "DEC",    { 0.0, 4.0, 0.0, 0.35 },      0.25,   " s",   2, true, {} },
        { ids::fenvSustain,  "SUS",    { 0.0, 1.0 },                 0.2,    {},     2, true, {} },
        { ids::fenvRelease,  "REL",    { 0.0, 4.0, 0.0, 0.35 },      0.08,   " s",   2, true, {} },
        { ids::attack,       "ATT",    { 0.0, 2.0, 0.0, 0.35 },      0.004,  " s",   3, true, "AMP" },
        { ids::decay,        "DEC",    { 0.0, 4.0, 0.0, 0.35 },      0.25,   " s",   2, true, {} },
        { ids::sustain,      "SUS",    { 0.0, 1.0 },                 0.7,    {},     2, true, {} },
        { ids::release,      "REL",    { 0.0, 4.0, 0.0, 0.35 },      0.08,   " s",   2, true, {} },
        { ids::lfoRate,      "RATE",   { 0.02, 20.0, 0.0, 0.35 },    5.0,    " Hz",  2, true, "LFO   0 CUT   1 PITCH   2 WARP   3 PAN" },
        { ids::lfoAmount,    "AMT",    { 0.0, 1.0 },                 0.0,    {},     2, true, {} },
        { ids::lfoTarget,    "DEST",   { 0.0, 3.0, 1.0 },            0.0,    {},     0, true, {} },
        { ids::glide,        "GLIDE",  { 0.0, 1.0, 0.0, 0.4 },       0.0,    " s",   2, true, "VOICE" },
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
