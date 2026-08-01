# What's next

All 19 build tasks are done. This is the honest gap list, ordered by what
actually blocks finishing a track in techno / schranz / tekk / DnB / uptempo /
hardcore / frenchcore.

## P0 — you hit these within the first hour of producing

- **Loop range + selection playback.** There is no way to loop 8 bars while you
  work on them. Currently playback is either the whole pattern or the whole
  song from the top. This is the single most missed thing.
- **Per-step graph editor** (velocity / pan / pitch per step in the rack).
  Hi-hat grooves and rolls are unusable without per-step velocity. Partly
  reachable via the piano roll velocity lane, but not at rack speed.
- **Pattern management.** You can add patterns and rename them, but not clone,
  delete, or reorder. Cloning is how you build variations.
- **Undo coalescing.** One knob drag currently pushes dozens of undo steps, so
  ⌘Z is close to useless after tweaking.
- **Note repeat / roll tool** in the piano roll — rolls and ratchets are the
  backbone of tekk and frenchcore.

## P1 — needed for these genres specifically

- **Built-in effects.** The mixer has slots but Eurydice ships zero effects, so
  a fresh install can only change gain. Minimum viable set, in genre order:
  clipper/distortion, filter (with envelope + LFO for risers), parametric EQ,
  compressor with an external sidechain input, delay, reverb. Free VSTs cover
  a lot of this (see `docs/free-plugins.md`), but stock effects matter for
  projects that must open on another machine.
- **Sidechain ducking.** Insert-to-insert routing exists in the engine, but
  there is no UI for "duck this bus from the kick", which is the defining
  sound of the genre.
- **Kick design tooling.** For hardcore/frenchcore the kick *is* the track.
  The sampler needs sample-start offset, a pitch envelope (for the classic
  downward kick tail), reverse, and a distortion stage. A dedicated kick
  synth channel would be better still.
- **Time-stretch modes.** Rubber Band is wired for offline stretching only;
  no per-clip formant/transient options, and no realtime stretch.

## P2 — quality of life

- Channel reorder by drag; channel colours and grouping.
- MIDI learn for hardware knobs (deferred from v1).
- Piano roll: chop/glue, arpeggiator, strum, per-note pan lane, slide notes.
- Autosave and crash recovery.
- Plugin sandboxing — a crashing VST currently takes the whole DAW down.
- Export: render only the loop range; normalisation; per-channel (not just
  per-insert) stems.
- Audio recording: loop-record takes and comping.
- Metronome and count-in.
- Per-pattern swing (currently global only).

## Known smaller gaps

- Per-note pan is stored but not applied at playback (needs an engine event
  refactor to pass per-note pan through to generators).
- Time signature is assumed 4/4 throughout — fine for these genres, wrong for
  anything else.
- The browser supports double-click-to-add but not drag-and-drop into the rack.
