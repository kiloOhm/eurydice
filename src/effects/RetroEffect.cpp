#include "RetroEffect.h"
#include "SaturatorEffect.h"
#include "model/Ids.h"

namespace
{
constexpr auto relaxed = std::memory_order_relaxed;

// Everything a preset sets, so a preset reads as a description of the rack
// rather than as thirty-six unlabelled numbers. Fields the preset leaves out
// fall back to these, which are the effect's own defaults.
struct Setup
{
    double magnitude = 1.0;

    double wobbleOn = 0.0, wobbleAmount = 0.30;
    double wobbleType = 0.0, wobbleRate = 0.8, flutter = 0.35, drift = 0.25;

    double distortOn = 0.0, distortAmount = 0.30;
    double distortType = 0.0, distortTone = 20000.0, bias = 0.0;

    double digitalOn = 0.0, digitalAmount = 0.35;
    double bits = 12.0, downsample = 24000.0, jitter = 0.0;

    double noiseOn = 0.0, noiseAmount = 0.20;
    double noiseType = 0.0, noiseTone = 8000.0, follow = 0.0, noiseWidth = 1.0;

    double spaceOn = 0.0, spaceAmount = 0.30;
    double size = 0.45, damp = 4000.0, pre = 0.0, width = 1.0;

    double dropsOn = 0.0, dropsAmount = 0.35;
    double dropRate = 0.5, dropLength = 180.0, dropVary = 0.5;

    double oversample = 1.0, output = 0.0;
};

fx::BuiltinPreset makePreset (juce::String name, const Setup& s)
{
    return { std::move (name),
        { { ids::fxMagnitude, s.magnitude },
          { ids::fxWobbleOn, s.wobbleOn },   { ids::fxWobbleAmount, s.wobbleAmount },
          { ids::fxWobbleType, s.wobbleType }, { ids::fxWobbleRate, s.wobbleRate },
          { ids::fxFlutter, s.flutter },     { ids::fxDrift, s.drift },
          { ids::fxDistortOn, s.distortOn }, { ids::fxDistortAmount, s.distortAmount },
          { ids::fxDistortType, s.distortType }, { ids::fxDistortTone, s.distortTone },
          { ids::fxBias, s.bias },
          { ids::fxDigitalOn, s.digitalOn }, { ids::fxDigitalAmount, s.digitalAmount },
          { ids::fxBits, s.bits },           { ids::fxDownsample, s.downsample },
          { ids::fxJitter, s.jitter },
          { ids::fxNoiseOn, s.noiseOn },     { ids::fxNoiseAmount, s.noiseAmount },
          { ids::fxNoiseType, s.noiseType }, { ids::fxNoiseTone, s.noiseTone },
          { ids::fxNoiseFollow, s.follow },  { ids::fxNoiseWidth, s.noiseWidth },
          { ids::fxSpaceOn, s.spaceOn },     { ids::fxSpaceAmount, s.spaceAmount },
          { ids::fxSize, s.size },           { ids::fxDampFreq, s.damp },
          { ids::fxPreDelay, s.pre },        { ids::fxWidth, s.width },
          { ids::fxDropsOn, s.dropsOn },     { ids::fxDropsAmount, s.dropsAmount },
          { ids::fxDropRate, s.dropRate },   { ids::fxDropLength, s.dropLength },
          { ids::fxDropVary, s.dropVary },
          { ids::fxOversample, s.oversample }, { ids::fxOutput, s.output } } };
}
} // namespace

const juce::String& RetroEffect::identifier()
{
    static const juce::String id ("builtin:retro");
    return id;
}

const juce::String& RetroEffect::displayName()
{
    static const juce::String name ("Retro");
    return name;
}

const std::array<RetroEffect::ModuleInfo, RetroEffect::numModules>& RetroEffect::modules()
{
    static const std::array<ModuleInfo, numModules> list { {
        { "Wobble",  &ids::fxWobbleOn,  &ids::fxWobbleAmount },
        { "Distort", &ids::fxDistortOn, &ids::fxDistortAmount },
        { "Digital", &ids::fxDigitalOn, &ids::fxDigitalAmount },
        { "Noise",   &ids::fxNoiseOn,   &ids::fxNoiseAmount },
        { "Space",   &ids::fxSpaceOn,   &ids::fxSpaceAmount },
        { "Drops",   &ids::fxDropsOn,   &ids::fxDropsAmount },
    } };
    return list;
}

const juce::StringArray& RetroEffect::noiseTypeNames()
{
    static const juce::StringArray names { "Vinyl", "Crackle", "Hiss", "Studio",
                                           "Hum 50", "Hum 60", "Radio" };
    return names;
}

const juce::StringArray& RetroEffect::wobbleTypeNames()
{
    static const juce::StringArray names { "Tape", "Vinyl", "Random", "Sine" };
    return names;
}

const juce::StringArray& RetroEffect::distortTypeNames()
{
    static const juce::StringArray names { "Tube", "Tape", "Diode", "Fuzz", "Fold", "Rectify" };
    return names;
}

const std::vector<fx::ParamSpec>& RetroEffect::specs()
{
    static const std::vector<fx::ParamSpec> s = []
    {
        std::vector<fx::ParamSpec> list;

        // The rack draws these thirteen: one LED and one slider per module, plus
        // the Magnitude that scales every amount at once. A fresh instance comes
        // up with gentle tape wobble and tape drive, because an effect called
        // Retro that does nothing until you find a knob is no use — but with the
        // generators (Noise, Space, Drops) switched off, so dropping it on a bus
        // can't put anything into a passage that was silent.
        list.push_back (fx::ParamSpec { ids::fxMagnitude, "Magnitude", 0.0, 1.0, 1.0, 1.0, {}, 2 }
                            .drawnByDisplay());

        static const double startsOn[numModules] { 1.0, 1.0, 0.0, 0.0, 0.0, 0.0 };
        static const double startsAt[numModules] { 0.30, 0.30, 0.35, 0.20, 0.30, 0.35 };
        for (int m = 0; m < numModules; ++m)
        {
            const auto& info = modules()[(size_t) m];
            list.push_back (fx::ParamSpec { *info.enableId, info.name, 0.0, 1.0, 1.0,
                                            startsOn[m], {}, 0, { "Off", "On" } }.drawnByDisplay());
            list.push_back (fx::ParamSpec { *info.amountId, info.name + " Amount",
                                            0.0, 1.0, 1.0, startsAt[m], {}, 2 }.drawnByDisplay());
        }

        list.push_back (fx::ParamSpec { ids::fxWobbleType, "Type", 0.0, 3.0, 1.0, 0.0,
                                        {}, 0, wobbleTypeNames() }.inGroup ("Wobble"));
        list.push_back ({ ids::fxWobbleRate, "Rate", 0.05, 8.0, 0.4, 0.8, " Hz", 2 });
        list.push_back ({ ids::fxFlutter, "Flutter", 0.0, 1.0, 1.0, 0.35, {}, 2 });
        list.push_back ({ ids::fxDrift, "Drift", 0.0, 1.0, 1.0, 0.25, {}, 2 });

        list.push_back (fx::ParamSpec { ids::fxDistortType, "Type", 0.0, 5.0, 1.0, 0.0,
                                        {}, 0, distortTypeNames() }.inGroup ("Distort"));
        list.push_back ({ ids::fxDistortTone, "Tone", 500.0, 20000.0, 0.35, 20000.0, " Hz", 0 });
        list.push_back ({ ids::fxBias, "Bias", 0.0, 1.0, 1.0, 0.0, {}, 2 });

        list.push_back (fx::ParamSpec { ids::fxBits, "Bits", 2.0, 16.0, 1.0, 12.0, {}, 1 }
                            .inGroup ("Digital"));
        list.push_back ({ ids::fxDownsample, "Rate", 1000.0, 48000.0, 0.3, 24000.0, " Hz", 0 });
        list.push_back ({ ids::fxJitter, "Jitter", 0.0, 1.0, 1.0, 0.0, {}, 2 });

        list.push_back (fx::ParamSpec { ids::fxNoiseType, "Type", 0.0, 6.0, 1.0, 2.0,
                                        {}, 0, noiseTypeNames() }.inGroup ("Noise"));
        list.push_back ({ ids::fxNoiseTone, "Tone", 200.0, 18000.0, 0.35, 8000.0, " Hz", 0 });
        // Negative ducks the noise under the signal, positive lifts it with it.
        list.push_back ({ ids::fxNoiseFollow, "Follow", -1.0, 1.0, 1.0, 0.0, {}, 2 });
        list.push_back ({ ids::fxNoiseWidth, "Width", 0.0, 1.0, 1.0, 1.0, {}, 2 });

        list.push_back (fx::ParamSpec { ids::fxSize, "Size", 0.0, 1.0, 1.0, 0.45, {}, 2 }
                            .inGroup ("Space"));
        list.push_back ({ ids::fxDampFreq, "Damp", 500.0, 16000.0, 0.35, 4000.0, " Hz", 0 });
        list.push_back ({ ids::fxPreDelay, "Pre", 0.0, 200.0, 0.6, 0.0, " ms", 0 });
        list.push_back ({ ids::fxWidth, "Width", 0.0, 1.0, 1.0, 1.0, {}, 2 });

        list.push_back (fx::ParamSpec { ids::fxDropRate, "Rate", 0.05, 8.0, 0.4, 0.5, " Hz", 2 }
                            .inGroup ("Drops"));
        list.push_back ({ ids::fxDropLength, "Length", 10.0, 2000.0, 0.4, 180.0, " ms", 0 });
        list.push_back ({ ids::fxDropVary, "Vary", 0.0, 1.0, 1.0, 0.5, {}, 2 });

        list.push_back (fx::ParamSpec { ids::fxOversample, "Quality", 0.0, 3.0, 1.0, 1.0,
                                        {}, 0, { "1x", "2x", "4x", "8x" } }.inGroup ("Output"));
        list.push_back ({ ids::fxOutput, "Out", -24.0, 12.0, 1.0, 0.0, " dB", 1 });
        list.push_back ({ ids::fxMix, "Mix", 0.0, 1.0, 1.0, 1.0, {}, 2 });

        return list;
    }();
    return s;
}

const std::vector<fx::BuiltinPreset>& RetroEffect::presets()
{
    // Type indices: wobble 0 Tape, 1 Vinyl, 2 Random, 3 Sine. Distort 0 Tube,
    // 1 Tape, 2 Diode, 3 Fuzz, 4 Fold, 5 Rectify. Noise 0 Vinyl, 1 Crackle,
    // 2 Hiss, 3 Studio, 4 Hum 50, 5 Hum 60, 6 Radio.
    static const std::vector<fx::BuiltinPreset> p {
        makePreset ("Warm Tape", { .wobbleOn = 1.0, .wobbleAmount = 0.18, .wobbleRate = 0.7,
                                   .flutter = 0.25, .drift = 0.2,
                                   .distortOn = 1.0, .distortAmount = 0.3, .distortType = 1.0,
                                   .distortTone = 14000.0,
                                   .noiseOn = 1.0, .noiseAmount = 0.12, .noiseType = 2.0,
                                   .noiseTone = 7000.0 }),
        makePreset ("Cassette", { .wobbleOn = 1.0, .wobbleAmount = 0.32, .wobbleRate = 1.1,
                                  .flutter = 0.5, .drift = 0.3,
                                  .distortOn = 1.0, .distortAmount = 0.38, .distortType = 1.0,
                                  .distortTone = 9000.0,
                                  .digitalOn = 1.0, .digitalAmount = 0.2, .bits = 13.0,
                                  .downsample = 22000.0,
                                  .noiseOn = 1.0, .noiseAmount = 0.22, .noiseType = 2.0,
                                  .noiseTone = 6000.0, .follow = -0.3,
                                  .dropsOn = 1.0, .dropsAmount = 0.2, .dropRate = 0.25,
                                  .dropLength = 260.0, .output = -1.0 }),
        makePreset ("Old Vinyl", { .wobbleOn = 1.0, .wobbleAmount = 0.16, .wobbleType = 1.0,
                                   .wobbleRate = 0.55, .flutter = 0.1, .drift = 0.4,
                                   .distortOn = 1.0, .distortAmount = 0.2, .distortTone = 12000.0,
                                   .noiseOn = 1.0, .noiseAmount = 0.3, .noiseType = 0.0,
                                   .noiseTone = 9000.0 }),
        makePreset ("Dusty Vinyl", { .wobbleOn = 1.0, .wobbleAmount = 0.28, .wobbleType = 1.0,
                                     .wobbleRate = 0.55, .flutter = 0.15, .drift = 0.6,
                                     .distortOn = 1.0, .distortAmount = 0.3, .distortTone = 6500.0,
                                     .noiseOn = 1.0, .noiseAmount = 0.45, .noiseType = 0.0,
                                     .noiseTone = 4500.0,
                                     .dropsOn = 1.0, .dropsAmount = 0.3, .dropRate = 0.4,
                                     .dropLength = 140.0, .dropVary = 0.8, .output = -1.5 }),
        makePreset ("Crackle Layer", { .noiseOn = 1.0, .noiseAmount = 0.4, .noiseType = 1.0,
                                       .noiseTone = 11000.0 }),
        makePreset ("Tape Hiss", { .noiseOn = 1.0, .noiseAmount = 0.22, .noiseType = 2.0,
                                   .noiseTone = 9000.0 }),
        makePreset ("Studio Air", { .noiseOn = 1.0, .noiseAmount = 0.35, .noiseType = 3.0,
                                    .noiseTone = 3000.0, .follow = 0.5 }),
        makePreset ("Mains Hum", { .noiseOn = 1.0, .noiseAmount = 0.2, .noiseType = 4.0,
                                   .noiseTone = 1200.0, .noiseWidth = 0.0 }),
        makePreset ("AM Radio", { .distortOn = 1.0, .distortAmount = 0.45, .distortType = 2.0,
                                  .distortTone = 3000.0, .bias = 0.35,
                                  .digitalOn = 1.0, .digitalAmount = 0.5, .bits = 9.0,
                                  .downsample = 11000.0,
                                  .noiseOn = 1.0, .noiseAmount = 0.3, .noiseType = 6.0,
                                  .noiseTone = 4000.0, .noiseWidth = 0.2,
                                  .spaceOn = 1.0, .spaceAmount = 0.15, .size = 0.2,
                                  .damp = 1800.0, .oversample = 2.0, .output = -1.0 }),
        makePreset ("Broken Radio", { .wobbleOn = 1.0, .wobbleAmount = 0.5, .wobbleType = 2.0,
                                      .wobbleRate = 2.5, .flutter = 0.7, .drift = 0.8,
                                      .distortOn = 1.0, .distortAmount = 0.7, .distortType = 3.0,
                                      .distortTone = 2600.0, .bias = 0.5,
                                      .digitalOn = 1.0, .digitalAmount = 0.7, .bits = 6.0,
                                      .downsample = 7000.0, .jitter = 0.4,
                                      .noiseOn = 1.0, .noiseAmount = 0.45, .noiseType = 6.0,
                                      .noiseTone = 3500.0,
                                      .dropsOn = 1.0, .dropsAmount = 0.7, .dropRate = 1.2,
                                      .dropLength = 90.0, .dropVary = 0.9,
                                      .oversample = 2.0, .output = -4.0 }),
        makePreset ("Telephone", { .distortOn = 1.0, .distortAmount = 0.4, .distortType = 2.0,
                                   .distortTone = 2200.0, .bias = 0.4,
                                   .digitalOn = 1.0, .digitalAmount = 0.4, .bits = 10.0,
                                   .downsample = 8000.0,
                                   .noiseOn = 1.0, .noiseAmount = 0.15, .noiseType = 2.0,
                                   .noiseTone = 2500.0, .noiseWidth = 0.0,
                                   .oversample = 2.0, .output = -1.0 }),
        makePreset ("8-Bit Toy", { .digitalOn = 1.0, .digitalAmount = 1.0, .bits = 6.0,
                                   .downsample = 8000.0, .output = -2.0 }),
        makePreset ("12-Bit Sampler", { .digitalOn = 1.0, .digitalAmount = 1.0, .bits = 12.0,
                                        .downsample = 26040.0 }),
        makePreset ("SP Crunch", { .distortOn = 1.0, .distortAmount = 0.35, .distortType = 0.0,
                                   .distortTone = 15000.0,
                                   .digitalOn = 1.0, .digitalAmount = 1.0, .bits = 12.0,
                                   .downsample = 26040.0, .oversample = 2.0, .output = -1.0 }),
        makePreset ("Lo-Fi Beat", { .wobbleOn = 1.0, .wobbleAmount = 0.22, .wobbleRate = 0.9,
                                    .flutter = 0.3, .drift = 0.3,
                                    .distortOn = 1.0, .distortAmount = 0.32, .distortType = 1.0,
                                    .distortTone = 8000.0,
                                    .digitalOn = 1.0, .digitalAmount = 0.5, .bits = 12.0,
                                    .downsample = 20000.0,
                                    .noiseOn = 1.0, .noiseAmount = 0.28, .noiseType = 0.0,
                                    .noiseTone = 6000.0,
                                    .spaceOn = 1.0, .spaceAmount = 0.14, .size = 0.3,
                                    .damp = 2600.0, .output = -1.0 }),
        makePreset ("VHS", { .wobbleOn = 1.0, .wobbleAmount = 0.35, .wobbleRate = 1.6,
                             .flutter = 0.75, .drift = 0.35,
                             .distortOn = 1.0, .distortAmount = 0.3, .distortTone = 7000.0,
                             .noiseOn = 1.0, .noiseAmount = 0.3, .noiseType = 2.0,
                             .noiseTone = 5000.0, .follow = -0.25,
                             .spaceOn = 1.0, .spaceAmount = 0.12, .size = 0.25, .damp = 2200.0,
                             .dropsOn = 1.0, .dropsAmount = 0.35, .dropRate = 0.6,
                             .dropLength = 120.0, .dropVary = 0.7, .output = -1.5 }),
        makePreset ("Wow & Flutter", { .wobbleOn = 1.0, .wobbleAmount = 0.55, .wobbleRate = 1.2,
                                       .flutter = 0.6, .drift = 0.35 }),
        makePreset ("Turntable Warp", { .wobbleOn = 1.0, .wobbleAmount = 0.7, .wobbleType = 1.0,
                                        .wobbleRate = 0.55, .flutter = 0.1, .drift = 0.7 }),
        makePreset ("Random Speed", { .wobbleOn = 1.0, .wobbleAmount = 0.6, .wobbleType = 2.0,
                                      .wobbleRate = 3.0, .flutter = 0.4, .drift = 0.9 }),
        makePreset ("Fuzz Box", { .distortOn = 1.0, .distortAmount = 0.8, .distortType = 3.0,
                                  .distortTone = 5000.0, .bias = 0.3,
                                  .oversample = 2.0, .output = -3.0 }),
        makePreset ("Tube Glue", { .distortOn = 1.0, .distortAmount = 0.22, .distortType = 0.0,
                                   .distortTone = 18000.0 }),
        makePreset ("Fold Damage", { .distortOn = 1.0, .distortAmount = 0.6, .distortType = 4.0,
                                     .distortTone = 9000.0, .oversample = 3.0, .output = -4.0 }),
        makePreset ("Dropout Tape", { .wobbleOn = 1.0, .wobbleAmount = 0.3, .wobbleRate = 0.8,
                                      .flutter = 0.4, .drift = 0.4,
                                      .noiseOn = 1.0, .noiseAmount = 0.2, .noiseType = 2.0,
                                      .noiseTone = 6000.0,
                                      .dropsOn = 1.0, .dropsAmount = 0.85, .dropRate = 0.8,
                                      .dropLength = 200.0, .dropVary = 0.8 }),
        makePreset ("Ghost Room", { .noiseOn = 1.0, .noiseAmount = 0.2, .noiseType = 3.0,
                                    .noiseTone = 2400.0,
                                    .spaceOn = 1.0, .spaceAmount = 0.55, .size = 0.8,
                                    .damp = 1600.0, .pre = 40.0, .output = -2.0 }),
        makePreset ("Full Decay", { .wobbleOn = 1.0, .wobbleAmount = 0.6, .wobbleRate = 1.4,
                                    .flutter = 0.7, .drift = 0.7,
                                    .distortOn = 1.0, .distortAmount = 0.6, .distortType = 1.0,
                                    .distortTone = 4500.0, .bias = 0.3,
                                    .digitalOn = 1.0, .digitalAmount = 0.7, .bits = 8.0,
                                    .downsample = 12000.0, .jitter = 0.3,
                                    .noiseOn = 1.0, .noiseAmount = 0.4, .noiseType = 0.0,
                                    .noiseTone = 5000.0,
                                    .spaceOn = 1.0, .spaceAmount = 0.3, .size = 0.5,
                                    .damp = 2000.0,
                                    .dropsOn = 1.0, .dropsAmount = 0.6, .dropRate = 0.9,
                                    .dropLength = 150.0, .dropVary = 0.8,
                                    .oversample = 2.0, .output = -4.0 }),
        // Same rack as Full Decay, pulled back by Magnitude alone — the pair is
        // there to show what that one control does.
        makePreset ("Subtle Age", { .magnitude = 0.22,
                                    .wobbleOn = 1.0, .wobbleAmount = 0.6, .wobbleRate = 1.4,
                                    .flutter = 0.7, .drift = 0.7,
                                    .distortOn = 1.0, .distortAmount = 0.6, .distortType = 1.0,
                                    .distortTone = 4500.0, .bias = 0.3,
                                    .digitalOn = 1.0, .digitalAmount = 0.7, .bits = 8.0,
                                    .downsample = 12000.0, .jitter = 0.3,
                                    .noiseOn = 1.0, .noiseAmount = 0.4, .noiseType = 0.0,
                                    .noiseTone = 5000.0,
                                    .spaceOn = 1.0, .spaceAmount = 0.3, .size = 0.5,
                                    .damp = 2000.0,
                                    .dropsOn = 1.0, .dropsAmount = 0.6, .dropRate = 0.9,
                                    .dropLength = 150.0, .dropVary = 0.8,
                                    .oversample = 2.0 }),
    };
    return p;
}

float RetroEffect::shapeSample (int type, float x) noexcept
{
    switch ((DistortType) type)
    {
        // Four of the six curves are the Saturator's, so "Tape" means the same
        // bend in both effects.
        case DistortType::tube:
            return SaturatorEffect::shapeSample ((int) SaturatorEffect::Style::tube, x);
        case DistortType::tape:
            return SaturatorEffect::shapeSample ((int) SaturatorEffect::Style::tape, x);
        case DistortType::fold:
            return SaturatorEffect::shapeSample ((int) SaturatorEffect::Style::fold, x);
        case DistortType::rectify:
            return SaturatorEffect::shapeSample ((int) SaturatorEffect::Style::rectify, x);

        case DistortType::diode:
            // Germanium-ish: the halves have very different knees, so the
            // positive peaks squash first and the result is full of evens.
            return x >= 0.0f ? std::tanh (1.7f * x) : std::tanh (0.65f * x);

        case DistortType::fuzz:
        default:
            // Nearly square once it is driven, with a steeper term mixed in for
            // the buzz that sits on top of the fundamental.
            return 0.85f * std::tanh (6.0f * x) + 0.15f * std::tanh (18.0f * x);
    }
}

void RetroEffect::prepare (double sampleRate, int maxBlockSize)
{
    sampleRateHz = sampleRate;
    const int block = juce::jmax (32, maxBlockSize);

    latencySamples[0] = 0;
    for (size_t i = 0; i < oversamplers.size(); ++i)
    {
        auto os = std::make_unique<juce::dsp::Oversampling<float>> (
            2, i + 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
        os->initProcessing ((size_t) block);
        latencySamples[i + 1] = (int) std::lround (os->getLatencyInSamples());
        oversamplers[i] = std::move (os);
    }

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) block, 2 };
    distortTone.prepare (spec);
    distortTone.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    noiseTone.prepare (spec);
    noiseTone.setType (juce::dsp::StateVariableTPTFilterType::lowpass);

    wobbleLine.prepare (2, (int) ((wobbleBaseMs + wobbleDepthMs + 2.0) * 0.001 * sampleRate) + 8);
    preDelay.prepare (2, (int) (maxPreDelayMs * 0.001 * sampleRate) + 8);
    // Enough for the wobble's base delay plus the deepest oversampler latency.
    dryDelay.prepare (2, (int) ((wobbleBaseMs + 2.0) * 0.001 * sampleRate) + 256);

    dry.setSize (2, block);
    spaceBuffer.setSize (2, block);
    reverb.setSampleRate (sampleRate);

    clickDecay = (float) std::exp (-1.0 / (0.0012 * sampleRate));
    rumbleCoeff = 1.0f - (float) std::exp (-juce::MathConstants<double>::twoPi * 120.0 / sampleRate);

    activeOversample = -1;
    reset();
}

void RetroEffect::reset()
{
    for (auto& os : oversamplers)
        if (os != nullptr)
            os->reset();
    distortTone.reset();
    noiseTone.reset();
    wobbleLine.reset();
    preDelay.reset();
    dryDelay.reset();
    reverb.reset();
    dry.clear();
    spaceBuffer.clear();

    wowPhase = flutterPhase = 0.0;
    driftValue = driftTarget = 0.0f;
    wobbleRunning = false;

    dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0f;

    holdPhase = 1.0;   // so the first sample takes a fresh hold
    held[0] = held[1] = 0.0f;

    for (auto& channel : pinkState)
        for (auto& pole : channel)
            pole = 0.0f;
    clickAmp[0] = clickAmp[1] = 0.0f;
    rumbleState = 0.0f;
    humPhase = 0.0;
    burstEnv = 0.5f;
    burstCountdown = 0;
    inputEnv = 0.0f;

    dropCountdown = -1;
    dropPos = dropLength = 0;
    dropDepth = 0.0f;

    displayWobble.store (0.0f, relaxed);
    displayDrop.store (0.0f, relaxed);
    displayNoise.store (0.0f, relaxed);
}

void RetroEffect::setParameter (const juce::Identifier& paramId, double value)
{
    for (int m = 0; m < numModules; ++m)
    {
        const auto& info = modules()[(size_t) m];
        if (paramId == *info.enableId)
        {
            moduleState[(size_t) m].on.store (value >= 0.5, relaxed);
            return;
        }
        if (paramId == *info.amountId)
        {
            moduleState[(size_t) m].amount.store ((float) value, relaxed);
            return;
        }
    }

    if (paramId == ids::fxMagnitude)         magnitude.store ((float) value, relaxed);
    else if (paramId == ids::fxWobbleType)   wobbleType.store ((int) std::lround (value), relaxed);
    else if (paramId == ids::fxWobbleRate)   wobbleRateHz.store ((float) value, relaxed);
    else if (paramId == ids::fxFlutter)      flutterAmount.store ((float) value, relaxed);
    else if (paramId == ids::fxDrift)        driftAmount.store ((float) value, relaxed);
    else if (paramId == ids::fxDistortType)  distortType.store ((int) std::lround (value), relaxed);
    else if (paramId == ids::fxDistortTone)  distortToneHz.store ((float) value, relaxed);
    else if (paramId == ids::fxBias)         distortBias.store ((float) value, relaxed);
    else if (paramId == ids::fxBits)         bitDepth.store ((float) value, relaxed);
    else if (paramId == ids::fxDownsample)   downsampleHz.store ((float) value, relaxed);
    else if (paramId == ids::fxJitter)       jitterAmount.store ((float) value, relaxed);
    else if (paramId == ids::fxNoiseType)    noiseType.store ((int) std::lround (value), relaxed);
    else if (paramId == ids::fxNoiseTone)    noiseToneHz.store ((float) value, relaxed);
    else if (paramId == ids::fxNoiseFollow)  noiseFollow.store ((float) value, relaxed);
    else if (paramId == ids::fxNoiseWidth)   noiseWidth.store ((float) value, relaxed);
    else if (paramId == ids::fxSize)         spaceSize.store ((float) value, relaxed);
    else if (paramId == ids::fxDampFreq)     spaceDampHz.store ((float) value, relaxed);
    else if (paramId == ids::fxPreDelay)     spacePreMs.store ((float) value, relaxed);
    else if (paramId == ids::fxWidth)        spaceWidthAmount.store ((float) value, relaxed);
    else if (paramId == ids::fxDropRate)     dropRateHz.store ((float) value, relaxed);
    else if (paramId == ids::fxDropLength)   dropLengthMs.store ((float) value, relaxed);
    else if (paramId == ids::fxDropVary)     dropVary.store ((float) value, relaxed);
    else if (paramId == ids::fxOversample)   oversampleIndex.store ((int) std::lround (value), relaxed);
    else if (paramId == ids::fxOutput)       outputDb.store ((float) value, relaxed);
    else if (paramId == ids::fxMix)          mix.store ((float) value, relaxed);
}

float RetroEffect::amountOf (Module m) const noexcept
{
    const auto& state = moduleState[(size_t) m];
    if (! state.on.load (relaxed))
        return 0.0f;
    return juce::jlimit (0.0f, 1.0f, state.amount.load (relaxed))
         * juce::jlimit (0.0f, 1.0f, magnitude.load (relaxed));
}

// ============================== wobble ===============================

float RetroEffect::wobbleMod (int type, double phaseOffset, float flutterAmt, float driftAmt) const noexcept
{
    constexpr auto twoPi = juce::MathConstants<double>::twoPi;
    const auto wow  = (float) std::sin (twoPi * (wowPhase + phaseOffset));
    const auto fast = (float) std::sin (twoPi * (flutterPhase + phaseOffset));

    float mod = wow;
    switch ((WobbleType) type)
    {
        case WobbleType::vinyl:
            // Once-per-revolution warp: a strong fundamental with a little
            // second harmonic, riding on the platter's slow wander.
            mod = 0.80f * wow
                + 0.16f * (float) std::sin (twoPi * 2.0 * (wowPhase + phaseOffset))
                + 0.70f * driftAmt * driftValue
                + 0.10f * flutterAmt * fast;
            break;

        case WobbleType::random:
            // The wander *is* the shape here, so Drift widens its range rather
            // than deciding whether there is any.
            mod = driftValue * (0.6f + 0.4f * driftAmt) + 0.25f * flutterAmt * fast;
            break;

        case WobbleType::sine:
            mod = wow;
            break;

        case WobbleType::tape:
        default:
            mod = 0.70f * wow + 0.30f * flutterAmt * fast + 0.45f * driftAmt * driftValue;
            break;
    }
    return juce::jlimit (-1.0f, 1.0f, mod);
}

void RetroEffect::applyWobble (juce::AudioBuffer<float>& buffer, int numCh, int numSamples,
                               float amount) noexcept
{
    const int type = juce::jlimit (0, wobbleTypeNames().size() - 1, wobbleType.load (relaxed));
    const float flutterAmt = juce::jlimit (0.0f, 1.0f, flutterAmount.load (relaxed));
    const float driftAmt = juce::jlimit (0.0f, 1.0f, driftAmount.load (relaxed));
    const double rate = juce::jlimit (0.05, 8.0, (double) wobbleRateHz.load (relaxed));

    const double wowInc = rate / sampleRateHz;
    // The flutter partner runs about nine times the wow rate — fast enough to
    // hear as texture rather than as pitch drift.
    const double flutterInc = juce::jmin (0.45, rate * 9.0 / sampleRateHz);
    const auto driftGlide = (float) std::exp (-1.0 / juce::jmax (1.0, 0.25 / rate * sampleRateHz));

    const auto base = (float) (wobbleBaseMs * 0.001 * sampleRateHz);
    const auto depth = (float) (wobbleDepthMs * 0.001 * sampleRateHz) * amount;

    float lastMod = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        wowPhase += wowInc;
        if (wowPhase >= 1.0)
        {
            wowPhase -= std::floor (wowPhase);
            driftTarget = rng.nextFloat() * 2.0f - 1.0f;   // a fresh place to wander to each turn
        }
        flutterPhase += flutterInc;
        if (flutterPhase >= 1.0)
            flutterPhase -= std::floor (flutterPhase);
        driftValue = driftTarget + driftGlide * (driftValue - driftTarget);

        // The channels read the same motion a little apart, which is what makes
        // a warped record sound wide rather than centred.
        const float modL = wobbleMod (type, 0.0, flutterAmt, driftAmt);
        const float modR = wobbleMod (type, 0.12, flutterAmt, driftAmt);
        lastMod = modL;

        for (int ch = 0; ch < numCh; ++ch)
            wobbleLine.write (ch, buffer.getSample (ch, i));
        buffer.setSample (0, i, wobbleLine.read (0, base + depth * modL));
        if (numCh > 1)
            buffer.setSample (1, i, wobbleLine.read (1, base + depth * modR));
        wobbleLine.advance();
    }

    displayWobble.store (lastMod, relaxed);
}

// ============================== distort ==============================

void RetroEffect::applyDistort (juce::AudioBuffer<float>& buffer, int numCh, int numSamples,
                                float amount, int osIndex) noexcept
{
    const int type = juce::jlimit (0, distortTypeNames().size() - 1, distortType.load (relaxed));
    const float bias = juce::jlimit (0.0f, 1.0f, distortBias.load (relaxed)) * 0.7f;
    const float driveGain = 1.0f + amount * amount * 30.0f;   // up to about +30 dB
    // Bounded curves get *louder* as they are driven, not quieter, so the stage
    // pulls back by up to 8 dB: turning Amount up changes the colour, not the
    // level. Shaping the bias on its own is pure DC, so it comes back off.
    const float comp = 1.0f / (1.0f + amount * 1.5f);
    const float resting = shapeSample (type, bias);

    auto shapeRange = [&] (float* data, size_t count)
    {
        for (size_t i = 0; i < count; ++i)
        {
            const float x = data[i];
            const float wet = (shapeSample (type, x * driveGain + bias) - resting) * comp;
            data[i] = x + amount * (wet - x);
        }
    };

    juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) numCh,
                                        0, (size_t) numSamples);
    if (osIndex == 0)
    {
        for (int ch = 0; ch < numCh; ++ch)
            shapeRange (buffer.getWritePointer (ch), (size_t) numSamples);
    }
    else if (auto* os = oversamplers[(size_t) osIndex - 1].get())
    {
        auto up = os->processSamplesUp (block);
        for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
            shapeRange (up.getChannelPointer (ch), up.getNumSamples());
        os->processSamplesDown (block);
    }

    // ~5 Hz one-pole DC blocker — an asymmetric curve leaves an offset that
    // moves with the programme, so subtracting the resting value isn't enough —
    // then the tone control decides how much of the new top end survives.
    const float r = 1.0f - (float) (juce::MathConstants<double>::twoPi * 5.0 / sampleRateHz);
    distortTone.setCutoffFrequency (juce::jlimit (200.0f, (float) (sampleRateHz * 0.45),
                                                  distortToneHz.load (relaxed)));
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        float x1 = dcX[ch];
        float y1 = dcY[ch];
        for (int i = 0; i < numSamples; ++i)
        {
            const float x = data[i];
            y1 = x - x1 + r * y1;
            x1 = x;
            data[i] = distortTone.processSample (ch, y1);
        }
        dcX[ch] = x1;
        dcY[ch] = y1;
    }
}

// ============================== digital ==============================

void RetroEffect::applyDigital (juce::AudioBuffer<float>& buffer, int numCh, int numSamples,
                                float amount) noexcept
{
    const float bits = juce::jlimit (2.0f, 16.0f, bitDepth.load (relaxed));
    const float levels = std::pow (2.0f, bits - 1.0f);
    const double target = juce::jlimit (1000.0, sampleRateHz, (double) downsampleHz.load (relaxed));
    const float jitter = juce::jlimit (0.0f, 1.0f, jitterAmount.load (relaxed));
    const double increment = target / sampleRateHz;

    for (int i = 0; i < numSamples; ++i)
    {
        // Sample and hold first, quantise second: that is the order the gear
        // being imitated does it in, and it is what makes a low bit depth sound
        // stepped rather than merely noisy.
        double step = increment;
        if (jitter > 0.0f)
            step *= 1.0 + (double) jitter * 0.5 * (rng.nextFloat() * 2.0f - 1.0f);

        holdPhase += juce::jmax (1.0e-4, step);
        if (holdPhase >= 1.0)
        {
            holdPhase -= std::floor (holdPhase);
            for (int ch = 0; ch < numCh; ++ch)
                held[ch] = buffer.getSample (ch, i);
        }

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float x = buffer.getSample (ch, i);
            const float crushed = std::round (held[ch] * levels) / levels;
            buffer.setSample (ch, i, x + amount * (crushed - x));
        }
    }
}

// =============================== noise ===============================

float RetroEffect::nextPink (int channel, float white) noexcept
{
    // Kellett's economy pink filter: three poles, close enough to -3 dB/octave
    // across the audible range for a noise floor.
    auto* b = pinkState[channel];
    b[0] = 0.99765f * b[0] + white * 0.0990460f;
    b[1] = 0.96300f * b[1] + white * 0.2965164f;
    b[2] = 0.57000f * b[2] + white * 1.0526913f;
    return (b[0] + b[1] + b[2] + white * 0.1848f) * 0.25f;
}

float RetroEffect::nextClick (int channel, float perSecond) noexcept
{
    if (rng.nextFloat() < perSecond / (float) sampleRateHz)
        clickAmp[channel] = (0.3f + 0.7f * rng.nextFloat()) * (rng.nextBool() ? 1.0f : -1.0f);

    const float out = clickAmp[channel];
    clickAmp[channel] *= clickDecay;
    return out;
}

void RetroEffect::nextNoise (int type, float& left, float& right) noexcept
{
    const float whiteL = rng.nextFloat() * 2.0f - 1.0f;
    const float whiteR = rng.nextFloat() * 2.0f - 1.0f;

    switch ((NoiseType) type)
    {
        case NoiseType::vinyl:
        case NoiseType::crackle:
        {
            const bool sparse = (NoiseType) type == NoiseType::vinyl;
            const float density = sparse ? 14.0f : 110.0f;
            const float surface = sparse ? 0.22f : 0.06f;
            left  = surface * nextPink (0, whiteL) + nextClick (0, density);
            right = surface * nextPink (1, whiteR) + nextClick (1, density);
            break;
        }

        case NoiseType::hiss:
            // Flat and quiet; the Tone control is what turns it into tape hiss.
            left = 0.45f * whiteL;
            right = 0.45f * whiteR;
            break;

        case NoiseType::studio:
        {
            // Room tone: correlated low rumble with a breath of air over it.
            rumbleState += rumbleCoeff * (nextPink (0, whiteL) - rumbleState);
            left  = 1.8f * rumbleState + 0.06f * whiteL;
            right = 1.8f * rumbleState + 0.06f * whiteR;
            break;
        }

        case NoiseType::hum50:
        case NoiseType::hum60:
        {
            const double freq = (NoiseType) type == NoiseType::hum50 ? 50.0 : 60.0;
            humPhase += freq / sampleRateHz;
            if (humPhase >= 1.0)
                humPhase -= std::floor (humPhase);
            const double p = juce::MathConstants<double>::twoPi * humPhase;
            const auto hum = (float) (0.50 * std::sin (p)
                                      + 0.20 * std::sin (2.0 * p)
                                      + 0.12 * std::sin (3.0 * p));
            left = hum + 0.03f * whiteL;
            right = hum + 0.03f * whiteR;
            break;
        }

        case NoiseType::radio:
        default:
            // Stompbox static: white noise whose level jumps every few tens of
            // milliseconds, with the odd zap on top.
            if (--burstCountdown <= 0)
            {
                burstEnv = 0.15f + 0.85f * rng.nextFloat();
                burstCountdown = juce::jmax (16, (int) ((0.02 + 0.06 * rng.nextFloat()) * sampleRateHz));
            }
            left  = 0.4f * whiteL * burstEnv + nextClick (0, 30.0f);
            right = 0.4f * whiteR * burstEnv + nextClick (1, 30.0f);
            break;
    }
}

void RetroEffect::addNoise (juce::AudioBuffer<float>& buffer, int numCh, int numSamples,
                            float amount) noexcept
{
    const int type = juce::jlimit (0, noiseTypeNames().size() - 1, noiseType.load (relaxed));
    const float follow = juce::jlimit (-1.0f, 1.0f, noiseFollow.load (relaxed));
    const float width = juce::jlimit (0.0f, 1.0f, noiseWidth.load (relaxed));
    const float gain = amount * amount * 0.7f;

    noiseTone.setCutoffFrequency (juce::jlimit (100.0f, (float) (sampleRateHz * 0.45),
                                                noiseToneHz.load (relaxed)));

    const auto attack = (float) std::exp (-1.0 / (0.005 * sampleRateHz));
    const auto release = (float) std::exp (-1.0 / (0.150 * sampleRateHz));

    float loudest = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            peak = juce::jmax (peak, std::abs (buffer.getSample (ch, i)));
        const float coeff = peak > inputEnv ? attack : release;
        inputEnv = peak + coeff * (inputEnv - peak);

        // -12 dBFS and above counts as "the track is playing".
        const float playing = juce::jlimit (0.0f, 1.0f, inputEnv * 4.0f);
        const float dyn = follow >= 0.0f ? (1.0f - follow) + follow * playing
                                        : (1.0f + follow) + (-follow) * (1.0f - playing);
        const float level = gain * dyn;

        float rawL = 0.0f, rawR = 0.0f;
        nextNoise (type, rawL, rawR);
        // Width collapses the two generators together rather than narrowing the
        // result, so a mono noise floor is genuinely one source in both ears.
        const float wide = rawL + width * (rawR - rawL);

        buffer.setSample (0, i, buffer.getSample (0, i) + noiseTone.processSample (0, rawL) * level);
        if (numCh > 1)
            buffer.setSample (1, i, buffer.getSample (1, i) + noiseTone.processSample (1, wide) * level);
        loudest = juce::jmax (loudest, std::abs (rawL) * level);
    }

    displayNoise.store (juce::jmin (1.0f, loudest), relaxed);
}

// =============================== space ===============================

void RetroEffect::applySpace (juce::AudioBuffer<float>& buffer, int numCh, int numSamples,
                              float amount) noexcept
{
    const float dampHz = juce::jlimit (500.0f, 16000.0f, spaceDampHz.load (relaxed));
    // 500 Hz..16 kHz of damping corner mapped onto the tank's damping: a dark
    // room damps hard, an open one barely at all.
    const auto bright = (float) juce::jlimit (0.0, 1.0, std::log (dampHz / 500.0) / std::log (32.0));

    juce::Reverb::Parameters params;
    params.roomSize = 0.18f + 0.80f * juce::jlimit (0.0f, 1.0f, spaceSize.load (relaxed));
    params.damping = 1.0f - bright;
    params.wetLevel = 1.0f;   // the tank is wet-only; Amount is what blends it in
    params.dryLevel = 0.0f;
    params.width = juce::jlimit (0.0f, 1.0f, spaceWidthAmount.load (relaxed));
    params.freezeMode = 0.0f;
    reverb.setParameters (params);

    const auto pre = (float) (juce::jlimit (0.0, maxPreDelayMs, (double) spacePreMs.load (relaxed))
                              * 0.001 * sampleRateHz);

    for (int i = 0; i < numSamples; ++i)
    {
        for (int ch = 0; ch < numCh; ++ch)
            preDelay.write (ch, buffer.getSample (ch, i));
        for (int ch = 0; ch < numCh; ++ch)
            spaceBuffer.setSample (ch, i, preDelay.read (ch, pre));
        preDelay.advance();
    }

    if (numCh > 1)
        reverb.processStereo (spaceBuffer.getWritePointer (0), spaceBuffer.getWritePointer (1), numSamples);
    else
        reverb.processMono (spaceBuffer.getWritePointer (0), numSamples);

    for (int ch = 0; ch < numCh; ++ch)
        buffer.addFrom (ch, 0, spaceBuffer, ch, 0, numSamples, amount);
}

// =============================== drops ===============================

void RetroEffect::scheduleDrop (float rateHz, float vary) noexcept
{
    const float spread = 1.0f + vary * (rng.nextFloat() * 2.0f - 1.0f) * 0.8f;
    const double seconds = juce::jmax (0.02, (double) spread / juce::jmax (0.05, (double) rateHz));
    dropCountdown = juce::jmax (1, (int) (seconds * sampleRateHz));
}

void RetroEffect::startDrop (float amount, float lengthMs, float vary) noexcept
{
    const float spread = 1.0f + vary * (rng.nextFloat() - 0.5f);
    dropLength = juce::jmax (8, (int) ((double) lengthMs * (double) spread * 0.001 * sampleRateHz));
    dropDepth = juce::jlimit (0.0f, 1.0f, amount * (1.0f - vary * rng.nextFloat() * 0.5f));
    dropPos = 0;
    dropCountdown = 0;
}

void RetroEffect::applyDrops (juce::AudioBuffer<float>& buffer, int numCh, int numSamples,
                              float amount) noexcept
{
    const float rate = juce::jlimit (0.05f, 8.0f, dropRateHz.load (relaxed));
    const float lengthMs = juce::jlimit (10.0f, 2000.0f, dropLengthMs.load (relaxed));
    const float vary = juce::jlimit (0.0f, 1.0f, dropVary.load (relaxed));

    float deepest = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        float gain = 1.0f;

        if (dropLength > 0)
        {
            // Raised cosine: the level slides away and back rather than
            // stepping, which is what worn tape actually does.
            const float t = (float) dropPos / (float) dropLength;
            gain = 1.0f - dropDepth * 0.5f
                        * (1.0f - std::cos (juce::MathConstants<float>::twoPi * t));
            if (++dropPos >= dropLength)
            {
                dropLength = 0;
                scheduleDrop (rate, vary);
            }
        }
        else if (dropCountdown < 0)
        {
            scheduleDrop (rate, vary);
        }
        else if (--dropCountdown <= 0)
        {
            startDrop (amount, lengthMs, vary);
        }

        deepest = juce::jmax (deepest, 1.0f - gain);
        for (int ch = 0; ch < numCh; ++ch)
            buffer.setSample (ch, i, buffer.getSample (ch, i) * gain);
    }

    displayDrop.store (deepest, relaxed);
}

// ============================== process ==============================

void RetroEffect::process (juce::AudioBuffer<float>& stereoBus, int numSamples, const Context& context)
{
    juce::ignoreUnused (context);
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (2, stereoBus.getNumChannels());
    if (numCh < 1 || numSamples < 1 || numSamples > dry.getNumSamples())
        return;

    const float wobbleAmt = amountOf (Module::wobble);
    const float distortAmt = amountOf (Module::distort);
    const float digitalAmt = amountOf (Module::digital);
    const float noiseAmt = amountOf (Module::noise);
    const float spaceAmt = amountOf (Module::space);
    const float dropsAmt = amountOf (Module::drops);

    const bool wobbling = wobbleAmt > 0.0f;
    const bool distorting = distortAmt > 0.0f;
    // Nothing to oversample when the drive is out of the path, and skipping it
    // keeps the latency at zero.
    const int osIndex = distorting ? juce::jlimit (0, 3, oversampleIndex.load (relaxed)) : 0;

    if (osIndex != activeOversample)
    {
        // A different factor is a different latency, so the converters and the
        // stage behind them start clean instead of smearing the old rate.
        activeOversample = osIndex;
        for (auto& os : oversamplers)
            if (os != nullptr)
                os->reset();
        distortTone.reset();
        dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0f;
    }
    if (wobbling != wobbleRunning)
    {
        wobbleRunning = wobbling;
        wobbleLine.reset();
        dryDelay.reset();
    }

    // Whatever latency the wet path just picked up, the dry path picks up too,
    // so a partial Mix stays aligned instead of comb-filtering. Both are zero
    // with the drive and the wobble out of the path, which is what makes a
    // Magnitude of zero a true bypass.
    const auto latency = (float) ((wobbling ? (int) std::lround (wobbleBaseMs * 0.001 * sampleRateHz) : 0)
                                  + latencySamples[(size_t) osIndex]);
    for (int i = 0; i < numSamples; ++i)
    {
        for (int ch = 0; ch < numCh; ++ch)
        {
            dryDelay.write (ch, stereoBus.getSample (ch, i));
            dry.setSample (ch, i, dryDelay.read (ch, latency));
        }
        dryDelay.advance();
    }

    if (wobbling)
        applyWobble (stereoBus, numCh, numSamples, wobbleAmt);
    else
        displayWobble.store (0.0f, relaxed);

    if (distorting)
        applyDistort (stereoBus, numCh, numSamples, distortAmt, osIndex);

    if (digitalAmt > 0.0f)
        applyDigital (stereoBus, numCh, numSamples, digitalAmt);

    if (noiseAmt > 0.0f)
    {
        addNoise (stereoBus, numCh, numSamples, noiseAmt);
    }
    else
    {
        inputEnv = 0.0f;
        displayNoise.store (0.0f, relaxed);
    }

    if (spaceAmt > 0.0f)
        applySpace (stereoBus, numCh, numSamples, spaceAmt);

    if (dropsAmt > 0.0f)
    {
        applyDrops (stereoBus, numCh, numSamples, dropsAmt);
    }
    else
    {
        dropLength = 0;
        dropCountdown = -1;
        displayDrop.store (0.0f, relaxed);
    }

    const float outGain = juce::Decibels::decibelsToGain (outputDb.load (relaxed));
    const auto gains = fx::mixGains (mix.load (relaxed));
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* out = stereoBus.getWritePointer (ch);
        const auto* dryData = dry.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
            out[i] = out[i] * outGain * gains.wet + dryData[i] * gains.dry;
    }
}
