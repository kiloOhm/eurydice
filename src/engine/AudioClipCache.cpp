#include "AudioClipCache.h"
#include <rubberband/RubberBandStretcher.h>

std::shared_ptr<const juce::AudioBuffer<float>> AudioClipCache::getResampled (const juce::String& path)
{
    if (auto it = resampled.find (path); it != resampled.end())
        return it->second;

    std::unique_ptr<juce::AudioFormatReader> reader (
        formats.createReaderFor (juce::File (path)));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return nullptr;

    // Cap at 10 minutes to bound memory.
    const auto numSource = (int) juce::jmin<juce::int64> (reader->lengthInSamples,
                                                          (juce::int64) (600.0 * reader->sampleRate));
    juce::AudioBuffer<float> source (2, numSource);
    reader->read (&source, 0, numSource, 0, true, true);
    if (reader->numChannels == 1)
        source.copyFrom (1, 0, source, 0, 0, numSource);

    const double srcRate = reader->sampleRate;
    std::shared_ptr<juce::AudioBuffer<float>> out;

    if (juce::approximatelyEqual (srcRate, engineSampleRate))
    {
        out = std::make_shared<juce::AudioBuffer<float>> (std::move (source));
    }
    else
    {
        const double speed = srcRate / engineSampleRate;
        const int numOut = (int) ((double) numSource / speed);
        out = std::make_shared<juce::AudioBuffer<float>> (2, numOut);
        for (int ch = 0; ch < 2; ++ch)
        {
            juce::LagrangeInterpolator interp;
            interp.process (speed, source.getReadPointer (ch),
                            out->getWritePointer (ch), numOut);
        }
    }

    resampled[path] = out;
    return out;
}

std::shared_ptr<const juce::AudioBuffer<float>> AudioClipCache::getStretched (const juce::String& path,
                                                                              double ratio)
{
    ratio = juce::jlimit (0.1, 10.0, ratio);

    if (std::abs (ratio - 1.0) < 1.0e-3)
        return getResampled (path);

    const auto key = path + "|" + juce::String (ratio, 4);
    if (auto it = stretched.find (key); it != stretched.end())
        return it->second;

    auto raw = getResampled (path);
    if (raw == nullptr)
        return nullptr;

    using RB = RubberBand::RubberBandStretcher;
    RB stretcher ((size_t) engineSampleRate, 2,
                  RB::OptionProcessOffline | RB::OptionEngineFiner | RB::OptionChannelsTogether,
                  ratio, 1.0);

    const int numIn = raw->getNumSamples();
    stretcher.setExpectedInputDuration ((size_t) numIn);

    const float* inPtrs[2] = { raw->getReadPointer (0), raw->getReadPointer (1) };
    stretcher.study (inPtrs, (size_t) numIn, true);
    stretcher.process (inPtrs, (size_t) numIn, true);

    const int avail = (int) stretcher.available();
    auto out = std::make_shared<juce::AudioBuffer<float>> (2, juce::jmax (1, avail));
    float* outPtrs[2] = { out->getWritePointer (0), out->getWritePointer (1) };
    stretcher.retrieve (outPtrs, (size_t) juce::jmax (0, avail));

    stretched[key] = out;
    return out;
}
