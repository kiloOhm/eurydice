#pragma once

#include <map>
#include <juce_audio_formats/juce_audio_formats.h>

// Rubber Band R3 flavour for offline clip stretching. Values are persisted on
// the CLIP tree (ids::stretchMode), so they must stay stable.
enum class StretchMode
{
    smooth     = 0,   // R3 defaults: best for sustained / melodic material
    percussive = 1,   // crisp transients + short window: drums, chops
    formant    = 2,   // preserve the spectral envelope: vocals, pitched material
};

// Message-thread cache of audio clip buffers. Raw files are loaded once and
// resampled to the engine rate; stretched variants (Rubber Band R3, offline)
// are rendered on demand and cached by (path, ratio, mode). Snapshots hold
// shared_ptrs, so the audio thread only ever touches immutable buffers.
class AudioClipCache
{
public:
    static StretchMode modeFrom (int value)
    {
        return (StretchMode) juce::jlimit (0, 2, value);
    }

    AudioClipCache() { formats.registerBasicFormats(); }

    void setEngineSampleRate (double sr)
    {
        if (! juce::approximatelyEqual (engineSampleRate, sr))
        {
            engineSampleRate = sr;
            resampled.clear();
            stretched.clear();
        }
    }

    double getEngineSampleRate() const { return engineSampleRate; }

    // Natural duration in seconds at the engine rate (0 if unreadable).
    double getNaturalSeconds (const juce::String& path)
    {
        if (auto buffer = getResampled (path))
            return buffer->getNumSamples() / engineSampleRate;
        return 0.0;
    }

    // ratio 1.0 = natural speed (no Rubber Band involved, mode irrelevant).
    // ratio 2.0 = twice as long, pitch preserved.
    std::shared_ptr<const juce::AudioBuffer<float>> getStretched (const juce::String& path,
                                                                  double ratio,
                                                                  StretchMode mode = StretchMode::smooth);

private:
    std::shared_ptr<const juce::AudioBuffer<float>> getResampled (const juce::String& path);

    juce::AudioFormatManager formats;
    double engineSampleRate = 44100.0;

    std::map<juce::String, std::shared_ptr<const juce::AudioBuffer<float>>> resampled;
    std::map<juce::String, std::shared_ptr<const juce::AudioBuffer<float>>> stretched;   // key: path|ratio|mode

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioClipCache)
};
