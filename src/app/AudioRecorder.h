#pragma once

#include "AppServices.h"
#include "engine/WavWriter.h"
#include "TakeSplitter.h"
#include "model/UndoGesture.h"

// Records the hardware input to a WAV while the transport plays, then drops
// the take onto the playlist as an audio clip. When the transport loop was
// armed, each loop pass becomes its own take (see TakeSplitter.h): the latest
// pass lands unmuted, earlier passes muted on the tracks below.
// Message-thread only.
class AudioRecorder
{
public:
    explicit AudioRecorder (AppServices& s) : services (s) {}

    ~AudioRecorder() { discard(); }

    bool isRecording() const { return threadedWriter != nullptr; }

    static juce::File recordingsDir()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                       .getChildFile ("Eurydice Recordings");
        dir.createDirectory();
        return dir;
    }

    void start()
    {
        if (isRecording())
            return;

        const auto file = recordingsDir().getNonexistentChildFile ("take", ".wav");
        const double sr = services.engine.getSampleRate();
        auto writer = wavwriter::forFile (file, sr, 2, 24);
        if (writer == nullptr)
            return;

        writeThread.startThread();
        threadedWriter = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (
            writer.release(), writeThread, 1 << 17);

        recordFile = file;
        recordStartTick = services.engine.getPositionTicks();
        recordSampleRate = sr;
        // Loop state is captured here: toggling the loop mid-recording would
        // desync the split math either way, so the state at start wins.
        loopWasEnabled = services.project.isLoopEnabled();
        loopStartAtRecord = services.project.getLoopStart();
        loopEndAtRecord = services.project.getLoopEnd();
        services.engine.setRecorder (threadedWriter.get());
    }

    // Finalises the recording and places it on the playlist — one clip per
    // loop pass, or a single clip when the loop was off. One undo step.
    void stopAndPlaceClip()
    {
        if (! isRecording())
            return;

        services.engine.setRecorder (nullptr);
        threadedWriter = nullptr;   // flushes + closes the file
        writeThread.stopThread (5000);

        juce::int64 totalSamples = 0;
        {
            juce::WavAudioFormat wav;
            auto stream = recordFile.createInputStream();
            if (std::unique_ptr<juce::AudioFormatReader> reader
                    { stream != nullptr ? wav.createReaderFor (stream.release(), true) : nullptr })
                totalSamples = reader->lengthInSamples;
        }

        const auto minSamples = (juce::int64) (0.05 * recordSampleRate);
        auto& project = services.project;
        const double tps = (project.getTempo() / 60.0) * ids::ticksPerQuarter / recordSampleRate;

        auto allTakes = takes::splitIntoTakes (totalSamples, recordStartTick,
                                               loopStartAtRecord, loopEndAtRecord,
                                               loopWasEnabled, tps, minSamples);
        if (totalSamples < minSamples || allTakes.empty())
        {
            recordFile.deleteFile();
            return;
        }

        juce::Array<juce::File> takeFiles;
        if (allTakes.size() > 1)
            takeFiles = takes::writeTakeFiles (recordFile, allTakes);

        if (takeFiles.isEmpty())
        {
            // Single pass — or splitting failed, in which case the whole file
            // placed as one clip beats losing the recording.
            if (allTakes.size() > 1)
                allTakes = takes::splitIntoTakes (totalSamples, recordStartTick,
                                                  0, 0, false, tps, minSamples);
            takeFiles.add (recordFile);
        }
        else
        {
            recordFile.deleteFile();
        }

        const undoGesture::Scoped step (project, "Record takes");
        for (const auto& placement : takes::planPlacement (project.playlist(), allTakes))
        {
            const auto& take = allTakes[(size_t) placement.takeIndex];
            auto clip = project.addPlaylistClip ("audio", placement.trackIndex,
                                                 take.startTicks, take.lengthTicks);
            clip.setProperty (ids::audioPath,
                              takeFiles[placement.takeIndex].getFullPathName(), nullptr);
            clip.setProperty (ids::stretchRatio, 1.0, nullptr);
            clip.setProperty (ids::audioOffsetTicks, 0, nullptr);
            if (placement.muted)
                clip.setProperty (ids::muted, true, nullptr);
        }
    }

    void discard()
    {
        if (! isRecording())
            return;
        services.engine.setRecorder (nullptr);
        threadedWriter = nullptr;
        writeThread.stopThread (5000);
        recordFile.deleteFile();
    }

private:
    AppServices& services;
    juce::TimeSliceThread writeThread { "RecordWriter" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter;
    juce::File recordFile;
    double recordStartTick = 0.0;
    double recordSampleRate = 44100.0;
    bool loopWasEnabled = false;
    int loopStartAtRecord = 0;
    int loopEndAtRecord = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioRecorder)
};
