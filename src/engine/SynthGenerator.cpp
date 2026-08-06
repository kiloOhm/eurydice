#include "SynthGenerator.h"
#include "SynthOsc.h"
#include "NotePan.h"

SynthGenerator::SynthGenerator() = default;

void SynthGenerator::prepare (double sr, int blockSize)
{
    sampleRate = sr;
    for (auto& v : voices)
    {
        v.ampEnv.setSampleRate (sr);
        v.filterEnv.setSampleRate (sr);
        v.filter.prepare ({ sr, (juce::uint32) blockSize, 2 });
        v.filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        v.active = false;
    }
}

void SynthGenerator::reset()
{
    for (auto& v : voices)
        v.active = false;
    lastSemi = -1.0;
}

void SynthGenerator::noteOn (int key, float velocity)
{
    Voice* slot = nullptr;
    for (auto& v : voices)
        if (! v.active) { slot = &v; break; }
    if (slot == nullptr)
        slot = &voices[0];

    juce::ADSR::Parameters ampParams { p.attack.load(), p.decay.load(),
                                       p.sustain.load(), p.release.load() };
    juce::ADSR::Parameters filtParams { p.fenvAttack.load(), juce::jmax (0.05f, p.fenvDecay.load()),
                                        p.fenvSustain.load(), p.fenvRelease.load() };

    slot->key = key;
    slot->velocity = velocity;
    notepan::gains (pendingPan, slot->panL, slot->panR);
    pendingPan = 0.0f;   // consumed: the next un-CC'd note plays centred

    // Unison layout is latched per note: detune positions spread symmetric
    // around the centre, stereo placement alternating outward. Random start
    // phases keep stacked voices from reinforcing like one loud oscillator.
    const int unison = juce::jlimit (1, maxUnison, juce::roundToInt (p.unisonVoices.load()));
    const float width = p.unisonWidth.load();
    slot->unison = unison;
    for (int u = 0; u < unison; ++u)
    {
        noiseState = noiseState * 1664525u + 1013904223u;
        const double startPhase = unison == 1 ? 0.0
                                              : (noiseState >> 8) * (1.0 / 16777216.0);
        slot->phase1[(size_t) u] = startPhase;
        noiseState = noiseState * 1664525u + 1013904223u;
        slot->phase2[(size_t) u] = unison == 1 ? 0.0 : (noiseState >> 8) * (1.0 / 16777216.0);

        const float pos = unison == 1 ? 0.0f
                                      : 2.0f * (float) u / (float) (unison - 1) - 1.0f;
        slot->uniOffset[(size_t) u] = pos;
        notepan::gains (pos * width, slot->uniPanL[(size_t) u], slot->uniPanR[(size_t) u]);
    }

    // Glide starts from the last played key; 0 (or no prior note) is instant.
    slot->targetSemi = key;
    slot->currentSemi = (p.glide.load() > 0.0f && lastSemi >= 0.0) ? lastSemi : (double) key;
    lastSemi = key;

    slot->subPhase = 0.0;
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

    const float morph   = p.oscShape.load();
    const float warp    = p.oscWarp.load();
    const float semi2   = p.osc2Semi.load();
    const float detune2 = p.osc2DetuneCents.load();
    const float mix2    = p.osc2Mix.load();
    const float uniDet  = p.unisonDetune.load();
    const float sub     = p.subLevel.load();
    const float noise   = p.noiseLevel.load();
    const float cutoff  = p.cutoffHz.load();
    const float reso    = p.resonance.load();
    const float keyAmt  = p.filterKey.load();
    const float envAmt  = p.filterEnvAmount.load();
    const float gain    = p.masterGain.load();
    const float glide   = p.glide.load();
    const float lfoAmt  = p.lfoAmount.load();
    const int   lfoDest = juce::jlimit (0, 3, juce::roundToInt (p.lfoTarget.load()));
    const int   ftype   = juce::jlimit (0, 2, juce::roundToInt (p.filterType.load()));

    // One LFO for the whole generator: every voice reads the same phase
    // (base + offset into the segment), so they move together and no buffer
    // has to match whatever block size the caller renders.
    const double lfoInc = juce::jlimit (0.01f, 30.0f, p.lfoRate.load()) / sampleRate;
    const double lfoBase = lfoPhase;
    lfoPhase += (to - from) * lfoInc;
    lfoPhase -= std::floor (lfoPhase);

    const auto filterMode = ftype == 1 ? juce::dsp::StateVariableTPTFilterType::bandpass
                          : ftype == 2 ? juce::dsp::StateVariableTPTFilterType::highpass
                                       : juce::dsp::StateVariableTPTFilterType::lowpass;

    // Glide steps every glideChunk samples (~1.5 ms), independent of how big
    // a buffer the caller renders — a single huge block must not turn the
    // slide into one jump.
    constexpr int glideChunk = 64;

    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        const float keyFactor = std::pow (2.0f, ((float) v.key - 60.0f) / 12.0f * keyAmt);
        const float uniNorm = 1.0f / std::sqrt ((float) v.unison);

        v.filter.setType (filterMode);
        v.filter.setResonance (synthosc::resonanceToQ (reso));

        for (int chunkStart = from; chunkStart < to && v.active; chunkStart += glideChunk)
        {
            const int chunkEnd = juce::jmin (to, chunkStart + glideChunk);

            const double glideCoef = glide > 0.001f
                ? 1.0 - std::exp (-(double) (chunkEnd - chunkStart) / (glide * sampleRate)) : 1.0;
            v.currentSemi += (v.targetSemi - v.currentSemi) * glideCoef;

            const double f1 = 440.0 * std::pow (2.0, (v.currentSemi - 69.0) / 12.0);
            const double f2 = f1 * std::pow (2.0, ((double) semi2 + detune2 / 100.0) / 12.0);

            for (int i = chunkStart; i < chunkEnd; ++i)
            {
                const float fenv = v.filterEnv.getNextSample();
                const float aenv = v.ampEnv.getNextSample();

                if (! v.ampEnv.isActive())
                {
                    v.active = false;
                    break;
                }

                const float lfo = lfoAmt <= 0.0f ? 0.0f
                    : (float) std::sin ((lfoBase + (i - from) * lfoInc)
                                        * juce::MathConstants<double>::twoPi) * lfoAmt;
                const float warpEff = lfoDest == 2
                    ? juce::jlimit (0.0f, 1.0f, warp + lfo * 0.5f) : warp;
                // ±~1 semitone of vibrato, linearised (2^(1/12) ≈ 1.0595).
                const double pitchFactor = lfoDest == 1 ? 1.0 + lfo * 0.0595 : 1.0;

                float sL = 0.0f, sR = 0.0f;
                for (int u = 0; u < v.unison; ++u)
                {
                    const double spread = v.unison == 1 ? 1.0
                        : std::pow (2.0, (double) (v.uniOffset[(size_t) u] * uniDet) / 1200.0);
                    const double dt1 = juce::jmin (0.45, f1 * spread * pitchFactor / sampleRate);
                    const double dt2 = juce::jmin (0.45, f2 * spread * pitchFactor / sampleRate);

                    auto& ph1 = v.phase1[(size_t) u];
                    auto& ph2 = v.phase2[(size_t) u];
                    const float o1 = synthosc::sample (morph, warpEff, ph1, dt1);
                    const float o2 = synthosc::sample (morph, warpEff, ph2, dt2);
                    ph1 += dt1; if (ph1 >= 1.0) ph1 -= 1.0;
                    ph2 += dt2; if (ph2 >= 1.0) ph2 -= 1.0;

                    const float s = (o1 * (1.0f - mix2) + o2 * mix2) * uniNorm;
                    sL += s * v.uniPanL[(size_t) u];
                    sR += s * v.uniPanR[(size_t) u];
                }

                if (sub > 0.0f)
                {
                    const float subSample = synthosc::sine (v.subPhase) * sub;
                    v.subPhase += f1 * 0.5 * pitchFactor / sampleRate;
                    if (v.subPhase >= 1.0) v.subPhase -= 1.0;
                    sL += subSample;
                    sR += subSample;
                }
                if (noise > 0.0f)
                {
                    noiseState = noiseState * 1664525u + 1013904223u;
                    const float n = ((float) (noiseState >> 8) * (1.0f / 8388608.0f) - 1.0f) * noise;
                    sL += n;
                    sR += n;
                }

                const float lfoCutOctaves = lfoDest == 0 ? lfo * 2.0f : 0.0f;
                const float modCutoff = juce::jlimit (40.0f, 18000.0f,
                    cutoff * keyFactor * std::pow (2.0f, fenv * envAmt * 4.0f + lfoCutOctaves));
                v.filter.setCutoffFrequency (modCutoff);

                // The filter runs true stereo so the unison spread survives it.
                float outL = v.filter.processSample (0, sL);
                float outR = v.filter.processSample (1, sR);

                float panL = v.panL, panR = v.panR;
                if (lfoDest == 3)
                {
                    float autoL, autoR;
                    notepan::gains (lfo, autoL, autoR);
                    panL *= autoL;
                    panR *= autoR;
                }

                const float amp = aenv * v.velocity * gain;
                l[i] += outL * amp * panL;
                r[i] += outR * amp * panR;
            }
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
