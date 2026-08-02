# What's next

The 19 build tasks and the first two gap rounds (P0 polish, built-in effects,
kick tooling, undo, autosave, export options, metronome) are done — see git
history and `todo/README.md`. This is what's still open, ordered by what
actually blocks finishing a track in techno / schranz / tekk / DnB / uptempo /
hardcore / frenchcore.

## P1 — genre blockers

- **Sidechain ducking preset.** The compressor takes any insert as its
  sidechain source, but the defining move of the genre — "duck this bus from
  the kick" — still takes four manual steps (add compressor, open editor, pick
  source, tune). Wants a one-click action on the insert context menu.
- **Compressor defaults are heavy-handed.** A kick through Clipper → EQ →
  Compressor → Reverb lost ~5x RMS versus Clipper alone (0.350 → 0.066). The
  DSP is correct and tested; the stock threshold/ratio clamp hard with no
  makeup gain. Retune so a fresh instance is roughly unity.
- **Time-stretch modes.** Rubber Band is wired for offline stretching only; no
  per-clip formant/transient options, no realtime stretch.

## P2 — quality of life

- Channel reorder by drag; channel colours and grouping.
- Browser drag-and-drop into the rack (double-click-to-add works today).
- Piano roll: arpeggiator, per-note pan lane, slide notes (chop/glue/strum
  exist).
- Audio recording: loop-record takes and comping.
- Plugin sandboxing — a crashing VST takes the whole DAW down (big: changes
  the hosting architecture to out-of-process).
- MIDI learn for hardware knobs (deferred from v1: needs hardware to verify).
- Effect editors: EQ/filter curve displays and a compressor gain-reduction
  meter; today they're knob grids.
- The rack swing knob is not automatable (project-level property with no
  engine target kind).
- Recovery files in `~/Library/Application Support/Eurydice/recovery/` are
  never garbage-collected.

## Engine / platform debt

- **JUCE AudioIODeviceCombiner is unsafe on this machine.** Opening default
  input + output (different hardware devices) builds a combiner around a
  private aggregate device; creating it fires a device-list notification that
  re-enters the combiner's restart path from a CoreAudio dispatch thread
  (use-after-free under Guard Malloc; 7/8 launches died). We start output-only
  and enable input when recording arms, which avoids the combiner at startup
  but still uses it while armed. Real fix: newer JUCE (upstream has had races
  fixed in this area) or a second AudioDeviceManager for input. Disarming
  record deliberately leaves the input open to avoid a device restart per
  toggle.
- Per-note pan is stored but not applied at playback (needs an engine event
  refactor to pass per-note pan through to generators).
- Time signature is assumed 4/4 throughout — fine for these genres, wrong for
  anything else.
- `channel.set` over the control API deliberately does not record automation
  while write is armed; revisit if AI-driven automation recording is wanted.
