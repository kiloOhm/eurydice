#include <gtest/gtest.h>
#include "app/TakeSplitter.h"
#include "model/ProjectModel.h"

// tps = 0.05 ticks/sample keeps the arithmetic exact: a 4-bar loop of 3840
// ticks is exactly 76800 samples.
namespace
{
constexpr double kTps = 0.05;
constexpr int kLoopEnd = ids::ticksPerBar;          // 3840
constexpr juce::int64 kLoopSamples = 76800;
constexpr juce::int64 kMinSamples = 2205;           // 0.05 s at 44.1 kHz
}

TEST (TakeSplitter, NoLoopIsOneTake)
{
    const auto takes = takes::splitIntoTakes (200000, 0.0, 0, kLoopEnd, false, kTps, kMinSamples);
    ASSERT_EQ (takes.size(), 1u);
    EXPECT_EQ (takes[0].startSample, 0);
    EXPECT_EQ (takes[0].numSamples, 200000);
    EXPECT_EQ (takes[0].startTicks, 0);
    EXPECT_EQ (takes[0].lengthTicks, 10000);
}

TEST (TakeSplitter, SinglePassRecordingUnchanged)
{
    // Stopped before the first wrap: one take, natural length, kept even
    // though shorter than the loop.
    const auto takes = takes::splitIntoTakes (40000, 0.0, 0, kLoopEnd, true, kTps, kMinSamples);
    ASSERT_EQ (takes.size(), 1u);
    EXPECT_EQ (takes[0].numSamples, 40000);
    EXPECT_EQ (takes[0].lengthTicks, 2000);
}

TEST (TakeSplitter, SplitsAtEveryLoopWrap)
{
    const auto takes = takes::splitIntoTakes (3 * kLoopSamples, 0.0, 0, kLoopEnd, true,
                                              kTps, kMinSamples);
    ASSERT_EQ (takes.size(), 3u);
    for (int i = 0; i < 3; ++i)
    {
        EXPECT_EQ (takes[(size_t) i].startSample, i * kLoopSamples);
        EXPECT_EQ (takes[(size_t) i].numSamples, kLoopSamples);
        EXPECT_EQ (takes[(size_t) i].startTicks, 0);
        EXPECT_EQ (takes[(size_t) i].lengthTicks, kLoopEnd);
    }
}

TEST (TakeSplitter, LoopStartOffsetPlacesLaterPassesAtLoopStart)
{
    // Recording begins one bar before a one-bar loop [3840, 7680).
    const auto takes = takes::splitIntoTakes (3 * kLoopSamples, 0.0,
                                              kLoopEnd, 2 * kLoopEnd, true, kTps, kMinSamples);
    ASSERT_EQ (takes.size(), 2u);
    EXPECT_EQ (takes[0].startTicks, 0);
    EXPECT_EQ (takes[0].lengthTicks, 2 * kLoopEnd);
    EXPECT_EQ (takes[0].numSamples, 2 * kLoopSamples);
    EXPECT_EQ (takes[1].startTicks, kLoopEnd);
    EXPECT_EQ (takes[1].lengthTicks, kLoopEnd);
    EXPECT_EQ (takes[1].numSamples, kLoopSamples);
}

TEST (TakeSplitter, PartialLastPassKeptAsShorterTake)
{
    const auto takes = takes::splitIntoTakes (2 * kLoopSamples + kLoopSamples / 2, 0.0,
                                              0, kLoopEnd, true, kTps, kMinSamples);
    ASSERT_EQ (takes.size(), 3u);
    EXPECT_EQ (takes[2].startSample, 2 * kLoopSamples);
    EXPECT_EQ (takes[2].numSamples, kLoopSamples / 2);
    EXPECT_EQ (takes[2].lengthTicks, kLoopEnd / 2);
}

TEST (TakeSplitter, TinyPartialLastPassDropped)
{
    const auto takes = takes::splitIntoTakes (2 * kLoopSamples + kMinSamples - 1, 0.0,
                                              0, kLoopEnd, true, kTps, kMinSamples);
    ASSERT_EQ (takes.size(), 2u);
    EXPECT_EQ (takes[1].numSamples, kLoopSamples);
}

TEST (TakeSplitter, StartBeyondLoopEndNeverWraps)
{
    // The engine only wraps while the position is inside the loop.
    const auto takes = takes::splitIntoTakes (200000, (double) kLoopEnd + 100.0,
                                              0, kLoopEnd, true, kTps, kMinSamples);
    ASSERT_EQ (takes.size(), 1u);
    EXPECT_EQ (takes[0].numSamples, 200000);
    EXPECT_EQ (takes[0].startTicks, kLoopEnd + 100);
}

TEST (TakeSplitter, DegenerateLoopRangeIsOneTake)
{
    const auto takes = takes::splitIntoTakes (100000, 0.0, kLoopEnd, kLoopEnd, true,
                                              kTps, kMinSamples);
    EXPECT_EQ (takes.size(), 1u);
}

TEST (TakeSplitter, NonIntegerPassLengthRoundsLikeTheEngine)
{
    // The wrap lands on the first sample at or past the loop end.
    const double tps = 0.049;
    const auto passLen = (juce::int64) std::ceil (kLoopEnd / tps);
    const auto takes = takes::splitIntoTakes (2 * passLen, 0.0, 0, kLoopEnd, true,
                                              tps, kMinSamples);
    ASSERT_EQ (takes.size(), 2u);
    EXPECT_EQ (takes[0].numSamples, passLen);
    EXPECT_EQ (takes[1].startSample, passLen);
    EXPECT_EQ (takes[1].numSamples, passLen);
}

TEST (TakeSplitter, EmptyRecordingYieldsNoTakes)
{
    EXPECT_TRUE (takes::splitIntoTakes (0, 0.0, 0, kLoopEnd, true, kTps, kMinSamples).empty());
}

// ---------------- placement ----------------

namespace
{
std::vector<takes::Take> threeTakes()
{
    std::vector<takes::Take> t (3);
    for (int i = 0; i < 3; ++i)
        t[(size_t) i] = { i * kLoopSamples, kLoopSamples, 0, kLoopEnd };
    return t;
}
}

TEST (TakePlacement, LatestTakeUnmutedOnFirstFreeTrack)
{
    ProjectModel model;
    const auto plan = takes::planPlacement (model.playlist(), threeTakes());
    ASSERT_EQ (plan.size(), 3u);

    EXPECT_EQ (plan[0].takeIndex, 2);   // latest first
    EXPECT_EQ (plan[0].trackIndex, 0);
    EXPECT_FALSE (plan[0].muted);

    EXPECT_EQ (plan[1].takeIndex, 1);
    EXPECT_EQ (plan[1].trackIndex, 1);
    EXPECT_TRUE (plan[1].muted);

    EXPECT_EQ (plan[2].takeIndex, 0);
    EXPECT_EQ (plan[2].trackIndex, 2);
    EXPECT_TRUE (plan[2].muted);
}

TEST (TakePlacement, SkipsOccupiedTracks)
{
    ProjectModel model;
    model.addPlaylistClip ("pattern", 0, 0, ids::ticksPerBar);      // overlaps
    model.addPlaylistClip ("pattern", 2, 8 * ids::ticksPerBar, ids::ticksPerBar);  // elsewhere

    const auto plan = takes::planPlacement (model.playlist(), threeTakes());
    ASSERT_EQ (plan.size(), 3u);
    EXPECT_EQ (plan[0].trackIndex, 1);
    EXPECT_EQ (plan[1].trackIndex, 2);   // clip there does not overlap the loop range
    EXPECT_EQ (plan[2].trackIndex, 3);
}

TEST (TakePlacement, SingleTakeFallsBackToTrackZeroWhenNothingIsFree)
{
    ProjectModel model;
    for (int i = 0; i < model.numPlaylistTracks(); ++i)
        model.addPlaylistClip ("pattern", i, 0, ids::ticksPerBar);

    std::vector<takes::Take> one { { 0, kLoopSamples, 0, kLoopEnd } };
    const auto plan = takes::planPlacement (model.playlist(), one);
    ASSERT_EQ (plan.size(), 1u);
    EXPECT_EQ (plan[0].trackIndex, 0);
    EXPECT_FALSE (plan[0].muted);
}

TEST (TakePlacement, EarlierTakesUnplacedWhenTracksRunOut)
{
    juce::ValueTree playlist (ids::PLAYLIST);
    for (int i = 0; i < 2; ++i)
        playlist.appendChild (juce::ValueTree (ids::TRACK), nullptr);

    const auto plan = takes::planPlacement (playlist, threeTakes());
    ASSERT_EQ (plan.size(), 2u);
    EXPECT_EQ (plan[0].takeIndex, 2);
    EXPECT_EQ (plan[1].takeIndex, 1);
}

// ---------------- file splitting ----------------

TEST (TakeFiles, SplitFilesCarryTheRightSampleRegions)
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("eurydice-take-tests");
    dir.createDirectory();
    const auto source = dir.getNonexistentChildFile ("take", ".wav");

    // A stereo ramp where sample i has value i / 2^23, exactly representable
    // in 24-bit, so regions are identifiable after the round trip.
    constexpr int total = 3000;
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (source.createOutputStream().release(), 44100.0, 2, 24, {}, 0));
        ASSERT_NE (writer, nullptr);
        juce::AudioBuffer<float> buffer (2, total);
        for (int i = 0; i < total; ++i)
            for (int ch = 0; ch < 2; ++ch)
                buffer.setSample (ch, i, (float) i / 8388608.0f);
        ASSERT_TRUE (writer->writeFromAudioSampleBuffer (buffer, 0, total));
    }

    std::vector<takes::Take> regions { { 0, 1000, 0, 960 },
                                       { 1000, 1000, 0, 960 },
                                       { 2000, 1000, 0, 960 } };
    const auto files = takes::writeTakeFiles (source, regions);
    ASSERT_EQ (files.size(), 3);

    juce::WavAudioFormat wav;
    for (int i = 0; i < 3; ++i)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (
            wav.createReaderFor (files[i].createInputStream().release(), true));
        ASSERT_NE (reader, nullptr);
        EXPECT_EQ (reader->lengthInSamples, 1000);
        EXPECT_EQ ((int) reader->numChannels, 2);
        EXPECT_DOUBLE_EQ (reader->sampleRate, 44100.0);

        juce::AudioBuffer<float> read (2, 1000);
        ASSERT_TRUE (reader->read (&read, 0, 1000, 0, true, true));
        EXPECT_NEAR (read.getSample (0, 0), (float) (i * 1000) / 8388608.0f, 1.0e-6f);
        EXPECT_NEAR (read.getSample (1, 999), (float) (i * 1000 + 999) / 8388608.0f, 1.0e-6f);
    }

    dir.deleteRecursively();
}

TEST (TakeFiles, MissingSourceReturnsNoFiles)
{
    std::vector<takes::Take> regions { { 0, 100, 0, 960 } };
    EXPECT_TRUE (takes::writeTakeFiles (juce::File ("/nonexistent/take.wav"), regions).isEmpty());
}
