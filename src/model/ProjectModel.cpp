#include "ProjectModel.h"

ProjectModel::ProjectModel()
{
    createDefaultProject();
}

void ProjectModel::createDefaultProject()
{
    undo.clearUndoHistory();

    // Rebuild in place instead of assigning a new tree — see adoptLoadedTree.
    if (! root.isValid())
        root = juce::ValueTree (ids::PROJECT);
    root.removeAllChildren (nullptr);
    root.removeAllProperties (nullptr);

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

    // Master + a handful of inserts; the mixer's "+" grows the row on demand
    // (the engine preallocates buffers up to a generous cap, see addInsert).
    juce::ValueTree mixerTree (ids::MIXER);
    mixerTree.appendChild (makeInsert (0, "Master"), nullptr);
    for (int i = 1; i <= 8; ++i)
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

void ProjectModel::setSendEnabled (int fromInsert, int toInsert, bool enabled)
{
    auto ins = getInsert (fromInsert);
    if (! ins.isValid() || toInsert < 0 || toInsert >= numInserts() || toInsert == fromInsert)
        return;

    auto sends = ins.getChildWithName (ids::SENDS);
    if (! sends.isValid())
    {
        sends = juce::ValueTree (ids::SENDS);
        ins.appendChild (sends, &undo);
    }

    for (int i = sends.getNumChildren(); --i >= 0;)
        if ((int) sends.getChild (i)[ids::destInsert] == toInsert)
        {
            if (! enabled)
                sends.removeChild (i, &undo);
            return;
        }

    if (enabled)
    {
        juce::ValueTree send (ids::SEND);
        send.setProperty (ids::destInsert, toInsert, nullptr);
        send.setProperty (ids::level, 1.0, nullptr);
        sends.appendChild (send, &undo);
    }
}

bool ProjectModel::hasSend (int fromInsert, int toInsert) const
{
    for (const auto send : getInsert (fromInsert).getChildWithName (ids::SENDS))
        if ((int) send[ids::destInsert] == toInsert)
            return true;
    return false;
}

bool ProjectModel::sendWouldCycle (int fromInsert, int toInsert) const
{
    // Walk the send graph from `toInsert`; reaching `fromInsert` means the
    // proposed edge would close a loop.
    std::vector<int> stack { toInsert };
    std::vector<bool> seen ((size_t) numInserts(), false);
    while (! stack.empty())
    {
        const int at = stack.back();
        stack.pop_back();
        if (at == fromInsert)
            return true;
        if (at < 0 || at >= numInserts() || seen[(size_t) at])
            continue;
        seen[(size_t) at] = true;
        for (const auto send : getInsert (at).getChildWithName (ids::SENDS))
            stack.push_back (send[ids::destInsert]);
    }
    return false;
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

bool ProjectModel::moveChannel (int fromIndex, int toIndex)
{
    if (fromIndex == toIndex
        || ! juce::isPositiveAndBelow (fromIndex, numChannels())
        || ! juce::isPositiveAndBelow (toIndex, numChannels()))
        return false;

    channels().moveChild (fromIndex, toIndex, &undo);
    return true;
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

juce::ValueTree ProjectModel::clonePattern (int patternId)
{
    auto source = getPatternById (patternId);
    if (! source.isValid())
        return {};

    auto copy = source.createCopy();
    copy.setProperty (ids::id, nextId(), nullptr);
    copy.setProperty (ids::name, source[ids::name].toString() + " (copy)", nullptr);
    patterns().addChild (copy, patterns().indexOf (source) + 1, &undo);
    return copy;
}

bool ProjectModel::removePattern (int patternId)
{
    auto pattern = getPatternById (patternId);
    if (! pattern.isValid() || numPatterns() <= 1)
        return false;

    const int index = patterns().indexOf (pattern);

    for (auto track : playlist())
        for (int i = track.getNumChildren(); --i >= 0;)
            if (track.getChild (i).hasType (ids::CLIP)
                && (int) track.getChild (i)[ids::patternId] == patternId)
                track.removeChild (i, &undo);

    patterns().removeChild (pattern, &undo);

    if ((int) root[ids::activePattern] == patternId)
        root.setProperty (ids::activePattern,
                          (int) getPattern (juce::jmin (index, numPatterns() - 1))[ids::id],
                          nullptr);
    return true;
}

bool ProjectModel::movePattern (int fromIndex, int toIndex)
{
    if (fromIndex == toIndex
        || ! juce::isPositiveAndBelow (fromIndex, numPatterns())
        || ! juce::isPositiveAndBelow (toIndex, numPatterns()))
        return false;

    patterns().moveChild (fromIndex, toIndex, &undo);
    return true;
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
    adoptLoadedTree (loaded);

    // ids::writing only means "a write pass is running right now". A project
    // saved mid-pass would otherwise come back with automation permanently
    // yielding to a control nobody is holding.
    for (auto automation : root.getChildWithName (ids::AUTOMATIONS))
        automation.removeProperty (ids::writing, nullptr);

    return true;
}

void ProjectModel::adoptLoadedTree (const juce::ValueTree& loaded)
{
    // Copy the loaded content into the existing root rather than rebinding
    // `root` to the loaded tree. Every panel and the engine hold a listener on
    // the root *object*; swapping the object out left them all listening to a
    // tree nobody edits again, so the UI kept showing the previous project
    // (most visibly: the channel rack's pattern list) until something else
    // forced a repaint.
    if (! root.isValid())
    {
        root = loaded.createCopy();
        return;
    }

    root.copyPropertiesFrom (loaded, nullptr);

    // Sync the top-level containers (CHANNELS, PATTERNS, PLAYLIST, ...) in
    // place too: panels react to child events *inside* those containers, so
    // replacing a container object wholesale would go unnoticed just like
    // replacing the root would.
    for (int i = root.getNumChildren(); --i >= 0;)
        if (! loaded.getChildWithName (root.getChild (i).getType()).isValid())
            root.removeChild (i, nullptr);

    for (const auto& child : loaded)
    {
        auto mine = root.getChildWithName (child.getType());
        if (mine.isValid())
            mine.copyPropertiesAndChildrenFrom (child, nullptr);
        else
            root.appendChild (child.createCopy(), nullptr);
    }
}
