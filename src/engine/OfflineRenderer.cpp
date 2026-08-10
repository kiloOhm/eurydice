#include "OfflineRenderer.h"
#include "WavWriter.h"
#include <juce_dsp/juce_dsp.h>
#include <set>
#include <map>

namespace
{
std::unique_ptr<juce::AudioFormatWriter> makeWavWriter (const juce::File& file, double sampleRate, int bitDepth)
{
    return wavwriter::forFile (file, sampleRate, 2, bitDepth);
}

std::unique_ptr<juce::AudioFormatReader> makeWavReader (const juce::File& file)
{
    juce::WavAudioFormat wav;
    return std::unique_ptr<juce::AudioFormatReader> (
        wav.createReaderFor (new juce::FileInputStream (file), true));
}

float peakOfFile (const juce::File& file)
{
    auto reader = makeWavReader (file);
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return 0.0f;

    juce::Range<float> levels[2];
    reader->readMaxLevels (0, reader->lengthInSamples, levels, (int) juce::jmin (2u, reader->numChannels));

    float peak = 0.0f;
    for (const auto& range : levels)
        peak = juce::jmax (peak, std::abs (range.getStart()), std::abs (range.getEnd()));
    return peak;
}

// Rewrites the file in place with a gain applied and, when the target rate
// differs, resampled. Rendering streams straight to disk, so both happen here
// rather than by holding a whole arrangement in memory.
bool rewriteFile (const juce::File& file, float gain, int targetSampleRate, int bitDepth)
{
    auto reader = makeWavReader (file);
    if (reader == nullptr)
        return false;

    const double sourceRate = reader->sampleRate;
    const int rate = targetSampleRate > 0 ? targetSampleRate : juce::roundToInt (sourceRate);
    const juce::int64 numIn = reader->lengthInSamples;
    const bool resample = rate != juce::roundToInt (sourceRate) && sourceRate > 0.0;
    const juce::int64 numOut = resample
                                   ? (juce::int64) std::llround ((double) numIn * rate / sourceRate)
                                   : numIn;

    juce::TemporaryFile temp (file);
    auto writer = makeWavWriter (temp.getFile(), (double) rate, bitDepth);
    if (writer == nullptr)
        return false;

    constexpr int chunk = 8192;
    juce::AudioBuffer<float> buffer (2, chunk);

    if (resample)
    {
        juce::AudioFormatReaderSource source (reader.get(), false);
        juce::ResamplingAudioSource resampler (&source, false, 2);
        resampler.setResamplingRatio (sourceRate / rate);
        resampler.prepareToPlay (chunk, (double) rate);

        for (juce::int64 pos = 0; pos < numOut;)
        {
            const int n = (int) juce::jmin<juce::int64> (chunk, numOut - pos);
            juce::AudioSourceChannelInfo info (&buffer, 0, n);
            resampler.getNextAudioBlock (info);
            buffer.applyGain (0, n, gain);
            writer->writeFromAudioSampleBuffer (buffer, 0, n);
            pos += n;
        }
        resampler.releaseResources();
    }
    else
    {
        for (juce::int64 pos = 0; pos < numIn;)
        {
            const int n = (int) juce::jmin<juce::int64> (chunk, numIn - pos);
            reader->read (&buffer, 0, n, pos, true, true);
            buffer.applyGain (0, n, gain);
            writer->writeFromAudioSampleBuffer (buffer, 0, n);
            pos += n;
        }
    }

    writer = nullptr;
    reader = nullptr;   // must close before the temporary replaces it
    return temp.overwriteTargetFileWithTemporary();
}

juce::String sanitiseForFileName (const juce::String& name)
{
    return name.replaceCharacters ("/\\:", "---").trim();
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
    double startTick = 0.0;
    double endTick = 0.0;

    if (opts.loopRangeOnly)
    {
        startTick = (double) project.getLoopStart();
        endTick   = (double) project.getLoopEnd();
        if (endTick <= startTick)
        {
            result.error = juce::String (juce::CharPointer_UTF8 (
            "Loop range is empty \xe2\x80\x94 mark one in the playlist ruler first."));
            return result;
        }
    }
    else if (project.isSongMode())
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

    if (endTick <= startTick)
    {
        result.error = "Nothing to render (empty pattern/playlist).";
        return result;
    }

    const double sampleRate = engine.getSampleRate();
    const int blockSize = engine.getBlockSize();
    const double tps = (project.getTempo() / 60.0) * ids::ticksPerQuarter / sampleRate;
    const juce::int64 mainSamples = (juce::int64) std::ceil ((endTick - startTick) / tps);
    const juce::int64 tailSamples = (juce::int64) (opts.tailSeconds * sampleRate);

    // --- figure out what gets a stem ---
    std::vector<int> stemInserts;
    if (opts.stems == Stems::perInsert)
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

    const int numChannels = juce::jmin (project.numChannels(), AudioEngine::maxChannels);
    const bool channelStems = opts.stems == Stems::perChannel;

    // --- writers ---
    auto masterWriter = makeWavWriter (opts.wavFile, sampleRate, opts.bitDepth);
    if (masterWriter == nullptr)
    {
        result.error = "Could not create " + opts.wavFile.getFullPathName();
        return result;
    }
    result.writtenFiles.add (opts.wavFile.getFullPathName());

    // Stem names come from the project, so two channels can easily ask for the
    // same file; the index keeps them apart instead of one overwriting the other.
    std::set<juce::String> usedNames;
    auto stemFileFor = [&opts, &usedNames] (const juce::String& rawName, int index)
    {
        auto name = sanitiseForFileName (rawName);
        if (name.isEmpty())
            name = "Stem";
        if (! usedNames.insert (name).second)
            name << " " << (index + 1);
        return opts.wavFile.getSiblingFile (
            opts.wavFile.getFileNameWithoutExtension() + "-" + name + ".wav");
    };

    std::map<int, std::unique_ptr<juce::AudioFormatWriter>> insertStemWriters, channelStemWriters;

    for (int idx : stemInserts)
    {
        const auto file = stemFileFor (project.getInsert (idx)[ids::name].toString(), idx);
        if (auto writer = makeWavWriter (file, sampleRate, opts.bitDepth))
        {
            insertStemWriters[idx] = std::move (writer);
            result.writtenFiles.add (file.getFullPathName());
        }
    }

    if (channelStems)
    {
        for (int i = 0; i < numChannels; ++i)
        {
            const auto file = stemFileFor (project.getChannel (i)[ids::name].toString(), i);
            if (auto writer = makeWavWriter (file, sampleRate, opts.bitDepth))
            {
                channelStemWriters[i] = std::move (writer);
                result.writtenFiles.add (file.getFullPathName());
            }
        }
    }

    // --- render ---
    const bool metronomeWasOn = engine.isMetronomeEnabled();
    engine.setMetronomeEnabled (false);   // a click in a bounce is never wanted

    engine.detachFromDevice();
    engine.setChannelStemCapture (channelStems);
    // A loop armed for editing must not truncate the render: the range is an
    // explicit option here, never an implicit one.
    engine.setLoopBypassed (true);
    engine.prepareOffline (sampleRate, blockSize);

    engine.stop();
    engine.setPositionTicks (startTick);
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
        for (auto& [idx, writer] : insertStemWriters)
            writer->writeFromAudioSampleBuffer (engine.getInsertBusForStem (idx), 0, n);
        for (auto& [idx, writer] : channelStemWriters)
            writer->writeFromAudioSampleBuffer (engine.getChannelBusForStem (idx), 0, n);

        rendered += n;
        if (opts.progress)
            opts.progress ((double) rendered / (double) totalSamples);
    }

    engine.stop();
    masterWriter = nullptr;
    insertStemWriters.clear();
    channelStemWriters.clear();

    engine.setChannelStemCapture (false);
    engine.setLoopBypassed (false);
    engine.setMetronomeEnabled (metronomeWasOn);
    engine.reattachToDevice();   // re-prepares via audioDeviceAboutToStart

    // --- normalise + sample-rate conversion ---
    float gain = 1.0f;
    if (opts.normalise)
    {
        const float peak = peakOfFile (opts.wavFile);
        if (peak > 1.0e-6f)
            gain = (float) juce::Decibels::decibelsToGain (opts.normaliseTargetDb) / peak;
        else
            result.error = "Normalisation skipped: the render is silent.";
    }

    const int targetRate = opts.sampleRate > 0 ? opts.sampleRate : juce::roundToInt (sampleRate);
    // Stems take the master's gain, not their own peak, so the balance between
    // them still adds up to the mix.
    if (gain != 1.0f || targetRate != juce::roundToInt (sampleRate))
        for (const auto& path : result.writtenFiles)
            if (! rewriteFile (juce::File (path), gain, targetRate, opts.bitDepth))
                result.error = "Could not post-process " + juce::File (path).getFileName();

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

// ===================== analysis =====================

namespace
{
// Accumulates peak / RMS / averaged power spectrum for one bus.
struct StatAccumulator
{
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;   // 2048

    StatAccumulator() : fft (fftOrder)
    {
        window.resize (fftSize);
        for (int i = 0; i < fftSize; ++i)
            window[(size_t) i] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi
                                                          * (float) i / (float) (fftSize - 1)));
    }

    void push (const juce::AudioBuffer<float>& bus, int numSamples)
    {
        const int numCh = juce::jmin (2, bus.getNumChannels());
        for (int i = 0; i < numSamples; ++i)
        {
            float mono = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float s = bus.getSample (ch, i);
                peak = juce::jmax (peak, std::abs (s));
                sumSquares += (double) s * s;
                mono += s;
            }
            samples += numCh;

            fill[(size_t) fillCount++] = mono / (float) juce::jmax (1, numCh);
            if (fillCount == fftSize)
                flushFft();
        }
    }

    void flushFft()
    {
        std::array<float, (size_t) fftSize * 2> data {};
        for (int i = 0; i < fillCount; ++i)
            data[(size_t) i] = fill[(size_t) i] * window[(size_t) i];
        fillCount = 0;
        fft.performFrequencyOnlyForwardTransform (data.data());
        for (int bin = 0; bin < fftSize / 2; ++bin)
            power[(size_t) bin] += (double) data[(size_t) bin] * data[(size_t) bin];
        ++fftFrames;
    }

    OfflineRenderer::TargetStats finish (double sampleRate)
    {
        if (fillCount > fftSize / 8)   // don't drop a mostly-full last frame
        {
            std::fill (fill.begin() + fillCount, fill.end(), 0.0f);
            fillCount = fftSize;
            flushFft();
        }

        OfflineRenderer::TargetStats stats;
        stats.peakDb = juce::Decibels::gainToDecibels (peak, -120.0f);
        stats.rmsDb = juce::Decibels::gainToDecibels (
            (float) std::sqrt (sumSquares / (double) juce::jmax<juce::int64> (1, samples)), -120.0f);

        double total = 0.0;
        std::array<double, 5> bands {};
        const double binHz = sampleRate / fftSize;
        for (int bin = 1; bin < fftSize / 2; ++bin)
        {
            const double f = bin * binHz;
            const double p = power[(size_t) bin];
            total += p;
            bands[f < 60.0 ? 0 : f < 250.0 ? 1 : f < 1000.0 ? 2 : f < 4000.0 ? 3 : 4] += p;
        }
        if (total > 1.0e-18)
        {
            const auto shareDb = [total] (double p)
            { return (float) juce::jmax (-120.0, 10.0 * std::log10 (juce::jmax (1.0e-18, p) / total)); };
            stats.bands.subDb     = shareDb (bands[0]);
            stats.bands.lowDb     = shareDb (bands[1]);
            stats.bands.lowMidDb  = shareDb (bands[2]);
            stats.bands.highMidDb = shareDb (bands[3]);
            stats.bands.highDb    = shareDb (bands[4]);
        }
        return stats;
    }

    juce::dsp::FFT fft;
    std::vector<float> window;
    std::array<float, fftSize> fill {};
    int fillCount = 0;
    float peak = 0.0f;
    double sumSquares = 0.0;
    juce::int64 samples = 0;
    std::array<double, fftSize / 2> power {};
    juce::int64 fftFrames = 0;
};
}

OfflineRenderer::Analysis OfflineRenderer::analyze (AudioEngine& engine, ProjectModel& project,
                                                    const AnalysisOptions& opts)
{
    Analysis analysis;

    double startTick = 0.0, endTick = 0.0;
    if (opts.loopRangeOnly)
    {
        startTick = (double) project.getLoopStart();
        endTick   = (double) project.getLoopEnd();
        if (endTick <= startTick)
        {
            analysis.error = "Loop range is empty.";
            return analysis;
        }
    }
    else if (project.isSongMode())
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

    if (endTick <= startTick)
    {
        analysis.error = "Nothing to analyze (empty pattern/playlist).";
        return analysis;
    }

    // Every insert that carries a channel or receives a send gets stats.
    std::set<int> used;
    for (int i = 0; i < project.numChannels(); ++i)
        used.insert ((int) project.getChannel (i)[ids::insertIndex]);
    for (int i = 0; i < project.numInserts(); ++i)
        for (const auto send : project.getInsert (i).getChildWithName (ids::SENDS))
            if (used.count (i))
                used.insert ((int) send[ids::destInsert]);
    used.erase (0);

    const double sampleRate = engine.getSampleRate();
    const int blockSize = engine.getBlockSize();
    const double tps = (project.getTempo() / 60.0) * ids::ticksPerQuarter / sampleRate;
    const juce::int64 totalSamples = (juce::int64) std::ceil ((endTick - startTick) / tps)
                                   + (juce::int64) (opts.tailSeconds * sampleRate);

    StatAccumulator masterStats;
    std::map<int, StatAccumulator> insertStats;
    for (int idx : used)
        insertStats.try_emplace (idx);

    const bool metronomeWasOn = engine.isMetronomeEnabled();
    engine.setMetronomeEnabled (false);
    engine.detachFromDevice();
    engine.setLoopBypassed (true);
    engine.prepareOffline (sampleRate, blockSize);
    engine.stop();
    engine.setPositionTicks (startTick);
    engine.play();

    juce::AudioBuffer<float> out (2, blockSize);
    float* outPtrs[2] = { out.getWritePointer (0), out.getWritePointer (1) };
    const juce::int64 mainSamples = (juce::int64) std::ceil ((endTick - startTick) / tps);
    juce::int64 rendered = 0;
    bool tailStarted = false;

    while (rendered < totalSamples)
    {
        const int n = (int) juce::jmin<juce::int64> (blockSize, totalSamples - rendered);
        if (! tailStarted && rendered >= mainSamples)
        {
            engine.pausePlayback();
            tailStarted = true;
        }
        engine.processBlockOffline (outPtrs, 2, n);

        masterStats.push (out, n);
        for (auto& [idx, acc] : insertStats)
            acc.push (engine.getInsertBusForStem (idx), n);
        rendered += n;
    }

    engine.stop();
    engine.setLoopBypassed (false);
    engine.setMetronomeEnabled (metronomeWasOn);
    engine.reattachToDevice();

    analysis.sampleRate = sampleRate;
    analysis.durationSeconds = (double) totalSamples / sampleRate;
    analysis.master = masterStats.finish (sampleRate);
    analysis.master.insertIndex = 0;
    analysis.master.name = project.getInsert (0)[ids::name].toString();

    for (auto& [idx, acc] : insertStats)
    {
        auto stats = acc.finish (sampleRate);
        stats.insertIndex = idx;
        stats.name = project.getInsert (idx)[ids::name].toString();
        analysis.inserts.push_back (std::move (stats));
    }

    analysis.ok = true;
    return analysis;
}
