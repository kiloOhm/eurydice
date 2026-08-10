#include "TestHelpers.h"
#include "app/ProjectFileState.h"
#include "engine/SamplerGenerator.h"
#include "engine/SynthGenerator.h"

using test::EngineFixture;

namespace
{
juce::AudioBuffer<float> renderWithNote (Generator& gen, int numSamples, bool sendNoteOff,
                                         int noteOffAt = 2048)
{
    juce::AudioBuffer<float> out (2, numSamples);
    out.clear();

    int pos = 0;
    while (pos < numSamples)
    {
        const int n = juce::jmin (512, numSamples - pos);
        juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, pos, n);
        juce::MidiBuffer midi;
        if (pos == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
        if (sendNoteOff && pos <= noteOffAt && noteOffAt < pos + n)
            midi.addEvent (juce::MidiMessage::noteOff (1, 60), noteOffAt - pos);
        gen.render (view, midi);
        pos += n;
    }
    return out;
}
}

TEST (ChannelParams, SamplerParamsSyncFromTree)
{
    EngineFixture fx;
    auto channel = fx.model.getChannel (0);
    channel.setProperty (ids::attack, 0.5, nullptr);
    channel.setProperty (ids::release, 1.25, nullptr);
    channel.setProperty (ids::cutoff, 800.0, nullptr);
    channel.setProperty (ids::resonance, 0.6, nullptr);
    channel.setProperty (ids::oneShot, false, nullptr);
    channel.setProperty (ids::rootNote, 48, nullptr);

    auto generator = fx.generators.getOrCreate (channel);
    auto* sampler = dynamic_cast<SamplerGenerator*> (generator.get());
    ASSERT_NE (sampler, nullptr);

    EXPECT_FLOAT_EQ (sampler->params().attack.load(), 0.5f);
    EXPECT_FLOAT_EQ (sampler->params().release.load(), 1.25f);
    EXPECT_FLOAT_EQ (sampler->params().cutoff.load(), 800.0f);
    EXPECT_FLOAT_EQ (sampler->params().resonance.load(), 0.6f);
    EXPECT_FALSE (sampler->params().oneShot.load());
    EXPECT_EQ (sampler->getRootNote(), 48);
}

TEST (ChannelParams, SynthParamsSyncFromTree)
{
    EngineFixture fx;
    auto channel = fx.model.addChannel ("synth", "Lead");
    channel.setProperty (ids::oscShape, 1.0, nullptr);
    channel.setProperty (ids::osc2Detune, -20.0, nullptr);
    channel.setProperty (ids::cutoff, 900.0, nullptr);
    channel.setProperty (ids::filterEnvAmt, 0.8, nullptr);

    auto generator = fx.generators.getOrCreate (channel);
    auto* synth = dynamic_cast<SynthGenerator*> (generator.get());
    ASSERT_NE (synth, nullptr);

    EXPECT_FLOAT_EQ (synth->params().oscShape.load(), 1.0f);
    EXPECT_FLOAT_EQ (synth->params().osc2DetuneCents.load(), -20.0f);
    EXPECT_FLOAT_EQ (synth->params().cutoffHz.load(), 900.0f);
    EXPECT_FLOAT_EQ (synth->params().filterEnvAmount.load(), 0.8f);
}

TEST (ChannelParams, SamplerAttackRampsIn)
{
    SamplerGenerator sampler;
    sampler.prepare (test::kSampleRate, 512);
    sampler.useSynthesizedDrum ("Kick", test::kSampleRate);
    sampler.params().attack.store (0.25f);   // long fade-in

    auto out = renderWithNote (sampler, 8192, false);
    const float early = out.getMagnitude (0, 0, 256);
    const float later = out.getMagnitude (0, 4096, 2048);
    EXPECT_LT (early, later) << "attack did not ramp the level up";
}

TEST (ChannelParams, SamplerOneShotIgnoresNoteOff)
{
    SamplerGenerator sampler;
    sampler.prepare (test::kSampleRate, 512);
    sampler.useSynthesizedDrum ("Kick", test::kSampleRate);   // 0.4 s
    sampler.params().oneShot.store (true);
    sampler.params().release.store (0.001f);

    auto out = renderWithNote (sampler, 16384, true, 1024);
    // Still sounding well past the note-off.
    EXPECT_GT (out.getMagnitude (0, 4096, 2048), 0.01f);
}

TEST (ChannelParams, SamplerSustainedModeRespectsNoteOff)
{
    SamplerGenerator sampler;
    sampler.prepare (test::kSampleRate, 512);
    sampler.useSynthesizedDrum ("Kick", test::kSampleRate);
    sampler.params().oneShot.store (false);
    sampler.params().release.store (0.005f);

    auto out = renderWithNote (sampler, 16384, true, 1024);
    EXPECT_GT (out.getMagnitude (0, 0, 512), 0.01f) << "note never started";
    EXPECT_LT (out.getMagnitude (0, 4096, 2048), 1.0e-3f) << "note-off was ignored";
}

TEST (ChannelParams, SamplerLowpassRemovesEnergy)
{
    auto renderWithCutoff = [] (float cutoff)
    {
        SamplerGenerator sampler;
        sampler.prepare (test::kSampleRate, 512);
        sampler.useSynthesizedDrum ("Hat", test::kSampleRate);   // bright noise
        sampler.params().cutoff.store (cutoff);
        return renderWithNote (sampler, 8192, false).getRMSLevel (0, 0, 4096);
    };

    EXPECT_LT (renderWithCutoff (200.0f), renderWithCutoff (20000.0f) * 0.6f);
}

TEST (ChannelParams, SamplerParamsSurviveSaveLoad)
{
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getNonexistentChildFile ("eurytest-params", ".eury");
    {
        ProjectModel model;
        auto channel = model.getChannel (0);
        channel.setProperty (ids::attack, 0.33, nullptr);
        channel.setProperty (ids::cutoff, 1234.0, nullptr);
        channel.setProperty (ids::oneShot, false, nullptr);
        ASSERT_TRUE (model.saveToFile (file));
    }
    {
        ProjectModel model;
        ASSERT_TRUE (model.loadFromFile (file));
        auto channel = model.getChannel (0);
        EXPECT_DOUBLE_EQ ((double) channel[ids::attack], 0.33);
        EXPECT_DOUBLE_EQ ((double) channel[ids::cutoff], 1234.0);
        EXPECT_FALSE ((bool) channel[ids::oneShot]);
    }
    file.deleteFile();
}

// ---------------- project file state ----------------

TEST (ProjectFileState, StartsCleanAndUntitled)
{
    ProjectModel model;
    ProjectFileState state (model);
    EXPECT_FALSE (state.isDirty());
    EXPECT_EQ (state.getDisplayName(), "Untitled");
    EXPECT_TRUE (state.getWindowTitle().contains ("Untitled"));
    EXPECT_FALSE (state.getWindowTitle().contains (
        juce::String (juce::CharPointer_UTF8 ("Eurydice \xe2\x80\x94  \xe2\x80\xa2"))));
}

TEST (ProjectFileState, EditsMarkDirty)
{
    ProjectModel model;
    ProjectFileState state (model);
    model.setTempo (123.0);
    EXPECT_TRUE (state.isDirty());
}

TEST (ProjectFileState, SelectionChangesDoNotMarkDirty)
{
    ProjectModel model;
    ProjectFileState state (model);
    model.getRoot().setProperty (ids::selectedChannel, 3, nullptr);
    model.setSongMode (true);
    EXPECT_FALSE (state.isDirty());
}

TEST (ProjectFileState, SaveClearsDirtyAndNamesWindow)
{
    ProjectModel model;
    ProjectFileState state (model);
    model.setTempo (99.0);
    ASSERT_TRUE (state.isDirty());

    const juce::File file ("/tmp/MyTrack.eury");
    state.markSaved (file);
    EXPECT_FALSE (state.isDirty());
    EXPECT_EQ (state.getDisplayName(), "MyTrack");
    EXPECT_TRUE (state.getWindowTitle().contains ("MyTrack"));

    model.setTempo (100.0);
    EXPECT_TRUE (state.isDirty());
    EXPECT_TRUE (state.getWindowTitle().endsWith (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa2"))));
}

TEST (ProjectFileState, NewProjectResetsState)
{
    ProjectModel model;
    ProjectFileState state (model);
    state.markSaved (juce::File ("/tmp/Old.eury"));
    model.setTempo (150.0);

    model.createDefaultProject();
    state.markNewProject();
    EXPECT_FALSE (state.isDirty());
    EXPECT_EQ (state.getDisplayName(), "Untitled");
}
