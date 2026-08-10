#include "DrumMachineGenerator.h"
#include "DrumSynth.h"
#include "NotePan.h"

void DrumMachineGenerator::prepare (double sampleRate, int maxBlockSize)
{
    juce::ignoreUnused (maxBlockSize);
    deviceSampleRate = sampleRate;
    chokeFadeCoef = (float) std::exp (-1.0 / (0.003 * sampleRate));
    reset();
}

void DrumMachineGenerator::reset()
{
    for (auto& v : voices)
        v.active = false;
}

std::shared_ptr<const DrumMachineGenerator::Sample> DrumMachineGenerator::getPadSample (int pad) const
{
    const juce::SpinLock::ScopedLockType sl (sampleLock);
    return pads[(size_t) pad].sample;
}

void DrumMachineGenerator::storePadSample (int pad, std::shared_ptr<const Sample> sample)
{
    const juce::SpinLock::ScopedLockType sl (sampleLock);
    pads[(size_t) pad].sample = std::move (sample);
}

bool DrumMachineGenerator::padHasSample (int pad) const
{
    return juce::isPositiveAndBelow (pad, maxPads) && getPadSample (pad) != nullptr;
}

double DrumMachineGenerator::getPadLengthSeconds (int pad) const
{
    if (! juce::isPositiveAndBelow (pad, maxPads))
        return 0.0;
    if (auto sample = getPadSample (pad))
        return sample->data.getNumSamples() / sample->sourceSampleRate;
    return 0.0;
}

void DrumMachineGenerator::setPadSource (int pad, const juce::String& samplePath,
                                         const juce::String& synthKind)
{
    if (! juce::isPositiveAndBelow (pad, maxPads))
        return;

    const juce::String wanted = samplePath.isNotEmpty() ? samplePath
                              : synthKind.isNotEmpty() ? "synth:" + synthKind
                              : juce::String();
    auto& p = pads[(size_t) pad];
    if (p.source == wanted && (wanted.isEmpty() || p.sample != nullptr))
        return;
    p.source = wanted;

    if (wanted.isEmpty())
    {
        storePadSample (pad, nullptr);
        return;
    }

    auto sample = std::make_shared<Sample>();
    if (samplePath.isNotEmpty())
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (
            formats.createReaderFor (juce::File (samplePath)));
        if (reader == nullptr || reader->lengthInSamples <= 0)
        {
            storePadSample (pad, nullptr);
            return;
        }
        const int numSamples = (int) juce::jmin<juce::int64> (
            reader->lengthInSamples, 60 * (juce::int64) reader->sampleRate);
        sample->data.setSize (2, numSamples);
        reader->read (&sample->data, 0, numSamples, 0, true, true);
        if (reader->numChannels == 1)
            sample->data.copyFrom (1, 0, sample->data, 0, 0, numSamples);
        sample->sourceSampleRate = reader->sampleRate;
    }
    else
    {
        sample->data = drumsynth::render (synthKind, deviceSampleRate);
        sample->sourceSampleRate = deviceSampleRate;
    }

    storePadSample (pad, std::move (sample));
}

void DrumMachineGenerator::triggerNote (int key, float velocity)
{
    const int count = numPads.load();
    for (int padIndex = 0; padIndex < count; ++padIndex)
    {
        auto& pad = pads[(size_t) padIndex];
        if (pad.params.key.load() != key)
            continue;

        std::shared_ptr<const Sample> sample;
        {
            const juce::SpinLock::ScopedTryLockType tl (sampleLock);
            if (! tl.isLocked())
                continue;
            sample = pad.sample;
        }

        pad.triggerCount.fetch_add (1, std::memory_order_relaxed);
        if (sample == nullptr || sample->data.getNumSamples() < 4)
            continue;

        // Choke first, so a pad in its own group restarts cleanly too.
        const int group = pad.params.choke.load();
        if (group != 0)
            for (auto& v : voices)
                if (v.active && ! v.choked
                    && pads[(size_t) v.padIndex].params.choke.load() == group)
                    v.choked = true;

        Voice* slot = nullptr;
        for (auto& v : voices)
            if (! v.active) { slot = &v; break; }
        if (slot == nullptr)
        {
            // Steal the voice furthest into its sample.
            slot = &voices[0];
            for (auto& v : voices)
                if (v.pos > slot->pos)
                    slot = &v;
        }

        slot->padIndex = padIndex;
        slot->pos = 0.0;
        slot->rate = std::exp2 ((double) pad.params.tune.load() / 12.0)
                     * sample->sourceSampleRate / deviceSampleRate;

        const float notePan = pendingPan;
        float panL = 1.0f, panR = 1.0f;
        notepan::gains (juce::jlimit (-1.0f, 1.0f, pad.params.pan.load() + notePan), panL, panR);
        const float gain = velocity * pad.params.gain.load();
        slot->gainL = gain * panL;
        slot->gainR = gain * panR;

        slot->choked = false;
        slot->fade = 1.0f;
        slot->sample = std::move (sample);
        slot->active = true;
    }
    pendingPan = 0.0f;   // consumed: the next un-CC'd note plays centred
}

void DrumMachineGenerator::render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi)
{
    const int numSamples = out.getNumSamples();
    int segmentStart = 0;

    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        const int pos = juce::jlimit (0, numSamples, meta.samplePosition);
        renderSegment (out, segmentStart, pos);
        segmentStart = pos;

        if (msg.isNoteOn())
            triggerNote (msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isController() && msg.getControllerNumber() == notepan::controller)
            pendingPan = notepan::fromController (msg.getControllerValue());
        // note-offs are ignored: pads are one-shots
    }
    renderSegment (out, segmentStart, numSamples);
}

void DrumMachineGenerator::renderSegment (juce::AudioBuffer<float>& out, int from, int to)
{
    if (to <= from)
        return;

    auto* l = out.getWritePointer (0);
    auto* r = out.getWritePointer (1);

    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        const auto& data = v.sample->data;
        const float* srcL = data.getReadPointer (0);
        const float* srcR = data.getReadPointer (1);
        const double lastFrame = data.getNumSamples() - 2.0;

        for (int i = from; i < to; ++i)
        {
            if (v.pos >= lastFrame || (v.choked && v.fade < 1.0e-3f))
            {
                v.active = false;
                break;
            }

            const int idx = (int) v.pos;
            const float frac = (float) (v.pos - idx);
            const float sl = srcL[idx] + frac * (srcL[idx + 1] - srcL[idx]);
            const float sr = srcR[idx] + frac * (srcR[idx + 1] - srcR[idx]);

            if (v.choked)
                v.fade *= chokeFadeCoef;

            l[i] += sl * v.gainL * v.fade;
            r[i] += sr * v.gainR * v.fade;
            v.pos += v.rate;
        }
    }
}
