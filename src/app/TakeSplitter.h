#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>
#include <vector>
#include "model/Ids.h"

// Loop-record takes. A recording made with the transport loop armed is still
// one continuous file; the engine wraps sample-accurately at the loop end, so
// the file splits into passes afterwards by pure tick math — nothing extra on
// the audio thread. All functions here are pure so they test without a device.
namespace takes
{

// One loop pass: a region of the recorded file plus its playlist position.
struct Take
{
    juce::int64 startSample = 0;
    juce::int64 numSamples  = 0;
    int startTicks  = 0;
    int lengthTicks = 0;
};

// Splits totalSamples of recording into loop passes. Mirrors the engine wrap:
// a pass ends on the first sample at or past the loop end, then restarts at
// the loop start. Without an active loop — or when recording began at or past
// the loop end, where the engine never wraps — the whole file is one take.
// Policy: a partial final pass is kept as a shorter take when it carries at
// least minTakeSamples of audio; a shorter tail is dropped.
inline std::vector<Take> splitIntoTakes (juce::int64 totalSamples,
                                         double recordStartTick,
                                         int loopStartTick, int loopEndTick,
                                         bool loopActive,
                                         double ticksPerSample,
                                         juce::int64 minTakeSamples)
{
    std::vector<Take> result;
    if (totalSamples <= 0 || ticksPerSample <= 0.0)
        return result;

    const bool wraps = loopActive
                       && loopEndTick > loopStartTick
                       && recordStartTick < (double) loopEndTick;

    if (! wraps)
    {
        Take t;
        t.numSamples  = totalSamples;
        t.startTicks  = (int) std::llround (recordStartTick);
        t.lengthTicks = juce::jmax (ids::ticksPerStep,
                                    (int) std::llround ((double) totalSamples * ticksPerSample));
        result.push_back (t);
        return result;
    }

    juce::int64 passStartSample = 0;
    double passStartTick = recordStartTick;

    while (true)
    {
        const auto passLen = (juce::int64) std::ceil (((double) loopEndTick - passStartTick)
                                                      / ticksPerSample);
        Take t;
        t.startSample = passStartSample;
        t.startTicks  = (int) std::llround (passStartTick);

        if (passStartSample + passLen >= totalSamples)
        {
            t.numSamples = totalSamples - passStartSample;
            const bool partial = passStartSample + passLen > totalSamples;
            if (! partial || t.numSamples >= minTakeSamples)
            {
                t.lengthTicks = partial
                    ? juce::jmax (ids::ticksPerStep,
                                  (int) std::llround ((double) t.numSamples * ticksPerSample))
                    : loopEndTick - t.startTicks;
                result.push_back (t);
            }
            return result;
        }

        t.numSamples  = passLen;
        t.lengthTicks = loopEndTick - t.startTicks;
        result.push_back (t);

        passStartSample += passLen;
        passStartTick = (double) loopStartTick;
    }
}

// Where one take lands on the playlist.
struct Placement
{
    int takeIndex  = 0;
    int trackIndex = 0;
    bool muted     = false;
};

inline bool trackIsFreeAt (juce::ValueTree track, int startTicks, int lengthTicks)
{
    for (const auto clip : track)
        if (clip.hasType (ids::CLIP)
            && (int) clip[ids::startTicks] < startTicks + lengthTicks
            && (int) clip[ids::startTicks] + (int) clip[ids::lengthTicks] > startTicks)
            return false;
    return true;
}

// The latest take is what you hear: it goes unmuted on the first track free
// over its range (track 0 if none is, matching single-clip recording); each
// earlier take goes muted on the next free track below, one unmute away.
// Takes that run out of tracks are left unplaced — their files stay on disk.
inline std::vector<Placement> planPlacement (juce::ValueTree playlist,
                                             const std::vector<Take>& takes)
{
    std::vector<Placement> plan;
    int searchFrom = 0;

    for (int i = (int) takes.size() - 1; i >= 0; --i)
    {
        const auto& take = takes[(size_t) i];
        int trackIndex = -1;
        for (int t = searchFrom; t < playlist.getNumChildren(); ++t)
        {
            if (trackIsFreeAt (playlist.getChild (t), take.startTicks, take.lengthTicks))
            {
                trackIndex = t;
                break;
            }
        }

        const bool isLatest = (i == (int) takes.size() - 1);
        if (trackIndex < 0)
        {
            if (! isLatest)
                break;
            trackIndex = 0;
        }

        plan.push_back ({ i, trackIndex, ! isLatest });
        searchFrom = trackIndex + 1;
    }

    return plan;
}

// Writes each take region of the source WAV to its own file next to it.
// Returns one file per take, or an empty list on any failure (the source is
// left untouched so the caller can fall back to placing it whole).
inline juce::Array<juce::File> writeTakeFiles (const juce::File& source,
                                               const std::vector<Take>& takes)
{
    juce::Array<juce::File> files;
    juce::WavAudioFormat wav;

    auto stream = source.createInputStream();
    if (stream == nullptr)
        return files;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (stream.release(), true));
    if (reader == nullptr)
        return files;

    auto cleanup = [&files]
    {
        for (auto& f : files)
            f.deleteFile();
        return juce::Array<juce::File>();
    };

    for (size_t i = 0; i < takes.size(); ++i)
    {
        const auto file = source.getSiblingFile (source.getFileNameWithoutExtension()
                                                 + "_take" + juce::String ((int) i + 1) + ".wav")
                              .getNonexistentSibling();
        auto out = file.createOutputStream();
        if (out == nullptr)
            return cleanup();

        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (out.get(), reader->sampleRate,
                                 reader->numChannels, (int) reader->bitsPerSample, {}, 0));
        if (writer == nullptr)
            return cleanup();
        [[maybe_unused]] auto* owned = out.release();   // the writer owns it now

        if (! writer->writeFromAudioReader (*reader, takes[i].startSample, takes[i].numSamples))
        {
            writer.reset();
            file.deleteFile();
            return cleanup();
        }

        files.add (file);
    }

    return files;
}

} // namespace takes
