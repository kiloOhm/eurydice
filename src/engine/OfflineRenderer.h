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
    struct Options
    {
        juce::File wavFile;                 // required
        bool renderMp3 = false;             // needs `lame` binary
        bool renderStems = false;           // one wav per audible insert
        int bitDepth = 24;
        double tailSeconds = 2.0;
        std::function<void (double)> progress;   // 0..1
    };

    struct Result
    {
        bool ok = false;
        juce::String error;
        juce::StringArray writtenFiles;
    };

    static Result render (AudioEngine& engine, ProjectModel& project, const Options& opts);

    static juce::File findLameBinary();
};
