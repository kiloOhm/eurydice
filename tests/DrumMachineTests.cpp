#include <gtest/gtest.h>
#include "engine/DrumMachineGenerator.h"
#include "model/DrumKits.h"
#include "model/DrumPads.h"
#include "TestHelpers.h"

namespace
{
juce::AudioBuffer<float> renderDrums (DrumMachineGenerator& drums, const juce::MidiBuffer& midi,
                                      int numSamples)
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
        drums.render (view, block);
        pos += n;
    }
    return out;
}

juce::MidiBuffer noteOnAt0 (int key, float velocity = 0.9f)
{
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, key, velocity), 0);
    return midi;
}

// A one-pad machine with a synthesised sound on the given note.
void setupPad (DrumMachineGenerator& drums, int pad, int key, const char* kind = "kick")
{
    drums.padParams (pad).key.store (key);
    drums.setPadSource (pad, {}, kind);
}
} // namespace

TEST (DrumMachine, PadPlaysOnItsNoteOnly)
{
    DrumMachineGenerator drums;
    drums.prepare (test::kSampleRate, 512);
    setupPad (drums, 0, 36);
    drums.setNumPads (1);

    auto hit = renderDrums (drums, noteOnAt0 (36), 8192);
    EXPECT_GT (hit.getMagnitude (0, 0, 8192), 0.05f);

    drums.reset();   // silence the ringing kick before the wrong-note check
    auto miss = renderDrums (drums, noteOnAt0 (37), 8192);
    EXPECT_EQ (miss.getMagnitude (0, 0, 8192), 0.0f);
}

TEST (DrumMachine, NoteOffIsIgnoredAndTheSampleEnds)
{
    DrumMachineGenerator drums;
    drums.prepare (test::kSampleRate, 512);
    setupPad (drums, 0, 36, "hat");   // 0.09 s
    drums.setNumPads (1);

    juce::MidiBuffer midi = noteOnAt0 (36);
    midi.addEvent (juce::MidiMessage::noteOff (1, 36), 100);

    auto out = renderDrums (drums, midi, 22050);
    EXPECT_GT (out.getMagnitude (0, 0, 4096), 0.05f);
    EXPECT_LT (out.getMagnitude (0, 8000, 14000), 1.0e-5f);
}

TEST (DrumMachine, VelocityAndGainScaleTheHit)
{
    auto peakFor = [] (float velocity, float gain)
    {
        DrumMachineGenerator drums;
        drums.prepare (test::kSampleRate, 512);
        setupPad (drums, 0, 36);
        drums.padParams (0).gain.store (gain);
        drums.setNumPads (1);
        return renderDrums (drums, noteOnAt0 (36, velocity), 4096).getMagnitude (0, 0, 4096);
    };

    const float full = peakFor (1.0f, 1.0f);
    EXPECT_NEAR (peakFor (0.5f, 1.0f), full * 0.5f, full * 0.05f);
    EXPECT_NEAR (peakFor (1.0f, 0.5f), full * 0.5f, full * 0.05f);
}

TEST (DrumMachine, PanMovesTheHitAcrossTheField)
{
    auto sides = [] (float pan)
    {
        DrumMachineGenerator drums;
        drums.prepare (test::kSampleRate, 512);
        setupPad (drums, 0, 36);
        drums.padParams (0).pan.store (pan);
        drums.setNumPads (1);
        auto out = renderDrums (drums, noteOnAt0 (36), 4096);
        return std::pair<float, float> (out.getMagnitude (0, 0, 4096),
                                        out.getMagnitude (1, 0, 4096));
    };

    const auto centre = sides (0.0f);
    EXPECT_NEAR (centre.first, centre.second, centre.first * 0.01f);

    const auto hardLeft = sides (-1.0f);
    EXPECT_GT (hardLeft.first, 0.05f);
    EXPECT_EQ (hardLeft.second, 0.0f);
}

TEST (DrumMachine, TuneRepitchesThePad)
{
    auto lengthAt = [] (float tune)
    {
        DrumMachineGenerator drums;
        drums.prepare (test::kSampleRate, 512);
        setupPad (drums, 0, 36);   // kick, 0.40 s at zero tune
        drums.padParams (0).tune.store (tune);
        drums.setNumPads (1);
        auto out = renderDrums (drums, noteOnAt0 (36), 22050);
        // Last sample index with energy.
        for (int i = out.getNumSamples(); --i >= 0;)
            if (std::abs (out.getSample (0, i)) > 1.0e-4f)
                return i;
        return 0;
    };

    // +12 st plays twice as fast, so the sound is about half as long.
    const int atRoot = lengthAt (0.0f);
    const int octaveUp = lengthAt (12.0f);
    EXPECT_GT (atRoot, 12000);
    EXPECT_NEAR (octaveUp, atRoot / 2, atRoot / 10);
}

TEST (DrumMachine, ChokeGroupCutsTheRingingPad)
{
    DrumMachineGenerator drums;
    drums.prepare (test::kSampleRate, 512);
    setupPad (drums, 0, 36, "kick");   // long sound to be choked
    setupPad (drums, 1, 37, "hat");    // the choker
    drums.padParams (0).choke.store (1);
    drums.padParams (1).choke.store (1);
    drums.setNumPads (2);

    // Without the choke the kick still rings at 0.2 s.
    auto unchoked = renderDrums (drums, noteOnAt0 (36), 22050);
    const float ringing = unchoked.getMagnitude (0, 8820, 4410);
    EXPECT_GT (ringing, 0.01f);

    // Hat on the same group 0.1 s later: kick is gone by 0.2 s (the hat
    // itself only lasts 0.09 s, so past 0.19 s the field is silent).
    drums.reset();
    juce::MidiBuffer midi = noteOnAt0 (36);
    midi.addEvent (juce::MidiMessage::noteOn (1, 37, 0.9f), 4410);
    auto choked = renderDrums (drums, midi, 22050);
    EXPECT_LT (choked.getMagnitude (0, 12000, 4410), ringing * 0.05f);
}

TEST (DrumMachine, PadsOnTheSameNoteLayer)
{
    DrumMachineGenerator drums;
    drums.prepare (test::kSampleRate, 512);
    setupPad (drums, 0, 36, "kick");
    setupPad (drums, 1, 36, "clap");
    drums.setNumPads (2);

    auto layered = renderDrums (drums, noteOnAt0 (36), 4096);
    EXPECT_EQ (drums.getTriggerCount (0), 1u);
    EXPECT_EQ (drums.getTriggerCount (1), 1u);
    EXPECT_GT (layered.getMagnitude (0, 0, 4096), 0.05f);
}

TEST (DrumMachine, LoadsASampleFileOntoAPad)
{
    const auto tone = test::makeToneFile (0.25);
    DrumMachineGenerator drums;
    drums.prepare (test::kSampleRate, 512);
    drums.padParams (0).key.store (36);
    drums.setPadSource (0, tone.getFullPathName(), {});
    drums.setNumPads (1);

    EXPECT_TRUE (drums.padHasSample (0));
    EXPECT_NEAR (drums.getPadLengthSeconds (0), 0.25, 0.01);

    auto out = renderDrums (drums, noteOnAt0 (36), 8192);
    EXPECT_GT (out.getMagnitude (0, 0, 8192), 0.05f);
}

TEST (DrumMachine, EmptyPadIsSilentButStillFlashes)
{
    DrumMachineGenerator drums;
    drums.prepare (test::kSampleRate, 512);
    drums.padParams (0).key.store (36);
    drums.setPadSource (0, {}, {});
    drums.setNumPads (1);

    auto out = renderDrums (drums, noteOnAt0 (36), 4096);
    EXPECT_EQ (out.getMagnitude (0, 0, 4096), 0.0f);
    EXPECT_EQ (drums.getTriggerCount (0), 1u);   // the editor can still light it
}

TEST (DrumMachine, ResetSilencesRingingPads)
{
    DrumMachineGenerator drums;
    drums.prepare (test::kSampleRate, 512);
    setupPad (drums, 0, 36);
    drums.setNumPads (1);

    juce::AudioBuffer<float> block (2, 512);
    block.clear();
    drums.render (block, noteOnAt0 (36));
    drums.reset();

    block.clear();
    drums.render (block, {});
    EXPECT_EQ (block.getMagnitude (0, 0, 512), 0.0f);
}

// ============================ model helpers ============================

TEST (DrumPads, DefaultKitHasSixteenPadsWithSynthSounds)
{
    juce::ValueTree channel (ids::CHANNEL);
    drumpads::initialiseDrumChannel (channel, nullptr);

    EXPECT_EQ (drumpads::padCount (channel), 16);
    EXPECT_EQ (drumpads::gridRows (channel), 4);
    EXPECT_EQ (drumpads::gridCols (channel), 4);

    // Pads count up from the base note.
    for (int i = 0; i < 16; ++i)
        EXPECT_EQ ((int) drumpads::getPad (channel, i)[ids::key],
                   drumpads::defaultBaseNote + i);

    // The starter kit occupies the first four pads.
    EXPECT_EQ (drumpads::getPad (channel, 0)[ids::synthDrum].toString(), "kick");
    EXPECT_EQ (drumpads::getPad (channel, 3)[ids::synthDrum].toString(), "hat");
    EXPECT_TRUE (drumpads::getPad (channel, 4)[ids::synthDrum].toString().isEmpty());
}

TEST (DrumPads, GrowingTheGridKeepsExistingPads)
{
    juce::ValueTree channel (ids::CHANNEL);
    drumpads::initialiseDrumChannel (channel, nullptr);
    auto pad0 = drumpads::getPad (channel, 0);
    pad0.setProperty (ids::key, 99, nullptr);

    drumpads::ensurePadCount (channel, 64, nullptr);
    EXPECT_EQ (drumpads::padCount (channel), 64);
    EXPECT_EQ ((int) drumpads::getPad (channel, 0)[ids::key], 99);

    // Shrinking the grid shape never deletes pads.
    channel.setProperty (ids::padRows, 2, nullptr);
    channel.setProperty (ids::padCols, 2, nullptr);
    EXPECT_EQ (drumpads::padCount (channel), 64);
}

TEST (DrumPads, GridMappingPutsPadOneBottomLeft)
{
    // 4x4: bottom row is pads 0-3, top row is pads 12-15.
    EXPECT_EQ (drumpads::padIndexForCell (0, 3, 4, 4), 0);
    EXPECT_EQ (drumpads::padIndexForCell (3, 3, 4, 4), 3);
    EXPECT_EQ (drumpads::padIndexForCell (0, 0, 4, 4), 12);
    EXPECT_EQ (drumpads::padIndexForCell (3, 0, 4, 4), 15);

    // 2x8 (a Maschine-style double row).
    EXPECT_EQ (drumpads::padIndexForCell (0, 1, 2, 8), 0);
    EXPECT_EQ (drumpads::padIndexForCell (7, 0, 2, 8), 15);
}

TEST (DrumPads, AutoMapNumberssPadsFromTheBaseNote)
{
    juce::ValueTree channel (ids::CHANNEL);
    channel.setProperty (ids::padBaseNote, 48, nullptr);
    drumpads::ensurePadCount (channel, 8, nullptr);
    drumpads::getPad (channel, 2).setProperty (ids::key, 99, nullptr);   // scrambled

    drumpads::autoMapNotes (channel, nullptr);
    for (int i = 0; i < 8; ++i)
        EXPECT_EQ ((int) drumpads::getPad (channel, i)[ids::key], 48 + i);
}

// ============================ kit presets ============================

namespace
{
// A kits directory with one manifest kit and one plain folder of samples.
struct KitFixture
{
    KitFixture()
    {
        base = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("eurydice-kit-tests");
        base.deleteRecursively();

        auto manifest = base.getChildFile ("Manifest Kit");
        manifest.createDirectory();
        writeWav (manifest.getChildFile ("kick.wav"));
        writeWav (manifest.getChildFile ("hat.wav"));
        manifest.getChildFile ("kit.json").replaceWithText (R"({
            "name": "My 808",
            "pads": [
              { "name": "Kick",     "file": "kick.wav" },
              { "name": "Open Hat", "file": "hat.wav", "choke": 3 },
              { "name": "Missing",  "file": "nope.wav" }
            ] })");

        auto plain = base.getChildFile ("Plain Kit");
        plain.createDirectory();
        writeWav (plain.getChildFile ("02 Snare.wav"));
        writeWav (plain.getChildFile ("01 Kick.wav"));
        writeWav (plain.getChildFile ("notes.txt"));   // ignored: not audio
    }

    ~KitFixture() { base.deleteRecursively(); }

    static void writeWav (const juce::File& file)
    {
        file.replaceWithData (nullptr, 0);
        const auto tone = test::makeToneFile (0.05);
        tone.copyFileTo (file);
    }

    juce::File base;
};
} // namespace

TEST (DrumKits, ScansManifestAndPlainFolders)
{
    KitFixture fixture;
    const auto kits = drumkits::scanKits (fixture.base);
    ASSERT_EQ (kits.size(), 2u);

    // Alphabetical by folder: "Manifest Kit" first, named by its manifest.
    EXPECT_EQ (kits[0].name, "My 808");
    ASSERT_EQ (kits[0].pads.size(), 2u);   // the missing file is skipped
    EXPECT_EQ (kits[0].pads[0].name, "Kick");
    EXPECT_EQ (kits[0].pads[1].name, "Open Hat");
    EXPECT_EQ (kits[0].pads[1].choke, 3);

    // Plain folder: filename order, numbers stripped from pad names.
    EXPECT_EQ (kits[1].name, "Plain Kit");
    ASSERT_EQ (kits[1].pads.size(), 2u);
    EXPECT_EQ (kits[1].pads[0].name, "Kick");
    EXPECT_EQ (kits[1].pads[1].name, "Snare");
}

TEST (DrumKits, ApplyKitFillsPadsAndKeepsNoteMapping)
{
    KitFixture fixture;
    const auto kits = drumkits::scanKits (fixture.base);
    ASSERT_FALSE (kits.empty());

    juce::ValueTree channel (ids::CHANNEL);
    drumpads::initialiseDrumChannel (channel, nullptr);
    drumpads::getPad (channel, 0).setProperty (ids::key, 99, nullptr);   // custom mapping
    drumpads::getPad (channel, 0).setProperty (ids::tune, -5.0, nullptr);

    drumkits::applyKit (channel, kits[0], nullptr);

    auto pad0 = drumpads::getPad (channel, 0);
    EXPECT_EQ (pad0[ids::name].toString(), "Kick");
    EXPECT_TRUE (pad0[ids::samplePath].toString().endsWith ("kick.wav"));
    EXPECT_FALSE (pad0.hasProperty (ids::synthDrum));    // sample replaces synth
    EXPECT_EQ ((int) pad0[ids::key], 99);                // mapping survives
    EXPECT_EQ ((double) pad0[ids::tune], 0.0);           // tweaks reset

    auto pad1 = drumpads::getPad (channel, 1);
    EXPECT_EQ ((int) pad1[ids::choke], 3);
}

TEST (DrumKits, ApplyKitGrowsTheGridWhenTheKitIsBigger)
{
    KitFixture fixture;
    auto big = fixture.base.getChildFile ("Big Kit");
    big.createDirectory();
    for (int i = 0; i < 20; ++i)
        KitFixture::writeWav (big.getChildFile (juce::String (i < 10 ? "0" : "") + juce::String (i) + " Pad.wav"));

    juce::ValueTree channel (ids::CHANNEL);
    channel.setProperty (ids::padRows, 2, nullptr);
    channel.setProperty (ids::padCols, 2, nullptr);
    drumpads::ensurePadCount (channel, 4, nullptr);

    drumkits::applyKit (channel, drumkits::loadKit (big), nullptr);
    EXPECT_GE (drumpads::gridRows (channel) * drumpads::gridCols (channel), 20);
    EXPECT_EQ (drumpads::padCount (channel), 20);
}

// ======================= engine integration =======================

TEST (DrumMachine, PlaysThroughTheEngineFromAChannelTree)
{
    test::EngineFixture fixture;
    auto channel = fixture.model.addChannel ("drums", "Drums");
    drumpads::initialiseDrumChannel (channel, nullptr);
    fixture.sync.rebuildNow();

    auto generator = fixture.generators.getOrCreate (channel);
    auto drums = std::dynamic_pointer_cast<DrumMachineGenerator> (generator);
    ASSERT_NE (drums, nullptr);
    EXPECT_EQ (drums->getNumPads(), 16);
    EXPECT_TRUE (drums->padHasSample (0));    // the starter kick
    EXPECT_FALSE (drums->padHasSample (5));   // an empty pad

    auto out = renderDrums (*drums, noteOnAt0 (drumpads::defaultBaseNote), 8192);
    EXPECT_GT (out.getMagnitude (0, 0, 8192), 0.05f);
}
