#include <gtest/gtest.h>
#include "ui/common/SampleDrop.h"
#include "TestHelpers.h"

// Geometry matches the rack rows: 30 px rows with a 2 px gap.
namespace
{
constexpr int kRowHeight = 30;
constexpr int kRowGap = 2;
constexpr int kPitch = kRowHeight + kRowGap;
}

TEST (SampleDrop, RackRowMiddleTargetsThatRow)
{
    const auto t = sampledrop::rackTargetForY (kPitch + kRowHeight / 2, 4, kRowHeight, kRowGap);
    EXPECT_EQ (t.replaceRow, 1);
    EXPECT_EQ (t.insertIndex, 2);
}

TEST (SampleDrop, RackRowEdgesInsertBetweenRows)
{
    const auto above = sampledrop::rackTargetForY (kPitch + 2, 4, kRowHeight, kRowGap);
    EXPECT_EQ (above.replaceRow, -1);
    EXPECT_EQ (above.insertIndex, 1) << "top quarter inserts before the row";

    const auto below = sampledrop::rackTargetForY (kPitch + kRowHeight - 2, 4, kRowHeight, kRowGap);
    EXPECT_EQ (below.replaceRow, -1);
    EXPECT_EQ (below.insertIndex, 2) << "bottom quarter inserts after the row";

    const auto gap = sampledrop::rackTargetForY (kPitch + kRowHeight + 1, 4, kRowHeight, kRowGap);
    EXPECT_EQ (gap.replaceRow, -1);
    EXPECT_EQ (gap.insertIndex, 2) << "the gap belongs to the row above";
}

TEST (SampleDrop, RackBelowAllRowsAppends)
{
    const auto t = sampledrop::rackTargetForY (4 * kPitch + 50, 4, kRowHeight, kRowGap);
    EXPECT_EQ (t.replaceRow, -1);
    EXPECT_EQ (t.insertIndex, 4);
}

TEST (SampleDrop, RackEmptyAndNegativeYAreSafe)
{
    const auto empty = sampledrop::rackTargetForY (10, 0, kRowHeight, kRowGap);
    EXPECT_EQ (empty.replaceRow, -1);
    EXPECT_EQ (empty.insertIndex, 0);

    const auto negative = sampledrop::rackTargetForY (-15, 4, kRowHeight, kRowGap);
    EXPECT_EQ (negative.replaceRow, -1);
    EXPECT_EQ (negative.insertIndex, 0);
}

TEST (SampleDrop, PlaylistTargetSnapsDownAndClampsTrack)
{
    const auto t = sampledrop::playlistTargetFor (5000.0, 3, 24, ids::ticksPerBar);
    EXPECT_EQ (t.track, 3);
    EXPECT_EQ (t.startTicks, ids::ticksPerBar);

    EXPECT_EQ (sampledrop::playlistTargetFor (-100.0, -2, 24, ids::ticksPerBar).track, 0);
    EXPECT_EQ (sampledrop::playlistTargetFor (-100.0, -2, 24, ids::ticksPerBar).startTicks, 0);
    EXPECT_EQ (sampledrop::playlistTargetFor (0.0, 99, 24, ids::ticksPerBar).track, 23);
}

TEST (SampleDrop, AudioClipLengthFollowsTempo)
{
    // Two seconds at 120 BPM = 4 quarters = one bar.
    EXPECT_EQ (sampledrop::audioClipLengthTicks (2.0, 120.0), ids::ticksPerBar);
    EXPECT_EQ (sampledrop::audioClipLengthTicks (1.0, 140.0), 2240);
    EXPECT_EQ (sampledrop::audioClipLengthTicks (0.001, 120.0), ids::ticksPerStep)
        << "never shorter than one step";
}

TEST (SampleDrop, AudioFileFilter)
{
    const auto audio = sampledrop::audioFilesIn ({ "/a/kick.wav", "/a/readme.txt",
                                                   "/a/loop.FLAC", "/a/patch.fxp" });
    ASSERT_EQ (audio.size(), 2);
    EXPECT_EQ (audio[0], "/a/kick.wav");
    EXPECT_EQ (audio[1], "/a/loop.FLAC");
}

TEST (SampleDrop, DropOntoSamplerRowReplacesItsSample)
{
    ProjectModel model;   // default project: 4 sampler channels
    const juce::File file ("/tmp/eurytest-claps/clap 03.wav");

    auto channel = sampledrop::dropOntoRack (model, file, { 0, 1 });
    EXPECT_EQ (model.numChannels(), 4) << "replacement must not add a channel";
    EXPECT_EQ (channel, model.getChannel (0));
    EXPECT_EQ (channel[ids::samplePath].toString(), file.getFullPathName());

    model.getUndoManager().undo();
    EXPECT_FALSE (model.getChannel (0).hasProperty (ids::samplePath));
}

TEST (SampleDrop, DropBetweenRowsInsertsSamplerChannelThere)
{
    ProjectModel model;
    const juce::File file ("/tmp/eurytest-claps/clap 03.wav");

    auto channel = sampledrop::dropOntoRack (model, file, { -1, 2 });
    ASSERT_EQ (model.numChannels(), 5);
    EXPECT_EQ (model.getChannel (2), channel);
    EXPECT_EQ (channel[ids::type].toString(), "sampler");
    EXPECT_EQ (channel[ids::name].toString(), "clap 03");
    EXPECT_EQ (channel[ids::samplePath].toString(), file.getFullPathName());

    model.getUndoManager().undo();
    EXPECT_EQ (model.numChannels(), 4);
}

TEST (SampleDrop, DropOntoNonSamplerRowInsertsInstead)
{
    ProjectModel model;
    auto synth = model.addChannel ("synth", "Lead");
    const int synthRow = model.numChannels() - 1;

    sampledrop::dropOntoRack (model, juce::File ("/tmp/x.wav"),
                              { synthRow, synthRow + 1 });
    EXPECT_EQ (model.numChannels(), 6);
    EXPECT_FALSE (synth.hasProperty (ids::samplePath));
    EXPECT_EQ (model.getChannel (synthRow + 1)[ids::type].toString(), "sampler");
}

TEST (SampleDrop, DropOntoPlaylistCreatesAudioClip)
{
    ProjectModel model;
    AudioClipCache clips;
    clips.setEngineSampleRate (test::kSampleRate);

    const auto file = test::makeToneFile (1.0);
    ASSERT_TRUE (file.existsAsFile());

    auto clip = sampledrop::dropOntoPlaylist (model, clips, file,
                                              { 5, 2 * ids::ticksPerBar });
    ASSERT_TRUE (clip.isValid());
    EXPECT_EQ (clip.getParent(), model.playlist().getChild (5));
    EXPECT_EQ (clip[ids::clipType].toString(), "audio");
    EXPECT_EQ (clip[ids::audioPath].toString(), file.getFullPathName());
    EXPECT_EQ ((int) clip[ids::startTicks], 2 * ids::ticksPerBar);
    EXPECT_DOUBLE_EQ ((double) clip[ids::stretchRatio], 1.0);
    EXPECT_EQ ((int) clip[ids::audioOffsetTicks], 0);
    EXPECT_FALSE ((bool) clip[ids::muted]);

    // Length reflects the file's duration at the project tempo.
    const double seconds = clips.getNaturalSeconds (file.getFullPathName());
    ASSERT_GT (seconds, 0.0);
    EXPECT_EQ ((int) clip[ids::lengthTicks],
               sampledrop::audioClipLengthTicks (seconds, model.getTempo()));
    EXPECT_EQ ((int) clip[ids::lengthTicks], 2240) << "1 s at 140 BPM";

    model.getUndoManager().undo();
    EXPECT_EQ (model.playlist().getChild (5).getNumChildren(), 0);
    file.deleteFile();
}

TEST (SampleDrop, UnreadableFileMakesNoClip)
{
    ProjectModel model;
    AudioClipCache clips;
    auto clip = sampledrop::dropOntoPlaylist (model, clips,
                                              juce::File ("/tmp/eurytest-missing.wav"),
                                              { 0, 0 });
    EXPECT_FALSE (clip.isValid());
    EXPECT_EQ (model.playlist().getChild (0).getNumChildren(), 0);
}
