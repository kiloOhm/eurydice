#pragma once

#include "model/ProjectModel.h"
#include "AudioClipCache.h"

// Keeps "follow tempo" audio clips musically sized: when the project tempo
// changes, their stretch ratio is recomputed so the stretched audio still
// fills the clip's (unchanged) tick length. Recomputes are coalesced per
// message-loop tick; the actual re-stretch happens in the next EngineSync
// snapshot rebuild, which the ratio write triggers.
//
// The follower writes the ratio without undo: it is a derived value, and
// undoing the tempo change fires the follower again, which restores it.
class StretchFollower : private juce::ValueTree::Listener,
                        private juce::AsyncUpdater
{
public:
    StretchFollower (ProjectModel& m, AudioClipCache& c)
        : model (m), clips (c)
    {
        attachToProject();
    }

    ~StretchFollower() override
    {
        cancelPendingUpdate();
        observedRoot.removeListener (this);
    }

    // Call when the root tree object is replaced (project load).
    void attachToProject()
    {
        observedRoot.removeListener (this);
        observedRoot = model.getRoot();
        observedRoot.addListener (this);
    }

    // Refit every follow-tempo clip synchronously (the coalesced update path;
    // also called directly by tests, which have no running message loop).
    void recomputeNow()
    {
        cancelPendingUpdate();
        for (auto track : model.playlist())
            for (auto clip : track)
                if (clip.hasType (ids::CLIP) && (bool) clip[ids::followTempo])
                    refit (clip);
    }

    // Stretch one audio clip so it exactly fills its tick length at the
    // current tempo. Pass the UndoManager only for user gestures (enabling
    // "Follow tempo"); tempo-driven refits stay out of the undo history.
    void refit (juce::ValueTree clip, juce::UndoManager* undo = nullptr)
    {
        if (clip[ids::clipType].toString() != "audio")
            return;

        const double naturalSec = clips.getNaturalSeconds (clip[ids::audioPath].toString());
        if (naturalSec <= 0.0)
            return;

        const double tps = (model.getTempo() / 60.0) * ids::ticksPerQuarter;
        const double ratio = juce::jlimit (0.1, 10.0,
                                           (double) (int) clip[ids::lengthTicks] / (naturalSec * tps));
        clip.setProperty (ids::stretchRatio, ratio, undo);
    }

private:
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override
    {
        if (property == ids::tempo && tree == observedRoot)
            triggerAsyncUpdate();
    }

    void handleAsyncUpdate() override { recomputeNow(); }

    ProjectModel& model;
    AudioClipCache& clips;
    juce::ValueTree observedRoot;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StretchFollower)
};
