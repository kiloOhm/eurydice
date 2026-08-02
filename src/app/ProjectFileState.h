#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include "model/ProjectModel.h"

// Tracks which file the project came from and whether it has unsaved edits,
// so the window title and save prompts can reflect reality.
class ProjectFileState : private juce::ValueTree::Listener,
                         public juce::ChangeBroadcaster
{
public:
    explicit ProjectFileState (ProjectModel& m) : model (m) { attach(); }
    ~ProjectFileState() override { observed.removeListener (this); }

    void attach()
    {
        observed.removeListener (this);
        observed = model.getRoot();
        observed.addListener (this);
    }

    void markSaved (const juce::File& file)
    {
        currentFile = file;
        dirty = false;
        sendChangeMessage();
    }

    void markLoaded (const juce::File& file)
    {
        attach();
        markSaved (file);
    }

    // A recovered autosave belongs to `file` but is not what sits on disk
    // there, so it stays dirty until the user saves over it.
    void markRestored (const juce::File& file)
    {
        attach();
        currentFile = file;
        dirty = true;
        sendChangeMessage();
    }

    void markNewProject()
    {
        attach();
        currentFile = juce::File();
        dirty = false;
        sendChangeMessage();
    }

    juce::File getFile() const  { return currentFile; }
    bool isDirty() const        { return dirty; }

    juce::String getDisplayName() const
    {
        return currentFile == juce::File() ? "Untitled"
                                           : currentFile.getFileNameWithoutExtension();
    }

    juce::String getWindowTitle() const
    {
        const juce::String emDash (juce::CharPointer_UTF8 ("\xe2\x80\x94"));
        const juce::String bullet (juce::CharPointer_UTF8 ("\xe2\x80\xa2"));
        return "Eurydice " + emDash + " " + getDisplayName() + (dirty ? " " + bullet : "");
    }

private:
    void markDirty()
    {
        if (! dirty)
        {
            dirty = true;
            sendChangeMessage();
        }
    }

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& prop) override
    {
        // Transport position/selection churn shouldn't count as an edit.
        if (prop == ids::selectedChannel || prop == ids::songMode)
            return;
        markDirty();
    }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override        { markDirty(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { markDirty(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override         { markDirty(); }

    ProjectModel& model;
    juce::ValueTree observed;
    juce::File currentFile;
    bool dirty = false;
};
