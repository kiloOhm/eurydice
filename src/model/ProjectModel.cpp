#include "ProjectModel.h"

ProjectModel::ProjectModel()
{
    createDefaultProject();
}

void ProjectModel::createDefaultProject()
{
    undo.clearUndoHistory();

    root = juce::ValueTree (ids::PROJECT);
    root.setProperty (ids::name, "Untitled", nullptr);
    root.setProperty (ids::tempo, 140.0, nullptr);
    root.setProperty (ids::swing, 0.0, nullptr);
    root.setProperty (ids::songMode, false, nullptr);
    root.setProperty (ids::loopStart, 0, nullptr);
    root.setProperty (ids::loopEnd, 4 * ids::ticksPerBar, nullptr);
    root.setProperty (ids::loopEnabled, false, nullptr);
    root.setProperty (ids::id, 1, nullptr);   // id counter lives on the root

    root.appendChild (juce::ValueTree (ids::CHANNELS), nullptr);
    root.appendChild (juce::ValueTree (ids::PATTERNS), nullptr);

    juce::ValueTree playlistTree (ids::PLAYLIST);
    for (int i = 0; i < 24; ++i)
    {
        juce::ValueTree track (ids::TRACK);
        track.setProperty (ids::name, "Track " + juce::String (i + 1), nullptr);
        track.setProperty (ids::mute, false, nullptr);
        track.setProperty (ids::solo, false, nullptr);
        playlistTree.appendChild (track, nullptr);
    }
    root.appendChild (playlistTree, nullptr);

    juce::ValueTree mixerTree (ids::MIXER);
    mixerTree.appendChild (makeInsert (0, "Master"), nullptr);
    for (int i = 1; i <= 32; ++i)
        mixerTree.appendChild (makeInsert (i, "Insert " + juce::String (i)), nullptr);
    root.appendChild (mixerTree, nullptr);

    root.appendChild (juce::ValueTree (ids::AUTOMATIONS), nullptr);

    // Starter content: one pattern, four classic drum channels, and a basic
    // four-on-the-floor so a fresh project plays something.
    auto firstPattern = addPattern ("Pattern 1");
    root.setProperty (ids::activePattern, (int) firstPattern[ids::id], nullptr);
    for (auto* n : { "Kick", "Clap", "Hat", "Snare" })
        addChannel ("sampler", n);

    const int kickId = getChannel (0)[ids::id];
    const int hatId  = getChannel (2)[ids::id];
    auto kickLane = getOrCreateLane (firstPattern, kickId);
    auto hatLane  = getOrCreateLane (firstPattern, hatId);
    for (int step = 0; step < 16; ++step)
    {
        if (step % 4 == 0)
            addNote (kickLane, 60, step * ids::ticksPerStep, ids::ticksPerStep);
        if (step % 4 == 2)
            addNote (hatLane, 60, step * ids::ticksPerStep, ids::ticksPerStep);
    }
    undo.clearUndoHistory();
}

void ProjectModel::setLoopRange (int startTicks, int endTicks)
{
    const int lo = juce::jmax (0, juce::jmin (startTicks, endTicks));
    const int hi = juce::jmax (0, juce::jmax (startTicks, endTicks));
    root.setProperty (ids::loopStart, lo, nullptr);
    root.setProperty (ids::loopEnd, hi, nullptr);
}

void ProjectModel::clearLoop()
{
    root.setProperty (ids::loopEnabled, false, nullptr);
    root.setProperty (ids::loopStart, 0, nullptr);
    root.setProperty (ids::loopEnd, 0, nullptr);
}

juce::ValueTree ProjectModel::makeInsert (int index, const juce::String& name)
{
    juce::ValueTree insert (ids::INSERT);
    insert.setProperty (ids::id, index, nullptr);
    insert.setProperty (ids::name, name, nullptr);
    insert.setProperty (ids::volume, 0.8, nullptr);
    insert.setProperty (ids::pan, 0.0, nullptr);
    insert.setProperty (ids::mute, false, nullptr);
    insert.appendChild (juce::ValueTree (ids::SENDS), nullptr);

    // Every insert except master routes to master by default.
    if (index != 0)
    {
        juce::ValueTree send (ids::SEND);
        send.setProperty (ids::destInsert, 0, nullptr);
        send.setProperty (ids::level, 1.0, nullptr);
        insert.getChildWithName (ids::SENDS).appendChild (send, nullptr);
    }
    return insert;
}

int ProjectModel::nextId()
{
    const int v = (int) root[ids::id];
    root.setProperty (ids::id, v + 1, nullptr);
    return v;
}

juce::ValueTree ProjectModel::addChannel (const juce::String& type, const juce::String& name)
{
    juce::ValueTree ch (ids::CHANNEL);
    ch.setProperty (ids::id, nextId(), nullptr);
    ch.setProperty (ids::type, type, nullptr);
    ch.setProperty (ids::name, name, nullptr);
    ch.setProperty (ids::volume, 0.78, nullptr);
    ch.setProperty (ids::pan, 0.0, nullptr);
    ch.setProperty (ids::mute, false, nullptr);
    ch.setProperty (ids::solo, false, nullptr);
    ch.setProperty (ids::insertIndex, 0, nullptr);
    ch.setProperty (ids::rootNote, 60, nullptr);
    channels().appendChild (ch, &undo);
    return ch;
}

void ProjectModel::removeChannel (const juce::ValueTree& channel)
{
    const int chId = channel[ids::id];

    // Remove the channel's lanes from all patterns too.
    for (auto pattern : patterns())
        for (int i = pattern.getNumChildren(); --i >= 0;)
            if (pattern.getChild (i).hasType (ids::LANE)
                && (int) pattern.getChild (i)[ids::channelId] == chId)
                pattern.removeChild (i, &undo);

    channels().removeChild (channel, &undo);
}

juce::ValueTree ProjectModel::getChannelById (int channelId) const
{
    return channels().getChildWithProperty (ids::id, channelId);
}

juce::ValueTree ProjectModel::addPattern (const juce::String& name)
{
    juce::ValueTree p (ids::PATTERN);
    p.setProperty (ids::id, nextId(), nullptr);
    p.setProperty (ids::name, name, nullptr);
    p.setProperty (ids::lengthTicks, ids::ticksPerBar, nullptr);
    patterns().appendChild (p, &undo);
    return p;
}

juce::ValueTree ProjectModel::getPatternById (int patternId) const
{
    return patterns().getChildWithProperty (ids::id, patternId);
}

juce::ValueTree ProjectModel::getOrCreateLane (juce::ValueTree pattern, int channelId)
{
    auto lane = getLane (pattern, channelId);
    if (! lane.isValid())
    {
        lane = juce::ValueTree (ids::LANE);
        lane.setProperty (ids::channelId, channelId, nullptr);
        pattern.appendChild (lane, &undo);
    }
    return lane;
}

juce::ValueTree ProjectModel::getLane (const juce::ValueTree& pattern, int channelId) const
{
    return pattern.getChildWithProperty (ids::channelId, channelId);
}

juce::ValueTree ProjectModel::addNote (juce::ValueTree lane, int key, int startTicks,
                                       int lengthTicks, double velocity, double pan)
{
    juce::ValueTree note (ids::NOTE);
    note.setProperty (ids::key, key, nullptr);
    note.setProperty (ids::startTicks, startTicks, nullptr);
    note.setProperty (ids::lengthTicks, lengthTicks, nullptr);
    note.setProperty (ids::velocity, velocity, nullptr);
    note.setProperty (ids::notePan, pan, nullptr);
    lane.appendChild (note, &undo);
    return note;
}

void ProjectModel::removeNote (juce::ValueTree lane, const juce::ValueTree& note)
{
    lane.removeChild (note, &undo);
}

juce::ValueTree ProjectModel::addPlaylistClip (const juce::String& clipType, int trackIndex,
                                               int startTicks, int lengthTicks)
{
    auto track = playlist().getChild (trackIndex);
    if (! track.isValid())
        return {};

    juce::ValueTree clip (ids::CLIP);
    clip.setProperty (ids::clipType, clipType, nullptr);
    clip.setProperty (ids::startTicks, startTicks, nullptr);
    clip.setProperty (ids::lengthTicks, lengthTicks, nullptr);
    clip.setProperty (ids::muted, false, nullptr);
    track.appendChild (clip, &undo);
    return clip;
}

juce::ValueTree ProjectModel::addAutomation (const juce::String& targetType, int targetId,
                                             const juce::String& paramId, const juce::String& name,
                                             double initialValue)
{
    juce::ValueTree automation (ids::AUTOMATION);
    automation.setProperty (ids::id, nextId(), nullptr);
    automation.setProperty (ids::name, name, nullptr);
    automation.setProperty (ids::targetType, targetType, nullptr);
    automation.setProperty (ids::targetId, targetId, nullptr);
    automation.setProperty (ids::paramId, paramId, nullptr);

    // Two flat points spanning 4 bars at the current value.
    for (int posTicks : { 0, 4 * ids::ticksPerBar })
    {
        juce::ValueTree point (ids::POINT);
        point.setProperty (ids::posTicks, posTicks, nullptr);
        point.setProperty (ids::value, initialValue, nullptr);
        point.setProperty (ids::tension, 0.0, nullptr);
        automation.appendChild (point, nullptr);
    }

    automations().appendChild (automation, &undo);
    return automation;
}

juce::ValueTree ProjectModel::getAutomationById (int automationId) const
{
    return automations().getChildWithProperty (ids::id, automationId);
}

int ProjectModel::numPlaylistTracks() const
{
    return playlist().getNumChildren();
}

bool ProjectModel::saveToFile (const juce::File& file) const
{
    juce::TemporaryFile temp (file);
    {
        juce::FileOutputStream out (temp.getFile());
        if (! out.openedOk())
            return false;
        root.writeToStream (out);
    }
    return temp.overwriteTargetFileWithTemporary();
}

bool ProjectModel::loadFromFile (const juce::File& file)
{
    juce::FileInputStream in (file);
    if (! in.openedOk())
        return false;

    auto loaded = juce::ValueTree::readFromStream (in);
    if (! loaded.hasType (ids::PROJECT))
        return false;

    undo.clearUndoHistory();
    root = loaded;
    return true;
}
