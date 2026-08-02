#pragma once

#include <vector>
#include <juce_data_structures/juce_data_structures.h>
#include "Effect.h"

namespace fx
{
// One built-in effect parameter: how it is stored on the SLOT tree and how the
// generic editor should present it.
struct ParamSpec
{
    ParamSpec (juce::Identifier paramId, juce::String paramName,
               double min, double max, double skewFactor, double defaultVal,
               juce::String valueSuffix = {}, int numDecimals = 2,
               juce::StringArray choiceList = {}, bool chooseInsert = false,
               bool breakRow = false)
        : id (std::move (paramId)), name (std::move (paramName)),
          minValue (min), maxValue (max), skew (skewFactor), defaultValue (defaultVal),
          suffix (std::move (valueSuffix)), decimals (numDecimals),
          choices (std::move (choiceList)), insertChooser (chooseInsert), startsRow (breakRow)
    {
    }

    juce::Identifier id;
    juce::String name;
    double minValue = 0.0;
    double maxValue = 1.0;
    double skew = 1.0;
    double defaultValue = 0.0;
    juce::String suffix;
    int decimals = 2;
    juce::StringArray choices;    // non-empty: drawn as a combo box
    bool insertChooser = false;   // combo filled from the mixer inserts (-1 = off)
    bool startsRow = false;       // editor hint: begin a fresh row here
};

// Tempo-synced note lengths, shortest first. Index is what gets stored on the
// SLOT tree, so entries may only ever be appended.
inline const juce::StringArray& syncDivisionNames()
{
    static const juce::StringArray names { "1/32", "1/16T", "1/16", "1/8T", "1/16.",
                                           "1/8", "1/4T", "1/8.", "1/4", "1/2T",
                                           "1/4.", "1/2", "1/1", "2/1", "4/1" };
    return names;
}

// Length of a division in quarter notes.
inline double syncDivisionQuarters (int index)
{
    static const double quarters[] = { 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.375,
                                       0.5, 2.0 / 3.0, 0.75, 1.0, 4.0 / 3.0,
                                       1.5, 2.0, 4.0, 8.0, 16.0 };
    const int n = (int) (sizeof (quarters) / sizeof (double));
    return quarters[juce::jlimit (0, n - 1, index)];
}

// Dry/wet gain pair for a 0..1 mix control.
struct MixGains
{
    float dry = 0.0f;
    float wet = 1.0f;
};

inline MixGains mixGains (float mix) noexcept
{
    const float m = juce::jlimit (0.0f, 1.0f, mix);
    return { 1.0f - m, m };
}
} // namespace fx

// Base for Eurydice's own effects: a fixed parameter list that is mirrored on
// the SLOT ValueTree, pushed into atomics on the message thread.
class BuiltinEffect : public Effect
{
public:
    virtual const std::vector<fx::ParamSpec>& getParamSpecs() const = 0;
    virtual void setParameter (const juce::Identifier& paramId, double value) = 0;

    void applyParameters (juce::ValueTree slot)
    {
        for (const auto& spec : getParamSpecs())
            setParameter (spec.id, (double) slot.getProperty (spec.id, spec.defaultValue));
    }

    static void writeDefaults (juce::ValueTree slot, const std::vector<fx::ParamSpec>& specs,
                               juce::UndoManager* undo)
    {
        for (const auto& spec : specs)
            slot.setProperty (spec.id, spec.defaultValue, undo);
    }
};
