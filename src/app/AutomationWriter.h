#pragma once

#include <juce_events/juce_events.h>
#include <map>
#include "engine/AudioEngine.h"
#include "engine/AutomationRecorder.h"
#include "model/ProjectModel.h"

// Live automation recording. While the write arm is on and the transport is
// running, every move of an automatable control is captured against the
// transport position and written into that parameter's automation source,
// creating the source and its playlist clip on first touch.
//
// Message thread only: the audio thread never sees this object, it only sees
// the snapshot EngineSync rebuilds from the tree.
class AutomationWriter : private juce::Timer
{
public:
    // Identifies one automatable parameter. `name` is only used when the
    // source has to be created.
    struct Target
    {
        juce::String type;      // "channel" | "insert" | "channel-param" | "plugin-*"
        int          id = 0;
        juce::String paramId;
        juce::String name;
    };

    AutomationWriter (ProjectModel& projectModel, AudioEngine& audioEngine)
        : project (projectModel), engine (audioEngine)
    {
        // Playback can also be stopped from the API, a command or the engine
        // itself, so watch the transport rather than trusting one call site.
        startTimerHz (12);
    }

    ~AutomationWriter() override { stopTimer(); }

    bool isArmed() const noexcept { return armed; }

    void setArmed (bool shouldBeArmed)
    {
        if (armed == shouldBeArmed)
            return;
        armed = shouldBeArmed;
        if (! armed)
            finaliseAll();
    }

    // Records one live value (0..1) at the current transport position. Does
    // nothing unless the write arm is on and the transport is running.
    void touch (const Target& target, double normalised)
    {
        if (armed && engine.isPlaying())
            touchAt (target, normalised, engine.getPositionTicks());
    }

    // Records at an explicit position. Split out from touch() so the write
    // logic can be driven without a running audio callback.
    void touchAt (const Target& target, double normalised, double positionTicks)
    {
        auto source = findSource (project, target);
        if (! source.isValid())
            source = createWithClip (project, target.type, target.id, target.paramId,
                                     target.name, normalised);
        if (! source.isValid())
            return;

        const int automationId = source[ids::id];
        auto clip = findClip (project, automationId, positionTicks);
        if (! clip.isValid())
            return;

        const int posTicks = (int) positionTicks - (int) clip[ids::startTicks];
        if (posTicks < 0)
            return;

        // Growing the clip keeps a pass that runs past its end recording
        // instead of silently stopping at the edge.
        if (posTicks >= (int) clip[ids::lengthTicks])
            clip.setProperty (ids::lengthTicks,
                              (posTicks / ids::ticksPerBar + 1) * ids::ticksPerBar, nullptr);

        auto entry = passes.find (automationId);
        if (entry == passes.end())
            entry = passes.emplace (automationId, autorec::Pass (readPoints (source))).first;

        source.setProperty (ids::writing, true, nullptr);
        if (entry->second.addSample (posTicks, juce::jlimit (0.0, 1.0, normalised)))
            writePoints (source, entry->second.merged(), nullptr);
    }

    // Closes every open pass and commits it as a single undoable edit.
    void finaliseAll()
    {
        if (passes.empty())
            return;

        auto& undo = project.getUndoManager();
        undo.beginNewTransaction ("Record automation");

        for (auto& [automationId, pass] : passes)
        {
            auto source = project.getAutomationById (automationId);
            if (! source.isValid())
                continue;
            pass.finish();
            // Rewind to the pre-pass curve first, so undo steps back over the
            // whole pass instead of only the last point committed during it.
            writePoints (source, pass.curveBeforePass(), nullptr);
            writePoints (source, pass.merged(), &undo);
            source.removeProperty (ids::writing, nullptr);
        }
        passes.clear();
    }

    bool isRecording (int automationId) const
    {
        return passes.find (automationId) != passes.end();
    }

    // --- tree helpers, shared with the menus and the control API ---

    static juce::ValueTree findSource (const ProjectModel& project, const Target& target)
    {
        for (const auto source : project.automations())
            if (source.hasType (ids::AUTOMATION)
                && source[ids::targetType].toString() == target.type
                && (int) source[ids::targetId] == target.id
                && source[ids::paramId].toString() == target.paramId)
                return source;
        return {};
    }

    // The clip playing `automationId`: the one covering positionTicks if there
    // is one, otherwise the first.
    static juce::ValueTree findClip (const ProjectModel& project, int automationId,
                                     double positionTicks = -1.0)
    {
        juce::ValueTree first;
        for (const auto track : project.playlist())
        {
            for (const auto clip : track)
            {
                if (! clip.hasType (ids::CLIP)
                    || clip[ids::clipType].toString() != "automation"
                    || (int) clip[ids::automationId] != automationId)
                    continue;
                if (! first.isValid())
                    first = clip;
                const double start = (int) clip[ids::startTicks];
                if (positionTicks >= start
                    && positionTicks < start + (int) clip[ids::lengthTicks])
                    return clip;
            }
        }
        return first;
    }

    // Creates an automation source plus the playlist clip that plays it. The
    // clip lands on the first track free at bar 0.
    static juce::ValueTree createWithClip (ProjectModel& project, const juce::String& targetType,
                                           int targetId, const juce::String& paramId,
                                           const juce::String& name, double initialValue)
    {
        auto automation = project.addAutomation (targetType, targetId, paramId, name, initialValue);
        const int lengthTicks = 4 * ids::ticksPerBar;

        int trackIndex = 0;
        for (int i = 0; i < project.numPlaylistTracks(); ++i)
        {
            bool free = true;
            for (const auto clip : project.playlist().getChild (i))
                if (clip.hasType (ids::CLIP) && (int) clip[ids::startTicks] < lengthTicks)
                    free = false;
            if (free) { trackIndex = i; break; }
        }

        auto clip = project.addPlaylistClip ("automation", trackIndex, 0, lengthTicks);
        clip.setProperty (ids::automationId, (int) automation[ids::id], nullptr);
        return automation;
    }

    static std::vector<autorec::Point> readPoints (const juce::ValueTree& source)
    {
        std::vector<autorec::Point> points;
        for (const auto point : source)
            if (point.hasType (ids::POINT))
                points.push_back ({ (int) point[ids::posTicks],
                                    (double) point[ids::value],
                                    (double) point[ids::tension] });
        std::sort (points.begin(), points.end(),
                   [] (const autorec::Point& a, const autorec::Point& b)
                   { return a.posTicks < b.posTicks; });
        return points;
    }

    static void writePoints (juce::ValueTree source, const std::vector<autorec::Point>& points,
                             juce::UndoManager* undo)
    {
        source.removeAllChildren (undo);
        for (const auto& point : points)
        {
            juce::ValueTree child (ids::POINT);
            child.setProperty (ids::posTicks, point.posTicks, nullptr);
            child.setProperty (ids::value, point.value, nullptr);
            child.setProperty (ids::tension, point.tension, nullptr);
            source.appendChild (child, undo);
        }
    }

private:
    void timerCallback() override
    {
        const bool playingNow = engine.isPlaying();
        if (wasPlaying && ! playingNow)
            finaliseAll();
        wasPlaying = playingNow;
    }

    ProjectModel& project;
    AudioEngine& engine;
    std::map<int, autorec::Pass> passes;   // keyed by automation id
    bool armed = false;
    bool wasPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationWriter)
};
