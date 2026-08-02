#pragma once

#include <gtest/gtest.h>
#include "model/ProjectModel.h"
#include "engine/AudioEngine.h"
#include "engine/EngineSync.h"
#include "engine/GeneratorPool.h"
#include "engine/AudioClipCache.h"
#include "plugins/PluginManager.h"
#include "plugins/EffectPool.h"
#include "effects/BuiltinEffectPool.h"

namespace test
{
inline constexpr double kSampleRate = 44100.0;
inline constexpr int kBlockSize = 512;

// Full model->engine stack prepared for offline processing; never opens an
// audio device, so it runs anywhere.
struct EngineFixture
{
    ProjectModel model;
    PluginManager plugins;
    GeneratorPool generators;
    EffectPool effects { plugins };
    BuiltinEffectPool builtinEffects;
    AudioClipCache audioClips;
    AudioEngine engine;
    EngineSync sync { model, generators, effects, builtinEffects, audioClips, engine };

    EngineFixture()
    {
        generators.setPluginContext (&plugins, [this] { sync.rebuildNow(); });
        effects.onInstanceReady = [this] { sync.rebuildNow(); };
        generators.setAudioSpec (kSampleRate, kBlockSize);
        effects.setAudioSpec (kSampleRate, kBlockSize);
        builtinEffects.setAudioSpec (kSampleRate, kBlockSize);
        audioClips.setEngineSampleRate (kSampleRate);
        engine.prepareOffline (kSampleRate, kBlockSize);
        sync.rebuildNow();
    }

    double ticksPerSample() const
    {
        return (model.getTempo() / 60.0) * ids::ticksPerQuarter / kSampleRate;
    }

    // Renders numSamples of engine output from the current position.
    juce::AudioBuffer<float> render (int numSamples)
    {
        juce::AudioBuffer<float> out (2, numSamples);
        out.clear();
        int pos = 0;
        while (pos < numSamples)
        {
            const int n = juce::jmin (kBlockSize, numSamples - pos);
            float* ptrs[2] = { out.getWritePointer (0, pos), out.getWritePointer (1, pos) };
            engine.processBlockOffline (ptrs, 2, n);
            pos += n;
        }
        return out;
    }

    juce::AudioBuffer<float> renderFromStart (int numSamples)
    {
        engine.stop();
        engine.setPositionTicks (0.0);
        engine.play();
        auto out = render (numSamples);
        engine.stop();
        return out;
    }
};

inline float rmsOf (const juce::AudioBuffer<float>& buffer, int start, int length)
{
    start = juce::jlimit (0, buffer.getNumSamples() - 1, start);
    length = juce::jmin (length, buffer.getNumSamples() - start);
    return buffer.getRMSLevel (0, start, juce::jmax (1, length));
}

// Writes a mono 440 Hz sine wav; returns its path.
inline juce::File makeToneFile (double seconds, double sampleRate = 44100.0)
{
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getNonexistentChildFile ("eurytest-tone", ".wav");
    juce::WavAudioFormat wav;
    auto stream = file.createOutputStream();
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.get(), sampleRate, 1, 16, {}, 0));
    if (writer == nullptr)
        return {};
    stream.release();

    const int n = (int) (seconds * sampleRate);
    juce::AudioBuffer<float> tone (1, n);
    for (int i = 0; i < n; ++i)
        tone.setSample (0, i, 0.5f * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / sampleRate));
    writer->writeFromAudioSampleBuffer (tone, 0, n);
    return file;
}
} // namespace test
