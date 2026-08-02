#include "ControlDispatcher.h"
#include "engine/OfflineRenderer.h"

namespace
{
juce::var getOr (const juce::var& params, const char* key, const juce::var& fallback)
{
    if (auto* obj = params.getDynamicObject())
        if (obj->hasProperty (key))
            return obj->getProperty (key);
    return fallback;
}

bool has (const juce::var& params, const char* key)
{
    auto* obj = params.getDynamicObject();
    return obj != nullptr && obj->hasProperty (key);
}

juce::var makeObj (std::initializer_list<std::pair<juce::String, juce::var>> fields)
{
    auto* obj = new juce::DynamicObject();
    for (auto& [k, v] : fields)
        obj->setProperty (k, v);
    return juce::var (obj);
}
}

juce::var ControlDispatcher::channelToVar (const juce::ValueTree& ch) const
{
    return makeObj ({
        { "id", (int) ch[ids::id] }, { "name", ch[ids::name].toString() },
        { "type", ch[ids::type].toString() }, { "volume", (double) ch[ids::volume] },
        { "pan", (double) ch[ids::pan] }, { "mute", (bool) ch[ids::mute] },
        { "insert", (int) ch[ids::insertIndex] },
        { "samplePath", ch[ids::samplePath].toString() },
        { "pluginId", ch[ids::pluginId].toString() } });
}

juce::ValueTree ControlDispatcher::requireChannel (const juce::var& params)
{
    auto ch = services.project.getChannelById ((int) getOr (params, "channelId", -1));
    if (! ch.isValid())
        throw ControlError { "unknown channelId" };
    return ch;
}

juce::ValueTree ControlDispatcher::requirePattern (const juce::var& params)
{
    const int patId = (int) getOr (params, "patternId",
                                   (int) services.project.getRoot()[ids::activePattern]);
    auto pattern = services.project.getPatternById (patId);
    if (! pattern.isValid())
        throw ControlError { "unknown patternId" };
    return pattern;
}

juce::var ControlDispatcher::dispatch (const juce::String& method, const juce::var& params)
{
    auto& project = services.project;
    auto& engine  = services.engine;
    auto& undo    = project.getUndoManager();

    if (method == "ping")
        return "pong";

    // ---------- state ----------
    if (method == "state.get")
    {
        juce::Array<juce::var> channels;
        for (int i = 0; i < project.numChannels(); ++i)
            channels.add (channelToVar (project.getChannel (i)));

        juce::Array<juce::var> patterns;
        for (int i = 0; i < project.numPatterns(); ++i)
        {
            const auto p = project.getPattern (i);
            patterns.add (makeObj ({ { "id", (int) p[ids::id] }, { "name", p[ids::name].toString() },
                                     { "lengthTicks", (int) p[ids::lengthTicks] } }));
        }

        return makeObj ({
            { "tempo", project.getTempo() }, { "swing", project.getSwing() },
            { "songMode", project.isSongMode() },
            { "loopStart", project.getLoopStart() },
            { "loopEnd", project.getLoopEnd() },
            { "loopEnabled", project.isLoopEnabled() },
            { "playing", engine.isPlaying() },
            { "positionTicks", engine.getPositionTicks() },
            { "activePatternId", (int) project.getRoot()[ids::activePattern] },
            { "ticksPerStep", ids::ticksPerStep }, { "ticksPerBar", ids::ticksPerBar },
            { "channels", channels }, { "patterns", patterns },
            { "numInserts", project.numInserts() },
            { "numPlaylistTracks", project.numPlaylistTracks() } });
    }

    // ---------- transport ----------
    if (method == "transport.play")  { engine.play();  return true; }
    if (method == "transport.stop")  { engine.stop();  return true; }
    if (method == "transport.seek")  { engine.setPositionTicks (getOr (params, "ticks", 0.0)); return true; }
    if (method == "transport.set")
    {
        if (has (params, "tempo"))    project.setTempo (getOr (params, "tempo", 140.0));
        if (has (params, "swing"))    project.setSwing (getOr (params, "swing", 0.0));
        if (has (params, "songMode")) project.setSongMode (getOr (params, "songMode", false));
        if (has (params, "loopStart") || has (params, "loopEnd"))
            project.setLoopRange ((int) getOr (params, "loopStart", project.getLoopStart()),
                                  (int) getOr (params, "loopEnd", project.getLoopEnd()));
        if (has (params, "loopEnabled")) project.setLoopEnabled (getOr (params, "loopEnabled", false));
        return true;
    }

    // ---------- patterns ----------
    if (method == "pattern.create")
    {
        auto p = project.addPattern (getOr (params, "name", "Pattern " + juce::String (project.numPatterns() + 1)));
        if (has (params, "lengthTicks"))
            p.setProperty (ids::lengthTicks, (int) getOr (params, "lengthTicks", ids::ticksPerBar), nullptr);
        return makeObj ({ { "id", (int) p[ids::id] } });
    }
    if (method == "pattern.clone")
    {
        auto copy = project.clonePattern ((int) requirePattern (params)[ids::id]);
        return makeObj ({ { "id", (int) copy[ids::id] } });
    }
    if (method == "pattern.remove")
    {
        if (! project.removePattern ((int) requirePattern (params)[ids::id]))
            throw ControlError { "cannot remove the last remaining pattern" };
        return true;
    }
    if (method == "pattern.select")
    {
        auto p = requirePattern (params);
        project.getRoot().setProperty (ids::activePattern, (int) p[ids::id], nullptr);
        return true;
    }
    if (method == "pattern.setLength")
    {
        requirePattern (params).setProperty (ids::lengthTicks,
                                             (int) getOr (params, "lengthTicks", ids::ticksPerBar), &undo);
        return true;
    }

    // ---------- channels ----------
    if (method == "channel.add")
    {
        const auto type = getOr (params, "type", "sampler").toString();
        if (type != "sampler" && type != "synth" && type != "plugin")
            throw ControlError { "type must be sampler|synth|plugin" };
        auto ch = project.addChannel (type, getOr (params, "name", type).toString());
        if (has (params, "pluginId"))
        {
            const auto pluginId = getOr (params, "pluginId", "").toString();
            if (! services.plugins.findByIdentifier (pluginId))
                throw ControlError { "unknown pluginId (use plugins.list)" };
            ch.setProperty (ids::pluginId, pluginId, nullptr);
        }
        if (has (params, "samplePath"))
            ch.setProperty (ids::samplePath, getOr (params, "samplePath", "").toString(), nullptr);
        return makeObj ({ { "id", (int) ch[ids::id] } });
    }
    if (method == "channel.set")
    {
        auto ch = requireChannel (params);
        if (has (params, "volume")) ch.setProperty (ids::volume, (double) getOr (params, "volume", 0.78), &undo);
        if (has (params, "pan"))    ch.setProperty (ids::pan, (double) getOr (params, "pan", 0.0), &undo);
        if (has (params, "mute"))   ch.setProperty (ids::mute, (bool) getOr (params, "mute", false), &undo);
        if (has (params, "insert")) ch.setProperty (ids::insertIndex, (int) getOr (params, "insert", 0), &undo);
        if (has (params, "name"))   ch.setProperty (ids::name, getOr (params, "name", "").toString(), &undo);
        if (has (params, "rootNote")) ch.setProperty (ids::rootNote, (int) getOr (params, "rootNote", 60), &undo);
        if (has (params, "samplePath")) ch.setProperty (ids::samplePath, getOr (params, "samplePath", "").toString(), &undo);
        return true;
    }
    if (method == "channel.remove")
    {
        project.removeChannel (requireChannel (params));
        return true;
    }

    // ---------- notes ----------
    if (method == "notes.get")
    {
        auto pattern = requirePattern (params);
        auto lane = project.getLane (pattern, (int) requireChannel (params)[ids::id]);
        juce::Array<juce::var> notes;
        if (lane.isValid())
            for (const auto note : lane)
                notes.add (makeObj ({ { "key", (int) note[ids::key] },
                                      { "start", (int) note[ids::startTicks] },
                                      { "length", (int) note[ids::lengthTicks] },
                                      { "velocity", (double) note[ids::velocity] },
                                      { "pan", (double) note[ids::notePan] } }));
        return notes;
    }
    if (method == "notes.add" || method == "notes.set" || method == "notes.clear")
    {
        auto pattern = requirePattern (params);
        auto channel = requireChannel (params);
        auto lane = project.getOrCreateLane (pattern, (int) channel[ids::id]);

        if (method != "notes.add")
            lane.removeAllChildren (&undo);

        if (method == "notes.add")
        {
            project.addNote (lane, (int) getOr (params, "key", 60),
                             (int) getOr (params, "start", 0),
                             (int) getOr (params, "length", ids::ticksPerStep),
                             (double) getOr (params, "velocity", 0.78),
                             (double) getOr (params, "pan", 0.0));
        }
        else if (method == "notes.set")
        {
            if (auto* arr = getOr (params, "notes", {}).getArray())
                for (const auto& n : *arr)
                    project.addNote (lane, (int) getOr (n, "key", 60),
                                     (int) getOr (n, "start", 0),
                                     (int) getOr (n, "length", ids::ticksPerStep),
                                     (double) getOr (n, "velocity", 0.78),
                                     (double) getOr (n, "pan", 0.0));
        }
        return true;
    }

    // ---------- playlist ----------
    if (method == "playlist.get")
    {
        juce::Array<juce::var> tracks;
        for (int i = 0; i < project.numPlaylistTracks(); ++i)
        {
            const auto track = project.playlist().getChild (i);
            juce::Array<juce::var> clips;
            for (const auto clip : track)
                if (clip.hasType (ids::CLIP))
                    clips.add (makeObj ({ { "type", clip[ids::clipType].toString() },
                                          { "patternId", (int) clip[ids::patternId] },
                                          { "start", (int) clip[ids::startTicks] },
                                          { "length", (int) clip[ids::lengthTicks] },
                                          { "muted", (bool) clip[ids::muted] } }));
            tracks.add (makeObj ({ { "index", i }, { "name", track[ids::name].toString() },
                                   { "mute", (bool) track[ids::mute] }, { "clips", clips } }));
        }
        return tracks;
    }
    if (method == "playlist.addClip")
    {
        auto pattern = requirePattern (params);
        const int trackIndex = (int) getOr (params, "track", 0);
        if (trackIndex < 0 || trackIndex >= project.numPlaylistTracks())
            throw ControlError { "track out of range" };
        auto clip = project.addPlaylistClip ("pattern", trackIndex,
                                             (int) getOr (params, "start", 0),
                                             (int) getOr (params, "length", (int) pattern[ids::lengthTicks]));
        clip.setProperty (ids::patternId, (int) pattern[ids::id], nullptr);
        return true;
    }
    if (method == "playlist.addAudioClip")
    {
        const auto path = getOr (params, "path", "").toString();
        const int trackIndex = (int) getOr (params, "track", 0);
        if (! juce::File (path).existsAsFile())
            throw ControlError { "audio file not found" };
        if (trackIndex < 0 || trackIndex >= project.numPlaylistTracks())
            throw ControlError { "track out of range" };

        const double seconds = services.audioClips.getNaturalSeconds (path);
        if (seconds <= 0.0)
            throw ControlError { "unreadable audio file" };

        const double tps = (project.getTempo() / 60.0) * ids::ticksPerQuarter;
        const int naturalTicks = juce::jmax (ids::ticksPerStep, (int) (seconds * tps));
        int lengthTicks = (int) getOr (params, "lengthTicks", naturalTicks);
        double ratio = 1.0;
        if ((bool) getOr (params, "stretchToFit", false))
            ratio = juce::jlimit (0.1, 10.0, (double) lengthTicks / naturalTicks);
        else
            lengthTicks = naturalTicks;

        auto clip = project.addPlaylistClip ("audio", trackIndex,
                                             (int) getOr (params, "start", 0), lengthTicks);
        clip.setProperty (ids::audioPath, path, nullptr);
        clip.setProperty (ids::stretchRatio, ratio, nullptr);
        clip.setProperty (ids::audioOffsetTicks, (int) getOr (params, "offsetTicks", 0), nullptr);
        return makeObj ({ { "lengthTicks", lengthTicks }, { "stretchRatio", ratio } });
    }
    if (method == "playlist.clear")
    {
        if (has (params, "track"))
        {
            auto track = project.playlist().getChild ((int) getOr (params, "track", 0));
            if (track.isValid())
                track.removeAllChildren (&undo);
        }
        else
            for (auto track : project.playlist())
                track.removeAllChildren (&undo);
        return true;
    }

    // ---------- mixer ----------
    if (method == "mixer.get")
    {
        juce::Array<juce::var> inserts;
        for (int i = 0; i < project.numInserts(); ++i)
        {
            const auto ins = project.getInsert (i);
            juce::Array<juce::var> sends, slots;
            for (const auto send : ins.getChildWithName (ids::SENDS))
                sends.add (makeObj ({ { "to", (int) send[ids::destInsert] },
                                      { "level", (double) send[ids::level] } }));
            for (const auto slot : ins)
                if (slot.hasType (ids::SLOT))
                    slots.add (makeObj ({ { "slot", (int) slot[ids::slotIndex] },
                                          { "pluginId", slot[ids::pluginId].toString() },
                                          { "bypass", (bool) slot[ids::bypass] } }));
            inserts.add (makeObj ({ { "index", i }, { "name", ins[ids::name].toString() },
                                    { "volume", (double) ins[ids::volume] },
                                    { "pan", (double) ins[ids::pan] },
                                    { "mute", (bool) ins[ids::mute] },
                                    { "sends", sends }, { "effects", slots },
                                    { "peakL", engine.getInsertPeak (i, 0) },
                                    { "peakR", engine.getInsertPeak (i, 1) } }));
        }
        return inserts;
    }
    if (method == "mixer.setInsert")
    {
        auto ins = project.getInsert ((int) getOr (params, "insert", -1));
        if (! ins.isValid())
            throw ControlError { "unknown insert" };
        if (has (params, "volume")) ins.setProperty (ids::volume, (double) getOr (params, "volume", 0.8), &undo);
        if (has (params, "pan"))    ins.setProperty (ids::pan, (double) getOr (params, "pan", 0.0), &undo);
        if (has (params, "mute"))   ins.setProperty (ids::mute, (bool) getOr (params, "mute", false), &undo);
        if (has (params, "name"))   ins.setProperty (ids::name, getOr (params, "name", "").toString(), &undo);
        return true;
    }
    if (method == "mixer.addSend")
    {
        auto ins = project.getInsert ((int) getOr (params, "from", -1));
        const int to = (int) getOr (params, "to", -1);
        if (! ins.isValid() || to < 0 || to >= project.numInserts())
            throw ControlError { "bad from/to" };
        juce::ValueTree send (ids::SEND);
        send.setProperty (ids::destInsert, to, nullptr);
        send.setProperty (ids::level, (double) getOr (params, "level", 0.8), nullptr);
        ins.getChildWithName (ids::SENDS).appendChild (send, &undo);
        return true;
    }
    if (method == "mixer.setEffect")
    {
        const int insertIndex = (int) getOr (params, "insert", -1);
        const int slotIndex   = (int) getOr (params, "slot", 0);
        auto ins = project.getInsert (insertIndex);
        if (! ins.isValid())
            throw ControlError { "unknown insert" };
        const auto pluginId = getOr (params, "pluginId", "").toString();
        const auto* builtin = fx::findBuiltin (pluginId);
        if (builtin == nullptr && ! services.plugins.findByIdentifier (pluginId))
            throw ControlError { "unknown pluginId (use plugins.list)" };

        services.effects.remove (insertIndex, slotIndex);
        services.builtinEffects.remove (insertIndex, slotIndex);
        juce::ValueTree slotTree;
        for (auto child : ins)
            if (child.hasType (ids::SLOT) && (int) child[ids::slotIndex] == slotIndex)
                slotTree = child;
        if (! slotTree.isValid())
        {
            slotTree = juce::ValueTree (ids::SLOT);
            slotTree.setProperty (ids::slotIndex, slotIndex, nullptr);
            ins.appendChild (slotTree, &undo);
        }
        slotTree.setProperty (ids::pluginId, pluginId, &undo);
        if (builtin != nullptr)
            BuiltinEffect::writeDefaults (slotTree, builtin->specs, &undo);
        return true;
    }
    if (method == "mixer.removeEffect")
    {
        const int insertIndex = (int) getOr (params, "insert", -1);
        const int slotIndex   = (int) getOr (params, "slot", 0);
        auto ins = project.getInsert (insertIndex);
        if (! ins.isValid())
            throw ControlError { "unknown insert" };
        services.effects.remove (insertIndex, slotIndex);
        services.builtinEffects.remove (insertIndex, slotIndex);
        for (int i = ins.getNumChildren(); --i >= 0;)
            if (ins.getChild (i).hasType (ids::SLOT) && (int) ins.getChild (i)[ids::slotIndex] == slotIndex)
                ins.removeChild (i, &undo);
        return true;
    }

    // ---------- plugins ----------
    if (method == "plugins.list")
    {
        juce::Array<juce::var> out;
        for (const auto& entry : fx::builtinEffects())
            out.add (makeObj ({ { "name", entry.name }, { "format", "Built-in" },
                                { "isInstrument", false }, { "id", entry.id } }));
        for (const auto& d : services.plugins.getKnownPlugins().getTypes())
            out.add (makeObj ({ { "name", d.name }, { "format", d.pluginFormatName },
                                { "isInstrument", d.isInstrument },
                                { "id", d.createIdentifierString() } }));
        return out;
    }
    if (method == "plugins.scan")
    {
        if (services.plugins.isScanning())
            return "already-scanning";
        services.plugins.startScan ([] {});
        return "scanning";
    }

    // ---------- meters ----------
    if (method == "meters.get")
    {
        juce::Array<juce::var> peaks;
        for (int i = 0; i < project.numInserts(); ++i)
        {
            juce::Array<juce::var> lr { engine.getInsertPeak (i, 0), engine.getInsertPeak (i, 1) };
            peaks.add (juce::var (lr));
        }
        return makeObj ({ { "inserts", peaks } });
    }

    // ---------- automation ----------
    if (method == "automation.create")
    {
        const auto targetType = getOr (params, "targetType", "").toString();
        if (targetType != "channel" && targetType != "insert" && targetType != "plugin-channel"
            && targetType != "plugin-insert" && targetType != "builtin-insert")
            throw ControlError { "targetType must be channel|insert|plugin-channel|plugin-insert"
                                 "|builtin-insert" };

        auto automation = services.createAutomationWithClip (
            targetType,
            (int) getOr (params, "targetId", 0),
            getOr (params, "paramId", "volume").toString(),
            getOr (params, "name", "automation").toString(),
            (double) getOr (params, "initialValue", 0.5));
        return makeObj ({ { "id", (int) automation[ids::id] } });
    }
    if (method == "automation.setPoints")
    {
        auto automation = project.getAutomationById ((int) getOr (params, "automationId", -1));
        if (! automation.isValid())
            throw ControlError { "unknown automationId" };
        automation.removeAllChildren (&undo);
        if (auto* arr = getOr (params, "points", {}).getArray())
        {
            for (const auto& p : *arr)
            {
                juce::ValueTree point (ids::POINT);
                point.setProperty (ids::posTicks, (int) getOr (p, "pos", 0), nullptr);
                point.setProperty (ids::value, (double) getOr (p, "value", 0.5), nullptr);
                point.setProperty (ids::tension, (double) getOr (p, "tension", 0.0), nullptr);
                automation.appendChild (point, &undo);
            }
        }
        return true;
    }

    // ---------- render ----------
    if (method == "render.export")
    {
        const auto path = getOr (params, "path", "").toString();
        if (path.isEmpty())
            throw ControlError { "path required (.wav)" };

        OfflineRenderer::Options opts;
        opts.wavFile     = juce::File (path).withFileExtension (".wav");
        opts.renderMp3   = (bool) getOr (params, "mp3", false);
        opts.renderStems = (bool) getOr (params, "stems", false);
        opts.tailSeconds = (double) getOr (params, "tailSeconds", 2.0);

        const auto r = OfflineRenderer::render (engine, project, opts);
        if (! r.ok)
            throw ControlError { r.error };

        juce::Array<juce::var> files;
        for (const auto& f : r.writtenFiles)
            files.add (f);
        return makeObj ({ { "files", files },
                          { "warning", r.error } });
    }

    // ---------- project ----------
    if (method == "project.save")
    {
        const auto path = getOr (params, "path", "").toString();
        if (path.isEmpty())
            throw ControlError { "path required" };
        if (! services.saveProject (juce::File (path)))
            throw ControlError { "save failed" };
        return true;
    }
    if (method == "project.load")
    {
        const auto path = getOr (params, "path", "").toString();
        if (! juce::File (path).existsAsFile())
            throw ControlError { "file not found" };
        if (! services.loadProject (juce::File (path)))
            throw ControlError { "load failed" };
        return true;
    }
    if (method == "project.new")
    {
        services.project.createDefaultProject();
        services.engineSync.attachToProject();
        return true;
    }

    throw ControlError { "unknown method: " + method };
}
