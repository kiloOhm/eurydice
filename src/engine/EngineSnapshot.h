#pragma once

#include <memory>
#include <vector>
#include "Generator.h"
#include "plugins/HostedPlugin.h"

// Immutable snapshot of everything the audio thread needs to play.
// Built on the message thread from the ValueTree model, swapped in via
// atomic shared_ptr. The audio thread only ever reads it.

struct SeqNote
{
    int   channelIndex = 0;    // index into EngineSnapshot::channels
    int   key = 60;
    int   startTicks = 0;      // pattern-relative
    int   lengthTicks = 240;
    float velocity = 0.78f;
    float pan = 0.0f;
};

struct PatternSnapshot
{
    int id = 0;
    int lengthTicks = 3840;
    std::vector<SeqNote> notes;   // sorted by startTicks
};

struct ChannelSnapshot
{
    int   id = 0;
    float volume = 0.78f;
    float pan = 0.0f;
    bool  audible = true;          // mute/solo already resolved
    int   insertIndex = 0;
    std::shared_ptr<Generator> generator;   // may be null (empty channel)
};

struct SendSnapshot
{
    int   destInsert = 0;
    float level = 1.0f;
};

struct InsertSnapshot
{
    float volume = 0.8f;
    float pan = 0.0f;
    bool  mute = false;
    std::vector<SendSnapshot> sends;
    std::vector<std::shared_ptr<HostedPlugin>> effects;   // in slot order, bypassed ones omitted
};

struct AutomationPoint
{
    double posTicks = 0.0;   // relative to clip start
    float  value = 0.0f;     // normalised 0..1
    float  tension = 0.0f;   // -1..1, shapes the segment to the NEXT point
};

struct AutomationSnapshot
{
    enum class Kind { channelVolume, channelPan, insertVolume, insertPan, pluginParam };
    Kind kind = Kind::channelVolume;
    int  channelIndex = -1;                       // for channel* kinds
    int  insertIndex = -1;                        // for insert* kinds
    juce::AudioProcessorParameter* param = nullptr;   // pluginParam
    std::shared_ptr<HostedPlugin> keepAlive;          // pins param's owner
    std::vector<AutomationPoint> points;              // sorted by posTicks

    float valueAt (double localTicks) const
    {
        if (points.empty())
            return 0.0f;
        if (localTicks <= points.front().posTicks)
            return points.front().value;
        if (localTicks >= points.back().posTicks)
            return points.back().value;
        for (size_t i = 1; i < points.size(); ++i)
        {
            const auto& a = points[i - 1];
            const auto& b = points[i];
            if (localTicks <= b.posTicks)
            {
                const double span = juce::jmax (1.0, b.posTicks - a.posTicks);
                double u = (localTicks - a.posTicks) / span;
                // tension > 0 curves fast-then-slow, < 0 slow-then-fast
                const double exponent = std::pow (4.0, (double) -a.tension);
                u = std::pow (u, exponent);
                return a.value + (b.value - a.value) * (float) u;
            }
        }
        return points.back().value;
    }
};

struct ClipSnapshot
{
    enum class Type { pattern, audio, automation };
    Type type = Type::pattern;
    int  startTicks = 0;
    int  lengthTicks = 0;
    int  patternIndex = -1;      // into EngineSnapshot::patterns
    int  automationIndex = -1;   // into EngineSnapshot::automations

    // audio clips: pre-stretched buffer at the engine sample rate
    std::shared_ptr<const juce::AudioBuffer<float>> audio;
    double audioOffsetSamples = 0.0;   // trim into the buffer
    float  audioGain = 1.0f;
};

struct EngineSnapshot
{
    double tempo = 140.0;
    double swing = 0.0;
    bool   songMode = false;
    int    activePatternIndex = 0;

    // Loop range. loopEnabled is only ever true for a positive-length range,
    // so the audio thread can trust it without re-validating.
    bool   loopEnabled = false;
    int    loopStartTicks = 0;
    int    loopEndTicks = 0;

    std::vector<PatternSnapshot> patterns;
    std::vector<ChannelSnapshot> channels;
    std::vector<InsertSnapshot>  inserts;      // index 0 = master
    std::vector<int>             insertOrder;  // topological processing order, master last
    std::vector<ClipSnapshot>    clips;        // playlist, sorted by startTicks
    std::vector<AutomationSnapshot> automations;
    int songLengthTicks = 0;
};
