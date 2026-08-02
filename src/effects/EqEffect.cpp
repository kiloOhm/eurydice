#include "EqEffect.h"
#include "model/Ids.h"

namespace
{
const juce::Identifier* bandTypeIds[] { &ids::fxBandType1, &ids::fxBandType2, &ids::fxBandType3, &ids::fxBandType4 };
const juce::Identifier* bandFreqIds[] { &ids::fxBandFreq1, &ids::fxBandFreq2, &ids::fxBandFreq3, &ids::fxBandFreq4 };
const juce::Identifier* bandGainIds[] { &ids::fxBandGain1, &ids::fxBandGain2, &ids::fxBandGain3, &ids::fxBandGain4 };
const juce::Identifier* bandQIds[]    { &ids::fxBandQ1,    &ids::fxBandQ2,    &ids::fxBandQ3,    &ids::fxBandQ4 };

const float bandDefaultFreq[] { 90.0f, 400.0f, 2000.0f, 8000.0f };
}

const juce::String& EqEffect::identifier()
{
    static const juce::String id ("builtin:eq");
    return id;
}

const juce::String& EqEffect::displayName()
{
    static const juce::String name ("EQ");
    return name;
}

const std::vector<fx::ParamSpec>& EqEffect::specs()
{
    static const std::vector<fx::ParamSpec> s = []
    {
        std::vector<fx::ParamSpec> list {
            { ids::fxHpFreq, "HP", 20.0, 20000.0, 0.25,    20.0, " Hz", 0 },
            { ids::fxLpFreq, "LP", 20.0, 20000.0, 0.25, 20000.0, " Hz", 0 },
        };
        const juce::StringArray types { "Bell", "LoShelf", "HiShelf", "Notch", "Off" };
        for (int b = 0; b < EqEffect::numBands; ++b)
        {
            const auto n = juce::String (b + 1);
            // Each band gets its own row in the editor.
            list.push_back ({ *bandTypeIds[b], "Type " + n, 0.0, 4.0, 1.0,
                              b == 0 ? 1.0 : (b == EqEffect::numBands - 1 ? 2.0 : 0.0),
                              {}, 0, types, false, true });
            list.push_back ({ *bandFreqIds[b], "Freq " + n, 20.0, 20000.0, 0.25,
                              (double) bandDefaultFreq[b], " Hz", 0 });
            list.push_back ({ *bandGainIds[b], "Gain " + n, -24.0,  24.0, 1.0, 0.0, " dB", 1 });
            list.push_back ({ *bandQIds[b],    "Q " + n,      0.1,  12.0, 0.4, 1.0, {},    2 });
        }
        return list;
    }();
    return s;
}

void EqEffect::prepare (double sampleRate, int maxBlockSize)
{
    juce::ignoreUnused (maxBlockSize);
    sampleRateHz = sampleRate;
    dirty.store (true, std::memory_order_relaxed);
    reset();
}

void EqEffect::reset()
{
    for (auto& channel : bandFilters)
        for (auto& f : channel)
            f.reset();
    for (auto& channel : hpFilters)
        for (auto& f : channel)
            f.reset();
    for (auto& channel : lpFilters)
        for (auto& f : channel)
            f.reset();
}

void EqEffect::setParameter (const juce::Identifier& paramId, double value)
{
    if (paramId == ids::fxHpFreq)
        hpFreq.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxLpFreq)
        lpFreq.store ((float) value, std::memory_order_relaxed);
    else
    {
        for (int b = 0; b < numBands; ++b)
        {
            auto& band = bands[(size_t) b];
            if (paramId == *bandTypeIds[b])      band.type.store ((int) std::lround (value), std::memory_order_relaxed);
            else if (paramId == *bandFreqIds[b]) band.freq.store ((float) value, std::memory_order_relaxed);
            else if (paramId == *bandGainIds[b]) band.gainDb.store ((float) value, std::memory_order_relaxed);
            else if (paramId == *bandQIds[b])    band.q.store ((float) value, std::memory_order_relaxed);
        }
    }
    dirty.store (true, std::memory_order_relaxed);
}

void EqEffect::updateCoefficients()
{
    // Butterworth Q pair for a cascade of two biquads = 24 dB/oct.
    static constexpr double butterworthQ[] { 0.54119610, 1.30656296 };

    const double hp = hpFreq.load (std::memory_order_relaxed);
    const double lp = lpFreq.load (std::memory_order_relaxed);
    hpActive = hp > 20.5;
    lpActive = lp < 19500.0;

    for (int ch = 0; ch < 2; ++ch)
    {
        for (int stage = 0; stage < 2; ++stage)
        {
            hpFilters[(size_t) ch][(size_t) stage].setHighPass (sampleRateHz, hp, butterworthQ[stage]);
            lpFilters[(size_t) ch][(size_t) stage].setLowPass (sampleRateHz, lp, butterworthQ[stage]);
        }

        for (int b = 0; b < numBands; ++b)
        {
            const auto& band = bands[(size_t) b];
            const double freq = band.freq.load (std::memory_order_relaxed);
            const double q = band.q.load (std::memory_order_relaxed);
            const double gain = band.gainDb.load (std::memory_order_relaxed);
            auto& filter = bandFilters[(size_t) ch][(size_t) b];

            switch ((BandType) juce::jlimit (0, 4, band.type.load (std::memory_order_relaxed)))
            {
                case BandType::lowShelf:  filter.setLowShelf (sampleRateHz, freq, q, gain); break;
                case BandType::highShelf: filter.setHighShelf (sampleRateHz, freq, q, gain); break;
                case BandType::notch:     filter.setNotch (sampleRateHz, freq, q); break;
                case BandType::off:       filter.setPassthrough(); break;
                case BandType::bell:
                default:                  filter.setPeak (sampleRateHz, freq, q, gain); break;
            }

            if (ch == 0)
            {
                const auto type = (BandType) juce::jlimit (0, 4, band.type.load (std::memory_order_relaxed));
                bandActive[(size_t) b] = type != BandType::off
                                         && (type == BandType::notch || std::abs (gain) > 0.01);
            }
        }
    }
}

void EqEffect::process (juce::AudioBuffer<float>& stereoBus, int numSamples, const Context& context)
{
    juce::ignoreUnused (context);
    juce::ScopedNoDenormals noDenormals;

    if (dirty.exchange (false, std::memory_order_relaxed))
        updateCoefficients();

    const int numCh = juce::jmin (2, stereoBus.getNumChannels());
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* data = stereoBus.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            float x = data[i];
            if (hpActive)
                for (auto& f : hpFilters[(size_t) ch])
                    x = f.processSample (x);
            if (lpActive)
                for (auto& f : lpFilters[(size_t) ch])
                    x = f.processSample (x);
            for (int b = 0; b < numBands; ++b)
                if (bandActive[(size_t) b])
                    x = bandFilters[(size_t) ch][(size_t) b].processSample (x);
            data[i] = x;
        }
    }
}

double EqEffect::magnitudeAt (double freqHz)
{
    if (dirty.exchange (false, std::memory_order_relaxed))
        updateCoefficients();

    const double w = juce::MathConstants<double>::twoPi
                     * juce::jlimit (1.0, sampleRateHz * 0.5, freqHz) / sampleRateHz;
    double magnitude = 1.0;
    if (hpActive)
        for (const auto& f : hpFilters[0])
            magnitude *= f.magnitudeAt (w);
    if (lpActive)
        for (const auto& f : lpFilters[0])
            magnitude *= f.magnitudeAt (w);
    for (int b = 0; b < numBands; ++b)
        if (bandActive[(size_t) b])
            magnitude *= bandFilters[0][(size_t) b].magnitudeAt (w);
    return magnitude;
}
