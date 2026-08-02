#include "KickGenerator.h"
#include "Drive.h"

namespace
{
// Triangle through (0,0), (0.25,1), (0.75,-1) so it lines up with the sine
// it is blended against.
float triangle (double phase) noexcept
{
    const double t = 4.0 * phase;
    if (t < 1.0) return (float) t;
    if (t < 3.0) return (float) (2.0 - t);
    return (float) (t - 4.0);
}

// Decay coefficient reaching e^-fall after seconds at this sample rate.
double decayCoef (float seconds, double sampleRate, double fall = 1.0) noexcept
{
    return std::exp (-fall / (juce::jmax (0.0002, (double) seconds) * sampleRate));
}

constexpr double clickFrequency = 1400.0;
constexpr double noiseLowpassCoef = 0.4;
} // namespace

KickGenerator::KickGenerator() = default;

void KickGenerator::prepare (double sr, int blockSize)
{
    juce::ignoreUnused (blockSize);
    sampleRate = sr;
    reset();
}

void KickGenerator::reset()
{
    for (auto& v : voices)
        v.active = false;
}

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

    slot->velocity      = velocity * p.gain.load();
    slot->transpose     = std::pow (2.0, (key - rootNote.load()) / 12.0);
    slot->bodyPhase     = 0.0;
    slot->clickPhase    = 0.0;
    slot->noiseLowpass  = 0.0f;

    slot->pitchEnv      = 1.0;
    slot->pitchEnvCoef  = decayCoef (p.pitchDecay.load(), sampleRate);
    slot->ampExp        = 1.0;
    slot->ampExpCoef    = decayCoef (ampDecay, sampleRate, 5.0);
    slot->ampLinear     = 1.0;
    slot->ampLinearStep = 1.0 / (ampDecay * sampleRate);
    slot->clickEnv      = 1.0;
    slot->clickEnvCoef  = decayCoef (p.clickDecay.load(), sampleRate, 5.0);
    slot->noiseEnv      = 1.0;
    slot->noiseEnvCoef  = decayCoef (p.noiseDecay.load(), sampleRate, 5.0);

    slot->samplesRemaining = (int) (ampDecay * 1.15 * sampleRate) + fadeOutSamples;
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
    const float envShape   = juce::jlimit (0.0f, 1.0f, p.envShape.load());
    const float clickLevel = juce::jmax (0.0f, p.clickLevel.load());
    const float noiseLevel = juce::jmax (0.0f, p.noiseLevel.load());

    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        for (int i = from; i < to; ++i)
        {
            if (v.samplesRemaining <= 0)
            {
                v.active = false;
                break;
            }

            const double freq = (endFreq + (startFreq - endFreq) * v.pitchEnv) * v.transpose;
            v.bodyPhase += freq / sampleRate;
            if (v.bodyPhase >= 1.0)
                v.bodyPhase -= std::floor (v.bodyPhase);
            v.pitchEnv *= v.pitchEnvCoef;

            const float sine = std::sin ((float) (v.bodyPhase * juce::MathConstants<double>::twoPi));
            const float body = sine + shape * (triangle (v.bodyPhase) - sine);

            const float ampEnv = (float) (v.ampLinear + envShape * (v.ampExp - v.ampLinear));
            v.ampExp *= v.ampExpCoef;
            v.ampLinear = juce::jmax (0.0, v.ampLinear - v.ampLinearStep);

            float s = body * ampEnv;

            if (clickLevel > 0.0f)
            {
                v.clickPhase += clickFrequency * v.transpose / sampleRate;
                if (v.clickPhase >= 1.0)
                    v.clickPhase -= std::floor (v.clickPhase);
                s += std::sin ((float) (v.clickPhase * juce::MathConstants<double>::twoPi))
                     * (float) v.clickEnv * clickLevel;
            }
            v.clickEnv *= v.clickEnvCoef;

            if (noiseLevel > 0.0f)
            {
                const float white = rng.nextFloat() * 2.0f - 1.0f;
                v.noiseLowpass += (float) noiseLowpassCoef * (white - v.noiseLowpass);
                s += v.noiseLowpass * (float) v.noiseEnv * noiseLevel;
            }
            v.noiseEnv *= v.noiseEnvCoef;

            const float tail = juce::jmin (1.0f, (float) v.samplesRemaining / (float) fadeOutSamples);
            s *= v.velocity * tail;
            --v.samplesRemaining;

            l[i] += s;
            r[i] += s;
        }
    }

    drive::processBlock (out, from, to, juce::jlimit (0.0f, 1.0f, p.drive.load()),
                         p.driveCurve.load());
}

void KickGenerator::render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi)
{
    const int numSamples = out.getNumSamples();
    int segmentStart = 0;

    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (! msg.isNoteOn())
            continue;   // one-shot: note-offs never stop the decay

        const int pos = juce::jlimit (0, numSamples, meta.samplePosition);
        renderSegment (out, segmentStart, pos);
        segmentStart = pos;
        noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
    }
    renderSegment (out, segmentStart, numSamples);
}
