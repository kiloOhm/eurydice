#include <gtest/gtest.h>
#include "ui/pianoroll/NoteTools.h"

using notetools::Note;
using notetools::Ramp;

namespace
{
constexpr int k8th  = 480;
constexpr int k16th = 240;
constexpr int k32nd = 120;
constexpr int k8thTriplet = 320;

Note makeNote (int key, int start, int length, double velocity = 0.8)
{
    Note note;
    note.key = key;
    note.startTicks = start;
    note.lengthTicks = length;
    note.velocity = velocity;
    return note;
}
}

// ---------- roll ----------

TEST (NoteTools, RollSubdivisionCount)
{
    EXPECT_EQ (notetools::rollCount (ids::ticksPerBar, k16th), 16);
    EXPECT_EQ (notetools::rollCount (ids::ticksPerQuarter, k8th), 2);
    EXPECT_EQ (notetools::rollCount (ids::ticksPerQuarter, k32nd), 8);
    EXPECT_EQ (notetools::rollCount (ids::ticksPerQuarter, k8thTriplet), 3) << "triplets";
    EXPECT_EQ (notetools::rollCount (k16th, k8th), 1) << "shorter than the division";
    EXPECT_EQ (notetools::rollCount (0, k16th), 1);
    EXPECT_EQ (notetools::rollCount (ids::ticksPerQuarter, 0), 1) << "no division set";
}

TEST (NoteTools, RollFillsOriginalSpanEvenly)
{
    const auto pieces = notetools::roll (makeNote (64, 1920, ids::ticksPerQuarter), k16th, Ramp::flat);
    ASSERT_EQ (pieces.size(), 4u);
    for (int i = 0; i < 4; ++i)
    {
        EXPECT_EQ (pieces[(size_t) i].startTicks, 1920 + i * k16th);
        EXPECT_EQ (pieces[(size_t) i].lengthTicks, k16th);
        EXPECT_EQ (pieces[(size_t) i].key, 64);
    }
}

TEST (NoteTools, RollWithRemainderStillEndsWithTheSource)
{
    // 1000 ticks at a 240 division: 4 pieces, the odd ticks spread across them.
    const auto pieces = notetools::roll (makeNote (60, 0, 1000), k16th, Ramp::flat);
    ASSERT_EQ (pieces.size(), 4u);
    EXPECT_EQ (pieces.front().startTicks, 0);
    EXPECT_EQ (pieces.back().startTicks + pieces.back().lengthTicks, 1000);

    for (size_t i = 1; i < pieces.size(); ++i)
        EXPECT_EQ (pieces[i - 1].startTicks + pieces[i - 1].lengthTicks, pieces[i].startTicks)
            << "no gaps between pieces";
}

TEST (NoteTools, RollTripletDivision)
{
    const auto pieces = notetools::roll (makeNote (60, 0, ids::ticksPerQuarter), k8thTriplet, Ramp::flat);
    ASSERT_EQ (pieces.size(), 3u);
    EXPECT_EQ (pieces[0].startTicks, 0);
    EXPECT_EQ (pieces[1].startTicks, 320);
    EXPECT_EQ (pieces[2].startTicks, 640);
    EXPECT_EQ (pieces[2].lengthTicks, 320);
}

TEST (NoteTools, RollKeepsVelocityFlat)
{
    const auto pieces = notetools::roll (makeNote (60, 0, ids::ticksPerQuarter, 0.6), k16th, Ramp::flat);
    ASSERT_EQ (pieces.size(), 4u);
    for (const auto& piece : pieces)
        EXPECT_DOUBLE_EQ (piece.velocity, 0.6);
}

TEST (NoteTools, RollRisingRampEndsAtSourceVelocity)
{
    const auto pieces = notetools::roll (makeNote (60, 0, ids::ticksPerQuarter, 0.8), k16th, Ramp::rising, 0.5);
    ASSERT_EQ (pieces.size(), 4u);
    EXPECT_DOUBLE_EQ (pieces[0].velocity, 0.4);
    EXPECT_NEAR (pieces[1].velocity, 0.8 * (0.5 + 0.5 / 3.0), 1.0e-9);
    EXPECT_NEAR (pieces[2].velocity, 0.8 * (0.5 + 1.0 / 3.0), 1.0e-9);
    EXPECT_DOUBLE_EQ (pieces[3].velocity, 0.8);
}

TEST (NoteTools, RollFallingRampStartsAtSourceVelocity)
{
    const auto pieces = notetools::roll (makeNote (60, 0, ids::ticksPerQuarter, 0.8), k16th, Ramp::falling, 0.5);
    ASSERT_EQ (pieces.size(), 4u);
    EXPECT_DOUBLE_EQ (pieces[0].velocity, 0.8);
    EXPECT_DOUBLE_EQ (pieces[3].velocity, 0.4);
    EXPECT_LT (pieces[1].velocity, pieces[0].velocity);
    EXPECT_LT (pieces[2].velocity, pieces[1].velocity);
}

TEST (NoteTools, RollRampDepthIsClampedAndVelocityStaysAudible)
{
    EXPECT_DOUBLE_EQ (notetools::rampVelocity (0.8, 0, 4, Ramp::rising, 2.0), notetools::minVelocity)
        << "depth clamps to 1, so the first piece would be silent";
    EXPECT_DOUBLE_EQ (notetools::rampVelocity (0.8, 0, 4, Ramp::rising, -1.0), 0.8)
        << "negative depth clamps to 0";
    EXPECT_DOUBLE_EQ (notetools::rampVelocity (0.8, 0, 1, Ramp::rising, 0.5), 0.8)
        << "a single piece has nothing to ramp across";
    EXPECT_DOUBLE_EQ (notetools::rampVelocity (2.0, 3, 4, Ramp::rising, 0.5), 1.0);
}

TEST (NoteTools, RollLeavesShortNotesAlone)
{
    const auto source = makeNote (60, 480, k16th, 0.5);
    const auto pieces = notetools::roll (source, k8th, Ramp::rising);
    ASSERT_EQ (pieces.size(), 1u);
    EXPECT_EQ (pieces[0].startTicks, 480);
    EXPECT_EQ (pieces[0].lengthTicks, k16th);
    EXPECT_DOUBLE_EQ (pieces[0].velocity, 0.5) << "no ramp on an unrolled note";
}

TEST (NoteTools, RollOfZeroLengthNoteIsANoOp)
{
    const auto pieces = notetools::roll (makeNote (60, 240, 0), k16th, Ramp::rising);
    ASSERT_EQ (pieces.size(), 1u);
    EXPECT_EQ (pieces[0].lengthTicks, 0);
}

TEST (NoteTools, RollPreservesKeyAndPan)
{
    Note source = makeNote (72, 0, ids::ticksPerQuarter);
    source.pan = -0.4;
    for (const auto& piece : notetools::roll (source, k16th, Ramp::rising))
    {
        EXPECT_EQ (piece.key, 72);
        EXPECT_DOUBLE_EQ (piece.pan, -0.4);
    }
}

TEST (NoteTools, RollAllHandlesEmptyAndMultipleSelections)
{
    EXPECT_TRUE (notetools::rollAll ({}, k16th, Ramp::flat).empty());

    const std::vector<Note> selection { makeNote (60, 0, ids::ticksPerQuarter),
                                        makeNote (67, 960, ids::ticksPerQuarter) };
    const auto pieces = notetools::rollAll (selection, k16th, Ramp::flat);
    EXPECT_EQ (pieces.size(), 8u);
}

// ---------- chop ----------

TEST (NoteTools, ChopSplitsAtSnapBoundaries)
{
    const auto pieces = notetools::chop (makeNote (60, 0, ids::ticksPerQuarter), k16th);
    ASSERT_EQ (pieces.size(), 4u);
    for (int i = 0; i < 4; ++i)
    {
        EXPECT_EQ (pieces[(size_t) i].startTicks, i * k16th);
        EXPECT_EQ (pieces[(size_t) i].lengthTicks, k16th);
    }
}

TEST (NoteTools, ChopAlignsOffGridNotesToTheGrid)
{
    // Starts 40 ticks late and runs 500 ticks: 40..240, 240..480, 480..540.
    const auto pieces = notetools::chop (makeNote (60, 40, 500), k16th);
    ASSERT_EQ (pieces.size(), 3u);
    EXPECT_EQ (pieces[0].startTicks, 40);
    EXPECT_EQ (pieces[0].lengthTicks, 200);
    EXPECT_EQ (pieces[1].startTicks, 240);
    EXPECT_EQ (pieces[1].lengthTicks, 240);
    EXPECT_EQ (pieces[2].startTicks, 480);
    EXPECT_EQ (pieces[2].lengthTicks, 60);
}

TEST (NoteTools, ChopKeepsVelocityAndKey)
{
    for (const auto& piece : notetools::chop (makeNote (55, 0, ids::ticksPerQuarter, 0.33), k16th))
    {
        EXPECT_EQ (piece.key, 55);
        EXPECT_DOUBLE_EQ (piece.velocity, 0.33);
    }
}

TEST (NoteTools, ChopLeavesNotesInsideOneDivisionAlone)
{
    const auto pieces = notetools::chop (makeNote (60, 480, k16th), k8th);
    ASSERT_EQ (pieces.size(), 1u);
    EXPECT_EQ (pieces[0].startTicks, 480);
    EXPECT_EQ (pieces[0].lengthTicks, k16th);
}

TEST (NoteTools, ChopEdgeCases)
{
    const auto zeroLength = notetools::chop (makeNote (60, 240, 0), k16th);
    ASSERT_EQ (zeroLength.size(), 1u);
    EXPECT_EQ (zeroLength[0].lengthTicks, 0);

    const auto noDivision = notetools::chop (makeNote (60, 0, ids::ticksPerQuarter), 0);
    ASSERT_EQ (noDivision.size(), 1u);
    EXPECT_EQ (noDivision[0].lengthTicks, ids::ticksPerQuarter);

    EXPECT_TRUE (notetools::chopAll ({}, k16th).empty());
}

TEST (NoteTools, ChopAllCoversWholeSelection)
{
    const std::vector<Note> selection { makeNote (60, 0, ids::ticksPerQuarter),
                                        makeNote (62, ids::ticksPerQuarter, k8th) };
    const auto pieces = notetools::chopAll (selection, k16th);
    EXPECT_EQ (pieces.size(), 6u);
}

// ---------- glue ----------

TEST (NoteTools, GlueMergesAdjacentNotes)
{
    const auto merged = notetools::glue ({ makeNote (60, 0, k16th, 0.9),
                                           makeNote (60, k16th, k16th, 0.2) });
    ASSERT_EQ (merged.size(), 1u);
    EXPECT_EQ (merged[0].startTicks, 0);
    EXPECT_EQ (merged[0].lengthTicks, 2 * k16th);
    EXPECT_DOUBLE_EQ (merged[0].velocity, 0.9) << "the earliest note supplies velocity";
}

TEST (NoteTools, GlueMergesOverlappingNotes)
{
    const auto merged = notetools::glue ({ makeNote (60, 0, 400), makeNote (60, 200, 400) });
    ASSERT_EQ (merged.size(), 1u);
    EXPECT_EQ (merged[0].startTicks, 0);
    EXPECT_EQ (merged[0].lengthTicks, 600);
}

TEST (NoteTools, GlueKeepsFullyContainedNoteInsideTheSpan)
{
    const auto merged = notetools::glue ({ makeNote (60, 0, 960), makeNote (60, 200, 100) });
    ASSERT_EQ (merged.size(), 1u);
    EXPECT_EQ (merged[0].lengthTicks, 960) << "a shorter overlap must not shrink the run";
}

TEST (NoteTools, GlueLeavesNonAdjacentNotesApart)
{
    const auto merged = notetools::glue ({ makeNote (60, 0, k16th), makeNote (60, 960, k16th) });
    ASSERT_EQ (merged.size(), 2u);
    EXPECT_EQ (merged[0].startTicks, 0);
    EXPECT_EQ (merged[1].startTicks, 960);
}

TEST (NoteTools, GlueRespectsGapTolerance)
{
    const std::vector<Note> notes { makeNote (60, 0, k16th), makeNote (60, k16th + 10, k16th) };
    EXPECT_EQ (notetools::glue (notes).size(), 2u);
    EXPECT_EQ (notetools::glue (notes, 10).size(), 1u);
}

TEST (NoteTools, GlueNeverMergesAcrossKeys)
{
    const auto merged = notetools::glue ({ makeNote (60, 0, k16th), makeNote (61, k16th, k16th),
                                           makeNote (60, k16th, k16th) });
    ASSERT_EQ (merged.size(), 2u);
    EXPECT_EQ (merged[0].key, 60);
    EXPECT_EQ (merged[0].lengthTicks, 2 * k16th);
    EXPECT_EQ (merged[1].key, 61);
    EXPECT_EQ (merged[1].lengthTicks, k16th);
}

TEST (NoteTools, GlueSortsUnorderedInput)
{
    const auto merged = notetools::glue ({ makeNote (60, 2 * k16th, k16th),
                                           makeNote (60, 0, k16th),
                                           makeNote (60, k16th, k16th) });
    ASSERT_EQ (merged.size(), 1u);
    EXPECT_EQ (merged[0].startTicks, 0);
    EXPECT_EQ (merged[0].lengthTicks, 3 * k16th);
}

TEST (NoteTools, GlueEdgeCases)
{
    EXPECT_TRUE (notetools::glue ({}).empty());

    const auto single = notetools::glue ({ makeNote (60, 240, k16th) });
    ASSERT_EQ (single.size(), 1u);
    EXPECT_EQ (single[0].startTicks, 240);
    EXPECT_EQ (single[0].lengthTicks, k16th);

    const auto zeroLength = notetools::glue ({ makeNote (60, 0, 0), makeNote (60, 0, k16th) });
    ASSERT_EQ (zeroLength.size(), 1u);
    EXPECT_EQ (zeroLength[0].lengthTicks, k16th);
}

// ---------- strum ----------

TEST (NoteTools, StrumOffsetsChordFromTheBottomUp)
{
    const auto strummed = notetools::strum ({ makeNote (60, 960, k16th),
                                              makeNote (64, 960, k16th),
                                              makeNote (67, 960, k16th) }, 20);
    ASSERT_EQ (strummed.size(), 3u);
    EXPECT_EQ (strummed[0].startTicks, 960) << "the lowest note anchors the strum";
    EXPECT_EQ (strummed[1].startTicks, 980);
    EXPECT_EQ (strummed[2].startTicks, 1000);
}

TEST (NoteTools, StrumPreservesInputOrder)
{
    const auto strummed = notetools::strum ({ makeNote (67, 960, k16th),
                                              makeNote (60, 960, k16th) }, 20);
    ASSERT_EQ (strummed.size(), 2u);
    EXPECT_EQ (strummed[0].key, 67);
    EXPECT_EQ (strummed[0].startTicks, 980);
    EXPECT_EQ (strummed[1].key, 60);
    EXPECT_EQ (strummed[1].startTicks, 960);
}

TEST (NoteTools, StrumOrdersByStartBeforePitch)
{
    const auto strummed = notetools::strum ({ makeNote (72, 0, k16th),
                                              makeNote (48, 480, k16th) }, 30);
    EXPECT_EQ (strummed[0].startTicks, 0);
    EXPECT_EQ (strummed[1].startTicks, 510);
}

TEST (NoteTools, StrumBackwardsClampsAtZero)
{
    const auto strummed = notetools::strum ({ makeNote (60, 100, k16th),
                                              makeNote (64, 100, k16th),
                                              makeNote (67, 100, k16th) }, -60);
    EXPECT_EQ (strummed[0].startTicks, 100);
    EXPECT_EQ (strummed[1].startTicks, 40);
    EXPECT_EQ (strummed[2].startTicks, 0) << "must not run before the pattern start";
}

TEST (NoteTools, StrumEdgeCases)
{
    EXPECT_TRUE (notetools::strum ({}, 20).empty());

    const auto single = notetools::strum ({ makeNote (60, 240, k16th) }, 20);
    ASSERT_EQ (single.size(), 1u);
    EXPECT_EQ (single[0].startTicks, 240) << "one note has nothing to strum against";

    const auto unchanged = notetools::strum ({ makeNote (60, 0, k16th), makeNote (64, 0, k16th) }, 0);
    EXPECT_EQ (unchanged[0].startTicks, 0);
    EXPECT_EQ (unchanged[1].startTicks, 0);
}

TEST (NoteTools, StrumKeepsLengthsAndVelocities)
{
    const auto strummed = notetools::strum ({ makeNote (60, 0, 480, 0.4), makeNote (64, 0, 480, 0.9) }, 15);
    EXPECT_EQ (strummed[0].lengthTicks, 480);
    EXPECT_DOUBLE_EQ (strummed[0].velocity, 0.4);
    EXPECT_EQ (strummed[1].lengthTicks, 480);
    EXPECT_DOUBLE_EQ (strummed[1].velocity, 0.9);
}

// ---------- lane classification ----------

namespace
{
juce::ValueTree makeLane (const std::vector<Note>& notes)
{
    juce::ValueTree lane (ids::LANE);
    for (const auto& note : notes)
    {
        juce::ValueTree tree (ids::NOTE);
        tree.setProperty (ids::key, note.key, nullptr);
        tree.setProperty (ids::startTicks, note.startTicks, nullptr);
        tree.setProperty (ids::lengthTicks, note.lengthTicks, nullptr);
        tree.setProperty (ids::velocity, note.velocity, nullptr);
        lane.appendChild (tree, nullptr);
    }
    return lane;
}
}

TEST (NoteTools, StepGridLaneIsNotPianoRollContent)
{
    const auto lane = makeLane ({ makeNote (60, 0, ids::ticksPerStep),
                                  makeNote (60, 4 * ids::ticksPerStep, ids::ticksPerStep),
                                  makeNote (60, 8 * ids::ticksPerStep, ids::ticksPerStep) });
    EXPECT_FALSE (notetools::laneUsesPianoRoll (lane, 60));
}

TEST (NoteTools, EmptyLaneIsNotPianoRollContent)
{
    EXPECT_FALSE (notetools::laneUsesPianoRoll (makeLane ({}), 60));
    EXPECT_FALSE (notetools::laneUsesPianoRoll (juce::ValueTree (ids::LANE), 60));
}

TEST (NoteTools, OffGridStartMakesALanePianoRollContent)
{
    EXPECT_TRUE (notetools::laneUsesPianoRoll (makeLane ({ makeNote (60, 120, ids::ticksPerStep) }), 60));
}

TEST (NoteTools, NonStepLengthMakesALanePianoRollContent)
{
    EXPECT_TRUE (notetools::laneUsesPianoRoll (makeLane ({ makeNote (60, 0, 480) }), 60));
    EXPECT_TRUE (notetools::laneUsesPianoRoll (makeLane ({ makeNote (60, 0, 120) }), 60));
}

TEST (NoteTools, NonRootPitchMakesALanePianoRollContent)
{
    const auto lane = makeLane ({ makeNote (60, 0, ids::ticksPerStep),
                                  makeNote (67, ids::ticksPerStep, ids::ticksPerStep) });
    EXPECT_TRUE (notetools::laneUsesPianoRoll (lane, 60));
    EXPECT_TRUE (notetools::laneUsesPianoRoll (makeLane ({ makeNote (60, 0, ids::ticksPerStep) }), 36))
        << "same notes, different channel root";
}

TEST (NoteTools, LaneClassificationIgnoresNonNoteChildren)
{
    auto lane = makeLane ({ makeNote (60, 0, ids::ticksPerStep) });
    lane.appendChild (juce::ValueTree ("SOMETHINGELSE"), nullptr);
    EXPECT_FALSE (notetools::laneUsesPianoRoll (lane, 60));
}
