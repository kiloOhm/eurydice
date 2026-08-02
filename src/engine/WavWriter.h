#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

// One home for JUCE's options-based writer creation (the positional
// createWriterFor overload is deprecated as of 8.0.15), so the old signature
// doesn't creep back in per call site.
namespace wavwriter
{
inline std::unique_ptr<juce::AudioFormatWriter> forStream (std::unique_ptr<juce::OutputStream> stream,
                                                           double sampleRate, int numChannels,
                                                           int bitsPerSample)
{
    if (stream == nullptr)
        return nullptr;
    juce::WavAudioFormat wav;
    // createWriterFor takes the stream by reference and only assumes
    // ownership on success; `owned` cleans up the failure path.
    std::unique_ptr<juce::OutputStream> owned (std::move (stream));
    return wav.createWriterFor (owned, juce::AudioFormatWriterOptions{}
                                           .withSampleRate (sampleRate)
                                           .withNumChannels (numChannels)
                                           .withBitsPerSample (bitsPerSample));
}

inline std::unique_ptr<juce::AudioFormatWriter> forFile (const juce::File& file, double sampleRate,
                                                         int numChannels, int bitsPerSample)
{
    file.deleteFile();
    return forStream (file.createOutputStream(), sampleRate, numChannels, bitsPerSample);
}
} // namespace wavwriter
