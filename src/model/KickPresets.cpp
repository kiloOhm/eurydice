#include "KickPresets.h"
#include "ChannelParams.h"
#include "KickEnvelope.h"

namespace kickpresets
{
namespace
{
kickdsp::Envelope curve (std::initializer_list<kickdsp::Point> points)
{
    kickdsp::Envelope envelope;
    envelope.points.assign (points);
    envelope.tidy();
    return envelope;
}

// Shorthands, so a preset row reads as a description of a sound rather than a
// wall of identifiers.
const juce::Identifier& from  = ids::kickStartFreq;   const juce::Identifier& to    = ids::kickEndFreq;
const juce::Identifier& ptime = ids::kickPitchDecay;  const juce::Identifier& shape = ids::kickBodyShape;
const juce::Identifier& harm  = ids::kickBodyHarm;    const juce::Identifier& blvl  = ids::kickBodyLevel;
const juce::Identifier& adec  = ids::kickAmpDecay;    const juce::Identifier& hold  = ids::kickHold;
const juce::Identifier& acrv  = ids::envShape;        const juce::Identifier& punch = ids::kickPunch;
const juce::Identifier& sub   = ids::kickSubLevel;    const juce::Identifier& stune = ids::kickSubTune;
const juce::Identifier& sdec  = ids::kickSubDecay;    const juce::Identifier& clvl  = ids::kickClickLevel;
const juce::Identifier& cdec  = ids::kickClickDecay;  const juce::Identifier& cfrq  = ids::kickClickFreq;
const juce::Identifier& ctyp  = ids::kickClickType;   const juce::Identifier& nlvl  = ids::kickNoiseLevel;
const juce::Identifier& ndec  = ids::kickNoiseDecay;  const juce::Identifier& ntone = ids::kickNoiseTone;
const juce::Identifier& damt  = ids::drive;           const juce::Identifier& dcrv  = ids::driveCurve;
const juce::Identifier& lof   = ids::kickEqLowFreq;   const juce::Identifier& log_  = ids::kickEqLowGain;
const juce::Identifier& midf  = ids::kickEqMidFreq;   const juce::Identifier& midg  = ids::kickEqMidGain;
const juce::Identifier& hif   = ids::kickEqHighFreq;  const juce::Identifier& hig   = ids::kickEqHighGain;
const juce::Identifier& comp  = ids::kickComp;        const juce::Identifier& limit = ids::kickLimit;
const juce::Identifier& outdb = ids::kickOutput;
} // namespace

const std::vector<Preset>& all()
{
    static const std::vector<Preset> bank {
        // ---------------- Basics ----------------
        { "Init Kick", "Basics", {} },

        // ---------------- House ----------------
        { "Warm Garage", "House", {
            { from, 220 }, { to, 52 }, { ptime, 0.045 }, { shape, 0.10 }, { harm, 0.05 },
            { adec, 0.42 }, { acrv, 0.90 },
            { clvl, 0.22 }, { cdec, 0.005 }, { cfrq, 1200 },
            { nlvl, 0.05 }, { ndec, 0.015 }, { ntone, 0.35 },
            { damt, 0.18 }, { log_, 2.0 }, { lof, 80 }, { midg, -1.5 }, { midf, 420 },
            { comp, 0.15 }, { limit, 0.10 } } },

        { "Deep Chicago", "House", {
            { from, 180 }, { to, 47 }, { ptime, 0.060 }, { adec, 0.55 }, { acrv, 0.85 },
            { sub, 0.15 }, { sdec, 0.35 },
            { clvl, 0.12 }, { cdec, 0.006 }, { cfrq, 900 }, { nlvl, 0.03 },
            { damt, 0.12 }, { log_, 3.0 }, { lof, 70 }, { hig, -3.0 }, { hif, 6000 },
            { comp, 0.15 }, { limit, 0.15 } } },

        { "Tech House Punch", "House", {
            { from, 320 }, { to, 54 }, { ptime, 0.030 }, { harm, 0.20 },
            { adec, 0.34 }, { punch, 0.25 },
            { clvl, 0.35 }, { cdec, 0.0035 }, { cfrq, 1800 },
            { nlvl, 0.08 }, { ndec, 0.012 },
            { damt, 0.32 }, { midg, 2.0 }, { midf, 700 },
            { comp, 0.30 }, { limit, 0.25 } } },

        { "Jackin Snap", "House", {
            { from, 400 }, { to, 58 }, { ptime, 0.022 }, { shape, 0.25 },
            { adec, 0.28 }, { punch, 0.35 },
            { clvl, 0.45 }, { cdec, 0.0025 }, { cfrq, 2400 },
            { nlvl, 0.12 }, { ndec, 0.010 }, { ntone, 0.60 },
            { damt, 0.28 }, { hig, 3.0 }, { hif, 5000 }, { comp, 0.35 }, { limit, 0.15 } } },

        { "Disco Thump", "House", {
            { from, 240 }, { to, 60 }, { ptime, 0.050 }, { shape, 0.15 },
            { adec, 0.38 }, { hold, 0.008 },
            { clvl, 0.18 }, { cdec, 0.006 }, { cfrq, 1100 },
            { nlvl, 0.09 }, { ndec, 0.020 }, { ntone, 0.30 },
            { damt, 0.15 }, { log_, 1.5 }, { midg, 1.0 }, { midf, 350 }, { limit, 0.15 } } },

        // ---------------- Techno ----------------
        { "Berlin Tunnel", "Techno", {
            { from, 480 }, { to, 49 }, { ptime, 0.028 }, { harm, 0.30 },
            { adec, 0.62 }, { acrv, 0.85 },
            { clvl, 0.30 }, { cdec, 0.004 }, { cfrq, 1600 },
            { nlvl, 0.10 }, { ndec, 0.018 }, { ntone, 0.45 },
            { damt, 0.42 }, { log_, 3.0 }, { lof, 65 }, { midg, -2.0 }, { midf, 500 },
            { comp, 0.30 }, { limit, 0.30 } } },

        { "Rumble Room", "Techno", {
            { from, 300 }, { to, 44 }, { ptime, 0.050 }, { adec, 0.95 },
            { sub, 0.30 }, { stune, -5 }, { sdec, 0.80 },
            { clvl, 0.18 }, { cdec, 0.005 }, { nlvl, 0.06 },
            { damt, 0.30 }, { log_, 4.0 }, { lof, 55 }, { hig, -4.0 }, { hif, 5000 },
            { comp, 0.40 }, { limit, 0.25 } },
          {},
          curve ({ { 0.0f, 1.0f, 0.90f }, { 0.22f, 0.38f, 0.35f }, { 1.0f, 0.0f, 0.0f } }) },

        { "Peak Time Steel", "Techno", {
            { from, 700 }, { to, 52 }, { ptime, 0.018 }, { harm, 0.45 }, { shape, 0.30 },
            { adec, 0.55 }, { punch, 0.30 },
            { clvl, 0.40 }, { cdec, 0.003 }, { cfrq, 2800 }, { ctyp, 2 },
            { damt, 0.55 }, { dcrv, 1 }, { midg, 3.0 }, { midf, 900 }, { hig, 2.0 },
            { comp, 0.35 }, { limit, 0.45 }, { outdb, -1.5 } } },

        { "Detroit Sub", "Techno", {
            { from, 260 }, { to, 41 }, { ptime, 0.040 }, { adec, 0.80 }, { acrv, 0.80 },
            { sub, 0.40 }, { sdec, 0.70 },
            { clvl, 0.14 }, { cdec, 0.006 }, { cfrq, 1000 }, { nlvl, 0.04 },
            { damt, 0.22 }, { log_, 5.0 }, { lof, 60 }, { comp, 0.25 }, { limit, 0.15 } } },

        { "Industrial Grind", "Techno", {
            { from, 900 }, { to, 55 }, { ptime, 0.020 }, { harm, 0.70 }, { shape, 0.50 },
            { adec, 0.70 },
            { clvl, 0.50 }, { cdec, 0.006 }, { cfrq, 3200 }, { ctyp, 1 },
            { nlvl, 0.25 }, { ndec, 0.060 }, { ntone, 0.70 },
            { damt, 0.72 }, { dcrv, 2 }, { midg, 4.0 }, { midf, 1200 },
            { comp, 0.50 }, { limit, 0.50 }, { outdb, -2.0 } } },

        // ---------------- Hardstyle ----------------
        { "Reverse Bass Kick", "Hardstyle", {
            { from, 1400 }, { to, 62 }, { ptime, 0.050 }, { harm, 0.80 }, { shape, 0.60 },
            { adec, 0.50 }, { punch, 0.50 },
            { clvl, 0.55 }, { cdec, 0.0025 }, { cfrq, 3600 }, { ctyp, 2 },
            { damt, 0.82 }, { dcrv, 1 }, { log_, 2.0 }, { midg, 5.0 }, { midf, 800 },
            { comp, 0.50 }, { limit, 0.60 }, { outdb, -3.0 } },
          curve ({ { 0.0f, 1.0f, 0.85f }, { 0.18f, 0.24f, 0.30f },
                   { 0.55f, 0.08f, 0.0f }, { 1.0f, 0.0f, 0.0f } }) },

        { "Euphoric Hard", "Hardstyle", {
            { from, 1100 }, { to, 66 }, { ptime, 0.015 }, { harm, 0.60 },
            { adec, 0.75 }, { acrv, 0.70 },
            { sub, 0.25 }, { sdec, 0.60 },
            { clvl, 0.40 }, { cdec, 0.003 }, { cfrq, 2600 },
            { damt, 0.70 }, { log_, 4.0 }, { lof, 70 },
            { comp, 0.45 }, { limit, 0.50 }, { outdb, -2.0 } } },

        { "Gabber 190", "Hardstyle", {
            { from, 1600 }, { to, 70 }, { ptime, 0.010 }, { harm, 0.90 }, { shape, 0.75 },
            { adec, 0.42 }, { punch, 0.60 },
            { clvl, 0.60 }, { cdec, 0.002 }, { cfrq, 4200 }, { ctyp, 2 },
            { nlvl, 0.20 }, { ndec, 0.020 }, { ntone, 0.80 },
            { damt, 0.90 }, { dcrv, 2 }, { midg, 6.0 }, { midf, 1500 },
            { comp, 0.55 }, { limit, 0.70 }, { outdb, -4.0 } } },

        { "Uptempo Screech", "Hardstyle", {
            { from, 2000 }, { to, 80 }, { ptime, 0.008 }, { harm, 1.0 }, { shape, 0.90 },
            { adec, 0.35 },
            { clvl, 0.50 }, { cdec, 0.0015 }, { cfrq, 5200 },
            { nlvl, 0.30 }, { ndec, 0.015 }, { ntone, 0.90 },
            { damt, 0.95 }, { dcrv, 1 }, { hig, 5.0 }, { hif, 7000 },
            { limit, 0.80 }, { outdb, -5.0 } } },

        { "Rawstyle Punch", "Hardstyle", {
            { from, 1200 }, { to, 58 }, { ptime, 0.014 }, { harm, 0.75 },
            { adec, 0.60 }, { hold, 0.006 }, { punch, 0.55 },
            { clvl, 0.50 }, { cdec, 0.0022 }, { cfrq, 3000 },
            { damt, 0.78 }, { dcrv, 1 }, { log_, 3.0 }, { midg, 4.0 }, { midf, 1000 },
            { comp, 0.50 }, { limit, 0.60 }, { outdb, -3.0 } },
          {},
          curve ({ { 0.0f, 1.0f, 0.0f }, { 0.12f, 0.92f, 0.80f }, { 1.0f, 0.0f, 0.0f } }) },

        // ---------------- Trance ----------------
        { "Uplifting Tight", "Trance", {
            { from, 380 }, { to, 50 }, { ptime, 0.025 }, { adec, 0.30 }, { punch, 0.20 },
            { clvl, 0.30 }, { cdec, 0.003 }, { cfrq, 2000 },
            { nlvl, 0.07 }, { ndec, 0.010 }, { ntone, 0.55 },
            { damt, 0.20 }, { log_, 2.0 }, { lof, 75 }, { hig, 2.0 }, { hif, 6000 },
            { comp, 0.25 }, { limit, 0.20 } } },

        { "Psy Pump", "Trance", {
            { from, 520 }, { to, 46 }, { ptime, 0.020 }, { harm, 0.25 },
            { adec, 0.24 }, { punch, 0.40 },
            { clvl, 0.35 }, { cdec, 0.0022 }, { cfrq, 2600 },
            { damt, 0.35 }, { log_, 3.0 }, { lof, 60 }, { midg, -2.0 }, { midf, 400 },
            { comp, 0.40 }, { limit, 0.35 } } },

        { "Progressive Round", "Trance", {
            { from, 260 }, { to, 48 }, { ptime, 0.040 }, { shape, 0.05 }, { adec, 0.45 },
            { clvl, 0.16 }, { cdec, 0.005 }, { cfrq, 1300 }, { nlvl, 0.05 },
            { damt, 0.16 }, { log_, 2.0 }, { hig, -2.0 }, { hif, 7000 }, { comp, 0.20 }, { limit, 0.15 } } },

        { "Goa Click", "Trance", {
            { from, 600 }, { to, 52 }, { ptime, 0.016 }, { harm, 0.30 }, { shape, 0.35 },
            { adec, 0.26 },
            { clvl, 0.50 }, { cdec, 0.002 }, { cfrq, 3400 }, { ctyp, 2 },
            { nlvl, 0.15 }, { ndec, 0.012 }, { ntone, 0.75 },
            { damt, 0.40 }, { dcrv, 1 }, { hig, 4.0 }, { hif, 6500 }, { comp, 0.30 }, { limit, 0.15 } } },

        // ---------------- Trap ----------------
        { "808 Long", "Trap", {
            { from, 90 }, { to, 42 }, { ptime, 0.090 }, { adec, 2.40 },
            { sub, 0.50 }, { sdec, 2.20 },
            { clvl, 0.10 }, { cdec, 0.004 }, { cfrq, 1200 }, { nlvl, 0.0 },
            { damt, 0.12 }, { log_, 2.0 }, { lof, 50 }, { comp, 0.15 }, { limit, 0.15 } },
          {},
          curve ({ { 0.0f, 1.0f, -0.20f }, { 0.15f, 0.88f, 0.60f }, { 1.0f, 0.0f, 0.0f } }) },

        { "Distorted 808", "Trap", {
            { from, 110 }, { to, 45 }, { ptime, 0.080 }, { harm, 0.35 },
            { adec, 1.80 }, { acrv, 0.50 },
            { sub, 0.45 }, { sdec, 1.60 },
            { clvl, 0.16 }, { cdec, 0.004 }, { cfrq, 1500 },
            { damt, 0.50 }, { log_, 1.0 }, { midg, 2.0 }, { midf, 600 },
            { comp, 0.30 }, { limit, 0.35 } } },

        { "Trap Knock", "Trap", {
            { from, 340 }, { to, 55 }, { ptime, 0.020 },
            { adec, 0.60 }, { hold, 0.004 }, { punch, 0.45 },
            { clvl, 0.40 }, { cdec, 0.003 }, { cfrq, 2200 },
            { nlvl, 0.10 }, { ndec, 0.010 }, { ntone, 0.60 },
            { damt, 0.30 }, { midg, 3.0 }, { midf, 800 },
            { comp, 0.40 }, { limit, 0.30 } } },

        { "Drill Slide", "Trap", {
            { from, 150 }, { to, 38 }, { ptime, 0.350 }, { adec, 1.60 }, { acrv, 0.60 },
            { sub, 0.40 }, { stune, -12 }, { sdec, 1.40 },
            { clvl, 0.12 }, { cdec, 0.004 }, { cfrq, 1000 },
            { damt, 0.20 }, { log_, 3.0 }, { lof, 45 }, { comp, 0.20 }, { limit, 0.15 } },
          curve ({ { 0.0f, 1.0f, -0.40f }, { 1.0f, 0.0f, 0.0f } }) },

        // ---------------- Drum & Bass ----------------
        { "Neuro Tight", "Drum & Bass", {
            { from, 450 }, { to, 56 }, { ptime, 0.020 }, { harm, 0.30 },
            { adec, 0.26 }, { punch, 0.40 },
            { clvl, 0.42 }, { cdec, 0.0025 }, { cfrq, 2800 },
            { nlvl, 0.14 }, { ndec, 0.010 }, { ntone, 0.70 },
            { damt, 0.40 }, { midg, 3.0 }, { midf, 900 }, { hig, 2.0 },
            { comp, 0.40 }, { limit, 0.35 } } },

        { "Liquid Round", "Drum & Bass", {
            { from, 220 }, { to, 52 }, { ptime, 0.045 }, { adec, 0.34 },
            { clvl, 0.20 }, { cdec, 0.005 }, { cfrq, 1400 },
            { nlvl, 0.06 }, { ndec, 0.015 }, { ntone, 0.40 },
            { damt, 0.18 }, { log_, 2.0 }, { lof, 70 }, { hig, -2.0 }, { comp, 0.20 }, { limit, 0.15 } } },

        { "Jungle Vintage", "Drum & Bass", {
            { from, 200 }, { to, 58 }, { ptime, 0.050 }, { shape, 0.40 }, { adec, 0.30 },
            { clvl, 0.25 }, { cdec, 0.006 }, { cfrq, 900 },
            { nlvl, 0.18 }, { ndec, 0.030 }, { ntone, 0.35 },
            { damt, 0.25 }, { dcrv, 2 }, { midg, 2.0 }, { midf, 500 },
            { hig, -3.0 }, { hif, 6000 }, { limit, 0.15 } } },

        { "Halftime Weight", "Drum & Bass", {
            { from, 180 }, { to, 43 }, { ptime, 0.070 }, { adec, 1.10 }, { acrv, 0.60 },
            { sub, 0.35 }, { sdec, 0.90 },
            { clvl, 0.18 }, { cdec, 0.004 }, { cfrq, 1600 },
            { damt, 0.28 }, { log_, 4.0 }, { lof, 55 }, { comp, 0.30 }, { limit, 0.25 } } },

        // ---------------- Dubstep ----------------
        { "Riddim Stomp", "Dubstep", {
            { from, 500 }, { to, 50 }, { ptime, 0.024 }, { harm, 0.40 },
            { adec, 0.50 }, { punch, 0.35 },
            { clvl, 0.45 }, { cdec, 0.003 }, { cfrq, 2400 }, { ctyp, 2 },
            { nlvl, 0.16 }, { ndec, 0.020 }, { ntone, 0.65 },
            { damt, 0.55 }, { dcrv, 1 }, { log_, 3.0 }, { midg, 3.0 }, { midf, 1000 },
            { comp, 0.45 }, { limit, 0.45 }, { outdb, -2.0 } } },

        { "Deep Wobble Kick", "Dubstep", {
            { from, 240 }, { to, 44 }, { ptime, 0.050 }, { adec, 0.70 }, { acrv, 0.80 },
            { sub, 0.40 }, { sdec, 0.60 },
            { clvl, 0.20 }, { cdec, 0.005 }, { cfrq, 1400 },
            { damt, 0.30 }, { log_, 4.0 }, { lof, 58 }, { hig, -3.0 }, { comp, 0.30 }, { limit, 0.15 } } },

        { "Brostep Slam", "Dubstep", {
            { from, 800 }, { to, 54 }, { ptime, 0.016 }, { harm, 0.55 }, { shape, 0.40 },
            { adec, 0.45 }, { punch, 0.50 },
            { clvl, 0.50 }, { cdec, 0.0022 }, { cfrq, 3200 },
            { nlvl, 0.20 }, { ndec, 0.015 }, { ntone, 0.80 },
            { damt, 0.68 }, { dcrv, 2 }, { midg, 4.0 }, { midf, 1300 },
            { comp, 0.50 }, { limit, 0.55 }, { outdb, -3.0 } } },

        // ---------------- Vintage ----------------
        { "TR-808 Boom", "Vintage", {
            { from, 90 }, { to, 47 }, { ptime, 0.070 }, { adec, 1.20 }, { acrv, 0.45 },
            { clvl, 0.14 }, { cdec, 0.0025 }, { cfrq, 2400 }, { nlvl, 0.0 },
            { damt, 0.05 }, { hig, -4.0 }, { hif, 6000 } } },

        { "TR-909 Punch", "Vintage", {
            { from, 320 }, { to, 56 }, { ptime, 0.028 }, { shape, 0.20 }, { adec, 0.40 },
            { clvl, 0.40 }, { cdec, 0.003 }, { cfrq, 2000 },
            { nlvl, 0.16 }, { ndec, 0.014 }, { ntone, 0.55 },
            { damt, 0.22 }, { midg, 2.0 }, { midf, 600 }, { limit, 0.15 } } },

        { "TR-606 Tick", "Vintage", {
            { from, 380 }, { to, 68 }, { ptime, 0.020 }, { shape, 0.50 }, { adec, 0.18 },
            { blvl, 0.80 },
            { clvl, 0.35 }, { cdec, 0.002 }, { cfrq, 3000 },
            { nlvl, 0.20 }, { ndec, 0.012 }, { ntone, 0.70 },
            { damt, 0.18 }, { hig, 3.0 }, { hif, 7000 }, { limit, 0.15 } } },

        { "LinnDrum Thud", "Vintage", {
            { from, 200 }, { to, 62 }, { ptime, 0.045 }, { shape, 0.30 }, { adec, 0.26 },
            { clvl, 0.22 }, { cdec, 0.005 }, { cfrq, 1500 },
            { nlvl, 0.14 }, { ndec, 0.020 }, { ntone, 0.40 },
            { damt, 0.10 }, { hig, -5.0 }, { hif, 5500 } } },

        { "CR-78 Soft", "Vintage", {
            { from, 160 }, { to, 64 }, { ptime, 0.050 }, { shape, 0.60 }, { adec, 0.20 },
            { clvl, 0.15 }, { cdec, 0.004 }, { cfrq, 1800 },
            { nlvl, 0.10 }, { ndec, 0.018 }, { ntone, 0.50 },
            { damt, 0.05 }, { log_, 1.0 }, { hig, -6.0 }, { hif, 5000 } } },

        // ---------------- Pop ----------------
        { "Acoustic Beater", "Pop", {
            { from, 260 }, { to, 68 }, { ptime, 0.030 }, { shape, 0.35 }, { adec, 0.24 },
            { blvl, 0.85 },
            { clvl, 0.40 }, { cdec, 0.004 }, { cfrq, 2200 }, { ctyp, 1 },
            { nlvl, 0.30 }, { ndec, 0.035 }, { ntone, 0.55 },
            { damt, 0.12 }, { log_, 2.0 }, { lof, 80 }, { midg, -2.0 }, { midf, 350 },
            { hig, 3.0 }, { hif, 5000 }, { limit, 0.15 } } },

        { "Rock Room", "Pop", {
            { from, 220 }, { to, 62 }, { ptime, 0.040 }, { shape, 0.25 },
            { adec, 0.35 }, { hold, 0.010 },
            { clvl, 0.32 }, { cdec, 0.006 }, { cfrq, 1700 },
            { nlvl, 0.26 }, { ndec, 0.050 }, { ntone, 0.45 },
            { damt, 0.20 }, { log_, 2.0 }, { midg, 1.0 }, { midf, 450 }, { comp, 0.35 }, { limit, 0.15 } } },

        { "Pop Click", "Pop", {
            { from, 300 }, { to, 66 }, { ptime, 0.026 }, { adec, 0.22 }, { punch, 0.25 },
            { clvl, 0.45 }, { cdec, 0.0025 }, { cfrq, 2600 },
            { nlvl, 0.18 }, { ndec, 0.012 }, { ntone, 0.70 },
            { damt, 0.15 }, { hig, 4.0 }, { hif, 6000 }, { comp, 0.30 }, { limit, 0.20 } } },

        { "Lo-Fi Tape", "Pop", {
            { from, 170 }, { to, 58 }, { ptime, 0.050 }, { shape, 0.45 }, { harm, 0.15 },
            { adec, 0.30 },
            { clvl, 0.18 }, { cdec, 0.006 }, { cfrq, 1200 },
            { nlvl, 0.15 }, { ndec, 0.040 }, { ntone, 0.30 },
            { damt, 0.30 }, { dcrv, 2 }, { log_, 2.0 }, { lof, 75 },
            { hig, -8.0 }, { hif, 4500 }, { comp, 0.35 }, { limit, 0.15 } } },
    };
    return bank;
}

juce::StringArray categories()
{
    juce::StringArray out;
    for (const auto& preset : all())
        if (! out.contains (preset.category))
            out.add (preset.category);
    return out;
}

const Preset* find (const juce::String& name)
{
    for (const auto& preset : all())
        if (preset.name == name)
            return &preset;
    return nullptr;
}

void apply (juce::ValueTree channel, const Preset& preset, juce::UndoManager* undo)
{
    for (const auto& descriptor : channelparams::kick())
    {
        if (descriptor.id == ids::rootNote)
            continue;   // the kick is tuned to the track, not to the preset

        double value = descriptor.defaultValue;
        for (const auto& [id, presetValue] : preset.values)
            if (id == descriptor.id)
            {
                value = presetValue;
                break;
            }
        channel.setProperty (descriptor.id, value, undo);
    }

    kickenv::write (channel, kickenv::pitchRole, preset.pitchEnvelope, undo);
    kickenv::write (channel, kickenv::ampRole, preset.ampEnvelope, undo);
    channel.setProperty (ids::presetName, preset.name, undo);
}
} // namespace kickpresets
