#include "TestHelpers.h"
#include "engine/OfflineRenderer.h"

using test::EngineFixture;
using test::rmsOf;

namespace
{
// Counted, because two names are handed out before either file exists.
juce::File tempWav()
{
    static int counter = 0;
    return juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getNonexistentChildFile ("eurytest-render-" + juce::String (++counter), ".wav");
}

juce::AudioFormatManager& formatManager()
{
    static juce::AudioFormatManager formats;
    if (formats.getNumKnownFormats() == 0)
        formats.registerBasicFormats();
    return formats;
}

juce::int64 lengthInSamples (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager().createReaderFor (file));
    return reader != nullptr ? reader->lengthInSamples : -1;
}

double sampleRateOf (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager().createReaderFor (file));
    return reader != nullptr ? reader->sampleRate : -1.0;
}

juce::AudioBuffer<float> readWav (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager().createReaderFor (file));
    if (reader == nullptr)
        return {};
    juce::AudioBuffer<float> buffer (2, (int) reader->lengthInSamples);
    reader->read (&buffer, 0, (int) reader->lengthInSamples, 0, true, true);
    return buffer;
}

float peakOf (const juce::File& file)
{
    const auto buffer = readWav (file);
    return buffer.getNumSamples() > 0 ? buffer.getMagnitude (0, buffer.getNumSamples()) : 0.0f;
}

// Samples per bar at the fixture's default tempo.
int barSamples (const EngineFixture& fx)
{
    return (int) (ids::ticksPerBar / fx.ticksPerSample());
}

void clearPatternNotes (EngineFixture& fx)
{
    auto pattern = fx.model.getPattern (0);
    for (int i = pattern.getNumChildren(); --i >= 0;)
        if (pattern.getChild (i).hasType (ids::LANE))
            pattern.removeChild (i, nullptr);
    fx.sync.rebuildNow();
}

void deleteAll (const OfflineRenderer::Result& result)
{
    for (const auto& f : result.writtenFiles)
        juce::File (f).deleteFile();
}
}

TEST (OfflineRenderer, PatternModeRendersExactDuration)
{
    EngineFixture fx;
    OfflineRenderer::Options opts;
    opts.wavFile = tempWav();
    opts.tailSeconds = 0.5;

    const auto result = OfflineRenderer::render (fx.engine, fx.model, opts);
    ASSERT_TRUE (result.ok) << result.error;
    ASSERT_TRUE (opts.wavFile.existsAsFile());

    // 1 bar @140 bpm = 4*60/140 s, plus 0.5 s tail.
    const double expected = (4.0 * 60.0 / 140.0 + 0.5) * test::kSampleRate;
    EXPECT_NEAR ((double) lengthInSamples (opts.wavFile), expected, test::kBlockSize + 1);
    opts.wavFile.deleteFile();
}

TEST (OfflineRenderer, EmptySongModeFails)
{
    EngineFixture fx;
    fx.model.setSongMode (true);
    fx.sync.rebuildNow();

    OfflineRenderer::Options opts;
    opts.wavFile = tempWav();
    const auto result = OfflineRenderer::render (fx.engine, fx.model, opts);
    EXPECT_FALSE (result.ok);
    EXPECT_FALSE (opts.wavFile.existsAsFile());
}

TEST (OfflineRenderer, StemsWrittenForUsedInserts)
{
    EngineFixture fx;
    // Hat channel -> insert 2.
    fx.model.getChannel (2).setProperty (ids::insertIndex, 2, nullptr);

    const int patId = fx.model.getRoot()[ids::activePattern];
    auto clip = fx.model.addPlaylistClip ("pattern", 0, 0, ids::ticksPerBar);
    clip.setProperty (ids::patternId, patId, nullptr);
    fx.model.setSongMode (true);
    fx.sync.rebuildNow();

    OfflineRenderer::Options opts;
    opts.wavFile = tempWav();
    opts.stems = OfflineRenderer::Stems::perInsert;
    opts.tailSeconds = 0.1;

    const auto result = OfflineRenderer::render (fx.engine, fx.model, opts);
    ASSERT_TRUE (result.ok) << result.error;

    const auto stem = opts.wavFile.getSiblingFile (
        opts.wavFile.getFileNameWithoutExtension() + "-Insert 2.wav");
    EXPECT_TRUE (result.writtenFiles.contains (stem.getFullPathName()));
    EXPECT_TRUE (stem.existsAsFile());
    EXPECT_GT (lengthInSamples (stem), 0);

    deleteAll (result);
}

TEST (OfflineRenderer, ProgressReachesOne)
{
    EngineFixture fx;
    OfflineRenderer::Options opts;
    opts.wavFile = tempWav();
    opts.tailSeconds = 0.1;
    double last = 0.0;
    opts.progress = [&last] (double p) { last = p; };

    ASSERT_TRUE (OfflineRenderer::render (fx.engine, fx.model, opts).ok);
    EXPECT_NEAR (last, 1.0, 1.0e-9);
    opts.wavFile.deleteFile();
}

// ---------------- loop range ----------------

TEST (OfflineRenderer, LoopRangeOnlyRendersExactlyTheRange)
{
    EngineFixture fx;
    fx.model.setLoopRange (ids::ticksPerBar, 3 * ids::ticksPerBar);
    fx.model.setLoopEnabled (true);
    fx.sync.rebuildNow();

    OfflineRenderer::Options opts;
    opts.wavFile = tempWav();
    opts.loopRangeOnly = true;
    opts.tailSeconds = 0.0;

    const auto result = OfflineRenderer::render (fx.engine, fx.model, opts);
    ASSERT_TRUE (result.ok) << result.error;

    const double expected = 2.0 * ids::ticksPerBar / fx.ticksPerSample();
    EXPECT_NEAR ((double) lengthInSamples (opts.wavFile), expected, 2.0);
    opts.wavFile.deleteFile();
}

TEST (OfflineRenderer, LoopRangeOnlyStartsAtTheLoopStart)
{
    EngineFixture fx;
    clearPatternNotes (fx);

    // One note at bar 2, reachable only if the render starts at the loop start.
    auto lane = fx.model.getOrCreateLane (fx.model.getPattern (0), fx.model.getChannel (0)[ids::id]);
    fx.model.getPattern (0).setProperty (ids::lengthTicks, 4 * ids::ticksPerBar, nullptr);
    fx.model.addNote (lane, 60, 2 * ids::ticksPerBar, ids::ticksPerStep);
    fx.model.setLoopRange (2 * ids::ticksPerBar, 3 * ids::ticksPerBar);
    fx.sync.rebuildNow();

    OfflineRenderer::Options opts;
    opts.wavFile = tempWav();
    opts.loopRangeOnly = true;
    opts.tailSeconds = 0.0;

    const auto result = OfflineRenderer::render (fx.engine, fx.model, opts);
    ASSERT_TRUE (result.ok) << result.error;

    const auto audio = readWav (opts.wavFile);
    EXPECT_GT (rmsOf (audio, 0, 2048), 0.01f) << "the note at the loop start was not rendered";
    opts.wavFile.deleteFile();
}

TEST (OfflineRenderer, LoopRangeOnlyFailsWithoutARange)
{
    EngineFixture fx;
    fx.model.clearLoop();

    OfflineRenderer::Options opts;
    opts.wavFile = tempWav();
    opts.loopRangeOnly = true;

    const auto result = OfflineRenderer::render (fx.engine, fx.model, opts);
    EXPECT_FALSE (result.ok);
    EXPECT_TRUE (result.error.contains ("Loop range"));
    EXPECT_FALSE (opts.wavFile.existsAsFile());
}

// A loop armed for editing must not bound a normal render: the transport used
// to wrap at loopEnd, so everything after it was silently dropped.
TEST (OfflineRenderer, ArmedLoopDoesNotTruncateASongRender)
{
    EngineFixture fx;
    const int patId = fx.model.getRoot()[ids::activePattern];
    for (int bar : { 0, 6 })
    {
        auto clip = fx.model.addPlaylistClip ("pattern", 0, bar * ids::ticksPerBar, ids::ticksPerBar);
        clip.setProperty (ids::patternId, patId, nullptr);
    }
    fx.model.setSongMode (true);
    fx.model.setLoopRange (0, 2 * ids::ticksPerBar);
    fx.model.setLoopEnabled (true);
    fx.sync.rebuildNow();

    OfflineRenderer::Options opts;
    opts.wavFile = tempWav();
    opts.tailSeconds = 0.1;

    const auto result = OfflineRenderer::render (fx.engine, fx.model, opts);
    ASSERT_TRUE (result.ok) << result.error;

    const auto audio = readWav (opts.wavFile);
    const int bar = barSamples (fx);
    ASSERT_GT (audio.getNumSamples(), 7 * bar - 1);

    EXPECT_GT (rmsOf (audio, 6 * bar, 4096), 0.01f) << "the last clip never rendered";
    EXPECT_LT (rmsOf (audio, 4 * bar, 4096), 1.0e-4f) << "the loop repeated over the empty bars";

    // Rendering is not an edit: the armed loop survives it untouched.
    EXPECT_TRUE (fx.model.isLoopEnabled());
    EXPECT_EQ (fx.model.getLoopStart(), 0);
    EXPECT_EQ (fx.model.getLoopEnd(), 2 * ids::ticksPerBar);

    deleteAll (result);
}

// ---------------- normalisation ----------------

TEST (OfflineRenderer, NormalisesThePeakToTheTarget)
{
    EngineFixture fx;
    OfflineRenderer::Options opts;
    opts.wavFile = tempWav();
    opts.tailSeconds = 0.1;
    opts.normalise = true;
    opts.normaliseTargetDb = -6.0;

    const auto result = OfflineRenderer::render (fx.engine, fx.model, opts);
    ASSERT_TRUE (result.ok) << result.error;
    EXPECT_TRUE (result.error.isEmpty());

    EXPECT_NEAR (peakOf (opts.wavFile), juce::Decibels::decibelsToGain (-6.0f), 0.002f);
    opts.wavFile.deleteFile();
}

TEST (OfflineRenderer, NormalisationLeavesTheLengthAlone)
{
    EngineFixture fx;
    OfflineRenderer::Options plain, normalised;
    plain.wavFile = tempWav();
    plain.tailSeconds = 0.1;
    normalised = plain;
    normalised.wavFile = tempWav();
    normalised.normalise = true;
    normalised.normaliseTargetDb = -1.0;

    ASSERT_TRUE (OfflineRenderer::render (fx.engine, fx.model, plain).ok);
    ASSERT_TRUE (OfflineRenderer::render (fx.engine, fx.model, normalised).ok);

    EXPECT_EQ (lengthInSamples (plain.wavFile), lengthInSamples (normalised.wavFile));
    EXPECT_GT (peakOf (normalised.wavFile), peakOf (plain.wavFile));
    plain.wavFile.deleteFile();
    normalised.wavFile.deleteFile();
}

TEST (OfflineRenderer, NormalisationOfSilenceWarnsInsteadOfExploding)
{
    EngineFixture fx;
    clearPatternNotes (fx);

    OfflineRenderer::Options opts;
    opts.wavFile = tempWav();
    opts.tailSeconds = 0.0;
    opts.normalise = true;

    const auto result = OfflineRenderer::render (fx.engine, fx.model, opts);
    ASSERT_TRUE (result.ok) << result.error;
    EXPECT_TRUE (result.error.contains ("silent"));
    EXPECT_EQ (peakOf (opts.wavFile), 0.0f);
    opts.wavFile.deleteFile();
}

// Stems carry the master's gain so their sum still matches the mix.
TEST (OfflineRenderer, NormalisationScalesStemsByTheSameGain)
{
    EngineFixture fx;

    OfflineRenderer::Options plain;
    plain.wavFile = tempWav();
    plain.tailSeconds = 0.1;
    plain.stems = OfflineRenderer::Stems::perChannel;
    const auto plainResult = OfflineRenderer::render (fx.engine, fx.model, plain);
    ASSERT_TRUE (plainResult.ok) << plainResult.error;
    const float plainMaster = peakOf (plain.wavFile);
    const float plainKick = peakOf (plain.wavFile.getSiblingFile (
        plain.wavFile.getFileNameWithoutExtension() + "-Kick.wav"));

    OfflineRenderer::Options normalised = plain;
    normalised.wavFile = tempWav();
    normalised.normalise = true;
    normalised.normaliseTargetDb = -6.0;
    const auto normalisedResult = OfflineRenderer::render (fx.engine, fx.model, normalised);
    ASSERT_TRUE (normalisedResult.ok) << normalisedResult.error;
    const float gain = juce::Decibels::decibelsToGain (-6.0f) / plainMaster;
    const float normalisedKick = peakOf (normalised.wavFile.getSiblingFile (
        normalised.wavFile.getFileNameWithoutExtension() + "-Kick.wav"));

    ASSERT_GT (plainKick, 0.0f);
    EXPECT_NEAR (normalisedKick, plainKick * gain, 0.002f);

    deleteAll (plainResult);
    deleteAll (normalisedResult);
}

// ---------------- sample rate ----------------

TEST (OfflineRenderer, RendersAtTheRequestedSampleRate)
{
    EngineFixture fx;
    OfflineRenderer::Options opts;
    opts.wavFile = tempWav();
    opts.tailSeconds = 0.1;
    opts.sampleRate = 48000;

    const auto result = OfflineRenderer::render (fx.engine, fx.model, opts);
    ASSERT_TRUE (result.ok) << result.error;

    EXPECT_DOUBLE_EQ (sampleRateOf (opts.wavFile), 48000.0);
    // Same duration, more samples.
    const double expectedSeconds = 4.0 * 60.0 / 140.0 + 0.1;
    EXPECT_NEAR ((double) lengthInSamples (opts.wavFile) / 48000.0, expectedSeconds, 0.02);
    opts.wavFile.deleteFile();
}

TEST (OfflineRenderer, WritesTheRequestedBitDepth)
{
    EngineFixture fx;
    for (int bits : { 16, 24, 32 })
    {
        OfflineRenderer::Options opts;
        opts.wavFile = tempWav();
        opts.tailSeconds = 0.0;
        opts.bitDepth = bits;

        const auto result = OfflineRenderer::render (fx.engine, fx.model, opts);
        ASSERT_TRUE (result.ok) << result.error;

        std::unique_ptr<juce::AudioFormatReader> reader (formatManager().createReaderFor (opts.wavFile));
        ASSERT_NE (reader, nullptr) << bits;
        EXPECT_EQ ((int) reader->bitsPerSample, bits);
        EXPECT_EQ (reader->usesFloatingPointData, bits == 32);
        reader = nullptr;
        opts.wavFile.deleteFile();
    }
}

// ---------------- per-channel stems ----------------

TEST (OfflineRenderer, PerChannelStemsWriteOneFilePerChannel)
{
    EngineFixture fx;
    OfflineRenderer::Options opts;
    opts.wavFile = tempWav();
    opts.stems = OfflineRenderer::Stems::perChannel;
    opts.tailSeconds = 0.1;

    const auto result = OfflineRenderer::render (fx.engine, fx.model, opts);
    ASSERT_TRUE (result.ok) << result.error;

    // Master plus the four default channels.
    EXPECT_EQ (result.writtenFiles.size(), 1 + fx.model.numChannels());
    for (const char* name : { "Kick", "Clap", "Hat", "Snare" })
    {
        const auto stem = opts.wavFile.getSiblingFile (
            opts.wavFile.getFileNameWithoutExtension() + "-" + name + ".wav");
        EXPECT_TRUE (stem.existsAsFile()) << name;
        EXPECT_EQ (lengthInSamples (stem), lengthInSamples (opts.wavFile)) << name;
    }

    deleteAll (result);
}

TEST (OfflineRenderer, PerChannelStemsHoldOnlyTheirOwnChannel)
{
    EngineFixture fx;
    OfflineRenderer::Options opts;
    opts.wavFile = tempWav();
    opts.stems = OfflineRenderer::Stems::perChannel;
    opts.tailSeconds = 0.1;

    const auto result = OfflineRenderer::render (fx.engine, fx.model, opts);
    ASSERT_TRUE (result.ok) << result.error;

    auto stemFor = [&opts] (const juce::String& name)
    {
        return readWav (opts.wavFile.getSiblingFile (
            opts.wavFile.getFileNameWithoutExtension() + "-" + name + ".wav"));
    };

    // The starter project plays the kick on every fourth step and the hat on
    // steps 2, 6, 10, 14; the clap and the snare have no notes at all.
    const auto kick = stemFor ("Kick");
    const auto hat = stemFor ("Hat");
    const int hatOnset = (int) (2 * ids::ticksPerStep / fx.ticksPerSample());

    EXPECT_GT (rmsOf (kick, 0, 2048), 0.01f) << "the kick is missing from its own stem";
    EXPECT_LT (rmsOf (hat, 0, 2048), 1.0e-5f) << "the kick leaked into the hat stem";
    EXPECT_GT (rmsOf (hat, hatOnset, 2048), 0.01f) << "the hat is missing from its own stem";
    EXPECT_LT (rmsOf (stemFor ("Clap"), 0, 8192), 1.0e-5f) << "a silent channel is not silent";
    EXPECT_LT (rmsOf (stemFor ("Snare"), 0, 8192), 1.0e-5f) << "a silent channel is not silent";

    deleteAll (result);
}

TEST (OfflineRenderer, PerChannelStemsHonourMute)
{
    EngineFixture fx;
    fx.model.getChannel (0).setProperty (ids::mute, true, nullptr);
    fx.sync.rebuildNow();

    OfflineRenderer::Options opts;
    opts.wavFile = tempWav();
    opts.stems = OfflineRenderer::Stems::perChannel;
    opts.tailSeconds = 0.1;

    const auto result = OfflineRenderer::render (fx.engine, fx.model, opts);
    ASSERT_TRUE (result.ok) << result.error;

    const auto kick = readWav (opts.wavFile.getSiblingFile (
        opts.wavFile.getFileNameWithoutExtension() + "-Kick.wav"));
    EXPECT_LT (kick.getMagnitude (0, kick.getNumSamples()), 1.0e-5f);

    deleteAll (result);
}

// Channels can share a name, and two stems must never write to one file.
TEST (OfflineRenderer, PerChannelStemsKeepDuplicateNamesApart)
{
    EngineFixture fx;
    fx.model.getChannel (1).setProperty (ids::name, "Kick", nullptr);
    fx.sync.rebuildNow();

    OfflineRenderer::Options opts;
    opts.wavFile = tempWav();
    opts.stems = OfflineRenderer::Stems::perChannel;
    opts.tailSeconds = 0.0;

    const auto result = OfflineRenderer::render (fx.engine, fx.model, opts);
    ASSERT_TRUE (result.ok) << result.error;

    juce::StringArray unique (result.writtenFiles);
    unique.removeDuplicates (true);
    EXPECT_EQ (unique.size(), result.writtenFiles.size());
    EXPECT_TRUE (opts.wavFile.getSiblingFile (
        opts.wavFile.getFileNameWithoutExtension() + "-Kick 2.wav").existsAsFile());

    deleteAll (result);
}

TEST (OfflineRenderer, ChannelStemCaptureIsOffAfterRendering)
{
    EngineFixture fx;
    OfflineRenderer::Options opts;
    opts.wavFile = tempWav();
    opts.stems = OfflineRenderer::Stems::perChannel;
    opts.tailSeconds = 0.0;

    const auto result = OfflineRenderer::render (fx.engine, fx.model, opts);
    ASSERT_TRUE (result.ok) << result.error;

    // A second, stem-free render must not keep filling the channel buses.
    OfflineRenderer::Options plain;
    plain.wavFile = tempWav();
    plain.tailSeconds = 0.0;
    const auto second = OfflineRenderer::render (fx.engine, fx.model, plain);
    ASSERT_TRUE (second.ok) << second.error;
    EXPECT_EQ (second.writtenFiles.size(), 1);

    deleteAll (result);
    deleteAll (second);
}

TEST (Analyze, ReportsLevelsAndSpectralBalancePerInsert)
{
    test::EngineFixture fx;

    // Kick to insert 1, hat to insert 2: analysis must see bass on one bus
    // and treble on the other.
    fx.model.getChannel (0).setProperty (ids::insertIndex, 1, nullptr);   // Kick
    fx.model.getChannel (2).setProperty (ids::insertIndex, 2, nullptr);   // Hat
    fx.sync.rebuildNow();

    OfflineRenderer::AnalysisOptions opts;
    opts.tailSeconds = 0.2;
    const auto analysis = OfflineRenderer::analyze (fx.engine, fx.model, opts);
    ASSERT_TRUE (analysis.ok) << analysis.error;

    EXPECT_GT (analysis.durationSeconds, 1.0);
    EXPECT_GT (analysis.master.rmsDb, -40.0f) << "the stock beat is not silent";
    EXPECT_LT (analysis.master.peakDb, 0.5f);
    EXPECT_GE (analysis.master.peakDb, analysis.master.rmsDb);

    const auto find = [&analysis] (int index) -> const OfflineRenderer::TargetStats*
    {
        for (const auto& s : analysis.inserts)
            if (s.insertIndex == index)
                return &s;
        return nullptr;
    };
    const auto* kick = find (1);
    const auto* hat = find (2);
    ASSERT_NE (kick, nullptr);
    ASSERT_NE (hat, nullptr);
    EXPECT_EQ (kick->name, "Insert 1");

    // The kick bus carries far more of its energy below 250 Hz than the hat
    // bus; the hat is the other way round above 1 kHz.
    const auto lowShare  = [] (const OfflineRenderer::TargetStats* s)
    { return juce::jmax (s->bands.subDb, s->bands.lowDb); };
    const auto highShare = [] (const OfflineRenderer::TargetStats* s)
    { return juce::jmax (s->bands.highMidDb, s->bands.highDb); };

    EXPECT_GT (lowShare (kick), highShare (kick)) << "kick bus should be bass-heavy";
    EXPECT_GT (highShare (hat), lowShare (hat)) << "hat bus should be treble-heavy";
    EXPECT_GT (kick->rmsDb, -60.0f);
    EXPECT_GT (hat->rmsDb, -60.0f);
}

TEST (Analyze, EmptyProjectFailsCleanly)
{
    test::EngineFixture fx;
    fx.model.setSongMode (true);   // song mode with an empty playlist
    for (auto track : fx.model.playlist())
        track.removeAllChildren (nullptr);
    fx.sync.rebuildNow();

    const auto analysis = OfflineRenderer::analyze (fx.engine, fx.model, {});
    EXPECT_FALSE (analysis.ok);
    EXPECT_TRUE (analysis.error.isNotEmpty());
}
