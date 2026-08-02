#include "TestHelpers.h"
#include "engine/WavWriter.h"
#include "engine/StretchFollower.h"

using namespace test;

namespace
{
// Percussive material: a decaying-click train exposes transient handling,
// where a pure sine would not.
juce::File makeClickTrainFile (double seconds, double sampleRate = 44100.0)
{
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getNonexistentChildFile ("eurytest-clicks", ".wav");
    auto writer = wavwriter::forFile (file, sampleRate, 1, 16);
    if (writer == nullptr)
        return {};

    const int n = (int) (seconds * sampleRate);
    const int clickSpacing = (int) (sampleRate / 8.0);
    juce::AudioBuffer<float> clicks (1, n);
    clicks.clear();
    for (int start = 0; start < n; start += clickSpacing)
        for (int i = 0; i < 200 && start + i < n; ++i)
            clicks.setSample (0, start + i, 0.9f * std::exp (-i / 30.0f)
                                                 * std::sin (0.7f * (float) i));
    writer->writeFromAudioSampleBuffer (clicks, 0, n);
    return file;
}

float maxAbsDifference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    const int n = juce::jmin (a.getNumSamples(), b.getNumSamples());
    float maxDiff = 0.0f;
    for (int i = 0; i < n; ++i)
        maxDiff = juce::jmax (maxDiff, std::abs (a.getSample (0, i) - b.getSample (0, i)));
    return maxDiff;
}
} // namespace

TEST (StretchMode, ModesProduceDifferentOutput)
{
    EngineFixture fx;
    const auto clicks = makeClickTrainFile (1.0);
    ASSERT_TRUE (clicks.existsAsFile());
    const auto path = clicks.getFullPathName();

    auto smooth     = fx.audioClips.getStretched (path, 1.5, StretchMode::smooth);
    auto percussive = fx.audioClips.getStretched (path, 1.5, StretchMode::percussive);
    auto formant    = fx.audioClips.getStretched (path, 1.5, StretchMode::formant);
    ASSERT_NE (smooth, nullptr);
    ASSERT_NE (percussive, nullptr);
    ASSERT_NE (formant, nullptr);

    // Distinct cache entries per mode; repeats hit the cache.
    EXPECT_NE (smooth.get(), percussive.get());
    EXPECT_NE (smooth.get(), formant.get());
    EXPECT_EQ (fx.audioClips.getStretched (path, 1.5, StretchMode::percussive).get(),
               percussive.get());

    // The short-window percussive engine renders audibly different samples.
    EXPECT_GT (maxAbsDifference (*smooth, *percussive), 1.0e-3f);

    clicks.deleteFile();
}

TEST (StretchMode, StretchedLengthCorrectForEachMode)
{
    EngineFixture fx;
    const auto tone = makeToneFile (1.0);
    ASSERT_TRUE (tone.existsAsFile());

    for (const auto mode : { StretchMode::smooth, StretchMode::percussive, StretchMode::formant })
    {
        auto stretched = fx.audioClips.getStretched (tone.getFullPathName(), 2.0, mode);
        ASSERT_NE (stretched, nullptr);
        EXPECT_NEAR (stretched->getNumSamples(), 88200, 3000);   // RB may pad slightly
    }
    tone.deleteFile();
}

TEST (StretchMode, ModeAndFollowTempoRoundTripThroughSaveLoad)
{
    ProjectModel model;
    auto clip = model.addPlaylistClip ("audio", 0, 0, 4480);
    clip.setProperty (ids::audioPath, "/tmp/some.wav", nullptr);
    clip.setProperty (ids::stretchRatio, 2.0, nullptr);
    clip.setProperty (ids::stretchMode, (int) StretchMode::formant, nullptr);
    clip.setProperty (ids::followTempo, true, nullptr);

    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getNonexistentChildFile ("eurytest-stretch", ".eury");
    ASSERT_TRUE (model.saveToFile (file));

    ProjectModel loaded;
    ASSERT_TRUE (loaded.loadFromFile (file));
    juce::ValueTree found;
    for (const auto child : loaded.playlist().getChild (0))
        if (child.hasType (ids::CLIP) && child[ids::clipType].toString() == "audio")
            found = child;
    ASSERT_TRUE (found.isValid());
    EXPECT_EQ ((int) found[ids::stretchMode], (int) StretchMode::formant);
    EXPECT_TRUE ((bool) found[ids::followTempo]);
    EXPECT_NEAR ((double) found[ids::stretchRatio], 2.0, 1.0e-9);

    file.deleteFile();
}

TEST (StretchMode, ModeChangeSwapsSnapshotBuffer)
{
    EngineFixture fx;
    const auto tone = makeToneFile (1.0);
    ASSERT_TRUE (tone.existsAsFile());

    auto clip = fx.model.addPlaylistClip ("audio", 0, 0, 4480);
    clip.setProperty (ids::audioPath, tone.getFullPathName(), nullptr);
    clip.setProperty (ids::stretchRatio, 2.0, nullptr);
    fx.sync.rebuildNow();
    auto snapA = fx.engine.getPendingSnapshot();
    ASSERT_EQ ((int) snapA->clips.size(), 1);

    clip.setProperty (ids::stretchMode, (int) StretchMode::percussive, nullptr);
    fx.sync.rebuildNow();
    auto snapB = fx.engine.getPendingSnapshot();
    ASSERT_EQ ((int) snapB->clips.size(), 1);

    EXPECT_NE (snapA->clips[0].audio.get(), snapB->clips[0].audio.get());
    tone.deleteFile();
}

TEST (StretchFollower, TempoChangeRestretchesFollowClipsOnly)
{
    EngineFixture fx;
    StretchFollower follower { fx.model, fx.audioClips };
    const auto tone = makeToneFile (1.0);
    ASSERT_TRUE (tone.existsAsFile());

    // 1 s of audio at 140 bpm = 2240 ticks; both clips stretched x2 to 4480.
    ASSERT_NEAR (fx.model.getTempo(), 140.0, 1.0e-9);
    auto follows = fx.model.addPlaylistClip ("audio", 0, 0, 4480);
    follows.setProperty (ids::audioPath, tone.getFullPathName(), nullptr);
    follows.setProperty (ids::stretchRatio, 2.0, nullptr);
    follows.setProperty (ids::followTempo, true, nullptr);

    auto fixed = fx.model.addPlaylistClip ("audio", 1, 0, 4480);
    fixed.setProperty (ids::audioPath, tone.getFullPathName(), nullptr);
    fixed.setProperty (ids::stretchRatio, 2.0, nullptr);

    fx.sync.rebuildNow();
    ASSERT_EQ ((int) fx.engine.getPendingSnapshot()->clips.size(), 2);
    EXPECT_NEAR (fx.engine.getPendingSnapshot()->clips[0].audio->getNumSamples(), 88200, 3000);

    // Halving the tempo doubles the seconds a tick lasts: the follower must
    // re-stretch to ratio 4 while the tick length stays 4480.
    fx.model.setTempo (70.0);
    follower.recomputeNow();

    EXPECT_NEAR ((double) follows[ids::stretchRatio], 4.0, 1.0e-6);
    EXPECT_EQ ((int) follows[ids::lengthTicks], 4480);
    EXPECT_NEAR ((double) fixed[ids::stretchRatio], 2.0, 1.0e-9);

    fx.sync.rebuildNow();
    auto snap = fx.engine.getPendingSnapshot();
    ASSERT_EQ ((int) snap->clips.size(), 2);
    for (const auto& cs : snap->clips)
    {
        ASSERT_NE (cs.audio, nullptr);
        // Track 0 follows (4 s of audio), track 1 keeps its old 2 s render.
        if (cs.audio->getNumSamples() > 120000)
            EXPECT_NEAR (cs.audio->getNumSamples(), 176400, 6000);
        else
            EXPECT_NEAR (cs.audio->getNumSamples(), 88200, 3000);
    }

    tone.deleteFile();
}
