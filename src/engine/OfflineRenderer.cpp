#include "OfflineRenderer.h"
#include <set>
#include <map>

namespace
{
std::unique_ptr<juce::AudioFormatWriter> makeWavWriter (const juce::File& file, double sampleRate, int bitDepth)
{
    file.deleteFile();
    auto stream = file.createOutputStream();
    if (stream == nullptr)
        return nullptr;
    juce::WavAudioFormat wav;
    auto* writer = wav.createWriterFor (stream.get(), sampleRate, 2, bitDepth, {}, 0);
    if (writer != nullptr)
        (void) stream.release();   // writer owns it now
    return std::unique_ptr<juce::AudioFormatWriter> (writer);
}
}

juce::File OfflineRenderer::findLameBinary()
{
    for (const char* path : { "/opt/homebrew/bin/lame", "/usr/local/bin/lame", "/usr/bin/lame" })
        if (juce::File (path).existsAsFile())
            return juce::File (path);
    return {};
}

OfflineRenderer::Result OfflineRenderer::render (AudioEngine& engine, ProjectModel& project,
                                                 const Options& opts)
{
    Result result;

    // --- determine render range in ticks ---
    double endTick = 0.0;
    if (project.isSongMode())
    {
        for (const auto track : project.playlist())
            for (const auto clip : track)
                if (clip.hasType (ids::CLIP) && ! (bool) clip[ids::muted])
                    endTick = juce::jmax (endTick, (double) ((int) clip[ids::startTicks]
                                                             + (int) clip[ids::lengthTicks]));
    }
    else
    {
        const auto pattern = project.getPatternById (project.getRoot()[ids::activePattern]);
        if (pattern.isValid())
            endTick = (double) (int) pattern[ids::lengthTicks];
    }

    if (endTick <= 0.0)
    {
        result.error = "Nothing to render (empty pattern/playlist).";
        return result;
    }

    const double sampleRate = engine.getSampleRate();
    const int blockSize = engine.getBlockSize();
    const double tps = (project.getTempo() / 60.0) * ids::ticksPerQuarter / sampleRate;
    const juce::int64 mainSamples = (juce::int64) std::ceil (endTick / tps);
    const juce::int64 tailSamples = (juce::int64) (opts.tailSeconds * sampleRate);

    // --- figure out which inserts get stems ---
    std::vector<int> stemInserts;
    if (opts.renderStems)
    {
        std::set<int> used;
        for (int i = 0; i < project.numChannels(); ++i)
            used.insert ((int) project.getChannel (i)[ids::insertIndex]);
        for (int i = 0; i < project.numInserts(); ++i)
        {
            const auto ins = project.getInsert (i);
            for (const auto send : ins.getChildWithName (ids::SENDS))
                if (used.count (i))
                    used.insert ((int) send[ids::destInsert]);
        }
        used.erase (0);   // master is the main mix
        stemInserts.assign (used.begin(), used.end());
    }

    // --- writers ---
    auto masterWriter = makeWavWriter (opts.wavFile, sampleRate, opts.bitDepth);
    if (masterWriter == nullptr)
    {
        result.error = "Could not create " + opts.wavFile.getFullPathName();
        return result;
    }
    result.writtenFiles.add (opts.wavFile.getFullPathName());

    std::map<int, std::unique_ptr<juce::AudioFormatWriter>> stemWriters;
    for (int idx : stemInserts)
    {
        auto name = project.getInsert (idx)[ids::name].toString()
                        .replaceCharacters ("/\\:", "---");
        auto file = opts.wavFile.getSiblingFile (
            opts.wavFile.getFileNameWithoutExtension() + "-" + name + ".wav");
        if (auto writer = makeWavWriter (file, sampleRate, opts.bitDepth))
        {
            stemWriters[idx] = std::move (writer);
            result.writtenFiles.add (file.getFullPathName());
        }
    }

    // --- render ---
    engine.detachFromDevice();
    engine.prepareOffline (sampleRate, blockSize);

    engine.stop();
    engine.setPositionTicks (0.0);
    engine.play();

    juce::AudioBuffer<float> out (2, blockSize);
    float* outPtrs[2] = { out.getWritePointer (0), out.getWritePointer (1) };

    const juce::int64 totalSamples = mainSamples + tailSamples;
    juce::int64 rendered = 0;
    bool tailStarted = false;

    while (rendered < totalSamples)
    {
        const int n = (int) juce::jmin<juce::int64> (blockSize, totalSamples - rendered);

        if (! tailStarted && rendered >= mainSamples)
        {
            engine.pausePlayback();   // let voices ring out, no retriggers
            tailStarted = true;
        }

        engine.processBlockOffline (outPtrs, 2, n);

        masterWriter->writeFromAudioSampleBuffer (out, 0, n);
        for (auto& [idx, writer] : stemWriters)
            writer->writeFromAudioSampleBuffer (engine.getInsertBusForStem (idx), 0, n);

        rendered += n;
        if (opts.progress)
            opts.progress ((double) rendered / (double) totalSamples);
    }

    engine.stop();
    masterWriter = nullptr;
    stemWriters.clear();

    engine.reattachToDevice();   // re-prepares via audioDeviceAboutToStart

    // --- mp3 ---
    if (opts.renderMp3)
    {
        const auto lame = findLameBinary();
        if (lame == juce::File())
        {
            result.error = "MP3 skipped: `lame` not found (brew install lame).";
        }
        else
        {
            const auto mp3File = opts.wavFile.withFileExtension (".mp3");
            juce::ChildProcess proc;
            if (proc.start (juce::StringArray { lame.getFullPathName(), "-b", "320",
                                                opts.wavFile.getFullPathName(),
                                                mp3File.getFullPathName() })
                && proc.waitForProcessToFinish (120000)
                && proc.getExitCode() == 0)
                result.writtenFiles.add (mp3File.getFullPathName());
            else
                result.error = "MP3 encode failed.";
        }
    }

    result.ok = true;
    return result;
}
