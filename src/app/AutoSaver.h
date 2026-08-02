#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include "ProjectFileState.h"
#include "model/ProjectModel.h"
#include "plugins/PluginManager.h"

// Mirrors a dirty project into the app support directory once a minute so a
// crash costs at most one interval. The tree is snapshotted on the message
// thread and written on a background one, because a large project takes long
// enough to serialise to be felt as a hitch mid-edit.
//
// One recovery file per project path, so two open-and-crash cycles on
// different projects do not overwrite each other.
class AutoSaver : private juce::Timer,
                  private juce::Thread
{
public:
    static constexpr int defaultIntervalMs = 60000;

    AutoSaver (ProjectModel& projectModel, ProjectFileState& state,
               juce::File directory = defaultDirectory(),
               int intervalMs = defaultIntervalMs)
        : juce::Thread ("Autosave"), model (projectModel), fileState (state),
          dir (std::move (directory))
    {
        startThread (juce::Thread::Priority::background);
        startTimer (intervalMs);
    }

    ~AutoSaver() override
    {
        stopTimer();
        signalThreadShouldExit();
        notify();
        stopThread (4000);
    }

    // Snapshots and queues a write when there are unsaved edits. Returns false
    // when there was nothing to do.
    bool saveIfDirty()
    {
        if (! fileState.isDirty())
            return false;

        if (onBeforeSnapshot)
            onBeforeSnapshot();

        auto snapshot = model.getRoot().createCopy();
        const auto source = fileState.getFile();
        snapshot.setProperty (ids::recoveryOf, source.getFullPathName(), nullptr);

        {
            const juce::ScopedLock sl (lock);
            pendingTree = snapshot;
            pendingTarget = fileFor (dir, source);
            written = pendingTarget;
            idle.reset();
        }
        notify();
        return true;
    }

    // Waits for a queued write to land. Used before quitting and by tests.
    bool flush (int timeoutMs = 4000)
    {
        {
            const juce::ScopedLock sl (lock);
            if (! pendingTree.isValid() && ! writing)
                return true;
        }
        return idle.wait (timeoutMs);
    }

    // A clean save, load or new project means the mirror is no longer wanted.
    void clearRecovery()
    {
        {
            const juce::ScopedLock sl (lock);
            pendingTree = {};
        }
        fileFor (dir, fileState.getFile()).deleteFile();
        if (written != juce::File())
            written.deleteFile();
    }

    juce::File getDirectory() const { return dir; }

    // Set by the app so hosted plugin state makes it into the mirror. Runs on
    // the message thread, immediately before the snapshot.
    std::function<void()> onBeforeSnapshot;

    // ---- static helpers, usable without an instance ----

    static juce::File defaultDirectory()
    {
        return PluginManager::getAppDataDir().getChildFile ("recovery");
    }

    static juce::File fileFor (const juce::File& directory, const juce::File& projectFile)
    {
        const auto path = projectFile.getFullPathName();
        const auto stem = path.isEmpty()
            ? juce::String ("untitled")
            : projectFile.getFileNameWithoutExtension() + "-"
                  + juce::String::toHexString (path.hashCode64());
        return directory.getChildFile (juce::File::createLegalFileName (stem) + ".eury-recovery");
    }

    static bool writeAtomically (juce::ValueTree tree, const juce::File& target)
    {
        target.getParentDirectory().createDirectory();

        juce::TemporaryFile temp (target);
        {
            juce::FileOutputStream out (temp.getFile());
            if (! out.openedOk())
                return false;
            tree.writeToStream (out);
        }
        return temp.overwriteTargetFileWithTemporary();
    }

    static juce::ValueTree read (const juce::File& recoveryFile)
    {
        juce::FileInputStream in (recoveryFile);
        if (! in.openedOk())
            return {};
        auto tree = juce::ValueTree::readFromStream (in);
        return tree.hasType (ids::PROJECT) ? tree : juce::ValueTree();
    }

    static juce::File projectShadowedBy (const juce::File& recoveryFile)
    {
        const auto tree = read (recoveryFile);
        const auto path = tree.isValid() ? tree[ids::recoveryOf].toString() : juce::String();
        return path.isEmpty() ? juce::File() : juce::File (path);
    }

    // A recovery copy is worth offering when it still parses and either the
    // project it shadows is gone (never saved, or deleted) or it holds edits
    // the project file does not have yet.
    static bool isWorthOffering (const juce::File& recoveryFile)
    {
        const auto tree = read (recoveryFile);
        if (! tree.isValid())
            return false;

        const auto path = tree[ids::recoveryOf].toString();
        if (path.isEmpty() || ! juce::File (path).existsAsFile())
            return true;
        return recoveryFile.getLastModificationTime()
                   > juce::File (path).getLastModificationTime();
    }

    // Newest first.
    static juce::Array<juce::File> findPending (const juce::File& directory = defaultDirectory())
    {
        juce::Array<juce::File> found;
        for (const auto& file : directory.findChildFiles (juce::File::findFiles, false,
                                                          "*.eury-recovery"))
            if (isWorthOffering (file))
                found.add (file);

        std::sort (found.begin(), found.end(), [] (const juce::File& a, const juce::File& b)
        {
            return a.getLastModificationTime() > b.getLastModificationTime();
        });
        return found;
    }

private:
    void timerCallback() override { saveIfDirty(); }

    void run() override
    {
        while (! threadShouldExit())
        {
            juce::ValueTree snapshot;
            juce::File target;
            {
                const juce::ScopedLock sl (lock);
                snapshot = pendingTree;
                target = pendingTarget;
                pendingTree = {};
                writing = snapshot.isValid();
            }

            if (snapshot.isValid())
                writeAtomically (snapshot, target);

            {
                const juce::ScopedLock sl (lock);
                writing = false;
                if (! pendingTree.isValid())
                    idle.signal();
            }

            if (! threadShouldExit())
                wait (-1);
        }
        idle.signal();
    }

    ProjectModel& model;
    ProjectFileState& fileState;
    juce::File dir;

    juce::CriticalSection lock;
    juce::ValueTree pendingTree;
    juce::File pendingTarget;
    juce::File written;
    bool writing = false;
    juce::WaitableEvent idle;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoSaver)
};
