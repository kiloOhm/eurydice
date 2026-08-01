#include "TestHelpers.h"
#include "engine/OfflineRenderer.h"

using test::EngineFixture;

namespace
{
juce::File tempWav()
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getNonexistentChildFile ("eurytest-render", ".wav");
}

juce::int64 lengthInSamples (const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    return reader != nullptr ? reader->lengthInSamples : -1;
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
    opts.renderStems = true;
    opts.tailSeconds = 0.1;

    const auto result = OfflineRenderer::render (fx.engine, fx.model, opts);
    ASSERT_TRUE (result.ok) << result.error;

    const auto stem = opts.wavFile.getSiblingFile (
        opts.wavFile.getFileNameWithoutExtension() + "-Insert 2.wav");
    EXPECT_TRUE (result.writtenFiles.contains (stem.getFullPathName()));
    EXPECT_TRUE (stem.existsAsFile());
    EXPECT_GT (lengthInSamples (stem), 0);

    for (const auto& f : result.writtenFiles)
        juce::File (f).deleteFile();
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
