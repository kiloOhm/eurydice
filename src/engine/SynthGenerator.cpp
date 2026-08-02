#include "SynthGenerator.h"
#include "NotePan.h"

SynthGenerator::SynthGenerator() = default;

void SynthGenerator::prepare (double sr, int blockSize)
{
    sampleRate = sr;
    for (auto& v : voices)
    {
        v.ampEnv.setSampleRate (sr);
        v.filterEnv.setSampleRate (sr);
        v.filter.prepare ({ sr, (juce::uint32) blockSize, 1 });
        v.filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        v.active = false;
    }
}

void SynthGenerator::reset()
{
    for (auto& v : voices)
        v.active = false;
}

float SynthGenerator::polyBlep (double t, double dt)
{
    if (t < dt)
    {
        const double x = t / dt;
        return (float) (x + x - x * x - 1.0);
    }
    if (t > 1.0 - dt)
    {
        const double x = (t - 1.0) / dt;
        return (float) (x * x + x + x + 1.0);
    }
    return 0.0f;
}

void SynthGenerator::noteOn (int key, float velocity)
{
    Voice* slot = nullptr;
    for (auto& v : voices)
        if (! v.active) { slot = &v; break; }
    if (slot == nullptr)
        slot = &voices[0];

    juce::ADSR::Parameters ampParams { p.attack.load(), p.decay.load(), p.sustain.load(), p.release.load() };
    juce::ADSR::Parameters filtParams { p.attack.load(), juce::jmax (0.05f, p.decay.load()), 0.2f, p.release.load() };

    slot->key = key;
    slot->velocity = velocity;
    notepan::gains (pendingPan, slot->panL, slot->panR);
    pendingPan = 0.0f;   // consumed: the next un-CC'd note plays centred
    slot->phase1 = slot->phase2 = 0.0;
    slot->ampEnv.setParameters (ampParams);
    slot->filterEnv.setParameters (filtParams);
    slot->ampEnv.noteOn();
    slot->filterEnv.noteOn();
    slot->filter.reset();
    slot->active = true;
}

void SynthGenerator::noteOff (int key)
{
    for (auto& v : voices)
        if (v.active && v.key == key)
        {
            v.ampEnv.noteOff();
            v.filterEnv.noteOff();
        }
}

void SynthGenerator::renderSegment (juce::AudioBuffer<float>& out, int from, int to)
{
    if (to <= from)
        return;

    auto* l = out.getWritePointer (0);
    auto* r = out.getWritePointer (1);

    const float detune  = p.osc2DetuneCents.load();
    const float mix2    = p.osc2Mix.load();
    const float shape   = p.oscShape.load();
    const float cutoff  = p.cutoffHz.load();
    const float reso    = p.resonance.load();
    const float envAmt  = p.filterEnvAmount.load();
    const float gain    = p.masterGain.load();

    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        const double f1 = juce::MidiMessage::getMidiNoteInHertz (v.key);
        const double f2 = f1 * std::pow (2.0, detune / 1200.0);
        const double dt1 = f1 / sampleRate;
        const double dt2 = f2 / sampleRate;

        v.filter.setResonance (juce::jmap (reso, 0.707f, 8.0f));

        for (int i = from; i < to; ++i)
        {
            const float fenv = v.filterEnv.getNextSample();
            const float aenv = v.ampEnv.getNextSample();

            if (! v.ampEnv.isActive())
            {
                v.active = false;
                break;
            }

            auto osc = [shape] (double& phase, double dt) -> float
            {
                float saw = (float) (2.0 * phase - 1.0) - polyBlep (phase, dt);
                float sq  = (phase < 0.5 ? 1.0f : -1.0f)
                            + polyBlep (phase, dt)
                            - polyBlep (std::fmod (phase + 0.5, 1.0), dt);
                phase += dt;
                if (phase >= 1.0) phase -= 1.0;
                return saw + shape * (sq - saw);
            };

            float s = osc (v.phase1, dt1) * (1.0f - mix2) + osc (v.phase2, dt2) * mix2;

            const float modCutoff = juce::jlimit (40.0f, 18000.0f,
                                                  cutoff * std::pow (2.0f, fenv * envAmt * 4.0f));
            v.filter.setCutoffFrequency (modCutoff);
            s = v.filter.processSample (0, s);

            s *= aenv * v.velocity * gain;
            l[i] += s * v.panL;
            r[i] += s * v.panR;
        }
    }
}

void SynthGenerator::render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi)
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
            noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff())
            noteOff (msg.getNoteNumber());
        else if (msg.isController() && msg.getControllerNumber() == notepan::controller)
            pendingPan = notepan::fromController (msg.getControllerValue());
    }
    renderSegment (out, segmentStart, numSamples);
}
