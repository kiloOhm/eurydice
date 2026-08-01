#pragma once

#include "AppServices.h"

// Records the hardware input to a WAV while the transport plays, then drops
// the take onto the playlist as an audio clip. Message-thread only.
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
        auto stream = file.createOutputStream();
        if (stream == nullptr)
            return;

        const double sr = services.engine.getSampleRate();
        juce::WavAudioFormat wav;
        auto* writer = wav.createWriterFor (stream.get(), sr, 2, 24, {}, 0);
        if (writer == nullptr)
            return;
        stream.release();

        writeThread.startThread();
        threadedWriter = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (
            writer, writeThread, 1 << 17);

        recordFile = file;
        recordStartTick = services.engine.getPositionTicks();
        services.engine.setRecorder (threadedWriter.get());
    }

    // Finalises the take and places it on the playlist.
    void stopAndPlaceClip()
    {
        if (! isRecording())
            return;

        services.engine.setRecorder (nullptr);
        threadedWriter = nullptr;   // flushes + closes the file
        writeThread.stopThread (5000);

        auto& project = services.project;
        const double seconds = services.audioClips.getNaturalSeconds (recordFile.getFullPathName());
        if (seconds < 0.05)
        {
            recordFile.deleteFile();
            return;
        }

        const double tps = (project.getTempo() / 60.0) * ids::ticksPerQuarter;
        const int lengthTicks = juce::jmax (ids::ticksPerStep, (int) (seconds * tps));

        // First track free at the record start position.
        int trackIndex = 0;
        for (int i = 0; i < project.numPlaylistTracks(); ++i)
        {
            bool free = true;
            for (const auto clip : project.playlist().getChild (i))
                if (clip.hasType (ids::CLIP)
                    && (int) clip[ids::startTicks] < (int) recordStartTick + lengthTicks
                    && (int) clip[ids::startTicks] + (int) clip[ids::lengthTicks] > (int) recordStartTick)
                    free = false;
            if (free) { trackIndex = i; break; }
        }

        auto clip = project.addPlaylistClip ("audio", trackIndex,
                                             (int) recordStartTick, lengthTicks);
        clip.setProperty (ids::audioPath, recordFile.getFullPathName(), nullptr);
        clip.setProperty (ids::stretchRatio, 1.0, nullptr);
        clip.setProperty (ids::audioOffsetTicks, 0, nullptr);
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioRecorder)
};
