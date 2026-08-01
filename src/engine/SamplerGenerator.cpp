#include "SamplerGenerator.h"

SamplerGenerator::SamplerGenerator() = default;

void SamplerGenerator::prepare (double sampleRate, int)
{
    deviceSampleRate = sampleRate;
}

void SamplerGenerator::reset()
{
    for (auto& v : voices)
        v.active = false;
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

    juce::Random rng (0x5eed);
    const auto lower = kind.toLowerCase();

    auto makeBuffer = [&] (double seconds)
    {
        sample->data.setSize (2, (int) (seconds * sr));
        sample->data.clear();
    };

    if (lower.contains ("kick"))
    {
        makeBuffer (0.40);
        double phase = 0.0;
        for (int i = 0; i < sample->data.getNumSamples(); ++i)
        {
            const double t = i / sr;
            const double freq = 50.0 + 110.0 * std::exp (-t * 22.0);
            phase += freq * juce::MathConstants<double>::twoPi / sr;
            const float env = (float) std::exp (-t * 9.0);
            const float click = (float) (std::exp (-t * 400.0) * 0.4 * (rng.nextFloat() * 2.0f - 1.0f));
            const float s = (float) std::sin (phase) * env * 0.9f + click;
            sample->data.setSample (0, i, s);
            sample->data.setSample (1, i, s);
        }
    }
    else if (lower.contains ("snare"))
    {
        makeBuffer (0.25);
        double phase = 0.0;
        float noiseLP = 0.0f;
        for (int i = 0; i < sample->data.getNumSamples(); ++i)
        {
            const double t = i / sr;
            phase += 185.0 * juce::MathConstants<double>::twoPi / sr;
            const float tone  = (float) (std::sin (phase) * std::exp (-t * 30.0) * 0.5);
            const float white = rng.nextFloat() * 2.0f - 1.0f;
            noiseLP += 0.35f * (white - noiseLP);   // tame the top end a bit
            const float noise = (white - noiseLP * 0.5f) * (float) std::exp (-t * 18.0) * 0.55f;
            const float s = tone + noise;
            sample->data.setSample (0, i, s);
            sample->data.setSample (1, i, s);
        }
    }
    else if (lower.contains ("clap"))
    {
        makeBuffer (0.30);
        float lp = 0.0f;
        for (int i = 0; i < sample->data.getNumSamples(); ++i)
        {
            const double t = i / sr;
            // Three quick bursts then a tail, like layered claps.
            double burstEnv = 0.0;
            for (int b = 0; b < 3; ++b)
                burstEnv = juce::jmax (burstEnv, std::exp (-(juce::jmax (0.0, t - 0.011 * b)) * 90.0)
                                                    * (t >= 0.011 * b ? 1.0 : 0.0));
            burstEnv = juce::jmax (burstEnv, std::exp (-t * 11.0) * 0.5);
            const float white = rng.nextFloat() * 2.0f - 1.0f;
            lp += 0.25f * (white - lp);
            const float band = white - lp;   // rough bandpass
            const float s = band * (float) burstEnv * 0.8f;
            sample->data.setSample (0, i, s);
            sample->data.setSample (1, i, s);
        }
    }
    else   // hat / anything else: short bright noise
    {
        makeBuffer (0.09);
        float lp = 0.0f;
        for (int i = 0; i < sample->data.getNumSamples(); ++i)
        {
            const double t = i / sr;
            const float white = rng.nextFloat() * 2.0f - 1.0f;
            lp += 0.55f * (white - lp);
            const float highpassed = white - lp;
            const float s = highpassed * (float) std::exp (-t * 55.0) * 0.6f;
            sample->data.setSample (0, i, s);
            sample->data.setSample (1, i, s);
        }
    }

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

    const double semis = key - rootNote.load();
    slot->rate  = std::pow (2.0, semis / 12.0) * sample->sourceSampleRate / deviceSampleRate;
    slot->pos   = 0.0;
    slot->gainL = velocity * sampleGain.load();
    slot->gainR = slot->gainL;
    slot->sample = std::move (sample);
    slot->active = true;
}

void SamplerGenerator::render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi)
{
    const int numSamples = out.getNumSamples();
    int segmentStart = 0;
    auto it = midi.begin();

    auto renderSegment = [&] (int from, int to)
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
            const int last = data.getNumSamples() - 2;

            for (int i = from; i < to; ++i)
            {
                if (v.pos >= last)
                {
                    v.active = false;
                    break;
                }
                const int idx = (int) v.pos;
                const float frac = (float) (v.pos - idx);
                l[i] += (srcL[idx] + frac * (srcL[idx + 1] - srcL[idx])) * v.gainL;
                r[i] += (srcR[idx] + frac * (srcR[idx + 1] - srcR[idx])) * v.gainR;
                v.pos += v.rate;
            }
        }
    };

    for (; it != midi.end(); ++it)
    {
        const auto meta = *it;
        const auto msg = meta.getMessage();
        const int pos = juce::jlimit (0, numSamples, meta.samplePosition);
        renderSegment (segmentStart, pos);
        segmentStart = pos;

        if (msg.isNoteOn())
            startVoice (msg.getNoteNumber(), msg.getFloatVelocity());
        // Note-offs ignored: FL one-shot behaviour.
    }
    renderSegment (segmentStart, numSamples);
}
