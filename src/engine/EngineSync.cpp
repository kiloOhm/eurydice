#include "EngineSync.h"
#include "model/ChannelParams.h"

static_assert (ProjectModel::maxInserts == AudioEngine::maxInserts,
               "the model's insert cap must match the engine's preallocated buses");
#include "plugins/PluginGenerator.h"
#include "sandbox/SandboxedGenerator.h"
#include <map>

EngineSync::EngineSync (ProjectModel& m, GeneratorPool& g, EffectPool& fx,
                        BuiltinEffectPool& builtinFx, AudioClipCache& ac, AudioEngine& e)
    : model (m), generators (g), effects (fx), builtins (builtinFx), audioClips (ac), engine (e)
{
    attachToProject();
}

EngineSync::~EngineSync()
{
    observedRoot.removeListener (this);
}

void EngineSync::attachToProject()
{
    observedRoot.removeListener (this);
    observedRoot = model.getRoot();
    observedRoot.addListener (this);
    rebuildNow();
}

void EngineSync::rebuildNow()
{
    cancelPendingUpdate();
    engine.publishSnapshot (build());
}

std::shared_ptr<const EngineSnapshot> EngineSync::build() const
{
    auto snap = std::make_shared<EngineSnapshot>();
    const auto root = model.getRoot();

    snap->tempo    = root[ids::tempo];
    snap->swing    = root[ids::swing];
    snap->songMode = root[ids::songMode];

    snap->loopStartTicks = juce::jmax (0, (int) root[ids::loopStart]);
    snap->loopEndTicks   = juce::jmax (0, (int) root[ids::loopEnd]);
    snap->loopEnabled    = (bool) root[ids::loopEnabled]
                               && snap->loopEndTicks > snap->loopStartTicks;

    // --- channels (resolve solo/mute) ---
    const auto channelsTree = model.channels();
    bool anySolo = false;
    for (const auto ch : channelsTree)
        if ((bool) ch[ids::solo])
            anySolo = true;

    std::map<int, int> channelIdToIndex;
    for (int i = 0; i < channelsTree.getNumChildren(); ++i)
    {
        auto ch = channelsTree.getChild (i);
        ChannelSnapshot cs;
        cs.id          = ch[ids::id];
        cs.volume      = (float) (double) ch[ids::volume];
        cs.pan         = (float) (double) ch[ids::pan];
        cs.audible     = anySolo ? (bool) ch[ids::solo] : ! (bool) ch[ids::mute];
        cs.insertIndex = ch[ids::insertIndex];
        cs.generator   = generators.getOrCreate (ch);
        channelIdToIndex[cs.id] = i;
        snap->channels.push_back (std::move (cs));
    }

    // --- patterns ---
    const auto patternsTree = model.patterns();
    std::map<int, int> patternIdToIndex;
    for (int i = 0; i < patternsTree.getNumChildren(); ++i)
    {
        const auto p = patternsTree.getChild (i);
        PatternSnapshot ps;
        ps.id = p[ids::id];
        ps.lengthTicks = p[ids::lengthTicks];
        ps.swing = model.getSwingForPattern (p);
        ps.overridesSwing = model.patternOverridesSwing (p);
        patternIdToIndex[ps.id] = i;

        for (const auto lane : p)
        {
            if (! lane.hasType (ids::LANE))
                continue;
            const auto chIt = channelIdToIndex.find ((int) lane[ids::channelId]);
            if (chIt == channelIdToIndex.end())
                continue;

            for (const auto note : lane)
            {
                if (! note.hasType (ids::NOTE))
                    continue;
                SeqNote n;
                n.channelIndex = chIt->second;
                n.key          = note[ids::key];
                n.startTicks   = note[ids::startTicks];
                n.lengthTicks  = note[ids::lengthTicks];
                n.velocity     = (float) (double) note[ids::velocity];
                n.pan          = (float) (double) note[ids::notePan];
                ps.notes.push_back (n);
            }
        }
        std::sort (ps.notes.begin(), ps.notes.end(),
                   [] (const SeqNote& a, const SeqNote& b) { return a.startTicks < b.startTicks; });
        snap->patterns.push_back (std::move (ps));
    }

    const int activeId = root[ids::activePattern];
    if (auto it = patternIdToIndex.find (activeId); it != patternIdToIndex.end())
        snap->activePatternIndex = it->second;
    else
        snap->activePatternIndex = snap->patterns.empty() ? -1 : 0;

    // --- mixer ---
    const auto mixerTree = model.mixer();
    const int numInserts = juce::jmin (mixerTree.getNumChildren(), AudioEngine::maxInserts);
    for (int i = 0; i < numInserts; ++i)
    {
        const auto ins = mixerTree.getChild (i);
        InsertSnapshot is;
        is.volume = (float) (double) ins[ids::volume];
        is.pan    = (float) (double) ins[ids::pan];
        is.mute   = ins[ids::mute];
        for (const auto send : ins.getChildWithName (ids::SENDS))
        {
            SendSnapshot ss;
            ss.destInsert = send[ids::destInsert];
            ss.level      = (float) (double) send[ids::level];
            if (ss.destInsert >= 0 && ss.destInsert < numInserts)
                is.sends.push_back (ss);
        }

        // Effect chain: slots in index order, skipping bypassed/empty ones.
        std::vector<juce::ValueTree> slots;
        for (const auto slot : ins)
            if (slot.hasType (ids::SLOT))
                slots.push_back (slot);
        std::sort (slots.begin(), slots.end(),
                   [] (const juce::ValueTree& a, const juce::ValueTree& b)
                   { return (int) a[ids::slotIndex] < (int) b[ids::slotIndex]; });
        for (const auto& slot : slots)
        {
            if ((bool) slot[ids::bypass])
                continue;
            const auto pluginId = slot[ids::pluginId].toString();
            if (pluginId.isEmpty())
                continue;
            if (fx::isBuiltinId (pluginId))
            {
                if (auto effect = builtins.getReady (i, slot[ids::slotIndex], pluginId, slot))
                    is.effects.push_back (std::move (effect));
            }
            else if (auto effect = effects.getReady (i, slot[ids::slotIndex], pluginId,
                                                     slot[ids::pluginState].toString()))
            {
                is.effects.push_back (std::move (effect));
            }
        }

        snap->inserts.push_back (std::move (is));
    }

    // Topological order over send edges (src processed before its dest).
    {
        std::vector<int> inDegree ((size_t) numInserts, 0);
        for (int src = 0; src < numInserts; ++src)
            for (const auto& s : snap->inserts[(size_t) src].sends)
                ++inDegree[(size_t) s.destInsert];

        std::vector<int> queue;
        for (int i = 0; i < numInserts; ++i)
            if (inDegree[(size_t) i] == 0)
                queue.push_back (i);

        auto& order = snap->insertOrder;
        for (size_t head = 0; head < queue.size(); ++head)
        {
            const int n = queue[head];
            order.push_back (n);
            for (const auto& s : snap->inserts[(size_t) n].sends)
                if (--inDegree[(size_t) s.destInsert] == 0)
                    queue.push_back (s.destInsert);
        }

        if ((int) order.size() != numInserts)   // cycle: fall back to index order
        {
            order.clear();
            for (int i = 1; i < numInserts; ++i) order.push_back (i);
            order.push_back (0);
        }
    }

    // --- automations ---
    std::map<int, int> automationIdToIndex;
    for (const auto automation : model.automations())
    {
        if (! automation.hasType (ids::AUTOMATION))
            continue;

        AutomationSnapshot as;
        const auto targetType = automation[ids::targetType].toString();
        const auto paramId    = automation[ids::paramId].toString();
        const int  targetId   = automation[ids::targetId];
        bool valid = false;

        if (targetType == "channel")
        {
            if (auto it = channelIdToIndex.find (targetId); it != channelIdToIndex.end())
            {
                as.channelIndex = it->second;
                as.kind = paramId == "pan" ? AutomationSnapshot::Kind::channelPan
                                           : AutomationSnapshot::Kind::channelVolume;
                valid = true;
            }
        }
        else if (targetType == "insert")
        {
            if (targetId >= 0 && targetId < numInserts)
            {
                as.insertIndex = targetId;
                as.kind = paramId == "pan" ? AutomationSnapshot::Kind::insertPan
                                           : AutomationSnapshot::Kind::insertVolume;
                valid = true;
            }
        }
        else if (targetType == "channel-param")
        {
            const auto channel = model.getChannelById (targetId);
            if (channel.isValid())
                if (const auto* descriptor = channelparams::find (channel[ids::type].toString(), paramId))
                    if (auto generator = generators.getOrCreate (channel))
                        if (auto* atomicValue = generator->getAutomatableParam (paramId))
                        {
                            as.kind = AutomationSnapshot::Kind::generatorParam;
                            as.genParam = atomicValue;
                            as.genRange = descriptor->floatRange();
                            as.genKeepAlive = std::move (generator);
                            valid = true;
                        }
        }
        else if (targetType == "project" && paramId == "swing")
        {
            as.kind = AutomationSnapshot::Kind::projectSwing;
            valid = true;
        }
        else if (targetType == "plugin-channel")
        {
            const auto channel = model.getChannelById (targetId);
            const auto generator = channel.isValid() ? generators.getOrCreate (channel)
                                                     : nullptr;
            if (auto gen = std::dynamic_pointer_cast<PluginGenerator> (generator))
            {
                if (auto hosted = gen->getPlugin())
                {
                    const int paramIndex = paramId.getIntValue();
                    const auto& params = hosted->getInstance()->getParameters();
                    if (paramIndex >= 0 && paramIndex < params.size())
                    {
                        as.kind = AutomationSnapshot::Kind::pluginParam;
                        as.param = params[paramIndex];
                        as.keepAlive = hosted;
                        valid = true;
                    }
                }
            }
            else if (auto sandboxGen = std::dynamic_pointer_cast<SandboxedGenerator> (generator))
            {
                if (auto sandboxed = sandboxGen->getPlugin())
                {
                    const int paramIndex = paramId.getIntValue();
                    if (paramIndex >= 0 && paramIndex < sandboxed->getParamNames().size())
                    {
                        as.kind = AutomationSnapshot::Kind::sandboxParam;
                        as.sandboxed = sandboxed;
                        as.sandboxParamIndex = paramIndex;
                        valid = true;
                    }
                }
            }
        }
        else if (targetType == "builtin-insert")
        {
            const int slot = paramId.upToFirstOccurrenceOf (":", false, false).getIntValue();
            const auto name = paramId.fromFirstOccurrenceOf (":", false, false);
            if (auto effect = builtins.peek (targetId, slot))
                for (const auto& spec : effect->getParamSpecs())
                    if (spec.id.toString() == name)
                    {
                        as.kind = AutomationSnapshot::Kind::builtinParam;
                        as.builtinEffect = effect.get();
                        as.builtinSpec = &spec;
                        as.keepAlive = effect;
                        valid = true;
                        break;
                    }
        }
        else if (targetType == "plugin-insert")
        {
            const int slot = paramId.upToFirstOccurrenceOf (":", false, false).getIntValue();
            const int paramIndex = paramId.fromFirstOccurrenceOf (":", false, false).getIntValue();
            if (auto hosted = effects.peek (targetId, slot))
            {
                const auto& params = hosted->getInstance()->getParameters();
                if (paramIndex >= 0 && paramIndex < params.size())
                {
                    as.kind = AutomationSnapshot::Kind::pluginParam;
                    as.param = params[paramIndex];
                    as.keepAlive = hosted;
                    valid = true;
                }
            }
            else if (auto sandboxed = effects.peekSandboxed (targetId, slot))
            {
                if (paramIndex >= 0 && paramIndex < sandboxed->getParamNames().size())
                {
                    as.kind = AutomationSnapshot::Kind::sandboxParam;
                    as.sandboxed = sandboxed;
                    as.sandboxParamIndex = paramIndex;
                    valid = true;
                }
            }
        }

        if (! valid)
            continue;

        as.writing = (bool) automation[ids::writing];

        for (const auto point : automation)
            if (point.hasType (ids::POINT))
                as.points.push_back ({ (double) (int) point[ids::posTicks],
                                       (float) (double) point[ids::value],
                                       (float) (double) point[ids::tension] });
        std::sort (as.points.begin(), as.points.end(),
                   [] (const AutomationPoint& a, const AutomationPoint& b)
                   { return a.posTicks < b.posTicks; });

        automationIdToIndex[(int) automation[ids::id]] = (int) snap->automations.size();
        snap->automations.push_back (std::move (as));
    }

    // --- playlist clips ---
    for (const auto track : model.playlist())
    {
        if ((bool) track[ids::mute])
            continue;
        for (const auto clip : track)
        {
            if (! clip.hasType (ids::CLIP) || (bool) clip[ids::muted])
                continue;
            ClipSnapshot cs;
            const juce::String type = clip[ids::clipType].toString();
            if (type == "pattern")
            {
                cs.type = ClipSnapshot::Type::pattern;
                const auto it = patternIdToIndex.find ((int) clip[ids::patternId]);
                if (it == patternIdToIndex.end())
                    continue;
                cs.patternIndex = it->second;
            }
            else if (type == "automation")
            {
                cs.type = ClipSnapshot::Type::automation;
                const auto it = automationIdToIndex.find ((int) clip[ids::automationId]);
                if (it == automationIdToIndex.end())
                    continue;
                cs.automationIndex = it->second;
            }
            else if (type == "audio")
            {
                cs.type = ClipSnapshot::Type::audio;
                const double ratio = clip.hasProperty (ids::stretchRatio)
                                         ? (double) clip[ids::stretchRatio] : 1.0;
                cs.audio = audioClips.getStretched (clip[ids::audioPath].toString(), ratio,
                                                    AudioClipCache::modeFrom ((int) clip[ids::stretchMode]));
                if (cs.audio == nullptr)
                    continue;
                const double tps = (snap->tempo / 60.0) * ids::ticksPerQuarter
                                       / audioClips.getEngineSampleRate();
                cs.audioOffsetSamples = (double) (int) clip[ids::audioOffsetTicks] / tps;
            }
            else
                continue;

            cs.startTicks  = clip[ids::startTicks];
            cs.lengthTicks = clip[ids::lengthTicks];
            snap->songLengthTicks = juce::jmax (snap->songLengthTicks,
                                                cs.startTicks + cs.lengthTicks);
            snap->clips.push_back (cs);
        }
    }
    std::sort (snap->clips.begin(), snap->clips.end(),
               [] (const ClipSnapshot& a, const ClipSnapshot& b) { return a.startTicks < b.startTicks; });

    return snap;
}
