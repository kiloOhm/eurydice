#include "SynthPresets.h"
#include <algorithm>
#include "ChannelParams.h"

namespace synthpresets
{
namespace
{
// Shorthands, so a preset row reads as a description of a sound rather than a
// wall of identifiers.
const juce::Identifier& morph = ids::oscShape;      const juce::Identifier& warp  = ids::oscWarp;
const juce::Identifier& semi2 = ids::osc2Semi;      const juce::Identifier& fine2 = ids::osc2Detune;
const juce::Identifier& mix2  = ids::osc2Mix;
const juce::Identifier& uvox  = ids::unisonVoices;  const juce::Identifier& udet  = ids::unisonDetune;
const juce::Identifier& uwid  = ids::unisonWidth;
const juce::Identifier& sub   = ids::subLevel;      const juce::Identifier& noise = ids::noiseLevel;
const juce::Identifier& ftype = ids::filterType;    const juce::Identifier& cut   = ids::cutoff;
const juce::Identifier& res   = ids::resonance;     const juce::Identifier& fkey  = ids::filterKey;
const juce::Identifier& famt  = ids::filterEnvAmt;
const juce::Identifier& fatt  = ids::fenvAttack;    const juce::Identifier& fdec  = ids::fenvDecay;
const juce::Identifier& fsus  = ids::fenvSustain;   const juce::Identifier& frel  = ids::fenvRelease;
const juce::Identifier& att   = ids::attack;        const juce::Identifier& dec   = ids::decay;
const juce::Identifier& sus   = ids::sustain;       const juce::Identifier& rel   = ids::release;
const juce::Identifier& lrate = ids::lfoRate;       const juce::Identifier& lamt  = ids::lfoAmount;
const juce::Identifier& ldest = ids::lfoTarget;     const juce::Identifier& gli   = ids::glide;

// Landmarks on the oscillator's continuous morph axis, and the two index
// parameters, spelled out so a row says what it means.
constexpr double sineWave = -2.0, triWave = -1.0, sawWave = 0.0, sqrWave = 1.0;
constexpr double bandpass = 1.0;
constexpr double toCutoff = 0.0, toWarp = 2.0, toPan = 3.0;
} // namespace

const std::vector<Preset>& factory()
{
    static const std::vector<Preset> bank {
        // ---------------- Basics ----------------
        { "Init Synth", "Basics", "One saw, open filter: the default patch to build from.", {} },

        // ---------------- Schranz ----------------
        // Hard, fast, distorted techno: everything here expects the mixer's
        // saturator or clipper after it, so the patches leave headroom rather
        // than trying to sound finished on their own.
        { "Schranz Hoover", "Schranz",
          "The hoover riff: saw and pulse stacked seven wide, with an octave below.", {
            { morph, 0.45 }, { warp, 0.35 }, { semi2, -12 }, { fine2, 14 }, { mix2, 0.40 },
            { uvox, 7 }, { udet, 34 }, { uwid, 0.85 },
            { cut, 4200 }, { res, 0.35 }, { fkey, 0.30 }, { famt, 0.35 },
            { fatt, 0.002 }, { fdec, 0.40 }, { fsus, 0.50 }, { frel, 0.20 },
            { att, 0.004 }, { dec, 0.60 }, { sus, 0.90 }, { rel, 0.15 } } },

        { "Tunnel Rumble", "Schranz",
          "Sub-heavy drone for under the kick; one held note per bar.", {
            { morph, -0.60 }, { uvox, 5 }, { udet, 12 }, { uwid, 0.45 }, { sub, 0.55 },
            { cut, 240 }, { res, 0.20 }, { fkey, 0.20 }, { famt, 0.15 },
            { fatt, 0.30 }, { fdec, 1.00 }, { fsus, 0.55 }, { frel, 0.80 },
            { att, 0.02 }, { dec, 1.50 }, { sus, 0.90 }, { rel, 0.60 },
            { lrate, 0.35 }, { lamt, 0.35 }, { ldest, toCutoff } } },

        { "Hard Stab", "Schranz",
          "Offbeat stab, all filter envelope: short, bright, gone.", {
            { morph, 0.55 }, { warp, 0.30 }, { fine2, 20 }, { mix2, 0.35 },
            { uvox, 5 }, { udet, 24 }, { uwid, 0.70 },
            { cut, 2800 }, { res, 0.50 }, { fkey, 0.40 }, { famt, 0.60 },
            { fatt, 0.001 }, { fdec, 0.10 }, { fsus, 0.0 }, { frel, 0.06 },
            { att, 0.002 }, { dec, 0.18 }, { sus, 0.0 }, { rel, 0.10 } } },

        { "Zap Screech", "Schranz",
          "Resonant bandpass scream with the LFO keeping it unstable.", {
            { morph, sqrWave }, { warp, 0.55 }, { uvox, 3 }, { udet, 20 }, { uwid, 0.60 },
            { ftype, bandpass }, { cut, 1300 }, { res, 0.70 }, { fkey, 0.50 }, { famt, 0.80 },
            { fatt, 0.001 }, { fdec, 0.30 }, { fsus, 0.10 }, { frel, 0.10 },
            { att, 0.002 }, { dec, 0.40 }, { sus, 0.50 }, { rel, 0.12 },
            { lrate, 7.5 }, { lamt, 0.45 }, { ldest, toCutoff } } },

        { "Pulse Loop", "Schranz",
          "Pulse width sweeping under a held note: the tonal layer of a loop.", {
            { morph, sqrWave }, { warp, 0.20 }, { fine2, 9 }, { mix2, 0.30 },
            { uvox, 3 }, { udet, 14 }, { uwid, 0.60 },
            { cut, 5200 }, { res, 0.25 }, { fkey, 0.35 }, { famt, 0.20 },
            { fatt, 0.01 }, { fdec, 0.50 }, { fsus, 0.60 }, { frel, 0.20 },
            { att, 0.005 }, { dec, 0.80 }, { sus, 0.90 }, { rel, 0.12 },
            { lrate, 0.75 }, { lamt, 0.60 }, { ldest, toWarp } } },

        { "Distorted Bass", "Schranz",
          "Hard low tone that wants a saturator after it.", {
            { morph, 0.25 }, { warp, 0.45 }, { semi2, -12 }, { fine2, 11 }, { mix2, 0.45 },
            { uvox, 3 }, { udet, 10 }, { uwid, 0.35 }, { sub, 0.45 },
            { cut, 620 }, { res, 0.60 }, { fkey, 0.30 }, { famt, 0.35 },
            { fatt, 0.001 }, { fdec, 0.15 }, { fsus, 0.25 }, { frel, 0.08 },
            { att, 0.003 }, { dec, 0.30 }, { sus, 0.85 }, { rel, 0.07 } } },

        { "Noise Riser", "Schranz",
          "Filtered noise that swells while held; automate the cutoff for the lift.", {
            { morph, sawWave }, { mix2, 0.0 }, { uvox, 1 }, { noise, 0.50 },
            { ftype, bandpass }, { cut, 900 }, { res, 0.45 }, { fkey, 0.0 }, { famt, 0.75 },
            { fatt, 1.20 }, { fdec, 2.00 }, { fsus, 1.0 }, { frel, 0.40 },
            { att, 0.60 }, { dec, 2.00 }, { sus, 1.0 }, { rel, 0.35 } } },

        { "Warehouse Lead", "Schranz",
          "Gliding lead with enough top end to cut over a loud kick.", {
            { morph, 0.20 }, { warp, 0.25 }, { fine2, 16 }, { mix2, 0.35 },
            { uvox, 5 }, { udet, 26 }, { uwid, 0.75 },
            { cut, 4600 }, { res, 0.30 }, { fkey, 0.45 }, { famt, 0.40 },
            { fatt, 0.002 }, { fdec, 0.35 }, { fsus, 0.45 }, { frel, 0.15 },
            { att, 0.004 }, { dec, 0.50 }, { sus, 0.80 }, { rel, 0.14 }, { gli, 0.06 } } },

        // ---------------- Drum & Bass ----------------
        { "Reese Bass", "Drum & Bass",
          "The staple: two saws a quarter-tone apart, filtered low and wide.", {
            { morph, sawWave }, { fine2, 26 }, { mix2, 0.50 },
            { uvox, 3 }, { udet, 14 }, { uwid, 0.55 }, { sub, 0.30 },
            { cut, 700 }, { res, 0.28 }, { fkey, 0.25 }, { famt, 0.0 },
            { fatt, 0.004 }, { fdec, 0.30 }, { fsus, 0.50 }, { frel, 0.10 },
            { att, 0.005 }, { dec, 0.30 }, { sus, 1.0 }, { rel, 0.12 },
            { lrate, 0.22 }, { lamt, 0.25 }, { ldest, toCutoff } } },

        { "Neuro Growl", "Drum & Bass",
          "Fast filter wobble on a detuned stack: the neuro starting point.", {
            { morph, 0.15 }, { warp, 0.50 }, { fine2, 32 }, { mix2, 0.50 },
            { uvox, 5 }, { udet, 18 }, { uwid, 0.50 },
            { cut, 480 }, { res, 0.68 }, { fkey, 0.25 }, { famt, 0.45 },
            { fatt, 0.001 }, { fdec, 0.20 }, { fsus, 0.30 }, { frel, 0.08 },
            { att, 0.003 }, { dec, 0.25 }, { sus, 1.0 }, { rel, 0.08 },
            { lrate, 6.2 }, { lamt, 0.70 }, { ldest, toCutoff } } },

        { "Sub Roller", "Drum & Bass",
          "Clean sine bottom octave; layer a mid bass on top of it.", {
            { morph, sineWave }, { mix2, 0.0 }, { uvox, 1 }, { sub, 0.55 },
            { cut, 220 }, { res, 0.0 }, { fkey, 0.50 }, { famt, 0.0 },
            { fatt, 0.004 }, { fdec, 0.20 }, { fsus, 0.50 }, { frel, 0.08 },
            { att, 0.006 }, { dec, 0.20 }, { sus, 0.95 }, { rel, 0.06 } } },

        { "Jump Up Bass", "Drum & Bass",
          "Squelchy pulse bass with a fast filter snap on every note.", {
            { morph, 0.85 }, { warp, 0.45 }, { fine2, 18 }, { mix2, 0.40 },
            { uvox, 3 }, { udet, 12 }, { uwid, 0.40 },
            { cut, 950 }, { res, 0.60 }, { fkey, 0.35 }, { famt, 0.60 },
            { fatt, 0.001 }, { fdec, 0.18 }, { fsus, 0.15 }, { frel, 0.08 },
            { att, 0.003 }, { dec, 0.25 }, { sus, 0.90 }, { rel, 0.08 },
            { lrate, 3.2 }, { lamt, 0.30 }, { ldest, toWarp } } },

        { "Wobble Bass", "Drum & Bass",
          "Halftime wobble: one note per bar, the LFO plays the rhythm.", {
            { morph, 0.10 }, { fine2, 22 }, { mix2, 0.50 },
            { uvox, 3 }, { udet, 10 }, { uwid, 0.45 }, { sub, 0.30 },
            { cut, 420 }, { res, 0.72 }, { fkey, 0.20 }, { famt, 0.30 },
            { fatt, 0.001 }, { fdec, 0.30 }, { fsus, 0.40 }, { frel, 0.10 },
            { att, 0.004 }, { dec, 0.40 }, { sus, 1.0 }, { rel, 0.10 },
            { lrate, 3.0 }, { lamt, 0.85 }, { ldest, toCutoff } } },

        { "Amen Stab", "Drum & Bass",
          "Detuned hoover stab for the 1993 jungle break.", {
            { morph, 0.30 }, { warp, 0.30 }, { semi2, -12 }, { fine2, 12 }, { mix2, 0.45 },
            { uvox, 7 }, { udet, 30 }, { uwid, 0.85 },
            { cut, 3200 }, { res, 0.40 }, { fkey, 0.35 }, { famt, 0.50 },
            { fatt, 0.002 }, { fdec, 0.25 }, { fsus, 0.20 }, { frel, 0.15 },
            { att, 0.003 }, { dec, 0.35 }, { sus, 0.40 }, { rel, 0.18 } } },

        { "Liquid Keys", "Drum & Bass",
          "Warm triangle keys for the rolling liquid tune.", {
            { morph, -1.20 }, { fine2, 8 }, { mix2, 0.40 },
            { uvox, 3 }, { udet, 10 }, { uwid, 0.60 },
            { cut, 2600 }, { res, 0.15 }, { fkey, 0.50 }, { famt, 0.35 },
            { fatt, 0.01 }, { fdec, 0.50 }, { fsus, 0.25 }, { frel, 0.30 },
            { att, 0.010 }, { dec, 0.80 }, { sus, 0.35 }, { rel, 0.50 } } },

        { "Darkside Pad", "Drum & Bass",
          "Slow wide pad that breathes; sits under a halftime beat.", {
            { morph, -0.50 }, { fine2, -18 }, { mix2, 0.50 },
            { uvox, 7 }, { udet, 24 }, { uwid, 1.0 },
            { cut, 1400 }, { res, 0.20 }, { fkey, 0.30 }, { famt, 0.25 },
            { fatt, 1.20 }, { fdec, 2.00 }, { fsus, 0.60 }, { frel, 1.50 },
            { att, 0.90 }, { dec, 1.50 }, { sus, 0.70 }, { rel, 1.40 },
            { lrate, 0.18 }, { lamt, 0.35 }, { ldest, toCutoff } } },

        { "Ragga Lead", "Drum & Bass",
          "Bright gliding lead for the dancehall-style hook.", {
            { morph, 0.60 }, { warp, 0.20 }, { fine2, 14 }, { mix2, 0.35 },
            { uvox, 3 }, { udet, 16 }, { uwid, 0.65 },
            { cut, 5200 }, { res, 0.30 }, { fkey, 0.60 }, { famt, 0.35 },
            { fatt, 0.002 }, { fdec, 0.30 }, { fsus, 0.40 }, { frel, 0.12 },
            { att, 0.004 }, { dec, 0.40 }, { sus, 0.85 }, { rel, 0.12 }, { gli, 0.08 } } },

        // ---------------- Techno ----------------
        { "Acid 303", "Techno",
          "Resonant saw with a snappy filter env; turn glide up for slides.", {
            { morph, sawWave }, { mix2, 0.0 }, { uvox, 1 },
            { cut, 620 }, { res, 0.60 }, { fkey, 0.70 }, { famt, 0.65 },
            { fatt, 0.001 }, { fdec, 0.22 }, { fsus, 0.0 }, { frel, 0.06 },
            { att, 0.004 }, { dec, 0.35 }, { sus, 0.60 }, { rel, 0.06 }, { gli, 0.05 } } },

        { "Detroit Stab", "Techno",
          "Detuned chord stab; hold three notes and let the release ring.", {
            { morph, 0.40 }, { semi2, -12 }, { fine2, 11 }, { mix2, 0.45 },
            { uvox, 5 }, { udet, 20 }, { uwid, 0.80 },
            { cut, 2400 }, { res, 0.30 }, { fkey, 0.35 }, { famt, 0.50 },
            { fatt, 0.002 }, { fdec, 0.15 }, { fsus, 0.0 }, { frel, 0.30 },
            { att, 0.004 }, { dec, 0.25 }, { sus, 0.0 }, { rel, 0.45 } } },

        { "Dub Chord", "Techno",
          "Soft wide chord drifting across the stereo field; feed it a long delay.", {
            { morph, -0.80 }, { fine2, 7 }, { mix2, 0.45 },
            { uvox, 3 }, { udet, 14 }, { uwid, 0.75 },
            { cut, 1200 }, { res, 0.20 }, { fkey, 0.35 }, { famt, 0.30 },
            { fatt, 0.05 }, { fdec, 0.60 }, { fsus, 0.30 }, { frel, 0.60 },
            { att, 0.02 }, { dec, 0.60 }, { sus, 0.30 }, { rel, 0.90 },
            { lrate, 0.45 }, { lamt, 0.55 }, { ldest, toPan } } },

        { "Hypnotic Blip", "Techno",
          "Ultra-short blip for 16th-note patterns.", {
            { morph, triWave }, { fine2, 5 }, { mix2, 0.30 }, { uvox, 1 },
            { cut, 3000 }, { res, 0.50 }, { fkey, 0.60 }, { famt, 0.70 },
            { fatt, 0.001 }, { fdec, 0.05 }, { fsus, 0.0 }, { frel, 0.04 },
            { att, 0.001 }, { dec, 0.09 }, { sus, 0.0 }, { rel, 0.05 } } },

        { "Peak Drone", "Techno",
          "Wide detuned drone that opens over half a minute: a B-section in one note.", {
            { morph, 0.10 }, { semi2, 12 }, { fine2, 24 }, { mix2, 0.50 },
            { uvox, 7 }, { udet, 40 }, { uwid, 1.0 },
            { cut, 900 }, { res, 0.30 }, { fkey, 0.20 }, { famt, 0.20 },
            { fatt, 2.00 }, { fdec, 3.00 }, { fsus, 0.80 }, { frel, 1.00 },
            { att, 0.30 }, { dec, 2.00 }, { sus, 1.0 }, { rel, 0.80 },
            { lrate, 0.12 }, { lamt, 0.40 }, { ldest, toCutoff } } },

        // ---------------- Trance ----------------
        { "Supersaw Lead", "Trance",
          "Seven detuned saws, wide open: the trance lead.", {
            { morph, sawWave }, { fine2, -14 }, { mix2, 0.50 },
            { uvox, 7 }, { udet, 30 }, { uwid, 0.90 },
            { cut, 7000 }, { res, 0.15 }, { fkey, 0.40 }, { famt, 0.30 },
            { fatt, 0.005 }, { fdec, 0.60 }, { fsus, 0.60 }, { frel, 0.30 },
            { att, 0.010 }, { dec, 0.60 }, { sus, 0.90 }, { rel, 0.35 } } },

        { "Trance Pluck", "Trance",
          "Plucked supersaw for 16th-note arps.", {
            { morph, 0.05 }, { fine2, -10 }, { mix2, 0.45 },
            { uvox, 5 }, { udet, 22 }, { uwid, 0.85 },
            { cut, 4200 }, { res, 0.30 }, { fkey, 0.45 }, { famt, 0.55 },
            { fatt, 0.001 }, { fdec, 0.12 }, { fsus, 0.0 }, { frel, 0.20 },
            { att, 0.002 }, { dec, 0.22 }, { sus, 0.0 }, { rel, 0.25 } } },

        { "Rolling Bass", "Trance",
          "Short offbeat bass that leaves the downbeat to the kick.", {
            { morph, sawWave }, { fine2, 6 }, { mix2, 0.20 }, { uvox, 1 }, { sub, 0.40 },
            { cut, 550 }, { res, 0.25 }, { fkey, 0.40 }, { famt, 0.30 },
            { fatt, 0.001 }, { fdec, 0.10 }, { fsus, 0.0 }, { frel, 0.05 },
            { att, 0.002 }, { dec, 0.14 }, { sus, 0.0 }, { rel, 0.06 } } },

        { "Uplifting Pad", "Trance",
          "Long swelling pad; hold the chord through the breakdown.", {
            { morph, -0.40 }, { fine2, -20 }, { mix2, 0.50 },
            { uvox, 7 }, { udet, 26 }, { uwid, 1.0 },
            { cut, 3000 }, { res, 0.15 }, { fkey, 0.30 }, { famt, 0.25 },
            { fatt, 1.50 }, { fdec, 2.50 }, { fsus, 0.70 }, { frel, 2.00 },
            { att, 1.20 }, { dec, 2.00 }, { sus, 0.80 }, { rel, 1.80 },
            { lrate, 0.20 }, { lamt, 0.30 }, { ldest, toCutoff } } },

        // ---------------- House ----------------
        { "Organ Bass", "House",
          "Sine plus the octave above: the classic house organ bass.", {
            { morph, -1.60 }, { semi2, 12 }, { mix2, 0.45 }, { uvox, 1 },
            { cut, 1800 }, { res, 0.10 }, { fkey, 0.50 }, { famt, 0.20 },
            { fatt, 0.002 }, { fdec, 0.15 }, { fsus, 0.40 }, { frel, 0.05 },
            { att, 0.004 }, { dec, 0.20 }, { sus, 0.80 }, { rel, 0.06 } } },

        { "Deep Chord", "House",
          "Warm filtered chord for a deep-house stab.", {
            { morph, -0.70 }, { fine2, 9 }, { mix2, 0.45 },
            { uvox, 3 }, { udet, 12 }, { uwid, 0.70 },
            { cut, 1600 }, { res, 0.20 }, { fkey, 0.35 }, { famt, 0.30 },
            { fatt, 0.01 }, { fdec, 0.40 }, { fsus, 0.30 }, { frel, 0.40 },
            { att, 0.03 }, { dec, 0.50 }, { sus, 0.50 }, { rel, 0.60 } } },

        { "French Pluck", "House",
          "Bright short pluck for filtered-disco chords.", {
            { morph, 0.25 }, { warp, 0.30 }, { fine2, -8 }, { mix2, 0.40 },
            { uvox, 3 }, { udet, 16 }, { uwid, 0.70 },
            { cut, 3400 }, { res, 0.35 }, { fkey, 0.45 }, { famt, 0.50 },
            { fatt, 0.001 }, { fdec, 0.10 }, { fsus, 0.0 }, { frel, 0.12 },
            { att, 0.002 }, { dec, 0.16 }, { sus, 0.0 }, { rel, 0.14 } } },

        // ---------------- Hardstyle ----------------
        { "Screech Lead", "Hardstyle",
          "Bandpass screech to sit over a reverse bass.", {
            { morph, sqrWave }, { warp, 0.55 }, { fine2, 20 }, { mix2, 0.40 },
            { uvox, 5 }, { udet, 28 }, { uwid, 0.75 },
            { ftype, bandpass }, { cut, 1600 }, { res, 0.65 }, { fkey, 0.55 }, { famt, 0.65 },
            { fatt, 0.002 }, { fdec, 0.30 }, { fsus, 0.25 }, { frel, 0.15 },
            { att, 0.003 }, { dec, 0.45 }, { sus, 0.90 }, { rel, 0.12 } } },

        { "Reverse Bass", "Hardstyle",
          "Offbeat swell that ducks out of the kick's way.", {
            { morph, 0.20 }, { semi2, -12 }, { fine2, 8 }, { mix2, 0.35 },
            { uvox, 1 }, { sub, 0.50 },
            { cut, 280 }, { res, 0.25 }, { fkey, 0.25 }, { famt, 0.20 },
            { fatt, 0.05 }, { fdec, 0.20 }, { fsus, 0.60 }, { frel, 0.05 },
            { att, 0.12 }, { dec, 0.20 }, { sus, 0.85 }, { rel, 0.04 } } },

        // ---------------- Dubstep ----------------
        { "Growl Bass", "Dubstep",
          "Slow LFO growl; ride the cutoff knob for the talking effect.", {
            { morph, 0.30 }, { warp, 0.40 }, { fine2, 24 }, { mix2, 0.50 },
            { uvox, 3 }, { udet, 12 }, { uwid, 0.45 }, { sub, 0.30 },
            { cut, 380 }, { res, 0.72 }, { fkey, 0.20 }, { famt, 0.40 },
            { fatt, 0.001 }, { fdec, 0.25 }, { fsus, 0.35 }, { frel, 0.10 },
            { att, 0.004 }, { dec, 0.30 }, { sus, 1.0 }, { rel, 0.10 },
            { lrate, 2.0 }, { lamt, 0.85 }, { ldest, toCutoff } } },

        { "Talk Wobble", "Dubstep",
          "Bandpass wobble that vowels as it moves.", {
            { morph, 0.20 }, { warp, 0.35 }, { fine2, 18 }, { mix2, 0.45 },
            { uvox, 3 }, { udet, 14 }, { uwid, 0.50 }, { sub, 0.25 },
            { ftype, bandpass }, { cut, 700 }, { res, 0.75 }, { fkey, 0.20 }, { famt, 0.30 },
            { fatt, 0.001 }, { fdec, 0.30 }, { fsus, 0.50 }, { frel, 0.10 },
            { att, 0.004 }, { dec, 0.35 }, { sus, 1.0 }, { rel, 0.10 },
            { lrate, 1.5 }, { lamt, 0.75 }, { ldest, toCutoff } } },

        { "Sub Drop", "Dubstep",
          "Pure sine sub on a long glide; play two notes to get the drop.", {
            { morph, sineWave }, { mix2, 0.0 }, { uvox, 1 }, { sub, 0.70 },
            { cut, 160 }, { res, 0.05 }, { fkey, 0.30 }, { famt, 0.0 },
            { fatt, 0.004 }, { fdec, 0.20 }, { fsus, 0.50 }, { frel, 0.10 },
            { att, 0.010 }, { dec, 0.30 }, { sus, 1.0 }, { rel, 0.20 }, { gli, 0.30 } } },
    };
    return bank;
}


// ---------------------------------------------------------------------------
// user patches
// ---------------------------------------------------------------------------

namespace
{
const juce::Identifier PRESET_TAG { "EURYPRESET" };
} // namespace

std::optional<Preset> readFile (const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (PRESET_TAG)
        || xml->getStringAttribute ("type", "synth") != "synth")
        return {};

    Preset preset;
    preset.name = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
    preset.category = userCategory;
    preset.description = xml->getStringAttribute ("description");

    // Only parameters the synth actually has, clamped into range: a
    // hand-edited file cannot push the engine somewhere its knobs cannot go.
    for (const auto& descriptor : channelparams::synth())
    {
        const auto id = descriptor.id.toString();
        if (! xml->hasAttribute (id))
            continue;
        preset.values.emplace_back (descriptor.id,
                                    juce::jlimit (descriptor.range.start, descriptor.range.end,
                                                  xml->getDoubleAttribute (id)));
    }
    return preset;
}

bool writeFile (const juce::File& file, const juce::ValueTree& channel, const juce::String& name)
{
    juce::XmlElement xml (PRESET_TAG);
    xml.setAttribute ("type", "synth");
    xml.setAttribute ("name", name);
    for (const auto& descriptor : channelparams::synth())
        xml.setAttribute (descriptor.id.toString(),
                          (double) channel.getProperty (descriptor.id, descriptor.defaultValue));
    return xml.writeTo (file);
}

std::vector<Preset> user()
{
    std::vector<Preset> out;
    const auto directory = userDirectory();
    if (! directory.isDirectory())
        return out;

    for (const auto& entry : juce::RangedDirectoryIterator (directory, false, "*" + fileExtension))
        if (auto preset = readFile (entry.getFile()))
            out.push_back (std::move (*preset));

    std::sort (out.begin(), out.end(), [] (const Preset& a, const Preset& b)
               { return a.name.compareNatural (b.name) < 0; });
    return out;
}

std::vector<Preset> all()
{
    auto out = factory();
    for (auto& preset : user())
        out.push_back (std::move (preset));
    return out;
}

juce::StringArray categories()
{
    juce::StringArray out;
    for (const auto& preset : all())
        if (! out.contains (preset.category))
            out.add (preset.category);
    return out;
}

std::optional<Preset> find (const juce::String& name)
{
    for (const auto& preset : all())
        if (preset.name == name)
            return preset;
    return {};
}

bool isFactoryName (const juce::String& name)
{
    for (const auto& preset : factory())
        if (preset.name == name)
            return true;
    return false;
}

void apply (juce::ValueTree channel, const Preset& preset, juce::UndoManager* undo)
{
    for (const auto& descriptor : channelparams::synth())
    {
        double value = descriptor.defaultValue;
        for (const auto& [id, presetValue] : preset.values)
            if (id == descriptor.id)
            {
                value = presetValue;
                break;
            }
        channel.setProperty (descriptor.id, value, undo);
    }

    channel.setProperty (ids::presetName, preset.name, undo);
}

juce::File save (const juce::ValueTree& channel, const juce::String& name)
{
    const auto directory = userDirectory();
    if (! directory.createDirectory())
        return {};

    const auto file = directory.getChildFile (juce::File::createLegalFileName (name) + fileExtension);
    return writeFile (file, channel, name) ? file : juce::File();
}
} // namespace synthpresets
