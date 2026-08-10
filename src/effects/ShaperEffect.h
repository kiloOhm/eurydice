#pragma once

#include <array>
#include <atomic>
#include <juce_dsp/juce_dsp.h>
#include "BuiltinEffect.h"
#include "Crossover.h"
#include "EffectRegistry.h"
#include "ShaperWave.h"

// The drawn-modulation effect: instead of choosing an LFO shape you draw the
// wave, it loops locked to the beat grid, and it drives one target — volume,
// pan, width, a filter sweep or drive. Optionally only one frequency band is
// shaped and the rest of the spectrum passes through untouched, so a 1/16 gate
// can chop the low end while the cymbals ride over the top of it.
//
// Every target reads the wave the same way: fx::ShaperWave::valueAt returns
// 0..1, the depth knob collapses that towards neutralValue() for the target,
// and neutral means "leave the signal alone". One depth law, six targets, and
// the editor can draw the neutral line because it asks for the same number.
//
// The wave itself lives on the SLOT tree as a string rather than as a
// ParamSpec: the message thread renders it into a lookup table and publishes
// that, and the audio thread picks up one table for the whole block.
class ShaperEffect : public BuiltinEffect
{
public:
    enum class Target { volume = 0, pan, width, lowPass, highPass, drive };
    enum class Band { full = 0, low, mid, high };

    // What the top and bottom of the curve mean for a target, for the editor's
    // axis captions.
    struct AxisLabels { juce::String top, bottom; };

    ShaperEffect();

    static const juce::String& identifier();
    static const juce::String& displayName();
    static const std::vector<fx::ParamSpec>& specs();
    static const std::vector<fx::BuiltinPreset>& presets();
    static const juce::StringArray& targetNames();
    static const juce::StringArray& bandNames();
    static const juce::StringArray& gridNames();

    // Wave value that leaves the target untouched: the line the editor draws
    // and the level the depth knob collapses the curve towards.
    static float neutralValue (int target) noexcept;
    static AxisLabels axisLabels (int target);

    // Editor grid, in steps per loop; 0 = no snapping.
    static int gridSteps (int index) noexcept;

    void prepare (double sampleRate, int maxBlockSize) override;
    void process (juce::AudioBuffer<float>& stereoBus, int numSamples, const Context& context) override;
    void reset() override;

    const std::vector<fx::ParamSpec>& getParamSpecs() const override { return specs(); }
    void setParameter (const juce::Identifier& paramId, double value) override;
    void applyExtraState (const juce::ValueTree& slot) override;

    // Where the wave is now and what it is doing there, for the editor's
    // playhead. Written once per block by the audio thread.
    float getDisplayPhase() const noexcept { return displayPhase.load (std::memory_order_relaxed); }
    float getDisplayValue() const noexcept { return displayValue.load (std::memory_order_relaxed); }

private:
    static constexpr int tableSize = 1024;
    // The message thread cycles through the tables and publishes an index, so
    // the audio thread never reads one that is being written. Four deep because
    // edits arrive at mouse rate: lapping the reader would take four of them
    // inside a single audio block.
    static constexpr int numTables = 4;
    static constexpr int controlBlock = 32;

    using Table = std::array<float, tableSize>;

    void publishWave();
    static float lookup (const Table& table, double phase) noexcept;

    // Advances the phase by one sample and returns the glided wave value with
    // invert and depth already folded in.
    float nextValue (const Table& table, double increment, double offset,
                     float neutral, float depth, bool inverted, float glide) noexcept;

    void applyGainStage (juce::AudioBuffer<float>& buffer, int numCh, int index,
                         Target target, float value) noexcept;
    void filterControlBlock (juce::AudioBuffer<float>& buffer, int numCh,
                             int start, int len, Target target, float value) noexcept;

    std::atomic<int> targetIndex { 0 };
    std::atomic<int> bandIndex { 0 };
    std::atomic<float> depthAmount { 1.0f };
    std::atomic<float> smoothMs { 3.0f };
    std::atomic<float> resonance { 0.2f };
    std::atomic<float> phaseDeg { 0.0f };
    std::atomic<float> mix { 1.0f };
    std::atomic<float> rateHz { 1.0f };
    std::atomic<int> division { 8 };   // 1/4
    std::atomic<bool> synced { true };
    std::atomic<bool> invert { false };
    std::atomic<float> crossLoHz { 150.0f };
    std::atomic<float> crossHiHz { 2500.0f };
    std::atomic<bool> crossoverDirty { true };

    std::atomic<float> displayPhase { 0.0f };
    std::atomic<float> displayValue { 1.0f };

    std::array<Table, numTables> tables {};
    std::atomic<int> liveTable { 0 };
    int nextTable = 1;                 // message thread only
    fx::ShaperWave wave;               // message thread only
    juce::String appliedWaveText;      // message thread only

    double sampleRateHz = 44100.0;
    double lfoPhase = 0.0;
    float smoothedValue = 1.0f;

    // Switching target or band mode brings a different filter/crossover into
    // the path; both are cleared so stale state can't ring into the new one.
    int activeTarget = -1;
    bool activeSplit = false;

    juce::dsp::StateVariableTPTFilter<float> filter;
    std::array<fx::Crossover3, 2> splits;
    std::array<juce::AudioBuffer<float>, 3> bandBuffers;
    juce::AudioBuffer<float> dry;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShaperEffect)
};
