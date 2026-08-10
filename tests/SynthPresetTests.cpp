#include <gtest/gtest.h>
#include "control/ControlDispatcher.h"
#include "model/ChannelParams.h"
#include "model/SynthChannel.h"
#include "model/SynthPresets.h"
#include "ui/rack/ChannelEditor.h"
#include "TestHelpers.h"

// The synth's factory bank: every patch has to be a real, playable sound made
// only of parameters the engine actually has, and loading one has to leave the
// channel in a completely defined state.
namespace
{
// Holds a note for holdSeconds, then renders the tail — long enough for the
// pads, whose attack alone runs over a second.
juce::AudioBuffer<float> renderPatch (const juce::ValueTree& channel, int key,
                                      double holdSeconds, double tailSeconds)
{
    SynthGenerator synth;
    synth.prepare (test::kSampleRate, test::kBlockSize);
    synthchannel::apply (synth, channel);

    const int held = (int) (test::kSampleRate * holdSeconds);
    const int total = held + (int) (test::kSampleRate * tailSeconds);

    juce::AudioBuffer<float> out (2, total);
    out.clear();

    for (int pos = 0; pos < total; pos += test::kBlockSize)
    {
        const int n = juce::jmin (test::kBlockSize, total - pos);
        juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, pos, n);
        juce::MidiBuffer block;
        if (pos == 0)
            block.addEvent (juce::MidiMessage::noteOn (1, key, 0.9f), 0);
        if (pos <= held && held < pos + n)
            block.addEvent (juce::MidiMessage::noteOff (1, key), held - pos);
        synth.render (view, block);
    }
    return out;
}
} // namespace

TEST (SynthPresets, BankIsWellFormed)
{
    const auto& bank = synthpresets::factory();
    ASSERT_GT (bank.size(), 20u);

    juce::StringArray seen;
    for (const auto& preset : bank)
    {
        EXPECT_FALSE (preset.name.isEmpty());
        EXPECT_FALSE (preset.category.isEmpty());
        EXPECT_FALSE (preset.description.isEmpty()) << preset.name;
        EXPECT_FALSE (seen.contains (preset.name)) << "duplicate preset " << preset.name;
        seen.add (preset.name);

        juce::StringArray named;
        for (const auto& [id, value] : preset.values)
        {
            const auto* descriptor = channelparams::find ("synth", id.toString());
            ASSERT_NE (descriptor, nullptr) << preset.name << ": unknown parameter " << id.toString();
            EXPECT_GE (value, descriptor->range.start) << preset.name << ": " << id.toString();
            EXPECT_LE (value, descriptor->range.end) << preset.name << ": " << id.toString();
            EXPECT_FALSE (named.contains (id.toString()))
                << preset.name << ": " << id.toString() << " set twice";
            named.add (id.toString());
        }
    }

    // The genres the bank is organised by, Schranz and DnB the deepest.
    const auto categories = synthpresets::categories();
    EXPECT_TRUE (categories.contains ("Schranz"));
    EXPECT_TRUE (categories.contains ("Drum & Bass"));
    EXPECT_GE (categories.size(), 6);

    const auto countIn = [&bank] (const juce::String& category)
    {
        int n = 0;
        for (const auto& preset : bank)
            n += preset.category == category ? 1 : 0;
        return n;
    };
    EXPECT_GE (countIn ("Schranz"), 6);
    EXPECT_GE (countIn ("Drum & Bass"), 6);
}

TEST (SynthPresets, ApplyWritesEveryParameterAndClearsTheOneBefore)
{
    test::EngineFixture fixture;
    auto channel = fixture.model.addChannel ("synth", "Lead");
    // A value the next preset does not name, left over from whatever was
    // loaded before it.
    channel.setProperty (ids::noiseLevel, 0.9, nullptr);
    channel.setProperty (ids::volume, 0.42, nullptr);

    const auto preset = synthpresets::find ("Reese Bass");
    ASSERT_TRUE (preset.has_value());
    synthpresets::apply (channel, *preset, nullptr);

    EXPECT_EQ (channel[ids::presetName].toString(), "Reese Bass");
    // Named by the preset...
    EXPECT_DOUBLE_EQ ((double) channel[ids::osc2Detune], 26.0);
    // ...and not named, so back to its default rather than the stale 0.9.
    const auto* noise = channelparams::find ("synth", ids::noiseLevel.toString());
    ASSERT_NE (noise, nullptr);
    EXPECT_DOUBLE_EQ ((double) channel[ids::noiseLevel], noise->defaultValue);
    // The channel's own settings are not the patch's business.
    EXPECT_DOUBLE_EQ ((double) channel[ids::volume], 0.42);
    EXPECT_EQ (channel[ids::name].toString(), "Lead");

    for (const auto& descriptor : channelparams::synth())
        EXPECT_TRUE (channel.hasProperty (descriptor.id)) << descriptor.id.toString();
}

TEST (SynthPresets, EveryPresetIsAudibleAndClean)
{
    test::EngineFixture fixture;
    auto channel = fixture.model.addChannel ("synth", "Synth");

    for (const auto& preset : synthpresets::factory())
    {
        synthpresets::apply (channel, preset, nullptr);
        // C3: low enough for the basses, high enough for the leads.
        const auto out = renderPatch (channel, 48, 2.0, 0.6);

        const float peak = out.getMagnitude (0, out.getNumSamples());
        EXPECT_GT (peak, 0.05f) << preset.name << " is inaudible";
        // The filter has no resonance gain compensation, so the deliberately
        // resonant patches do peak over unity — the channel fader (0.78 by
        // default) covers that. Anything past this is an accident, not a
        // design decision: the ceiling caught a noise layer at 1.23.
        EXPECT_LE (peak, 1.1f) << preset.name << " is too hot";

        for (int channelIndex = 0; channelIndex < 2; ++channelIndex)
            for (int i = 0; i < out.getNumSamples(); ++i)
                ASSERT_TRUE (std::isfinite (out.getSample (channelIndex, i)))
                    << preset.name << " at " << i;
    }
}

TEST (SynthPresets, PresetsSoundDifferentFromEachOther)
{
    test::EngineFixture fixture;
    auto channel = fixture.model.addChannel ("synth", "Synth");

    const auto brightnessOf = [&channel] (const juce::String& name)
    {
        const auto preset = synthpresets::find (name);
        EXPECT_TRUE (preset.has_value()) << name;
        synthpresets::apply (channel, *preset, nullptr);
        const auto out = renderPatch (channel, 48, 0.5, 0.2);

        // Energy of the first difference over signal energy: high for a bright
        // patch, near zero for a sine sub.
        const auto* data = out.getReadPointer (0);
        double edges = 0.0, total = 1.0e-12;
        for (int i = 1; i < out.getNumSamples(); ++i)
        {
            edges += juce::square ((double) data[i] - data[i - 1]);
            total += juce::square ((double) data[i]);
        }
        return std::sqrt (edges / total);
    };

    // A sine sub has next to no harmonics; a resonant square screech is all
    // harmonics. Anything in between is a patch, not a copy.
    EXPECT_LT (brightnessOf ("Sub Roller"), 0.05);
    EXPECT_GT (brightnessOf ("Zap Screech"), brightnessOf ("Sub Roller") * 10.0);
    EXPECT_GT (brightnessOf ("Supersaw Lead"), brightnessOf ("Liquid Keys"));
}

TEST (SynthPresets, TheWobblePatchesActuallyMove)
{
    test::EngineFixture fixture;
    auto channel = fixture.model.addChannel ("synth", "Synth");

    const auto growl = synthpresets::find ("Neuro Growl");
    ASSERT_TRUE (growl.has_value());
    synthpresets::apply (channel, *growl, nullptr);

    // The LFO drives the cutoff at 6.2 Hz, so the loudness of a held note has
    // to swing between windows a fraction of a cycle apart.
    const auto out = renderPatch (channel, 48, 1.0, 0.1);
    const int window = (int) (test::kSampleRate * 0.02);
    float quietest = 1.0e9f, loudest = 0.0f;
    for (int start = (int) (test::kSampleRate * 0.2);
         start + window < (int) (test::kSampleRate * 0.9); start += window)
    {
        const float rms = out.getRMSLevel (0, start, window);
        quietest = juce::jmin (quietest, rms);
        loudest = juce::jmax (loudest, rms);
    }
    EXPECT_GT (loudest, quietest * 1.5f);
}

TEST (SynthPresets, UserPatchesRoundTripThroughAFile)
{
    test::EngineFixture fixture;
    auto channel = fixture.model.addChannel ("synth", "Synth");
    synthpresets::apply (channel, *synthpresets::find ("Neuro Growl"), nullptr);
    channel.setProperty (ids::cutoff, 333.0, nullptr);

    juce::TemporaryFile temp (synthpresets::fileExtension);
    ASSERT_TRUE (synthpresets::writeFile (temp.getFile(), channel, "My Growl"));

    const auto reloaded = synthpresets::readFile (temp.getFile());
    ASSERT_TRUE (reloaded.has_value());
    EXPECT_EQ (reloaded->name, "My Growl");
    EXPECT_EQ (reloaded->category, synthpresets::userCategory);

    // Applying the file back onto a blank channel reproduces the patch it was
    // saved from, parameter for parameter.
    auto other = fixture.model.addChannel ("synth", "Other");
    synthpresets::apply (other, *reloaded, nullptr);
    for (const auto& descriptor : channelparams::synth())
        EXPECT_DOUBLE_EQ ((double) other[descriptor.id], (double) channel[descriptor.id])
            << descriptor.id.toString();
}

TEST (SynthPresets, AHandEditedFileCannotPushAKnobOutOfRange)
{
    juce::TemporaryFile temp (synthpresets::fileExtension);
    temp.getFile().replaceWithText (
        R"(<EURYPRESET type="synth" name="Wild" cutoff="99999" resonance="-3" nonsense="7"/>)");

    const auto preset = synthpresets::readFile (temp.getFile());
    ASSERT_TRUE (preset.has_value());
    for (const auto& [id, value] : preset->values)
    {
        const auto* descriptor = channelparams::find ("synth", id.toString());
        ASSERT_NE (descriptor, nullptr) << id.toString() << " is not a synth parameter";
        EXPECT_GE (value, descriptor->range.start);
        EXPECT_LE (value, descriptor->range.end);
    }
    EXPECT_EQ (preset->values.size(), 2u);   // the made-up attribute is dropped
}

TEST (SynthPresets, SomethingElseEntirelyIsNotAPreset)
{
    juce::TemporaryFile temp (synthpresets::fileExtension);
    temp.getFile().replaceWithText ("<PROJECT tempo=\"174\"/>");
    EXPECT_FALSE (synthpresets::readFile (temp.getFile()).has_value());

    temp.getFile().replaceWithText ("not xml at all");
    EXPECT_FALSE (synthpresets::readFile (temp.getFile()).has_value());
}

TEST (SynthControl, ListsAndLoadsFactoryPresets)
{
    AppServices services { false };   // no audio device in tests
    ControlDispatcher dispatcher (services);

    const auto listed = dispatcher.dispatch ("synth.presets", {});
    ASSERT_TRUE (listed.isArray());
    EXPECT_GE (listed.size(), 20);
    EXPECT_TRUE (listed[0]["name"].toString().isNotEmpty());
    EXPECT_TRUE (listed[0]["category"].toString().isNotEmpty());
    EXPECT_TRUE (listed[0]["description"].toString().isNotEmpty());

    const auto added = dispatcher.dispatch ("channel.add",
        juce::JSON::parse (R"({"type": "synth", "name": "Bass"})"));
    const int channelId = added["id"];

    const auto loaded = dispatcher.dispatch ("synth.loadPreset", juce::JSON::parse (
        R"({"channelId": )" + juce::String (channelId) + R"(, "preset": "Schranz Hoover"})"));
    EXPECT_EQ (loaded["preset"].toString(), "Schranz Hoover");

    auto channel = services.project.getChannelById (channelId);
    EXPECT_EQ (channel[ids::presetName].toString(), "Schranz Hoover");
    EXPECT_DOUBLE_EQ ((double) channel[ids::unisonVoices], 7.0);

    // A patch is a starting point: the knobs stay reachable afterwards.
    dispatcher.dispatch ("channel.set", juce::JSON::parse (
        R"({"channelId": )" + juce::String (channelId) + R"(, "cutoff": 900, "glide": 0.2})"));
    EXPECT_DOUBLE_EQ ((double) channel[ids::cutoff], 900.0);
    EXPECT_DOUBLE_EQ ((double) channel[ids::glide], 0.2);

    EXPECT_ANY_THROW (dispatcher.dispatch ("synth.loadPreset", juce::JSON::parse (
        R"({"channelId": )" + juce::String (channelId) + R"(, "preset": "Nope"})")));
}

TEST (SynthControl, RefusesToSaveOverAFactoryName)
{
    AppServices services { false };
    ControlDispatcher dispatcher (services);

    const auto added = dispatcher.dispatch ("channel.add",
        juce::JSON::parse (R"({"type": "synth", "name": "Bass"})"));
    const auto id = juce::String ((int) added["id"]);

    // A user patch that shadows a factory name would show twice in the browser.
    EXPECT_ANY_THROW (dispatcher.dispatch ("synth.savePreset",
        juce::JSON::parse (R"({"channelId": )" + id + R"(, "preset": "Reese Bass"})")));
    EXPECT_ANY_THROW (dispatcher.dispatch ("synth.savePreset",
        juce::JSON::parse (R"({"channelId": )" + id + R"(, "preset": "   "})")));
}

TEST (SynthControl, RefusesToLoadASynthPresetOntoAnotherGenerator)
{
    AppServices services { false };
    ControlDispatcher dispatcher (services);

    const auto added = dispatcher.dispatch ("channel.add",
        juce::JSON::parse (R"({"type": "kick", "name": "Kick"})"));
    EXPECT_ANY_THROW (dispatcher.dispatch ("synth.loadPreset", juce::JSON::parse (
        R"({"channelId": )" + juce::String ((int) added["id"]) + R"(, "preset": "Reese Bass"})")));
}

// ================================ the editor ===============================

TEST (SynthPresetUi, PresetBarSitsAboveTheModules)
{
    AppServices services (false);
    auto channel = services.project.addChannel ("synth", "Lead");
    SynthEditor editor (services, channel);   // the constructor sizes and lays out

    // Every child is inside the panel, and the bar leaves the modules alone:
    // the top strip is the only thing above the first module row.
    int barBottom = 0, contentTop = editor.getHeight();
    for (int i = 0; i < editor.getNumChildComponents(); ++i)
    {
        const auto bounds = editor.getChildComponent (i)->getBounds();
        EXPECT_TRUE (editor.getLocalBounds().contains (bounds))
            << "child " << i << " at " << bounds.toString().toStdString();
        if (bounds.getBottom() <= 36)
            barBottom = juce::jmax (barBottom, bounds.getBottom());
        else
            contentTop = juce::jmin (contentTop, bounds.getY());
    }
    EXPECT_GT (barBottom, 0) << "no preset bar";
    EXPECT_GE (contentTop, barBottom) << "the bar overlaps the modules";
}

TEST (SynthPresetUi, LoadingAPresetMovesTheKnobs)
{
    AppServices services (false);
    auto channel = services.project.addChannel ("synth", "Lead");
    synthpresets::apply (channel, *synthpresets::find ("Sub Roller"), nullptr);

    SynthEditor editor (services, channel);
    const juce::Rectangle<int> knobs { 0, 40, editor.getWidth(), editor.getHeight() - 100 };
    const auto before = editor.createComponentSnapshot (knobs);

    // A preset load writes the channel tree — exactly what the combo box, the
    // control API and undo all do.
    synthpresets::apply (channel, *synthpresets::find ("Schranz Hoover"), nullptr);
    const auto after = editor.createComponentSnapshot (knobs);

    bool moved = false;
    for (int y = 0; y < before.getHeight() && ! moved; ++y)
        for (int x = 0; x < before.getWidth() && ! moved; ++x)
            moved = before.getPixelAt (x, y) != after.getPixelAt (x, y);
    EXPECT_TRUE (moved) << "the knobs did not follow the preset";
}
