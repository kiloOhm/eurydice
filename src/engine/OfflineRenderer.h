#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include "AudioEngine.h"
#include "model/ProjectModel.h"

// Faster-than-realtime render of the project through the live engine graph.
// The engine is detached from the audio device for the duration, so the same
// generator/plugin instances render without concurrency. Call from the
// message thread (blocks; renders are typically seconds).
class OfflineRenderer
{
public:
    enum class Stems
    {
        none,
        perInsert,    // one wav per insert that carries a channel
        perChannel    // one wav per rack channel, in isolation
    };

    struct Options
    {
        juce::File wavFile;                 // required
        bool renderMp3 = false;             // needs `lame` binary
        Stems stems = Stems::none;
        // Bound the render to the project's loop range instead of the whole
        // pattern/arrangement. Without it the loop is ignored entirely.
        bool loopRangeOnly = false;
        bool normalise = false;
        double normaliseTargetDb = -0.3;    // peak target, dBFS
        int bitDepth = 24;
        int sampleRate = 0;                 // 0 = render at the engine rate
        double tailSeconds = 2.0;
        std::function<void (double)> progress;   // 0..1
    };

    struct Result
    {
        bool ok = false;
        juce::String error;                 // also carries warnings when ok
        juce::StringArray writtenFiles;
    };

    static Result render (AudioEngine& engine, ProjectModel& project, const Options& opts);

    static juce::File findLameBinary();
};
