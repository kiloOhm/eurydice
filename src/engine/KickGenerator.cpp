#include "KickGenerator.h"
#include "NotePan.h"
#include "Drive.h"

namespace
{
// Decay coefficient reaching e^-fall after seconds at this sample rate.
double decayCoef (float seconds, double sampleRate, double fall = 1.0) noexcept
{
    return std::exp (-fall / (juce::jmax (0.0002, (double) seconds) * sampleRate));
}

constexpr double punchDecaySeconds = 0.006;
} // namespace

KickGenerator::KickGenerator() = default;

void KickGenerator::prepare (double sr, int blockSize)
{
    juce::ignoreUnused (blockSize);
    sampleRate = sr;
    eq.prepare (sr);
    compressor.prepare (sr);
    reset();
}

void KickGenerator::reset()
{
    for (auto& v : voices)
    {
        v.active = false;
        v.clickSampleData.reset();
    }
    eq.reset();
    compressor.reset();
}

// ---------------------------------------------------------------------------
// message thread
// ---------------------------------------------------------------------------

void KickGenerator::setPitchEnvelope (kickdsp::Envelope envelope)
{
    envelope.tidy();
    auto shared = std::make_shared<const kickdsp::Envelope> (std::move (envelope));
    const juce::SpinLock::ScopedLockType lock (stateLock);
    pitchEnvelope = shared->empty() ? nullptr : shared;
}

void KickGenerator::setAmpEnvelope (kickdsp::Envelope envelope)
{
    envelope.tidy();
    auto shared = std::make_shared<const kickdsp::Envelope> (std::move (envelope));
    const juce::SpinLock::ScopedLockType lock (stateLock);
    ampEnvelope = shared->empty() ? nullptr : shared;
}

void KickGenerator::setClickSample (const juce::String& path)
{
    if (path == clickSamplePath)
        return;
    clickSamplePath = path;

    std::shared_ptr<const Sample> loaded;
    if (path.isNotEmpty())
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        if (auto reader = std::unique_ptr<juce::AudioFormatReader> (
                formats.createReaderFor (juce::File (path))))
        {
            // A click layer is a transient: two seconds is more than generous,
            // and it keeps a mis-dropped full track out of the audio thread.
            const int numSamples = (int) juce::jmin (reader->lengthInSamples,
                                                     (juce::int64) (reader->sampleRate * 2.0));
            if (numSamples > 0)
            {
                auto sample = std::make_shared<Sample>();
                juce::AudioBuffer<float> interleaved ((int) reader->numChannels, numSamples);
                reader->read (&interleaved, 0, numSamples, 0, true, true);

                sample->data.setSize (1, numSamples);
                sample->data.copyFrom (0, 0, interleaved, 0, 0, numSamples);
                for (int ch = 1; ch < interleaved.getNumChannels(); ++ch)
                    sample->data.addFrom (0, 0, interleaved, ch, 0, numSamples);
                if (interleaved.getNumChannels() > 1)
                    sample->data.applyGain (1.0f / (float) interleaved.getNumChannels());

                sample->sourceSampleRate = reader->sampleRate;
                loaded = sample;
            }
        }
    }

    const juce::SpinLock::ScopedLockType lock (stateLock);
    clickSample = loaded;
}

bool KickGenerator::hasClickSample() const
{
    return getClickSample() != nullptr;
}

std::atomic<float>* KickGenerator::getAutomatableParam (const juce::String& paramId)
{
    if (paramId == "kickStartFreq")  return &p.startFreq;
    if (paramId == "kickEndFreq")    return &p.endFreq;
    if (paramId == "kickPitchDecay") return &p.pitchDecay;
    if (paramId == "kickAmpDecay")   return &p.ampDecay;
    if (paramId == "kickBodyShape")  return &p.bodyShape;
    if (paramId == "kickBodyHarm")   return &p.bodyHarm;
    if (paramId == "kickBodyPhase")  return &p.bodyPhase;
    if (paramId == "kickBodyLevel")  return &p.bodyLevel;
    if (paramId == "kickHold")       return &p.hold;
    if (paramId == "kickPunch")      return &p.punch;
    if (paramId == "kickSubLevel")   return &p.subLevel;
    if (paramId == "kickSubTune")    return &p.subTune;
    if (paramId == "kickSubDecay")   return &p.subDecay;
    if (paramId == "kickClickLevel") return &p.clickLevel;
    if (paramId == "kickClickDecay") return &p.clickDecay;
    if (paramId == "kickClickFreq")  return &p.clickFreq;
    if (paramId == "kickNoiseLevel") return &p.noiseLevel;
    if (paramId == "kickNoiseDecay") return &p.noiseDecay;
    if (paramId == "kickNoiseTone")  return &p.noiseTone;
    if (paramId == "drive")          return &p.drive;
    if (paramId == "envShape")       return &p.envShape;
    if (paramId == "kickEqLowFreq")  return &p.eqLowFreq;
    if (paramId == "kickEqLowGain")  return &p.eqLowGain;
    if (paramId == "kickEqMidFreq")  return &p.eqMidFreq;
    if (paramId == "kickEqMidGain")  return &p.eqMidGain;
    if (paramId == "kickEqHighFreq") return &p.eqHighFreq;
    if (paramId == "kickEqHighGain") return &p.eqHighGain;
    if (paramId == "kickComp")       return &p.compression;
    if (paramId == "kickLimit")      return &p.limiter;
    if (paramId == "kickOutput")     return &p.outputDb;
    return nullptr;
}

std::shared_ptr<const kickdsp::Envelope> KickGenerator::getPitchEnvelope() const
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    return pitchEnvelope;
}

std::shared_ptr<const kickdsp::Envelope> KickGenerator::getAmpEnvelope() const
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    return ampEnvelope;
}

auto KickGenerator::getClickSample() const -> std::shared_ptr<const Sample>
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    return clickSample;
}

// ---------------------------------------------------------------------------
// audio thread
// ---------------------------------------------------------------------------

void KickGenerator::noteOn (int key, float velocity)
{
    Voice* slot = nullptr;
    for (auto& v : voices)
        if (! v.active) { slot = &v; break; }
    if (slot == nullptr)
    {
        // Steal the voice closest to the end of its decay.
        slot = &voices[0];
        for (auto& v : voices)
            if (v.samplesRemaining < slot->samplesRemaining)
                slot = &v;
    }

    const float ampDecay = juce::jmax (0.01f, p.ampDecay.load());
    const float hold = juce::jmax (0.0f, p.hold.load());
    const float pitchTime = juce::jmax (0.0005f, p.pitchDecay.load());

    slot->velocity      = velocity * p.gain.load();
    notepan::gains (pendingPan, slot->panL, slot->panR);
    pendingPan = 0.0f;   // consumed: the next un-CC'd note plays centred
    slot->transpose     = std::pow (2.0, (key - rootNote.load()) / 12.0);
    slot->bodyPhase     = juce::jlimit (0.0f, 1.0f, p.bodyPhase.load());
    slot->clickPhase    = 0.0;
    slot->subPhase      = 0.0;
    slot->noiseLowpass  = 0.0f;

    slot->pitchEnv      = 1.0;
    slot->pitchEnvCoef  = decayCoef (pitchTime, sampleRate);
    slot->ampExp        = 1.0;
    slot->ampExpCoef    = decayCoef (ampDecay, sampleRate, 5.0);
    slot->ampLinear     = 1.0;
    slot->ampLinearStep = 1.0 / (ampDecay * sampleRate);
    slot->clickEnv      = 1.0;
    slot->clickEnvCoef  = decayCoef (p.clickDecay.load(), sampleRate, 5.0);
    slot->noiseEnv      = 1.0;
    slot->noiseEnvCoef  = decayCoef (p.noiseDecay.load(), sampleRate, 5.0);
    slot->subEnv        = 1.0;
    slot->subEnvCoef    = decayCoef (p.subDecay.load(), sampleRate, 5.0);
    slot->punchEnv      = 1.0;
    slot->punchEnvCoef  = decayCoef ((float) punchDecaySeconds, sampleRate, 5.0);

    slot->elapsed     = 0;
    slot->holdSamples = (int) (hold * sampleRate);
    slot->pitchSpan   = juce::jmax (1.0, pitchTime * sampleRate);
    slot->ampSpan     = juce::jmax (1.0, ampDecay * sampleRate);

    // The click layer's sample is pinned for the note's lifetime, so a reload
    // on the message thread can never pull the buffer out from under it.
    if (p.clickType.load() == (int) kickdsp::ClickType::sample)
    {
        slot->clickSampleData = getClickSample();
        slot->clickSamplePos  = 0.0;
        slot->clickSampleStep = slot->clickSampleData != nullptr
                                    ? slot->clickSampleData->sourceSampleRate / sampleRate * slot->transpose
                                    : 1.0;
    }
    else
    {
        slot->clickSampleData.reset();
        slot->clickSamplePos = -1.0;
    }

    // A drawn amp envelope ends exactly at its span; the analytic one needs
    // the extra 15% to run out of level.
    const double tail = blockAmpEnv != nullptr ? (double) ampDecay : ampDecay * 1.15;
    slot->samplesRemaining = (int) ((hold + tail) * sampleRate) + fadeOutSamples;
    slot->active = true;
}

void KickGenerator::renderSegment (juce::AudioBuffer<float>& out, int from, int to)
{
    if (to <= from)
        return;

    auto* l = out.getWritePointer (0);
    auto* r = out.getWritePointer (1);

    const double startFreq = juce::jlimit (20.0f, 4000.0f, p.startFreq.load());
    const double endFreq   = juce::jlimit (20.0f, 4000.0f, p.endFreq.load());
    const float shape      = juce::jlimit (0.0f, 1.0f, p.bodyShape.load());
    const float harm       = juce::jlimit (0.0f, 1.0f, p.bodyHarm.load());
    const float bodyLevel  = juce::jlimit (0.0f, 2.0f, p.bodyLevel.load());
    const float envShape   = juce::jlimit (0.0f, 1.0f, p.envShape.load());
    const float punch      = juce::jlimit (0.0f, 1.0f, p.punch.load());
    const float clickLevel = juce::jmax (0.0f, p.clickLevel.load());
    const double clickFreq = juce::jlimit (20.0f, 12000.0f, p.clickFreq.load());
    const int clickType    = juce::jlimit (0, kickdsp::numClickTypes - 1, p.clickType.load());
    const float noiseLevel = juce::jmax (0.0f, p.noiseLevel.load());
    const float noiseTone  = juce::jlimit (0.02f, 1.0f, p.noiseTone.load());
    const float subLevel   = juce::jmax (0.0f, p.subLevel.load());
    const double subFreq   = endFreq * std::pow (2.0, p.subTune.load() / 12.0);

    const bool clickIsSample = clickType == (int) kickdsp::ClickType::sample;
    const bool clickIsNoise  = clickType == (int) kickdsp::ClickType::noise;
    const auto* pitchCurve = blockPitchEnv.get();
    const auto* ampCurve   = blockAmpEnv.get();

    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        for (int i = from; i < to; ++i)
        {
            if (v.samplesRemaining <= 0)
            {
                v.active = false;
                v.clickSampleData.reset();
                break;
            }

            // --- body: pitch envelope drives the sweep -----------------------
            const double pitchEnv = pitchCurve != nullptr
                                        ? (double) pitchCurve->valueAt ((float) (v.elapsed / v.pitchSpan))
                                        : v.pitchEnv;
            const double freq = (endFreq + (startFreq - endFreq) * pitchEnv) * v.transpose;
            v.bodyPhase += freq / sampleRate;
            if (v.bodyPhase >= 1.0)
                v.bodyPhase -= std::floor (v.bodyPhase);
            v.pitchEnv *= v.pitchEnvCoef;

            float s = kickdsp::body (v.bodyPhase, shape, harm) * bodyLevel;

            // --- amp envelope ------------------------------------------------
            double ampEnv;
            if (v.elapsed < v.holdSamples)
            {
                ampEnv = 1.0;
            }
            else if (ampCurve != nullptr)
            {
                ampEnv = ampCurve->valueAt ((float) ((v.elapsed - v.holdSamples) / v.ampSpan));
            }
            else
            {
                ampEnv = v.ampLinear + envShape * (v.ampExp - v.ampLinear);
                v.ampExp *= v.ampExpCoef;
                v.ampLinear = juce::jmax (0.0, v.ampLinear - v.ampLinearStep);
            }
            if (punch > 0.0f)
                ampEnv *= 1.0 + 2.0 * punch * v.punchEnv;
            v.punchEnv *= v.punchEnvCoef;

            s *= (float) ampEnv;

            // --- sub ---------------------------------------------------------
            if (subLevel > 0.0f)
            {
                v.subPhase += subFreq * v.transpose / sampleRate;
                if (v.subPhase >= 1.0)
                    v.subPhase -= std::floor (v.subPhase);
                s += std::sin ((float) (v.subPhase * juce::MathConstants<double>::twoPi))
                     * (float) v.subEnv * subLevel;
            }
            v.subEnv *= v.subEnvCoef;

            // One draw per sample, shared by whichever layers want noise, so
            // the legacy noise layer keeps its exact sequence.
            const bool needsNoise = noiseLevel > 0.0f || (clickLevel > 0.0f && clickIsNoise);
            const float white = needsNoise ? rng.nextFloat() * 2.0f - 1.0f : 0.0f;

            // --- click -------------------------------------------------------
            if (clickLevel > 0.0f)
            {
                float clickSignal = 0.0f;
                if (clickIsSample)
                {
                    if (const auto* sample = v.clickSampleData.get())
                    {
                        const int length = sample->data.getNumSamples();
                        const int index = (int) v.clickSamplePos;
                        if (index >= 0 && index + 1 < length)
                        {
                            const float* data = sample->data.getReadPointer (0);
                            const float frac = (float) (v.clickSamplePos - index);
                            clickSignal = data[index] + frac * (data[index + 1] - data[index]);
                        }
                        v.clickSamplePos += v.clickSampleStep;
                    }
                }
                else
                {
                    v.clickPhase += clickFreq * v.transpose / sampleRate;
                    if (v.clickPhase >= 1.0)
                        v.clickPhase -= std::floor (v.clickPhase);
                    clickSignal = kickdsp::click (clickType, v.clickPhase, white);
                }
                s += clickSignal * (float) v.clickEnv * clickLevel;
            }
            v.clickEnv *= v.clickEnvCoef;

            // --- noise -------------------------------------------------------
            if (noiseLevel > 0.0f)
            {
                v.noiseLowpass += noiseTone * (white - v.noiseLowpass);
                s += v.noiseLowpass * (float) v.noiseEnv * noiseLevel;
            }
            v.noiseEnv *= v.noiseEnvCoef;

            const float tail = juce::jmin (1.0f, (float) v.samplesRemaining / (float) fadeOutSamples);
            s *= v.velocity * tail;
            --v.samplesRemaining;
            ++v.elapsed;

            l[i] += s * v.panL;
            r[i] += s * v.panR;
        }
    }

    processChain (out, from, to);
}

void KickGenerator::processChain (juce::AudioBuffer<float>& out, int from, int to)
{
    drive::processBlock (out, from, to, juce::jlimit (0.0f, 1.0f, p.drive.load()),
                         p.driveCurve.load());

    const kickdsp::ToneEq::Settings eqSettings {
        juce::jlimit (20.0f, 500.0f, p.eqLowFreq.load()),
        juce::jlimit (-24.0f, 24.0f, p.eqLowGain.load()),
        juce::jlimit (100.0f, 6000.0f, p.eqMidFreq.load()),
        juce::jlimit (-24.0f, 24.0f, p.eqMidGain.load()),
        juce::jlimit (1000.0f, 16000.0f, p.eqHighFreq.load()),
        juce::jlimit (-24.0f, 24.0f, p.eqHighGain.load()) };
    const bool eqActive = ! eqSettings.isFlat();
    if (eqActive)
        eq.setSettings (eqSettings);

    const float compAmount = juce::jlimit (0.0f, 1.0f, p.compression.load());
    const float limitAmount = juce::jlimit (0.0f, 1.0f, p.limiter.load());
    const float output = juce::Decibels::decibelsToGain (
        juce::jlimit (-24.0f, 12.0f, p.outputDb.load()));

    if (! eqActive && compAmount <= 0.0f && limitAmount <= 0.0f && output == 1.0f)
        return;   // nothing to do: leave the samples bit-for-bit alone

    auto* l = out.getWritePointer (0);
    auto* r = out.getWritePointer (1);

    for (int i = from; i < to; ++i)
    {
        float left = l[i], right = r[i];

        if (eqActive)
        {
            left  = eq.processSample (0, left);
            right = eq.processSample (1, right);
        }

        const float gain = compressor.gainFor (juce::jmax (std::abs (left), std::abs (right)),
                                               compAmount);
        left  = kickdsp::limit (left * gain, limitAmount) * output;
        right = kickdsp::limit (right * gain, limitAmount) * output;

        l[i] = left;
        r[i] = right;
    }
}

void KickGenerator::render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi)
{
    // One look at the message-thread state per block. A try-lock keeps the
    // audio thread off a message-thread swap: it just runs last block's
    // envelopes for one more block.
    if (const juce::SpinLock::ScopedTryLockType lock (stateLock); lock.isLocked())
    {
        blockPitchEnv = pitchEnvelope;
        blockAmpEnv = ampEnvelope;
    }

    const int numSamples = out.getNumSamples();
    int segmentStart = 0;

    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isController() && msg.getControllerNumber() == notepan::controller)
        {
            pendingPan = notepan::fromController (msg.getControllerValue());
            continue;
        }
        if (! msg.isNoteOn())
            continue;   // one-shot: note-offs never stop the decay

        const int pos = juce::jlimit (0, numSamples, meta.samplePosition);
        renderSegment (out, segmentStart, pos);
        segmentStart = pos;
        noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
    }
    renderSegment (out, segmentStart, numSamples);
}
