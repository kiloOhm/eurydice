#include "ReverbEffect.h"
#include "model/Ids.h"

namespace
{
constexpr double maxPreDelayMs = 250.0;

// Tank delay lengths at scale 1.0, mutually inharmonic so the modes smear.
constexpr double baseLengthsMs[ReverbEffect::numLines] = { 31.9, 37.3, 41.6, 47.9,
                                                           53.4, 59.9, 67.1, 73.7 };

// Input diffusion allpass lengths, slightly different per side so the early
// build-up is decorrelated before the tank ever sees it.
constexpr double diffuserMs[2][3] = { { 5.3, 8.9, 13.7 },
                                      { 5.9, 9.7, 12.9 } };

// Per-line modulation: everyone drifts at their own speed and depth, so the
// tail chorushes instead of vibrating.
constexpr double lfoRateRatio[ReverbEffect::numLines] = { 1.0, 1.13, 0.87, 1.29,
                                                          0.73, 1.41, 0.93, 1.19 };
constexpr double lfoDepthRatio[ReverbEffect::numLines] = { 1.0, 0.9, 1.1, 0.85,
                                                           1.05, 0.95, 1.15, 0.8 };
constexpr double maxModDepthMs = 1.2;

struct ModeInfo
{
    double sizeScale;    // tank length multiplier
    double diffusion;    // input allpass coefficient
    double modScale;     // modulation depth multiplier
};

// room, chamber, plate, hall, cathedral
constexpr ModeInfo modeTable[] = { { 0.32, 0.50, 0.6 },
                                   { 0.55, 0.72, 0.8 },
                                   { 0.46, 0.88, 0.7 },
                                   { 1.00, 0.68, 1.0 },
                                   { 1.42, 0.78, 0.9 } };

struct ColorInfo
{
    double bandwidthHz;  // lowpass on everything entering the tank
    double modMul;       // the old boxes drifted more
};

// 1970s, 1980s, now
constexpr ColorInfo colorTable[] = { { 9000.0, 1.35 },
                                     { 13500.0, 1.0 },
                                     { 20500.0, 0.75 } };

constexpr double maxSizeScale = 1.42 * 1.6;   // cathedral at full size

double onePoleCoeff (double sampleRate, double freqHz)
{
    return std::exp (-juce::MathConstants<double>::twoPi * freqHz / sampleRate);
}
} // namespace

const juce::String& ReverbEffect::identifier()
{
    static const juce::String id ("builtin:reverb");
    return id;
}

const juce::String& ReverbEffect::displayName()
{
    static const juce::String name ("Reverb");
    return name;
}

const std::vector<fx::ParamSpec>& ReverbEffect::specs()
{
    static const std::vector<fx::ParamSpec> s {
        { ids::fxReverbMode, "Mode",  0.0,     4.0, 1.0,     3.0, {},    0,
          { "Room", "Chamber", "Plate", "Hall", "Cathedral" } },
        { ids::fxColor,    "Color",   0.0,     2.0, 1.0,     1.0, {},    0,
          { "1970s", "1980s", "Now" } },
        { ids::fxDecay,    "Decay",   0.2,    30.0, 0.35,    2.6, " s",  2 },
        { ids::fxSize,     "Size",    0.0,     1.0, 1.0,     0.6, {},    2 },
        { ids::fxPreDelay, "Pre",     0.0,   250.0, 0.5,    12.0, " ms", 0, {}, false, true },
        { ids::fxModRate,  "Rate",    0.05,    4.0, 0.5,     1.0, " Hz", 2 },
        { ids::fxModDepth, "Depth",   0.0,     1.0, 1.0,     0.5, {},    2 },
        { ids::fxDampFreq, "Damp", 1000.0, 20000.0, 0.3,  7500.0, " Hz", 0 },
        { ids::fxWidth,    "Width",   0.0,     1.0, 1.0,     1.0, {},    2 },
        { ids::fxHpFreq,   "Low cut", 20.0, 2000.0, 0.3,   120.0, " Hz", 0, {}, false, true },
        { ids::fxLpFreq,   "Hi cut", 500.0, 20000.0, 0.3, 16000.0, " Hz", 0 },
        { ids::fxMix,      "Mix",     0.0,     1.0, 1.0,    0.25, {},    2 },
    };
    return s;
}

const std::vector<fx::BuiltinPreset>& ReverbEffect::presets()
{
    auto make = [] (juce::String name, double reverbMode, double clr, double decay, double sz,
                    double pre, double rate, double depth, double damp, double w,
                    double low, double high)
    {
        return fx::BuiltinPreset { std::move (name),
            { { ids::fxReverbMode, reverbMode }, { ids::fxColor, clr },
              { ids::fxDecay, decay },           { ids::fxSize, sz },
              { ids::fxPreDelay, pre },          { ids::fxModRate, rate },
              { ids::fxModDepth, depth },        { ids::fxDampFreq, damp },
              { ids::fxWidth, w },               { ids::fxHpFreq, low },
              { ids::fxLpFreq, high } } };
    };

    static const std::vector<fx::BuiltinPreset> p {
        make ("Dream Hall",    3, 1,  4.5, 0.70, 20.0, 0.8, 0.70,  6500, 1.00, 100, 14000),
        make ("Concert Hall",  3, 0,  2.8, 0.65, 24.0, 1.0, 0.60,  5500, 1.00, 120, 10000),
        make ("Bright Hall",   3, 2,  3.2, 0.60, 16.0, 1.1, 0.45, 12000, 1.00, 140, 18000),
        make ("Cathedral",     4, 0,  8.0, 0.80, 40.0, 0.6, 0.65,  4500, 1.00, 100,  9000),
        make ("Lush Space",    3, 0,  5.5, 0.75, 30.0, 2.2, 1.00,  6000, 1.00, 120,  9000),
        make ("Vocal Plate",   2, 1,  2.2, 0.50, 30.0, 1.4, 0.35,  9000, 1.00, 180, 15000),
        make ("Dark Plate",    2, 0,  1.8, 0.45, 20.0, 1.2, 0.50,  4000, 1.00, 150,  8000),
        make ("Vocal Chamber", 1, 1,  1.6, 0.50, 26.0, 1.0, 0.40,  7000, 1.00, 150, 13000),
        make ("Drum Room",     0, 2,  0.9, 0.40,  8.0, 1.6, 0.25,  9500, 0.85, 140, 16000),
        make ("Small Room",    0, 1,  0.6, 0.30,  5.0, 1.3, 0.30,  8000, 0.80, 160, 14000),
        make ("Ambience",      0, 2, 0.45, 0.55,  2.0, 1.8, 0.35, 11000, 0.90, 120, 17000),
        make ("Endless",       4, 1, 25.0, 0.90, 60.0, 0.5, 0.80,  5000, 1.00, 150, 10000),
    };
    return p;
}

void ReverbEffect::prepare (double sampleRate, int maxBlockSize)
{
    sampleRateHz = sampleRate;
    maxPreDelaySamples = (int) (maxPreDelayMs * 0.001 * sampleRate) + 2;
    preDelay.prepare (2, maxPreDelaySamples);

    const double maxModSamples = maxModDepthMs * 0.001 * sampleRate * 1.35 * 1.15;
    maxLineSamples = (int) (baseLengthsMs[numLines - 1] * 0.001 * sampleRate * maxSizeScale
                            + maxModSamples) + 8;
    tank.prepare (numLines, maxLineSamples);

    for (int ch = 0; ch < 2; ++ch)
        for (int stage = 0; stage < 3; ++stage)
            diffusers[(size_t) ch][(size_t) stage].prepare (
                (int) (diffuserMs[ch][stage] * 0.001 * sampleRate));

    wet.setSize (2, juce::jmax (32, maxBlockSize));
    filterDirty.store (true, std::memory_order_relaxed);
    reset();
}

void ReverbEffect::reset()
{
    preDelay.reset();
    tank.reset();
    for (auto& channel : diffusers)
        for (auto& stage : channel)
            stage.reset();
    for (auto& d : damping) d.reset();
    for (auto& d : dcBlockers) d.reset();
    for (auto& b : bandwidth) b.reset();
    for (auto& f : lowCut)  f.reset();
    for (auto& f : highCut) f.reset();
    wet.clear();
    lengthsPrimed = false;
}

void ReverbEffect::setParameter (const juce::Identifier& paramId, double value)
{
    if (paramId == ids::fxDecay)           decaySeconds.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxSize)       size.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxPreDelay)   preDelayMs.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxModRate)    modRateHz.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxModDepth)   modDepth.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxDampFreq)   dampFreqHz.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxWidth)      width.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxMix)        mix.store ((float) value, std::memory_order_relaxed);
    else if (paramId == ids::fxReverbMode)
        mode.store (juce::jlimit (0, 4, (int) std::lround (value)), std::memory_order_relaxed);
    else if (paramId == ids::fxColor)
        color.store (juce::jlimit (0, 2, (int) std::lround (value)), std::memory_order_relaxed);
    else if (paramId == ids::fxHpFreq)
    {
        lowCutHz.store ((float) value, std::memory_order_relaxed);
        filterDirty.store (true, std::memory_order_relaxed);
    }
    else if (paramId == ids::fxLpFreq)
    {
        highCutHz.store ((float) value, std::memory_order_relaxed);
        filterDirty.store (true, std::memory_order_relaxed);
    }
}

void ReverbEffect::process (juce::AudioBuffer<float>& stereoBus, int numSamples, const Context& context)
{
    juce::ignoreUnused (context);
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (2, stereoBus.getNumChannels());
    if (numCh < 2 || numSamples < 1 || numSamples > wet.getNumSamples())
        return;

    if (filterDirty.exchange (false, std::memory_order_relaxed))
        for (int ch = 0; ch < 2; ++ch)
        {
            lowCut[(size_t) ch].setHighPass (sampleRateHz, lowCutHz.load (std::memory_order_relaxed), 0.707);
            highCut[(size_t) ch].setLowPass (sampleRateHz, highCutHz.load (std::memory_order_relaxed), 0.707);
        }

    const auto& modeInfo = modeTable[mode.load (std::memory_order_relaxed)];
    const auto& colorInfo = colorTable[color.load (std::memory_order_relaxed)];

    const double decay = juce::jmax (0.05, (double) decaySeconds.load (std::memory_order_relaxed));
    const double lengthScale = modeInfo.sizeScale
        * (0.4 + 1.2 * juce::jlimit (0.0, 1.0, (double) size.load (std::memory_order_relaxed)));
    const float diffusion = (float) modeInfo.diffusion;

    // Per-line targets: length in samples, and the feedback gain that makes a
    // signal circulating that line fall 60 dB in `decay` seconds.
    float targetLength[numLines];
    float lineGain[numLines];
    float depthSamples[numLines];
    double phaseInc[numLines];

    const double depthBase = modDepth.load (std::memory_order_relaxed)
        * modeInfo.modScale * colorInfo.modMul * maxModDepthMs * 0.001 * sampleRateHz;
    const double rate = modRateHz.load (std::memory_order_relaxed);

    for (int i = 0; i < numLines; ++i)
    {
        const double len = juce::jlimit (16.0, (double) (maxLineSamples - 4),
                                         baseLengthsMs[i] * 0.001 * sampleRateHz * lengthScale);
        targetLength[i] = (float) len;
        lineGain[i] = (float) std::pow (10.0, -3.0 * len / (decay * sampleRateHz));
        depthSamples[i] = (float) (depthBase * lfoDepthRatio[i]);
        phaseInc[i] = juce::MathConstants<double>::twoPi * rate * lfoRateRatio[i] / sampleRateHz;
    }

    if (! lengthsPrimed)
    {
        for (int i = 0; i < numLines; ++i)
            smoothedLength[i] = targetLength[i];
        lengthsPrimed = true;
    }

    const auto dampCoeff = (float) onePoleCoeff (sampleRateHz, dampFreqHz.load (std::memory_order_relaxed));
    const bool bandwidthActive = colorInfo.bandwidthHz < sampleRateHz * 0.5 * 0.98;
    const auto bandwidthCoeff = (float) onePoleCoeff (sampleRateHz, colorInfo.bandwidthHz);
    const auto dcCoeff = (float) (1.0 - juce::MathConstants<double>::twoPi * 20.0 / sampleRateHz);

    // ~80 ms glide on the line lengths, so size and mode changes bend the tail
    // instead of tearing it.
    const auto lengthGlide = (float) std::exp (-1.0 / (0.08 * sampleRateHz));

    const auto preSamples = (float) juce::jlimit (
        1.0, (double) maxPreDelaySamples,
        (double) preDelayMs.load (std::memory_order_relaxed) * 0.001 * sampleRateHz);

    const float widthAmount = juce::jlimit (0.0f, 1.0f, width.load (std::memory_order_relaxed));
    const auto gains = fx::mixGains (mix.load (std::memory_order_relaxed));
    const bool lowCutActive = lowCutHz.load (std::memory_order_relaxed) > 20.5f;
    const bool highCutActive = highCutHz.load (std::memory_order_relaxed) < 19500.0f;

    auto* left = stereoBus.getWritePointer (0);
    auto* right = stereoBus.getWritePointer (1);

    for (int i = 0; i < numSamples; ++i)
    {
        // Pre-delay ahead of everything, including the diffusion.
        float inputs[2];
        for (int ch = 0; ch < 2; ++ch)
            inputs[ch] = preDelay.read (ch, preSamples);
        preDelay.write (0, left[i]);
        preDelay.write (1, right[i]);
        preDelay.advance();

        // Colour bandwidth, then diffusion, before the signal enters the tank.
        for (int ch = 0; ch < 2; ++ch)
        {
            if (bandwidthActive)
                inputs[ch] = bandwidth[(size_t) ch].processSample (inputs[ch], bandwidthCoeff);
            for (auto& stage : diffusers[(size_t) ch])
                inputs[ch] = stage.processSample (inputs[ch], diffusion);
        }

        // Read every line at its modulated length.
        float taps[numLines];
        float tapSum = 0.0f;
        for (int line = 0; line < numLines; ++line)
        {
            smoothedLength[line] = targetLength[line]
                + lengthGlide * (smoothedLength[line] - targetLength[line]);
            lfoPhase[line] += phaseInc[line];
            if (lfoPhase[line] > juce::MathConstants<double>::twoPi)
                lfoPhase[line] -= juce::MathConstants<double>::twoPi;

            const float readLength = smoothedLength[line]
                + depthSamples[line] * (float) std::sin (lfoPhase[line]);
            taps[line] = tank.read (line, readLength);
            tapSum += taps[line];
        }

        // Householder feedback (energy preserving) with decay applied per
        // line, fresh input injected into alternating lines. The damping
        // filter sits inside the line, so even the first bounce is darkened.
        const float householder = 0.25f * tapSum;   // 2/N with N = 8
        for (int line = 0; line < numLines; ++line)
        {
            float fb = (taps[line] - householder) * lineGain[line]
                + 0.6f * inputs[line & 1];
            fb = damping[(size_t) line].processSample (fb, dampCoeff);
            fb = dcBlockers[(size_t) line].processSample (fb, dcCoeff);
            tank.write (line, fb);
        }
        tank.advance();

        // Alternating-sign taps give two decorrelated outputs from one tank.
        float outL = 0.4f * (taps[0] - taps[2] + taps[4] - taps[6]);
        float outR = 0.4f * (taps[1] - taps[3] + taps[5] - taps[7]);

        const float mid = 0.5f * (outL + outR);
        const float side = 0.5f * (outL - outR) * widthAmount;
        wet.setSample (0, i, mid + side);
        wet.setSample (1, i, mid - side);
    }

    for (int ch = 0; ch < 2; ++ch)
    {
        auto* dryData = stereoBus.getWritePointer (ch);
        auto* wetData = wet.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            float w = wetData[i];
            if (lowCutActive)
                w = lowCut[(size_t) ch].processSample (w);
            if (highCutActive)
                w = highCut[(size_t) ch].processSample (w);
            dryData[i] = dryData[i] * gains.dry + w * gains.wet;
        }
    }
}
