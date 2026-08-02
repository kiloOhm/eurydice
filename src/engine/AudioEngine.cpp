#include "AudioEngine.h"
#include "model/Ids.h"
#include "sandbox/SandboxedPlugin.h"

AudioEngine::AudioEngine()
{
    startTimer (500);
}

AudioEngine::~AudioEngine()
{
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}

juce::String AudioEngine::initialise()
{
    // Output only. Opening the default input too usually means a *different*
    // hardware device (mic vs speakers), which makes JUCE build an
    // AudioIODeviceCombiner around a private aggregate device — and creating
    // that aggregate fires a device-list notification that can re-enter the
    // combiner's restart path from a CoreAudio dispatch thread while it is
    // still being set up. That race corrupted the heap and crashed most
    // launches. Input is enabled on demand when recording arms.
    // EURYDICE_DUPLEX=1 opens input+output at startup — the configuration
    // that used to crash — so the combiner can be re-stress-tested after
    // JUCE upgrades without a code change.
    const bool duplex = juce::SystemStats::getEnvironmentVariable ("EURYDICE_DUPLEX", "") == "1";
    auto err = deviceManager.initialiseWithDefaultDevices (duplex ? 2 : 0, 2);
    deviceManager.addAudioCallback (this);
    return err;
}

juce::String AudioEngine::setInputEnabled (bool enabled)
{
    auto setup = deviceManager.getAudioDeviceSetup();
    const bool hasInput = setup.inputChannels.countNumberOfSetBits() > 0;
    if (enabled == hasInput)
        return {};

    if (enabled)
    {
        if (auto* type = deviceManager.getCurrentDeviceTypeObject())
        {
            const auto names = type->getDeviceNames (true);
            const int def = type->getDefaultDeviceIndex (true);
            if (names.isEmpty())
                return "no input devices available";
            setup.inputDeviceName = names[juce::jlimit (0, names.size() - 1, def)];
        }
        setup.useDefaultInputChannels = true;
    }
    else
    {
        setup.inputDeviceName.clear();
        setup.useDefaultInputChannels = false;
        setup.inputChannels.clear();
    }
    return deviceManager.setAudioDeviceSetup (setup, true);
}

bool AudioEngine::isInputEnabled() const
{
    return deviceManager.getAudioDeviceSetup().inputChannels.countNumberOfSetBits() > 0;
}

void AudioEngine::publishSnapshot (std::shared_ptr<const EngineSnapshot> snapshot)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    ++pendingGeneration;
    history.emplace_back (pendingGeneration, snapshot);

    const juce::SpinLock::ScopedLockType sl (pendingLock);
    pendingSnapshot = std::move (snapshot);
    snapshotDirty.store (true, std::memory_order_release);
}

void AudioEngine::timerCallback()
{
    // Drop history entries the audio thread has moved past. It holds a ref
    // to the generation it adopted, so freeing older ones is safe here.
    const auto gen = audioGeneration.load (std::memory_order_acquire);
    while (history.size() > 1 && history.front().first < gen)
        history.pop_front();
}

void AudioEngine::previewNote (int channelId, int key, float velocity, int durationMs)
{
    // Producers: message thread and CoreMIDI thread — serialise the writes.
    const juce::SpinLock::ScopedLockType sl (previewWriteLock);
    const auto scope = previewFifo.write (1);
    if (scope.blockSize1 > 0)
        previewQueue[(size_t) scope.startIndex1] = { channelId, key, velocity,
                                                     (int) (durationMs * 0.001 * sampleRate), true };
}

void AudioEngine::previewNoteOff (int channelId, int key)
{
    const juce::SpinLock::ScopedLockType sl (previewWriteLock);
    const auto scope = previewFifo.write (1);
    if (scope.blockSize1 > 0)
        previewQueue[(size_t) scope.startIndex1] = { channelId, key, 0.0f, 0, false };
}

void AudioEngine::processPreviewEvents (const EngineSnapshot& snap, int numSamples)
{
    auto indexForChannelId = [&snap] (int channelId) -> int
    {
        for (int i = 0; i < (int) snap.channels.size(); ++i)
            if (snap.channels[(size_t) i].id == channelId)
                return i;
        return -1;
    };

    // Drain queued on/off events.
    for (;;)
    {
        const auto scope = previewFifo.read (1);
        if (scope.blockSize1 + scope.blockSize2 == 0)
            break;
        const auto& ev = previewQueue[(size_t) scope.startIndex1];

        const int chIndex = indexForChannelId (ev.channelId);
        if (chIndex < 0 || chIndex >= maxChannels)
            continue;

        if (ev.isOn)
        {
            channelMidi[(size_t) chIndex].addEvent (juce::MidiMessage::noteOn (1, ev.key, ev.velocity), 0);
            for (auto& pa : previewActive)
                if (pa.channelIndex < 0)
                {
                    pa = { chIndex, ev.channelId, ev.key,
                           ev.durationSamples > 0 ? ev.durationSamples : std::numeric_limits<int>::max() };
                    break;
                }
        }
        else
        {
            for (auto& pa : previewActive)
                if (pa.channelIndex >= 0 && pa.channelId == ev.channelId && pa.key == ev.key)
                {
                    channelMidi[(size_t) pa.channelIndex].addEvent (juce::MidiMessage::noteOff (1, pa.key), 0);
                    pa.channelIndex = -1;
                }
        }
    }

    // Expire timed previews.
    for (auto& pa : previewActive)
    {
        if (pa.channelIndex < 0)
            continue;
        if (pa.samplesLeft != std::numeric_limits<int>::max())
        {
            pa.samplesLeft -= numSamples;
            if (pa.samplesLeft <= 0)
            {
                if (pa.channelIndex < (int) snap.channels.size())
                    channelMidi[(size_t) pa.channelIndex].addEvent (juce::MidiMessage::noteOff (1, pa.key),
                                                                    juce::jmax (0, numSamples - 1));
                pa.channelIndex = -1;
            }
        }
    }
}

void AudioEngine::play()                     { playing.store (true); }
void AudioEngine::playWithCountIn()
{
    if (countInBars.load() > 0)
        countInRequest.store (true);
    playing.store (true);
}
void AudioEngine::stop()
{
    countInRequest.store (false);
    if (playing.exchange (false))
        stopRequest.store (true);
    else
        seekRequest.store (0.0);   // second stop rewinds
}
void AudioEngine::togglePlayStop()           { if (isPlaying()) stop(); else play(); }
void AudioEngine::setPositionTicks (double t){ seekRequest.store (juce::jmax (0.0, t)); }

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    prepareInternal (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
}

void AudioEngine::prepareInternal (double newSampleRate, int newBlockSize)
{
    sampleRate = newSampleRate;
    blockSize  = newBlockSize;

    channelScratch.setSize (2, blockSize);

    insertBus.resize (maxInserts);
    for (auto& bus : insertBus)
        bus.setSize (2, blockSize);

    channelStemBus.resize (maxChannels);
    for (auto& bus : channelStemBus)
        bus.setSize (2, blockSize);

    channelMidi.resize (maxChannels);
    for (auto& mb : channelMidi)
        mb.ensureSize (4096);

    activeNotes.fill ({});
}

void AudioEngine::audioDeviceStopped() {}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                                    float* const* outputChannelData, int numOutputChannels,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    // Feed the recorder first, straight from the hardware input.
    if (auto* rec = recorder.load (std::memory_order_acquire);
        rec != nullptr && numInputChannels > 0 && inputChannelData != nullptr)
    {
        const float* channels[2] = {
            inputChannelData[0],
            numInputChannels > 1 ? inputChannelData[1] : inputChannelData[0]
        };
        rec->write (channels, numSamples);
    }

    // Adopt a new snapshot if one is pending (never blocks).
    if (snapshotDirty.load (std::memory_order_acquire))
    {
        const juce::SpinLock::ScopedTryLockType tl (pendingLock);
        if (tl.isLocked())
        {
            currentSnapshot = pendingSnapshot;   // copy; history keeps old alive
            snapshotDirty.store (false, std::memory_order_release);
            audioGeneration.fetch_add (1, std::memory_order_release);
        }
    }

    currentBlockSamples = numSamples;

    for (int ch = 0; ch < numOutputChannels; ++ch)
        juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);

    const auto* snap = currentSnapshot.get();
    if (snap == nullptr || numOutputChannels < 1)
        return;

    // --- transport events ---
    if (stopRequest.exchange (false))
    {
        allNotesOff (*snap);
        // keep position (FL: stop keeps pos at 0 anyway since we rewind below on 2nd stop)
        tickPos = 0.0;
        countInTicksLeft = 0.0;
    }
    const double seek = seekRequest.exchange (-1.0);
    if (seek >= 0.0)
    {
        allNotesOff (*snap);
        tickPos = seek;
    }
    if (countInRequest.exchange (false))
    {
        countInTicksLeft = (double) countInBars.load() * ids::ticksPerBar;
        countInTick = 0.0;
    }
    clickStartSample = -1;

    const int numChannels = juce::jmin ((int) snap->channels.size(), maxChannels);
    for (int i = 0; i < numChannels; ++i)
        channelMidi[(size_t) i].clear();

    // Cleared before sequencing so audio clips can be mixed straight into the
    // master bus per sub-range, ahead of the channels adding their output.
    const int numInserts = juce::jmin ((int) snap->inserts.size(), maxInserts);
    for (int i = 0; i < numInserts; ++i)
        insertBus[(size_t) i].clear (0, numSamples);

    // Automation overrides for this block.
    channelVolAuto.fill (-1000.0f);
    channelPanAuto.fill (-1000.0f);
    insertVolAuto.fill (-1000.0f);
    insertPanAuto.fill (-1000.0f);

    const double tps = (snap->tempo / 60.0) * (double) ids::ticksPerQuarter / sampleRate; // ticks per sample
    const bool isPlayingNow = playing.load (std::memory_order_relaxed);
    const double blockStartTick = tickPos;

    if (isPlayingNow)
    {
        const double loopEnd = (double) snap->loopEndTicks;
        const bool loopActive = snap->loopEnabled && ! loopBypassed.load (std::memory_order_relaxed);
        int done = 0;

        // Count-in: the transport stands still, only the click runs.
        if (countInTicksLeft > 0.0)
        {
            const int countInSamples = (int) std::ceil (countInTicksLeft / tps);
            done = juce::jmin (numSamples, countInSamples);
            const double consumed = done * tps;
            scheduleClicks (countInTick, countInTick + consumed, tps, 0);
            countInTick += consumed;
            countInTicksLeft = juce::jmax (0.0, countInTicksLeft - consumed);
        }

        // The block is sequenced in sub-ranges cut at the loop end, so the
        // wrap lands on its exact sample instead of the next block boundary.
        while (done < numSamples)
        {
            int chunk = numSamples - done;
            bool wrapAfterChunk = false;

            if (loopActive && tickPos < loopEnd)
            {
                const int samplesToLoopEnd = juce::jmax (1, (int) std::ceil ((loopEnd - tickPos) / tps));
                if (samplesToLoopEnd <= chunk)
                {
                    chunk = samplesToLoopEnd;
                    wrapAfterChunk = true;
                }
            }

            const double t0 = tickPos;
            const double t1 = t0 + tps * chunk;
            blockSampleBase = done;

            if (metronomeEnabled.load (std::memory_order_relaxed))
                scheduleClicks (t0, t1, tps, done);

            flushNoteOffs (*snap, t0, t1, tps);
            generateMidiForRange (*snap, t0, t1, tps);
            if (snap->songMode)
            {
                applyAutomation (*snap, t0);
                mixAudioClips (*snap, t0, t1, tps, done, chunk);
            }

            tickPos = t1;

            if (wrapAfterChunk)
            {
                // Release what is still sounding so notes at the loop start
                // retrigger instead of being cut mid-pass by a stale note-off.
                releaseActiveNotes (*snap, done + chunk - 1);
                tickPos = (double) snap->loopStartTicks;
            }
            else if (! loopActive && ! snap->songMode
                     && snap->activePatternIndex >= 0
                     && snap->activePatternIndex < (int) snap->patterns.size())
            {
                // Wrap in pattern mode so the position readout loops.
                const auto len = (double) snap->patterns[(size_t) snap->activePatternIndex].lengthTicks;
                if (len > 0 && tickPos >= len)
                    tickPos = std::fmod (tickPos, len);
            }

            done += chunk;
        }
    }

    publishedTickPos.store (tickPos, std::memory_order_relaxed);
    countingIn.store (isPlayingNow && countInTicksLeft > 0.0, std::memory_order_relaxed);

    processPreviewEvents (*snap, numSamples);

    // --- render channels into insert buses ---
    for (int i = 0; i < numChannels; ++i)
    {
        const auto& ch = snap->channels[(size_t) i];
        if (channelStemCapture)
            channelStemBus[(size_t) i].clear (0, numSamples);
        if (ch.generator == nullptr)
            continue;

        channelScratch.clear (0, numSamples);
        juce::AudioBuffer<float> scratchView (channelScratch.getArrayOfWritePointers(), 2, 0, numSamples);
        ch.generator->render (scratchView, channelMidi[(size_t) i]);

        if (! ch.audible)
            continue;

        const float gain = channelVolAuto[(size_t) i] > -100.0f ? channelVolAuto[(size_t) i] : ch.volume;
        const float pan  = channelPanAuto[(size_t) i] > -100.0f ? channelPanAuto[(size_t) i] : ch.pan;
        const float panL = std::cos ((pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi) * juce::MathConstants<float>::sqrt2;
        const float panR = std::sin ((pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi) * juce::MathConstants<float>::sqrt2;

        auto& bus = insertBus[(size_t) juce::jlimit (0, numInserts - 1, ch.insertIndex)];
        bus.addFrom (0, 0, scratchView, 0, 0, numSamples, gain * panL);
        bus.addFrom (1, 0, scratchView, 1, 0, numSamples, gain * panR);

        if (channelStemCapture)
        {
            auto& stem = channelStemBus[(size_t) i];
            stem.addFrom (0, 0, scratchView, 0, 0, numSamples, gain * panL);
            stem.addFrom (1, 0, scratchView, 1, 0, numSamples, gain * panR);
        }
    }

    // --- process inserts in topological order (master index 0 comes last) ---
    Effect::Context effectContext;
    effectContext.tempo = snap->tempo;
    effectContext.ppqPosition = blockStartTick / (double) ids::ticksPerQuarter;
    effectContext.playing = isPlayingNow;

    for (int idx : snap->insertOrder)
    {
        if (idx < 0 || idx >= numInserts)
            continue;
        const auto& ins = snap->inserts[(size_t) idx];
        auto& bus = insertBus[(size_t) idx];

        for (const auto& effect : ins.effects)
        {
            const int sidechain = effect->getSidechainInsert();
            effectContext.sidechain = (sidechain >= 0 && sidechain < numInserts && sidechain != idx)
                                          ? &insertBus[(size_t) sidechain] : nullptr;
            effect->process (bus, numSamples, effectContext);
        }

        const float vol = insertVolAuto[(size_t) idx] > -100.0f ? insertVolAuto[(size_t) idx] : ins.volume;
        const float bal = insertPanAuto[(size_t) idx] > -100.0f ? insertPanAuto[(size_t) idx] : ins.pan;
        const float g = ins.mute ? 0.0f : vol;
        bus.applyGain (0, 0, numSamples, g * juce::jmin (1.0f, 1.0f - bal));
        bus.applyGain (1, 0, numSamples, g * juce::jmin (1.0f, 1.0f + bal));

        insertPeaks[(size_t) idx * 2].store (bus.getMagnitude (0, 0, numSamples), std::memory_order_relaxed);
        insertPeaks[(size_t) idx * 2 + 1].store (bus.getMagnitude (1, 0, numSamples), std::memory_order_relaxed);

        if (idx == 0)
            continue;   // master doesn't send anywhere

        for (const auto& send : ins.sends)
        {
            if (send.destInsert < 0 || send.destInsert >= numInserts || send.destInsert == idx)
                continue;
            auto& dest = insertBus[(size_t) send.destInsert];
            dest.addFrom (0, 0, bus, 0, 0, numSamples, send.level);
            dest.addFrom (1, 0, bus, 1, 0, numSamples, send.level);
        }
    }

    // --- master to hardware ---
    auto& master = insertBus[0];
    for (int ch = 0; ch < juce::jmin (2, numOutputChannels); ++ch)
    {
        juce::FloatVectorOperations::copy (outputChannelData[ch], master.getReadPointer (ch), numSamples);
        masterPeak[ch].store (master.getMagnitude (ch, 0, numSamples), std::memory_order_relaxed);
    }

    renderMetronome (outputChannelData, numOutputChannels, numSamples);
}

void AudioEngine::scheduleClicks (double t0, double t1, double tps, int sampleBase)
{
    const double beat = (double) ids::ticksPerQuarter;
    const double firstBeat = std::ceil (t0 / beat) * beat;
    if (firstBeat >= t1)
        return;

    clickStartSample = juce::jlimit (0, juce::jmax (0, currentBlockSamples - 1),
                                     sampleBase + (int) ((firstBeat - t0) / tps));

    // The downbeat is higher and louder so bars are audible without counting.
    const bool accent = std::fmod (firstBeat, (double) ids::ticksPerBar) < 0.5;
    clickPhaseDelta = 2.0 * juce::MathConstants<double>::pi * (accent ? 1600.0 : 1000.0) / sampleRate;
    clickPhase = 0.0;
    clickEnv = accent ? 1.0 : 0.7;
    clickDecay = std::exp (-1.0 / (0.02 * sampleRate));
}

void AudioEngine::renderMetronome (float* const* outs, int numOuts, int numSamples)
{
    if (numOuts < 1 || (clickStartSample < 0 && clickEnv <= 0.0001))
        return;

    const auto level = (double) metronomeLevel.load (std::memory_order_relaxed);
    const int start = clickStartSample >= 0 ? clickStartSample : 0;

    for (int i = start; i < numSamples; ++i)
    {
        if (clickEnv <= 0.0001)
            break;
        const auto s = (float) (std::sin (clickPhase) * clickEnv * level);
        for (int ch = 0; ch < juce::jmin (2, numOuts); ++ch)
            outs[ch][i] += s;
        clickPhase += clickPhaseDelta;
        clickEnv *= clickDecay;
    }
}

void AudioEngine::mixAudioClips (const EngineSnapshot& snap, double t0, double t1, double tps,
                                 int startSample, int numChunkSamples)
{
    if (snap.inserts.empty())
        return;

    auto& masterBus = insertBus[0];   // pre-effects

    for (const auto& clip : snap.clips)
    {
        if (clip.type != ClipSnapshot::Type::audio || clip.audio == nullptr)
            continue;
        const double clipStart = clip.startTicks;
        const double clipEnd   = clipStart + clip.lengthTicks;
        if (clipEnd <= t0 || clipStart >= t1)
            continue;

        const auto& audio = *clip.audio;
        for (int i = 0; i < numChunkSamples; ++i)
        {
            const double tick = t0 + i * tps;
            if (tick < clipStart || tick >= clipEnd)
                continue;
            const auto sourcePos = (juce::int64) ((tick - clipStart) / tps + clip.audioOffsetSamples);
            if (sourcePos < 0 || sourcePos >= audio.getNumSamples())
                continue;
            masterBus.addSample (0, startSample + i, audio.getSample (0, (int) sourcePos) * clip.audioGain);
            masterBus.addSample (1, startSample + i, audio.getSample (1, (int) sourcePos) * clip.audioGain);
        }
    }
}

void AudioEngine::applyAutomation (const EngineSnapshot& snap, double tick)
{
    for (const auto& clip : snap.clips)
    {
        if (clip.type != ClipSnapshot::Type::automation || clip.automationIndex < 0)
            continue;
        if (tick < clip.startTicks || tick >= clip.startTicks + clip.lengthTicks)
            continue;

        const auto& automation = snap.automations[(size_t) clip.automationIndex];
        if (automation.writing)
            continue;

        const float value = automation.valueAt (tick - clip.startTicks);

        switch (automation.kind)
        {
            case AutomationSnapshot::Kind::channelVolume:
                if (automation.channelIndex >= 0 && automation.channelIndex < maxChannels)
                    channelVolAuto[(size_t) automation.channelIndex] = value;
                break;
            case AutomationSnapshot::Kind::channelPan:
                if (automation.channelIndex >= 0 && automation.channelIndex < maxChannels)
                    channelPanAuto[(size_t) automation.channelIndex] = value * 2.0f - 1.0f;
                break;
            case AutomationSnapshot::Kind::insertVolume:
                if (automation.insertIndex >= 0 && automation.insertIndex < maxInserts)
                    insertVolAuto[(size_t) automation.insertIndex] = value * 1.25f;
                break;
            case AutomationSnapshot::Kind::insertPan:
                if (automation.insertIndex >= 0 && automation.insertIndex < maxInserts)
                    insertPanAuto[(size_t) automation.insertIndex] = value * 2.0f - 1.0f;
                break;
            case AutomationSnapshot::Kind::pluginParam:
                if (automation.param != nullptr)
                    automation.param->setValueNotifyingHost (value);
                break;
            case AutomationSnapshot::Kind::sandboxParam:
                // Queued into the ring's event slots; the child applies them
                // before its next block. RT-safe on this side.
                if (automation.sandboxed != nullptr)
                    automation.sandboxed->setParameter (automation.sandboxParamIndex, value);
                break;
            case AutomationSnapshot::Kind::generatorParam:
                if (automation.genParam != nullptr)
                    automation.genParam->store (automation.genRange.convertFrom0to1 (value),
                                                std::memory_order_relaxed);
                break;

            case AutomationSnapshot::Kind::builtinParam:
                if (automation.builtinEffect != nullptr && automation.builtinSpec != nullptr)
                {
                    const auto& spec = *automation.builtinSpec;
                    const juce::NormalisableRange<double> range (spec.minValue, spec.maxValue,
                                                                 0.0, spec.skew);
                    automation.builtinEffect->setParameter (spec.id, range.convertFrom0to1 (value));
                }
                break;
        }
    }
}

void AudioEngine::generateMidiForRange (const EngineSnapshot& snap, double t0, double t1, double tps)
{
    if (snap.songMode)
    {
        for (const auto& clip : snap.clips)
        {
            if (clip.type != ClipSnapshot::Type::pattern || clip.patternIndex < 0)
                continue;
            const double clipStart = clip.startTicks;
            const double clipEnd   = clipStart + clip.lengthTicks;
            if (clipEnd <= t0 || clipStart >= t1)
                continue;

            const auto& pat = snap.patterns[(size_t) clip.patternIndex];
            // Pattern-relative window for this block (clips can be longer than
            // the pattern: it loops inside the clip, like FL).
            const double patLen = (double) pat.lengthTicks;
            double segStart = juce::jmax (t0, clipStart);
            double segEnd   = juce::jmin (t1, clipEnd);
            if (patLen <= 0 || segEnd <= segStart)
                continue;

            // Localise then possibly split across a loop boundary.
            double local0 = std::fmod (segStart - clipStart, patLen);
            double remaining = segEnd - segStart;
            double absTick = segStart;
            while (remaining > 0.0)
            {
                const double chunk = juce::jmin (remaining, patLen - local0);
                emitPatternSegment (snap, pat, local0, local0 + chunk, t0,
                                    (int) std::llround (absTick - local0), tps);
                remaining -= chunk;
                absTick   += chunk;
                local0 = 0.0;
            }
        }
        return;
    }

    if (snap.activePatternIndex < 0 || snap.activePatternIndex >= (int) snap.patterns.size())
        return;

    const auto& pat = snap.patterns[(size_t) snap.activePatternIndex];
    const double patLen = (double) pat.lengthTicks;
    if (patLen <= 0)
        return;

    double local0 = std::fmod (t0, patLen);
    double remaining = t1 - t0;
    double absTick = t0;
    while (remaining > 0.0)
    {
        const double chunk = juce::jmin (remaining, patLen - local0);
        emitPatternSegment (snap, pat, local0, local0 + chunk, t0,
                            (int) std::llround (absTick - local0), tps);
        remaining -= chunk;
        absTick   += chunk;
        local0 = 0.0;
    }
}

void AudioEngine::emitPatternSegment (const EngineSnapshot& snap, const PatternSnapshot& pat,
                                      double segStart, double segEnd, double blockStartTick,
                                      int tickOffsetToSong, double tps)
{
    const double swingShift = pat.swing * (double) ids::ticksPerStep * 0.5;

    for (const auto& note : pat.notes)
    {
        double start = (double) note.startTicks;

        // FL-style swing: delay every odd 16th step.
        const int stepIndex = note.startTicks / ids::ticksPerStep;
        if ((stepIndex & 1) != 0 && note.startTicks % ids::ticksPerStep == 0)
            start += swingShift;

        if (start < segStart || start >= segEnd)
            continue;
        if (note.channelIndex < 0 || note.channelIndex >= maxChannels
            || note.channelIndex >= (int) snap.channels.size())
            continue;

        const double songTick = start + tickOffsetToSong;
        const int sampleOffset = juce::jlimit (blockSampleBase,
                                               juce::jmax (blockSampleBase, currentBlockSamples - 1),
                                               blockSampleBase + (int) ((songTick - blockStartTick) / tps));
        const double offTick = songTick + note.lengthTicks;

        addNoteOn (note.channelIndex, note.key, note.velocity, sampleOffset, offTick);
    }
}

void AudioEngine::addNoteOn (int channelIndex, int key, float velocity, int sampleOffset, double offTick)
{
    auto& midi = channelMidi[(size_t) channelIndex];
    midi.addEvent (juce::MidiMessage::noteOn (1, key, velocity), sampleOffset);

    for (auto& slot : activeNotes)
    {
        if (slot.channelIndex < 0)
        {
            slot = { channelIndex, key, offTick };
            return;
        }
    }
    // Table full: oldest slot is silently overwritten; harmless for v1.
    activeNotes[0] = { channelIndex, key, offTick };
}

void AudioEngine::flushNoteOffs (const EngineSnapshot& snap, double t0, double t1, double tps)
{
    for (auto& slot : activeNotes)
    {
        if (slot.channelIndex < 0)
            continue;
        if (slot.offTick < t1)
        {
            if (slot.channelIndex < (int) snap.channels.size())
            {
                const int offset = juce::jlimit (blockSampleBase,
                                                 juce::jmax (blockSampleBase, currentBlockSamples - 1),
                                                 blockSampleBase + (int) ((slot.offTick - t0) / tps));
                channelMidi[(size_t) slot.channelIndex]
                    .addEvent (juce::MidiMessage::noteOff (1, slot.key), offset);
            }
            slot.channelIndex = -1;
        }
    }
}

void AudioEngine::releaseActiveNotes (const EngineSnapshot& snap, int sampleOffset)
{
    const int offset = juce::jlimit (0, juce::jmax (0, currentBlockSamples - 1), sampleOffset);

    for (auto& slot : activeNotes)
    {
        if (slot.channelIndex < 0)
            continue;
        if (slot.channelIndex < (int) snap.channels.size())
            channelMidi[(size_t) slot.channelIndex]
                .addEvent (juce::MidiMessage::noteOff (1, slot.key), offset);
        slot.channelIndex = -1;
    }
}

void AudioEngine::allNotesOff (const EngineSnapshot& snap)
{
    for (auto& slot : activeNotes)
        slot.channelIndex = -1;

    for (const auto& ch : snap.channels)
        if (ch.generator != nullptr)
            ch.generator->reset();
}
