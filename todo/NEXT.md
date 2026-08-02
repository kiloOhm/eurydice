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
- ~~Built-in effects~~ — clipper (4 curves, up to 8x oversampling), filter
  (LP/HP/BP with envelope follower and tempo-synced LFO), 4-band parametric EQ
  with HP/LP sweeps, compressor with an external sidechain, tempo-synced
  ping-pong delay and an algorithmic reverb. Under "Built-in" in the mixer's
  slot menu; parameters live on the SLOT tree, so they save, undo and automate
  like everything else.
- **Sidechain ducking presets.** The compressor takes any insert as its
  sidechain source, but there is still no one-click "duck this bus from the
  kick" — you pick the source from a combo in the compressor editor.
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
- ~~Autosave and crash recovery~~ — a dirty project is mirrored to
  `~/Library/Application Support/Eurydice/recovery/` once a minute, written
  atomically off the message thread, and offered back at startup.
- Plugin sandboxing — a crashing VST currently takes the whole DAW down.
- Audio recording: loop-record takes and comping.
- ~~Export options~~ — dialog with bit depth, sample rate, loop-range-only,
  MP3, per-insert or per-channel stems and peak normalisation; a render also
  ignores an armed loop instead of being truncated by it.
- ~~Metronome and count-in~~ — `CLICK` in the transport bar with a level, ⇧⌘M,
  and an optional one- or two-bar count-in before armed recording.
- ~~Per-pattern swing~~ — the rack swing knob now writes to the pattern;
  patterns without one follow the project value.

## Follow-ups from the dead-transport / crashy-startup fix (2026-08-02)

- **JUCE AudioIODeviceCombiner is unsafe on this machine.** Opening default
  input + output (different hardware devices) builds a combiner around a
  private aggregate device; creating it fires a device-list notification that
  re-enters the combiner's restart path from a CoreAudio dispatch thread
  (use-after-free caught under Guard Malloc; 7/8 launches died, and when it
  survived the device could come up dead/half-dead — frozen transport,
  right channel silent). We now start output-only and enable input when
  recording arms, which avoids the combiner at startup but still uses it
  while record is armed. Real fix: try a newer JUCE (races in this area have
  had upstream fixes) or drive input via a second AudioDeviceManager.
- Disarming record leaves the input open (avoids a device restart per toggle);
  revisit if the extra latency/CPU of a combined device while armed matters.

## Known smaller gaps

- Per-note pan is stored but not applied at playback (needs an engine event
  refactor to pass per-note pan through to generators).
- Time signature is assumed 4/4 throughout — fine for these genres, wrong for
  anything else.
- The browser supports double-click-to-add but not drag-and-drop into the rack.

## Noticed while verifying the merges

- **Compressor defaults are heavy-handed.** A kick through Clipper → EQ →
  Compressor → Reverb lost ~5x RMS versus Clipper alone (0.350 -> 0.066).
  The DSP is correct and tested; the stock threshold/ratio just clamp hard
  with no makeup. Worth retuning the defaults so dropping one on a bus is
  roughly unity to start with.
