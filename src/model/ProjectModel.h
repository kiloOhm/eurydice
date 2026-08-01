#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include "Ids.h"

// Owns the project ValueTree + UndoManager and provides typed helpers.
// All mutations from UI / command API go through here so undo and the
// engine-refresh listeners see everything.
class ProjectModel
{
public:
    ProjectModel();

    void createDefaultProject();

    juce::ValueTree getRoot() const           { return root; }
    juce::UndoManager& getUndoManager()       { return undo; }

    // --- Project-level ---
    double getTempo() const                   { return root[ids::tempo]; }
    void   setTempo (double bpm)              { root.setProperty (ids::tempo, juce::jlimit (20.0, 999.0, bpm), &undo); }
    double getSwing() const                   { return root[ids::swing]; }
    void   setSwing (double s)                { root.setProperty (ids::swing, juce::jlimit (0.0, 1.0, s), &undo); }
    bool   isSongMode() const                 { return root[ids::songMode]; }
    void   setSongMode (bool song)            { root.setProperty (ids::songMode, song, nullptr); }

    // --- Sections ---
    juce::ValueTree channels() const          { return root.getChildWithName (ids::CHANNELS); }
    juce::ValueTree patterns() const          { return root.getChildWithName (ids::PATTERNS); }
    juce::ValueTree playlist() const          { return root.getChildWithName (ids::PLAYLIST); }
    juce::ValueTree mixer() const             { return root.getChildWithName (ids::MIXER); }
    juce::ValueTree automations() const       { return root.getChildWithName (ids::AUTOMATIONS); }

    // --- Channels ---
    juce::ValueTree addChannel (const juce::String& type, const juce::String& name);
    void removeChannel (const juce::ValueTree& channel);
    juce::ValueTree getChannelById (int channelId) const;
    int numChannels() const                   { return channels().getNumChildren(); }
    juce::ValueTree getChannel (int index) const { return channels().getChild (index); }

    // --- Patterns ---
    juce::ValueTree addPattern (const juce::String& name);
    juce::ValueTree getPatternById (int patternId) const;
    int numPatterns() const                   { return patterns().getNumChildren(); }
    juce::ValueTree getPattern (int index) const { return patterns().getChild (index); }

    // Lane for a channel inside a pattern (created on demand).
    juce::ValueTree getOrCreateLane (juce::ValueTree pattern, int channelId);
    juce::ValueTree getLane (const juce::ValueTree& pattern, int channelId) const;

    // Notes. Steps are notes: a step at index i = note at i * ticksPerStep.
    juce::ValueTree addNote (juce::ValueTree lane, int key, int startTicks, int lengthTicks,
                             double velocity = 0.78, double pan = 0.0);
    void removeNote (juce::ValueTree lane, const juce::ValueTree& note);

    // --- Playlist ---
    juce::ValueTree addPlaylistClip (const juce::String& clipType, int trackIndex,
                                     int startTicks, int lengthTicks);
    int numPlaylistTracks() const;

    // --- Automation ---
    // targetType: "channel" | "insert" | "plugin-channel" | "plugin-insert"
    // paramId: "volume"/"pan" for internal targets; "<slot>:<paramIndex>" or
    // "<paramIndex>" for plugin targets.
    juce::ValueTree addAutomation (const juce::String& targetType, int targetId,
                                   const juce::String& paramId, const juce::String& name,
                                   double initialValue);
    juce::ValueTree getAutomationById (int automationId) const;

    // --- Mixer ---
    juce::ValueTree getInsert (int index) const { return mixer().getChild (index); }
    int numInserts() const                    { return mixer().getNumChildren(); }

    // --- Persistence ---
    bool saveToFile (const juce::File&) const;
    bool loadFromFile (const juce::File&);

    int nextId();  // unique id source for channels/patterns/automations

    static juce::ValueTree makeInsert (int index, const juce::String& name);

private:
    juce::ValueTree root { ids::PROJECT };
    juce::UndoManager undo;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProjectModel)
};
