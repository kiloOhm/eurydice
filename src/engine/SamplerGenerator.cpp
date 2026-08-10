#include "SamplerGenerator.h"
#include "DrumSynth.h"
#include "NotePan.h"
#include "Drive.h"

SamplerGenerator::SamplerGenerator() = default;

void SamplerGenerator::prepare (double sampleRate, int blockSize)
{
    deviceSampleRate = sampleRate;
    for (auto& v : voices)
    {
        v.env.setSampleRate (sampleRate);
        v.filter.prepare ({ sampleRate, (juce::uint32) juce::jmax (1, blockSize), 2 });
        v.filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        v.active = false;
    }
}

void SamplerGenerator::reset()
{
    for (auto& v : voices)
        v.active = false;
}

std::shared_ptr<const SamplerGenerator::Sample> SamplerGenerator::getSample() const
{
    const juce::SpinLock::ScopedLockType sl (sampleLock);
    return currentSample;
}

double SamplerGenerator::getSampleLengthSeconds() const
{
    if (auto sample = getSample())
        return sample->data.getNumSamples() / sample->sourceSampleRate;
    return 0.0;
}

std::vector<float> SamplerGenerator::getWaveformOutline (int numBuckets) const
{
    std::vector<float> outline;
    auto sample = getSample();
    if (sample == nullptr || numBuckets <= 0)
        return outline;

    const int total = sample->data.getNumSamples();
    if (total <= 0)
        return outline;

    outline.reserve ((size_t) numBuckets);
    const int perBucket = juce::jmax (1, total / numBuckets);
    for (int b = 0; b < numBuckets; ++b)
    {
        const int start = juce::jmin (total - 1, b * perBucket);
        const int len = juce::jmin (perBucket, total - start);
        outline.push_back (sample->data.getMagnitude (start, juce::jmax (1, len)));
    }
    return outline;
}

bool SamplerGenerator::loadSampleFile (const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;

    auto sample = std::make_shared<Sample>();
    const int numSamples = (int) juce::jmin<juce::int64> (reader->lengthInSamples, 60 * (juce::int64) reader->sampleRate);
    sample->data.setSize (2, numSamples);
    reader->read (&sample->data, 0, numSamples, 0, true, true);
    if (reader->numChannels == 1)
        sample->data.copyFrom (1, 0, sample->data, 0, 0, numSamples);
    sample->sourceSampleRate = reader->sampleRate;

    samplePath = file.getFullPathName();

    const juce::SpinLock::ScopedLockType sl (sampleLock);
    currentSample = std::move (sample);
    return true;
}

void SamplerGenerator::useSynthesizedDrum (const juce::String& kind, double sr)
{
    auto sample = std::make_shared<Sample>();
    sample->sourceSampleRate = sr;
    sample->data = drumsynth::render (kind, sr);

    const juce::SpinLock::ScopedLockType sl (sampleLock);
    currentSample = std::move (sample);
}

void SamplerGenerator::startVoice (int key, float velocity)
{
    std::shared_ptr<const Sample> sample;
    {
        const juce::SpinLock::ScopedTryLockType tl (sampleLock);
        if (! tl.isLocked())
            return;
        sample = currentSample;
    }
    if (sample == nullptr)
        return;

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

    // Interpolation reads pos and pos+1, so the region must end two frames
    // short of the buffer.
    const double lastFrame = sample->data.getNumSamples() - 2.0;
    if (lastFrame < 2.0)
        return;

    const double lo = juce::jlimit (0.0, lastFrame - 1.0,
                                    juce::jlimit (0.0f, 1.0f, p.sampleStart.load()) * lastFrame);
    const double hi = juce::jlimit (lo + 1.0, lastFrame,
                                    juce::jlimit (0.0f, 1.0f, p.sampleEnd.load()) * lastFrame);

    const double semis = key - rootNote.load();
    slot->rate     = std::pow (2.0, semis / 12.0) * sample->sourceSampleRate / deviceSampleRate;
    slot->startPos = lo;
    slot->endPos   = hi;
    slot->reverse  = p.reverse.load();
    slot->pos      = slot->reverse ? hi - 1.0 : lo;
    slot->key      = key;
    slot->velocity = velocity * p.gain.load();
    notepan::gains (pendingPan, slot->panL, slot->panR);
    pendingPan = 0.0f;   // consumed: the next un-CC'd note plays centred
    slot->sample   = std::move (sample);

    slot->pitchEnvDepth = p.pitchEnvDepth.load();
    slot->pitchEnv      = 1.0;
    slot->pitchEnvCoef  = std::exp (-1.0 / (juce::jmax (0.0005f, p.pitchEnvDecay.load()) * deviceSampleRate));

    slot->env.setSampleRate (deviceSampleRate);
    slot->env.setParameters ({ p.attack.load(), p.decay.load(),
                               p.sustain.load(), p.release.load() });
    slot->env.noteOn();

    slot->filter.reset();
    slot->filter.setCutoffFrequency (juce::jlimit (40.0f, 20000.0f, p.cutoff.load()));
    slot->filter.setResonance (juce::jmap (p.resonance.load(), 0.707f, 8.0f));

    slot->active = true;
}

void SamplerGenerator::stopVoice (int key)
{
    if (p.oneShot.load())
        return;   // one-shots ignore note-offs, like FL drum channels
    for (auto& v : voices)
        if (v.active && v.key == key)
            v.env.noteOff();
}

void SamplerGenerator::render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi)
{
    const int numSamples = out.getNumSamples();
    int segmentStart = 0;
    auto it = midi.begin();

    for (; it != midi.end(); ++it)
    {
        const auto meta = *it;
        const auto msg = meta.getMessage();
        const int pos = juce::jlimit (0, numSamples, meta.samplePosition);
        renderSegment (out, segmentStart, pos);
        segmentStart = pos;

        if (msg.isNoteOn())
            startVoice (msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff())
            stopVoice (msg.getNoteNumber());
        else if (msg.isController() && msg.getControllerNumber() == notepan::controller)
            pendingPan = notepan::fromController (msg.getControllerValue());
    }
    renderSegment (out, segmentStart, numSamples);
}

void SamplerGenerator::renderSegment (juce::AudioBuffer<float>& out, int from, int to)
{
    if (to <= from)
        return;

    auto* l = out.getWritePointer (0);
    auto* r = out.getWritePointer (1);
    const float cutoffHz = juce::jlimit (40.0f, 20000.0f, p.cutoff.load());
    const float reso = juce::jmap (p.resonance.load(), 0.707f, 8.0f);
    const float shape = juce::jlimit (0.0f, 1.0f, p.envShape.load());

    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        const auto& data = v.sample->data;
        const float* srcL = data.getReadPointer (0);
        const float* srcR = data.getReadPointer (1);

        v.filter.setCutoffFrequency (cutoffHz);
        v.filter.setResonance (reso);

        for (int i = from; i < to; ++i)
        {
            if (v.pos < v.startPos || v.pos >= v.endPos)
            {
                v.active = false;
                break;
            }

            const float envValue = v.env.getNextSample();
            if (! v.env.isActive())
            {
                v.active = false;
                break;
            }

            const int idx = (int) v.pos;
            const float frac = (float) (v.pos - idx);
            const float sl = (srcL[idx] + frac * (srcL[idx + 1] - srcL[idx]));
            const float sr = (srcR[idx] + frac * (srcR[idx + 1] - srcR[idx]));
            const float gain = drive::shapeEnvelope (envValue, shape) * v.velocity;

            l[i] += v.filter.processSample (0, sl) * gain * v.panL;
            r[i] += v.filter.processSample (1, sr) * gain * v.panR;

            double step = v.rate;
            if (v.pitchEnvDepth != 0.0f)
            {
                step *= std::exp2 (v.pitchEnvDepth * v.pitchEnv / 12.0);
                v.pitchEnv *= v.pitchEnvCoef;
            }
            v.pos += v.reverse ? -step : step;
        }
    }

    drive::processBlock (out, from, to, juce::jlimit (0.0f, 1.0f, p.drive.load()),
                         p.driveCurve.load());
}
