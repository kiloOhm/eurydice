#include <gtest/gtest.h>
#include "app/AutoSaver.h"

namespace
{
struct AutoSaveFixture
{
    ProjectModel model;
    ProjectFileState fileState { model };
    juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getNonexistentChildFile ("eurytest-recovery", {});
    AutoSaver saver { model, fileState, dir };

    ~AutoSaveFixture() { dir.deleteRecursively(); }

    juce::File writeRecovery()
    {
        EXPECT_TRUE (saver.saveIfDirty());
        EXPECT_TRUE (saver.flush());
        return AutoSaver::fileFor (dir, fileState.getFile());
    }
};

juce::File makeProjectFile (ProjectModel& model)
{
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getNonexistentChildFile ("eurytest-autosave", ".eury");
    EXPECT_TRUE (model.saveToFile (file));
    return file;
}
}

TEST (AutoSaver, WritesOnlyWhenDirty)
{
    AutoSaveFixture f;
    EXPECT_FALSE (f.saver.saveIfDirty());
    EXPECT_FALSE (AutoSaver::fileFor (f.dir, juce::File()).existsAsFile());

    f.model.setTempo (150.0);
    ASSERT_TRUE (f.fileState.isDirty());

    const auto recovery = f.writeRecovery();
    ASSERT_TRUE (recovery.existsAsFile());

    const auto restored = AutoSaver::read (recovery);
    ASSERT_TRUE (restored.isValid());
    EXPECT_DOUBLE_EQ ((double) restored[ids::tempo], 150.0);
    EXPECT_EQ (restored[ids::recoveryOf].toString(), juce::String());
}

TEST (AutoSaver, RewriteReplacesAtomically)
{
    AutoSaveFixture f;
    f.model.setTempo (150.0);
    const auto recovery = f.writeRecovery();

    f.model.setTempo (174.0);
    EXPECT_EQ (f.writeRecovery(), recovery);

    // The temporary is renamed over the target, never left behind next to it.
    EXPECT_EQ (f.dir.getNumberOfChildFiles (juce::File::findFilesAndDirectories), 1);
    EXPECT_DOUBLE_EQ ((double) AutoSaver::read (recovery)[ids::tempo], 174.0);
}

TEST (AutoSaver, RecoveryFileIsPerProjectPath)
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory);
    const auto a = AutoSaver::fileFor (dir, dir.getChildFile ("beat.eury"));
    const auto b = AutoSaver::fileFor (dir, dir.getChildFile ("nested/beat.eury"));
    EXPECT_NE (a, b);
    EXPECT_EQ (a, AutoSaver::fileFor (dir, dir.getChildFile ("beat.eury")));
    EXPECT_NE (a, AutoSaver::fileFor (dir, juce::File()));
}

TEST (AutoSaver, StalenessComparesAgainstShadowedProject)
{
    AutoSaveFixture f;
    const auto project = makeProjectFile (f.model);
    f.fileState.markSaved (project);

    // Pin the project older than the copy so the comparison is not decided by
    // how long the two writes happened to take.
    project.setLastModificationTime (juce::Time::getCurrentTime()
                                         - juce::RelativeTime::minutes (5.0));

    f.model.setTempo (160.0);
    const auto recovery = f.writeRecovery();

    EXPECT_EQ (AutoSaver::projectShadowedBy (recovery), project);
    EXPECT_TRUE (AutoSaver::isWorthOffering (recovery));
    ASSERT_EQ (AutoSaver::findPending (f.dir).size(), 1);
    EXPECT_EQ (AutoSaver::findPending (f.dir).getFirst(), recovery);

    // A later save of the project itself makes the copy stale.
    project.setLastModificationTime (recovery.getLastModificationTime()
                                         + juce::RelativeTime::seconds (5.0));
    EXPECT_FALSE (AutoSaver::isWorthOffering (recovery));
    EXPECT_TRUE (AutoSaver::findPending (f.dir).isEmpty());

    project.deleteFile();
}

// An untitled project has nothing on disk to compare against, so its copy is
// always worth offering.
TEST (AutoSaver, UntitledRecoveryIsAlwaysOffered)
{
    AutoSaveFixture f;
    f.model.setTempo (200.0);
    const auto recovery = f.writeRecovery();

    EXPECT_EQ (AutoSaver::projectShadowedBy (recovery), juce::File());
    EXPECT_TRUE (AutoSaver::isWorthOffering (recovery));
    EXPECT_EQ (AutoSaver::findPending (f.dir).size(), 1);
}

TEST (AutoSaver, CleanSaveClearsRecovery)
{
    AutoSaveFixture f;
    const auto project = makeProjectFile (f.model);
    f.fileState.markSaved (project);

    f.model.setTempo (128.0);
    const auto recovery = f.writeRecovery();
    ASSERT_TRUE (recovery.existsAsFile());

    ASSERT_TRUE (f.model.saveToFile (project));
    f.fileState.markSaved (project);
    f.saver.clearRecovery();

    EXPECT_FALSE (recovery.existsAsFile());
    EXPECT_TRUE (AutoSaver::findPending (f.dir).isEmpty());
    EXPECT_FALSE (f.saver.saveIfDirty());

    project.deleteFile();
}

// Save-as moves the project; the copy left under the old name must go too.
TEST (AutoSaver, ClearRemovesTheCopyItLastWrote)
{
    AutoSaveFixture f;
    f.model.setTempo (145.0);
    const auto untitledCopy = f.writeRecovery();
    ASSERT_TRUE (untitledCopy.existsAsFile());

    const auto project = makeProjectFile (f.model);
    f.fileState.markSaved (project);
    f.saver.clearRecovery();

    EXPECT_FALSE (untitledCopy.existsAsFile());
    EXPECT_TRUE (AutoSaver::findPending (f.dir).isEmpty());

    project.deleteFile();
}

TEST (AutoSaver, ReadRejectsGarbage)
{
    AutoSaveFixture f;
    f.dir.createDirectory();
    const auto junk = f.dir.getChildFile ("broken.eury-recovery");
    junk.replaceWithText ("not a project");

    EXPECT_FALSE (AutoSaver::read (junk).isValid());
    EXPECT_EQ (AutoSaver::projectShadowedBy (junk), juce::File());
    EXPECT_FALSE (AutoSaver::isWorthOffering (junk));
    EXPECT_TRUE (AutoSaver::findPending (f.dir).isEmpty());
}

TEST (AutoSave, GarbageCollectorKeepsRecentDropsOld)
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getNonexistentChildFile ("eurytest-gc", "");
    dir.createDirectory();

    // 12 files; make the 3 oldest ancient by back-dating their mtimes.
    juce::Array<juce::File> files;
    for (int i = 0; i < 12; ++i)
    {
        auto file = dir.getChildFile ("r" + juce::String (i) + ".eury-recovery");
        file.replaceWithText ("x");
        files.add (file);
    }
    const auto ancient = juce::Time::getCurrentTime() - juce::RelativeTime::days (30);
    for (int i = 0; i < 3; ++i)
        files.getReference (i).setLastModificationTime (ancient);

    // keepNewest=10: the 3 ancient ones die (age), plus enough of the rest to
    // fit the cap. 12 files, 3 old -> those 3 go; 9 remain <= 10 cap.
    const int removed = AutoSaver::garbageCollect (dir, 14, 10);
    EXPECT_EQ (removed, 3);
    EXPECT_EQ (dir.findChildFiles (juce::File::findFiles, false, "*.eury-recovery").size(), 9);

    // Tighten the cap: newest 5 survive regardless of age.
    EXPECT_EQ (AutoSaver::garbageCollect (dir, 14, 5), 4);
    EXPECT_EQ (dir.findChildFiles (juce::File::findFiles, false, "*.eury-recovery").size(), 5);

    dir.deleteRecursively();
}
