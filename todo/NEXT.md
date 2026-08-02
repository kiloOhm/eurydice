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
- ~~Undo coalescing~~ — `src/model/UndoGesture.h` brackets each gesture in one
  transaction; knobs, faders, sends, the graph lane, curve points, clip and
  note drags and every API call are now one undo step apiece.

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
- ~~Kick design tooling.~~ — the sampler now has start/end trim, reverse, a
  pitch envelope, a drive stage (soft / hard clip / foldback) and a
  linear-to-exponential gain envelope shape. There is also a dedicated `kick`
  channel type — swept sine/triangle body, click and noise layers, drive — in
  the rack's add-channel menu and over the control API.
- **Time-stretch modes.** Rubber Band is wired for offline stretching only;
  no per-clip formant/transient options, and no realtime stretch.

## P2 — quality of life

- Channel reorder by drag; channel colours and grouping.
- MIDI learn for hardware knobs (deferred from v1).
- Piano roll: chop/glue, arpeggiator, strum, per-note pan lane, slide notes.
- ~~Autosave and crash recovery~~ — a dirty project is mirrored to
  `~/Library/Application Support/Eurydice/recovery/` once a minute, written
  atomically off the message thread, and offered back at startup.
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
