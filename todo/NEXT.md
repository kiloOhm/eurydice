# What's next

All 19 build tasks are done. This is the honest gap list, ordered by what
actually blocks finishing a track in techno / schranz / tekk / DnB / uptempo /
hardcore / frenchcore.

## P0 — shipped

- ~~Loop range + selection playback~~ — drag the playlist ruler, `LOOP` in the
  transport bar, ⇧⌘L, with sample-accurate wrapping.
- ~~Per-step graph editor~~ — `GRAPH` toggle in the rack header; velocity, pan
  and pitch per step.
- ~~Pattern management~~ — clone, rename, delete and reorder.
- ~~Note repeat / roll tool~~ — plus chop, glue and strum, with velocity ramps
  for ratchets.

## P0 — still open

- **Undo coalescing.** One knob drag still pushes dozens of undo steps, so ⌘Z
  is close to useless after tweaking. The piano-roll batch tools wrap
  themselves in single transactions; knobs, faders and the graph lane do not.

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
- Audio recording: loop-record takes and comping.
- ~~Export options~~ — dialog with bit depth, sample rate, loop-range-only,
  MP3, per-insert or per-channel stems and peak normalisation; a render also
  ignores an armed loop instead of being truncated by it.
- ~~Metronome and count-in~~ — `CLICK` in the transport bar with a level, ⇧⌘M,
  and an optional one- or two-bar count-in before armed recording.
- ~~Per-pattern swing~~ — the rack swing knob now writes to the pattern;
  patterns without one follow the project value.

## Known smaller gaps

- Per-note pan is stored but not applied at playback (needs an engine event
  refactor to pass per-note pan through to generators).
- Time signature is assumed 4/4 throughout — fine for these genres, wrong for
  anything else.
- The browser supports double-click-to-add but not drag-and-drop into the rack.
