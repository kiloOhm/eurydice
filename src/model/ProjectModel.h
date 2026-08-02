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

    // Swing is per pattern, falling back to the project value for patterns
    // that never set one (which is every pattern in a pre-0.2 project).
    bool patternOverridesSwing (const juce::ValueTree& pattern) const
    {
        return pattern.hasProperty (ids::swing);
    }
    double getSwingForPattern (const juce::ValueTree& pattern) const
    {
        return patternOverridesSwing (pattern) ? (double) pattern[ids::swing] : getSwing();
    }
    void setPatternSwing (juce::ValueTree pattern, double s)
    {
        pattern.setProperty (ids::swing, juce::jlimit (0.0, 1.0, s), &undo);
    }
    void clearPatternSwing (juce::ValueTree pattern)
    {
        pattern.removeProperty (ids::swing, &undo);
    }
    bool   isSongMode() const                 { return root[ids::songMode]; }
    void   setSongMode (bool song)            { root.setProperty (ids::songMode, song, nullptr); }

    // Transport loop. Not undoable: it is a playback setting, not an edit.
    int    getLoopStart() const               { return root[ids::loopStart]; }
    int    getLoopEnd() const                 { return root[ids::loopEnd]; }
    bool   isLoopEnabled() const              { return root[ids::loopEnabled]; }
    void   setLoopRange (int startTicks, int endTicks);
    void   setLoopEnabled (bool enabled)      { root.setProperty (ids::loopEnabled, enabled, nullptr); }
    void   clearLoop();

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

    // Names of the channels routed to an insert. The mixer uses this for
    // "name insert after channel", which only makes sense for exactly one.
    juce::StringArray channelsRoutedTo (int insertIndex) const
    {
        juce::StringArray names;
        for (const auto ch : channels())
            if ((int) ch[ids::insertIndex] == insertIndex)
                names.add (ch[ids::name].toString());
        return names;
    }

    // --- Patterns ---
    juce::ValueTree addPattern (const juce::String& name);
    // Deep copy of the pattern, inserted right after the original.
    juce::ValueTree clonePattern (int patternId);
    // Also drops playlist clips pointing at it; refuses to remove the last pattern.
    bool removePattern (int patternId);
    bool movePattern (int fromIndex, int toIndex);
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
    //           | "builtin-insert"
    // paramId: "volume"/"pan" for internal targets; "<slot>:<paramIndex>" or
    // "<paramIndex>" for plugin targets; "<slot>:<propertyName>" for built-ins.
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
    // Replaces the project contents while keeping the root object identity, so
    // listeners attached to it survive a load.
    void adoptLoadedTree (const juce::ValueTree& loaded);

    juce::ValueTree root { ids::PROJECT };
    juce::UndoManager undo;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProjectModel)
};
