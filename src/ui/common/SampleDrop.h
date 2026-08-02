#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "model/ProjectModel.h"
#include "engine/AudioClipCache.h"

// Dropping sample files (browser drag or OS drag) onto the rack and playlist.
// Target resolution and the model edits live here so they can be unit tested;
// the panels only translate mouse points and paint hover indicators.
namespace sampledrop
{

inline const char* const audioExtensions = "wav;aif;aiff;mp3;flac;ogg;m4a";

// Drag description the browser's file tree announces itself with.
inline const juce::String browserDragDescription { "eurydice:browser-sample" };

inline juce::StringArray audioFilesIn (const juce::StringArray& paths)
{
    juce::StringArray out;
    for (const auto& path : paths)
        if (juce::File (path).hasFileExtension (audioExtensions))
            out.add (path);
    return out;
}

// Audio files carried by an internal drag; empty when it isn't the browser's.
inline juce::StringArray filesFromDragSource (const juce::DragAndDropTarget::SourceDetails& details)
{
    if (details.description.toString() != browserDragDescription)
        return {};

    if (auto* tree = dynamic_cast<juce::FileTreeComponent*> (details.sourceComponent.get()))
    {
        juce::StringArray paths;
        for (int i = 0; i < tree->getNumSelectedFiles(); ++i)
            paths.add (tree->getSelectedFile (i).getFullPathName());
        return audioFilesIn (paths);
    }
    return {};
}

// Rack rows are stacked at (rowHeight + rowGap). The middle half of a row
// means "replace this channel's sample"; the row edges, the gaps and the
// space below mean "insert a new channel here".
struct RackTarget
{
    int replaceRow = -1;   // row whose sample the drop replaces, -1 = none
    int insertIndex = 0;   // where a new channel would go instead
};

inline RackTarget rackTargetForY (int y, int numRows, int rowHeight, int rowGap)
{
    const int pitch = rowHeight + rowGap;
    if (numRows <= 0 || y >= numRows * pitch)
        return { -1, juce::jmax (0, numRows) };
    if (y < 0)
        return { -1, 0 };

    const int row = y / pitch;
    const int within = y - row * pitch;
    const int edge = rowHeight / 4;
    if (within < edge)
        return { -1, row };
    if (within >= rowHeight - edge)   // includes the gap under the row
        return { -1, row + 1 };
    return { row, row + 1 };
}

struct PlaylistTarget
{
    int track = 0;
    int startTicks = 0;
};

inline PlaylistTarget playlistTargetFor (double rawTicks, int rawTrack, int numTracks, int snapTicks)
{
    const int snap = juce::jmax (1, snapTicks);
    return { juce::jlimit (0, juce::jmax (0, numTracks - 1), rawTrack),
             (int) (std::floor (juce::jmax (0.0, rawTicks) / snap) * snap) };
}

// File duration -> clip length at the project tempo, never below one step.
inline int audioClipLengthTicks (double seconds, double tempoBpm)
{
    const double ticksPerSecond = (tempoBpm / 60.0) * ids::ticksPerQuarter;
    return juce::jmax (ids::ticksPerStep, (int) (seconds * ticksPerSecond));
}

// Replaces the sample of the channel at target.replaceRow when it is a
// sampler, otherwise inserts a fresh sampler channel at target.insertIndex.
// Undoable; the caller owns the gesture. Returns the affected channel.
inline juce::ValueTree dropOntoRack (ProjectModel& model, const juce::File& file, RackTarget target)
{
    auto& undo = model.getUndoManager();

    if (target.replaceRow >= 0)
        if (auto channel = model.getChannel (target.replaceRow);
            channel.isValid() && channel[ids::type].toString() == "sampler")
        {
            channel.setProperty (ids::samplePath, file.getFullPathName(), &undo);
            return channel;
        }

    auto channel = model.addChannel ("sampler", file.getFileNameWithoutExtension());
    channel.setProperty (ids::samplePath, file.getFullPathName(), &undo);
    const int from = model.numChannels() - 1;
    const int to = juce::jlimit (0, from, target.insertIndex);
    if (to != from)
        model.channels().moveChild (from, to, &undo);
    return channel;
}

// Creates the audio clip a drop produces — the same shape the control API's
// playlist.addAudioClip makes. Invalid tree when the file is unreadable.
inline juce::ValueTree dropOntoPlaylist (ProjectModel& model, AudioClipCache& clips,
                                         const juce::File& file, PlaylistTarget target)
{
    const double seconds = clips.getNaturalSeconds (file.getFullPathName());
    if (seconds <= 0.0)
        return {};

    auto clip = model.addPlaylistClip ("audio", target.track, target.startTicks,
                                       audioClipLengthTicks (seconds, model.getTempo()));
    if (clip.isValid())
    {
        clip.setProperty (ids::audioPath, file.getFullPathName(), nullptr);
        clip.setProperty (ids::stretchRatio, 1.0, nullptr);
        clip.setProperty (ids::audioOffsetTicks, 0, nullptr);
    }
    return clip;
}

} // namespace sampledrop
