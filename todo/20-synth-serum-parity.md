Status: pending

# Synth: Serum-2-class feature parity

Close the gap between the builtin synth and Serum 2 — not a 1:1 clone, but
near feature parity on the parts that make Serum *Serum*: wavetables, deep
modulation, dual filters, real voicing. Written as work packages (WPs) sized
for parallel subagents; Stage 0 is serial groundwork, everything after fans
out.

## Non-goals (deliberate, keep it that way)

- **Arpeggiator / clip sequencer / MIDI out** — the DAW already is one.
- **In-synth FX rack** — the mixer owns effects. The FX gap is closed
  app-wide instead (WP13: chorus/phaser/distortion as builtin mixer effects).
- **Granular, spectral, SFZ multisample oscillators** — hybrid-workstation
  tail, poor effort/value here. Sample osc is a stretch goal (WP14).
- **Wavetable editor** — import is enough; skip drawing/FFT editing.
- Skins, preset store, AAX. Never relevant.

## Architecture facts every subagent must know

- **Param flow**: descriptor table [`ChannelParams.h`](../src/model/ChannelParams.h)
  (`channelparams::synth()`) drives both the editor knobs
  ([`ChannelEditor.cpp`](../src/ui/rack/ChannelEditor.cpp)) and automation
  registration. Values live flat on the CHANNEL ValueTree
  ([`Ids.h`](../src/model/Ids.h)); edits reach the engine via
  `GeneratorPool::applySynthParams()` storing into `SynthGenerator::Params`
  atomics; automation grabs the same atomics via `getAutomatableParam()`.
  **Adding a knob = one descriptor row + one id + one atomic + one line in
  applySynthParams + one `getAutomatableParam` entry.** The editor and
  automation follow automatically.
- **Non-scalar state** (matrix rows, LFO shapes, wavetable refs) does NOT fit
  the flat-prop pattern. Precedent: drum PADs — child trees on the CHANNEL,
  iterated in `applyDrumParams()`. Follow that: child trees + message-thread
  rebuild into RT-safe structs owned by the generator.
- **Back-compat is law**: old projects must load and sound identical.
  `prop()` fallbacks in GeneratorPool are the mechanism; precedent: the
  filter env falling back to amp-env values (GeneratorPool.cpp:110). New
  params default to "off"/legacy behaviour.
- **Displays render the real DSP** ([`SynthDisplays.h`](../src/ui/rack/SynthDisplays.h)):
  every new module display must be measured from the same code the engine
  runs (share headers like [`SynthOsc.h`](../src/engine/SynthOsc.h)), never a
  drawing of it. Drift tests pin this (see SynthDisplayTests.cpp).
- **Tempo** exists in `EngineSnapshot` (EngineSnapshot.h:144) but generators
  don't see it — `Generator::render()` only gets buffer + MIDI. WP0.1 fixes
  this.
- **Pitch bend is currently dropped on the floor** — nothing in src/ handles
  `isPitchWheel()`. Live MIDI already lands in the MidiBuffer, so handling it
  is generator-local.
- **Tests**: googletest, `tests/`, binary at
  `build/EurydiceTests_artefacts/RelWithDebInfo/EurydiceTests`. Build with
  `cmake --build build --target EurydiceTests`, run with
  `--gtest_filter=Synth*` etc. Existing synth tests: SynthExpansionTests.cpp,
  SynthDisplayTests.cpp, GeneratorTests.cpp, ChannelParamTests.cpp.
- **RT rules**: render() is audio-thread; no locks/allocs. Generators are
  created/destroyed message-thread only.

## Merge discipline for parallel agents

Hot shared files: `Ids.h`, `ChannelParams.h`, `GeneratorPool.cpp`,
`SynthGenerator.h/.cpp`, `ChannelEditor.cpp`, `Dirty merges live here.`

1. Stage 0 lands **serially first** — it carves SynthGenerator into seams so
   later WPs mostly own separate new files.
2. Each WP appends its ids/descriptors in a contiguous block tagged with a
   comment (`// WP3: LFOs`) — append-only in shared files, never reorder.
3. One WP = one branch/worktree = one test file it owns. Run the full
   EurydiceTests suite before merge, not just your filter.
4. Merge order within a stage: whoever touches SynthGenerator's voice loop
   most merges first (noted per WP below).

---

## Stage 0 — serial groundwork (one agent, in order)

### WP0.1 Render context: tempo + pitch bend + wheel into generators
- Add a lightweight context to rendering: either extend `Generator::render`
  to `render (buffer, midi, const RenderContext&)` (`struct RenderContext
  { double bpm; }`) or a `setTempo(double)` atomic pushed on snapshot
  rebuild. Pick whichever touches fewer call sites (OfflineRenderer too).
- SynthGenerator: handle `isPitchWheel()` → per-generator bend value,
  `pitchBendRange` param (default 2 st); handle CC1 → `modWheel` value
  (0..1) stored for the mod system; handle channel aftertouch similarly.
- Bend multiplies voice frequency alongside glide (`currentSemi + bend`).
- Params: `pitchBendRange` (0–24 st, default 2, VOICE section).
- Tests (`SynthMidiTests.cpp`): bend moves a rendered note's pitch by the
  configured range; no bend = bit-identical output to before.

### WP0.2 Voice/mod architecture refactor (the big enabler)
- Split SynthGenerator internals into headers so later WPs own files, not
  regions: `SynthVoice.h` (Voice struct + per-sample tick),
  `SynthModSources.h` (a plain struct of per-sample mod source values:
  lfo[3], env3, fenv, velocity, keytrack, wheel, aftertouch, bend, noteRandom,
  macro[4] — zeros until later WPs feed them).
- Move the LFO **per-voice** (Serum semantics: retrigger per note; a shared
  "global" phase stays as one LFO mode later). Keep today's sound for old
  projects: with lfoTarget/lfoAmount semantics unchanged at defaults.
- Voice loop calls one seam: `modsources::evaluate(...)` then
  `applyModulation(...)`. Later WPs extend those, not the loop.
- No behaviour change: pin with a render-hash test (same params in, same
  buffer out as before the refactor, within float tolerance).

### WP0.3 Mod destination registry + matrix storage scaffold
- **Storage**: `ids::MODS`/`ids::MOD` child trees on CHANNEL with
  `modSource`, `modDest` (param id string), `modAmount` (-1..1), `bypass`.
- **Snapshot**: `applySynthParams` rebuilds a fixed-size RT struct
  (`std::array<ModRoute, 16>` + count, double-buffered or rebuilt under the
  existing snapshot mechanism — study how DrumMachineGenerator swaps pad
  state safely).
- **Destination registry**: table mapping dest param id → how modulation
  applies (additive in normalised units via the descriptor's
  `NormalisableRange`, except cutoff which modulates in octaves — match the
  existing `fenv * envAmt * 4.0f` idiom). The `channelparams::synth()` table
  already knows every range; reuse it, don't duplicate.
- Engine applies routes but with **zero sources wired yet** (WP8 wires them);
  matrix rows with amount 0 are free.
- Tests (`ModMatrixTests.cpp`): tree→snapshot rebuild, clamping, dest lookup,
  route with hardcoded source value modulates cutoff/pitch measurably.

---

## Stage 1 — engine fan-out (parallel after Stage 0)

### WP1 Wavetable oscillator ⭐ the identity feature
- New files: `src/engine/Wavetable.h/.cpp` (bank + mipmapped playback),
  `tests/WavetableTests.cpp`.
- Format: 2048-sample frames (Serum convention), N frames per table;
  mipmaps band-limited per octave (FFT → zero bins → iFFT at load, message
  thread); linear interp between frames = the WT position morph.
- `oscType` param per osc (0 = basic/analytic as today, 1 = wavetable) +
  `wtPosition` (0..1, the prime mod target) + `wtIndex` (which table).
- Factory bank: 12–16 tables generated analytically at first launch or
  compile time (saw→square PWM, formant sweeps, harmonic stacks, bell/FM
  sweeps, vocal-ish formants) — no licensing risk, no binary blobs.
- Import: drop/load a WAV; treat as consecutive 2048-frames; store file path
  on the channel (`wtPath`), embed nothing.
- UI: OscDisplay learns to draw the current frame (same code path the engine
  reads — share the bank).
- Merge note: touches the voice tick seam; merges FIRST in Stage 1.

### WP2 Independent oscillator 2 (+ per-osc mixer basics)
- Osc 2 gets its own `osc2Shape`, `osc2Warp`, `osc2Type`, `osc2WtPosition`,
  and per-osc `osc1Level/osc1Pan/osc2Level/osc2Pan`.
- Legacy: missing `osc2Shape` falls back to `oscShape` (today's slaved
  behaviour, bit-identical), `osc2Mix` keeps meaning as the crossfade until
  per-osc levels are set (fallback logic in applySynthParams).
- Merge note: second into the voice tick after WP1.

### WP3 LFO overhaul (3 LFOs, shapes, tempo sync, trigger modes)
- `lfo{1,2,3}Rate/Amount/Shape/Sync/Retrig`; shapes: sine, tri, saw up/down,
  square, S&H (per-voice RNG). Sync divisions when `Sync` on: 4/1 … 1/64
  incl. dotted/triplet (needs WP0.1 tempo).
- LFO1 keeps legacy ids (`lfoRate`/`lfoAmount`/`lfoTarget` still work as the
  hardwired route for old projects; new routing goes through the matrix).
- Retrig modes: free (today's global phase), note (reset at noteOn), env
  (one-shot).
- Owns `SynthLfo.h` + `SynthLfoTests.cpp`; feeds `SynthModSources`.

### WP4 Envelope 3 + curve shaping
- Replace `juce::ADSR` with own `CurveEnv` (attack/decay/release curvature
  -1..1, sampler's `envShape` precedent) for amp, filter, and a new free
  env3. Default curvature reproduces juce::ADSR's shape closely — pin amp
  behaviour with a tolerance test so old projects don't audibly change.
- Params: `env3Attack/Decay/Sustain/Release`, `ampCurve`, `fenvCurve`,
  `env3Curve`.
- EnvelopeDisplay reads the new env code (drift test).
- Owns `CurveEnv.h` + `CurveEnvTests.cpp`.

### WP5 Voicing: mono/legato, glide modes, stealing, unison 16
- `voiceMode` (0 poly / 1 mono / 2 legato): mono retrigs envs, legato
  doesn't; released-key memory returns to held notes (classic mono synth).
- `glideMode` (0 always / 1 legato-only); glide time semantics unchanged.
- Stealing: prefer inactive → quietest-in-release → oldest (never
  `voices[0]` blindly).
- `maxUnison` 7 → 16; `uniNorm` already scales; verify CPU at 16×2 osc.
- Owns `SynthVoicingTests.cpp`. Touches noteOn/noteOff only — low conflict.

### WP6 Filter expansion: dual filters + types
- Filter slot B: `filter2Type/2Cutoff/2Res/2Key/2EnvAmt`, `filterRouting`
  (0 serial / 1 parallel), `filterMix` (A↔B balance in parallel).
- New types beyond SVF LP/BP/HP: 24 dB ladder w/ drive (`filterDrive`),
  notch, peak, comb +/-. Keep the type list an enum shared with the display.
- Per-source routing (osc1/osc2/sub/noise → A / B / bypass) as 4 small
  int params — Serum's mixer page, minus the page.
- FilterResponseDisplay grows type + slot B support (same shared DSP).
- Owns `SynthFilter.h` + `SynthFilterTests.cpp`. Voice-loop seam: merges
  after WP2.

### WP7 Sub + noise upgrade (small)
- Sub: `subShape` (sine/tri/square), `subOctave` (-2/-1/0), routes through
  the filter (currently pre-filter — verify and keep or add `subDirect`).
- Noise: `noiseColor` (white/pink), `noiseKey` (pitch-tracked resampling
  optional; skip if >1 day), noise into filter routing (WP6 param).
- Owns `SynthLayerTests.cpp`.

---

## Stage 2 — modulation system (parallel; WP8 first, 10/11 need it)

### WP8 Mod matrix engine: wire the sources
- Fill `SynthModSources::evaluate()` per voice: lfo1-3 (WP3), env3/fenv
  (WP4), velocity, keytrack (note/127), modWheel, aftertouch (WP0.1),
  per-note random (seeded at noteOn), macro1-4 (WP9 atomics).
- 16 routes, bipolar amounts, per-route source×aux later (skip aux for v1).
- Control-rate is fine: evaluate at the existing 64-sample glide chunk,
  smooth destinations that zipper (cutoff, levels).
- Tests: each source measurably modulates pitch/cutoff; velocity→cutoff
  route reproduces the classic "vel to filter" patch; render remains
  RT-safe (no allocs — instrument with an assert-based allocator guard if
  cheap).

### WP9 Macros (tiny, fully parallel)
- `macro1..4` params (0..1, default 0) — plain descriptor rows, automatable
  like any knob, so playlist automation can drive macros driving the matrix.
- Editor: 4 knobs in a MACROS section. Done.

### WP10 Matrix UI
- Bespoke component in the synth editor (descriptor table can't express it):
  N rows of source-combo / dest-combo / bipolar amount slider / bypass,
  editing the MODS child trees (undoable via the existing UndoManager path).
- Mod rings: knobs whose param is a matrix dest paint an arc of the live
  modulated range (read base + sum of route amounts; static, not
  per-sample).
- Drag-drop assign: drag from an LFO/env display header onto a knob creates
  a route (Serum's signature workflow — cheap to build, huge feel win).
- Owns `ModMatrixPanel.h/.cpp` + `ModMatrixUiTests.cpp`.

### WP11 LFO + env display upgrades
- LfoDisplay: draw the actual selected shape at sync'd rate, click cycles
  shape, right-click division menu. EnvelopeDisplay: drag curvature.
- Stays honest: renders via SynthLfo/CurveEnv code (drift tests).

---

## Stage 3 — ecosystem (parallel, independent subsystems)

### WP12 Synth patch presets
- Save/load all synth CHANNEL props + MODS children as `.eurypreset` XML;
  right-click menu in ChannelEditor: Save preset / Load / factory list.
- 10–15 factory patches (bass, lead, pad, pluck, keys) proving the engine.
  Author them AFTER Stages 1–2 land.
- Generic enough to work for kick/sampler channels for free if trivial.

### WP13 Mixer FX gap: chorus, phaser, distortion
- Three new builtin effects following the existing pattern
  (ClipperEffect as template: identifier/displayName/specs/process +
  registry entry + BuiltinEffectEditor support): `builtin:chorus`
  (2–4 voice, rate/depth/mix), `builtin:phaser` (4–8 stage, feedback),
  `builtin:distortion` (Drive.h curves + tone + oversampling like clipper).
- Zero contact with synth files — safe to run any time in parallel.
- Owns tests in EffectTests.cpp style (own file: `NewFxTests.cpp`).

### WP14 (stretch) Sample oscillator
- Osc type 2 = sample: reuse SamplerGenerator's file loading/pitching for a
  per-osc sample layer (root note, loop on/off). Only start once WP1/WP2
  are merged and stable. Skip granular/spectral/SFZ permanently.

### WP15 Synth editor layout v2
- The synth editor will roughly double in module count (osc B, filter B,
  3 LFOs, env3, matrix, macros). Restructure into Serum-ish zones: OSC row,
  FILTER row, ENV/LFO tab strip, MATRIX panel below; keep every display
  live-DSP-backed. Do LAST — it consumes all other WPs' components.
- Owns the ChannelEditor synth section rewrite; nothing else runs in
  parallel with it on that file.

---

## Suggested execution waves

| Wave | WPs | Parallelism |
|------|-----|-------------|
| 0 | WP0.1 → WP0.2 → WP0.3 | serial, one agent |
| 1 | WP1, WP3, WP4, WP5, WP7, WP13 | 6 agents (WP1 merges first) |
| 2 | WP2, WP6, WP9 | 3 agents (after WP1) |
| 3 | WP8 → then WP10, WP11 | up to 3 agents |
| 4 | WP12, WP14, WP15 | 3 agents (WP15 last on ChannelEditor) |

Definition of done per WP: descriptor/id/apply/automation wiring complete,
old projects bit-compatible at defaults (or within stated tolerance), own
test file green, full EurydiceTests green, displays (if touched) drift-tested
against engine DSP.
