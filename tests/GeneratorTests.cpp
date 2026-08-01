#include <gtest/gtest.h>
#include "engine/SamplerGenerator.h"
#include "engine/SynthGenerator.h"
#include "TestHelpers.h"

namespace
{
juce::AudioBuffer<float> renderGenerator (Generator& gen, const juce::MidiBuffer& midi, int numSamples)
{
    juce::AudioBuffer<float> out (2, numSamples);
    out.clear();
    int pos = 0;
    while (pos < numSamples)
    {
        const int n = juce::jmin (512, numSamples - pos);
        juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, pos, n);
        juce::MidiBuffer block;
        if (pos == 0)
            block = midi;
        gen.render (view, block);
        pos += n;
    }
    return out;
}

juce::MidiBuffer noteOnAt0 (int key = 60)
{
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, key, 0.9f), 0);
    return midi;
}
}

TEST (SamplerGenerator, SynthesizedDrumsProduceAudio)
{
    for (const char* kind : { "Kick", "Snare", "Clap", "Hat" })
    {
        SamplerGenerator sampler;
        sampler.prepare (test::kSampleRate, 512);
        sampler.useSynthesizedDrum (kind, test::kSampleRate);
        auto out = renderGenerator (sampler, noteOnAt0(), 8192);
        EXPECT_GT (out.getMagnitude (0, 0, 8192), 0.05f) << kind;
    }
}

TEST (SamplerGenerator, OneShotIgnoresNoteOffAndEnds)
{
    SamplerGenerator sampler;
    sampler.prepare (test::kSampleRate, 512);
    sampler.useSynthesizedDrum ("Hat", test::kSampleRate);   // 0.09 s

    juce::MidiBuffer midi = noteOnAt0();
    midi.addEvent (juce::MidiMessage::noteOff (1, 60), 100);

    auto out = renderGenerator (sampler, midi, 22050);
    EXPECT_GT (out.getMagnitude (0, 0, 4096), 0.05f);          // plays past note-off
    EXPECT_LT (out.getMagnitude (0, 8000, 14000), 1.0e-5f);    // and ends by itself
}

TEST (SamplerGenerator, RepitchChangesPlaybackLength)
{
    SamplerGenerator sampler;
    sampler.prepare (test::kSampleRate, 512);
    sampler.useSynthesizedDrum ("Kick", test::kSampleRate);    // 0.40 s at root

    // One octave up plays twice as fast: energy gone after ~half the length.
    auto out = renderGenerator (sampler, noteOnAt0 (72), 22050);
    EXPECT_GT (out.getMagnitude (0, 0, 4410), 0.05f);
    EXPECT_LT (out.getMagnitude (0, 11025, 11000), 1.0e-4f);
}

TEST (SamplerGenerator, LoadSampleFileWorks)
{
    const auto tone = test::makeToneFile (0.25);
    SamplerGenerator sampler;
    sampler.prepare (test::kSampleRate, 512);
    EXPECT_TRUE (sampler.loadSampleFile (tone));
    EXPECT_EQ (sampler.getSamplePath(), tone.getFullPathName());

    auto out = renderGenerator (sampler, noteOnAt0(), 8192);
    EXPECT_GT (out.getMagnitude (0, 0, 8192), 0.1f);

    EXPECT_FALSE (sampler.loadSampleFile (juce::File ("/nonexistent/nope.wav")));
    tone.deleteFile();
}

TEST (SamplerGenerator, ResetKillsVoices)
{
    SamplerGenerator sampler;
    sampler.prepare (test::kSampleRate, 512);
    sampler.useSynthesizedDrum ("Kick", test::kSampleRate);

    juce::AudioBuffer<float> out (2, 512);
    out.clear();
    juce::MidiBuffer midi = noteOnAt0();
    sampler.render (out, midi);
    sampler.reset();

    out.clear();
    juce::MidiBuffer empty;
    sampler.render (out, empty);
    EXPECT_FLOAT_EQ (out.getMagnitude (0, 0, 512), 0.0f);
}

TEST (SynthGenerator, NoteProducesAndReleases)
{
    SynthGenerator synth;
    synth.prepare (test::kSampleRate, 512);
    synth.params().release.store (0.05f);

    juce::MidiBuffer midi = noteOnAt0();
    midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);   // handled below via block split

    // Hold the note for 4096 samples, then release.
    juce::AudioBuffer<float> out (2, 22050);
    out.clear();
    {
        juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, 0, 4096);
        juce::MidiBuffer on = noteOnAt0();
        synth.render (view, on);
    }
    {
        juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, 4096, 22050 - 4096);
        juce::MidiBuffer off;
        off.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
        synth.render (view, off);
    }

    EXPECT_GT (out.getMagnitude (0, 1024, 3000), 0.01f) << "sustain phase silent";
    EXPECT_LT (out.getMagnitude (0, 15000, 7000), 1.0e-4f) << "voice did not release";
}

TEST (SynthGenerator, PolyphonyMixesVoices)
{
    SynthGenerator synth;
    synth.prepare (test::kSampleRate, 512);

    juce::MidiBuffer chord;
    for (int key : { 60, 64, 67 })
        chord.addEvent (juce::MidiMessage::noteOn (1, key, 0.8f), 0);

    auto out = renderGenerator (synth, chord, 8192);
    EXPECT_GT (out.getRMSLevel (0, 2048, 4096), 0.02f);
}
