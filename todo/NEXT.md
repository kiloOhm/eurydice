# What's next — round 3

Agreed 2026-08-02 after the UX/dynamic-mixer batch. Ordered; top section is
the active work.

## Now — finish the sound

1. ~~Retune compressor defaults~~ — shipped: -12 dB / 2.5:1 / +2.5 dB makeup,
   measured +0.9 dB RMS on the stock beat (was -4.9 dB), pinned by a test.
2. ~~One-click sidechain ducking~~ — shipped: insert menu → "Sidechain duck >
   from <insert>"; preset lives in CompressorEffect::configureDuckSlot with an
   engine-level pump test.
3. ~~Effect editors show what they do~~ — shipped: EQ and filter editors plot
   their true response (drift tests prove plotted == processed), the
   compressor draws its transfer curve with a live gain-reduction meter.

## Next — the AI angle

4. ~~Let the AI hear~~ — shipped: `render.analyze` / `daw_render_analyze`
   returns peak dB, RMS dB and five-band spectral shares for the master and
   every insert carrying signal, without writing files. Verified against the
   schranz project (rumble bus reads sub-heavy, clap bus high-heavy).
5. **AI-recordable automation** — mostly moot: `automation.create` +
   `automation.setPoints` already let an AI write any curve directly, which
   beats emulating live recording. Only revisit if live capture over the API
   turns out to matter.

## Then — workflow depth

6. ~~Channel drag-reorder + colours~~ — shipped: vertical drag on the name
   area reorders (amber insertion line, viewport autoscroll, one undo step;
   engine/automation follow by id, pinned by a test); right-click → Colour
   gives 8 swatches + None, tinting the row. Grouping proper was not built —
   colours are the lightweight version; revisit if racks still feel unwieldy.
7. ~~Browser drag-and-drop onto rack and playlist~~ — shipped: browser rows
   (and Finder files) drop onto the rack — a sampler row swallows the sample,
   anywhere else inserts a new sampler channel — and onto the playlist as
   bar-snapped audio clips, with hover indicators and one undo step per drop.
8. ~~Loop-record takes and comping~~ — shipped: recording with the loop armed
   splits the file into one take per pass (pure tick math after the fact, see
   src/app/TakeSplitter.h); the latest pass lands unmuted, earlier passes
   muted on the tracks below, and audio clips grew a right-click Mute/Unmute
   menu. A partial last pass >= 0.05 s is kept as a shorter clip.
9. ~~Time-stretch modes~~ — shipped the pragmatic version: per-clip stretch
   mode (Smooth / Percussive / Formant preserved, on the CLIP tree, playlist
   right-click menu) and an opt-in per-clip "Follow tempo" flag —
   StretchFollower recomputes the ratio on tempo change (coalesced per
   message-loop tick) so tick lengths stay musically constant.
   TRUE realtime stretching in the audio callback stays open, and is a real
   project: RubberBandStretcher has an OptionProcessRealTime mode, but it
   allocates/locks internally unless carefully pre-primed, needs per-clip
   stretcher instances owned outside the snapshot, latency compensation
   (R3 realtime reports ~2k+ samples startDelay), and a feeder thread. The
   offline cache is the right call until clips get a dedicated worker-thread
   render pipeline.

## Big rocks (schedule deliberately)

- **Plugin sandboxing** — stages 1+2 shipped for effects: Options → "Sandbox
  Plugin Effects" (default off, persisted) loads new effect slots in an
  EurydiceHelper process each. Audio rides a shared-memory ring (one-block
  latency; the audio thread never blocks — a dead child degrades to silence);
  a crashed helper flags the slot "[crashed]", drops out of the chain, and
  the slot menu offers "Restart crashed plugin" from the last captured state.
  Param automation proxies through the ring's RT-safe event slots; state
  saves cross the process boundary; editors open in the helper's own window.
  Engine-level test: sandboxed AUDelay renders in the chain, SIGKILL, health
  check flags it, restart brings it back. Stage 3 shipped instruments too:
  MIDI rides per-slot event arrays in the ring, plugin channels load in a
  helper when the option is on (SandboxedGenerator), a dead instrument goes
  silent and is flagged (rack context menu / channel editor click offer the
  restart), and the whole loop — play, SIGKILL, silence, restart, play — is
  an engine-level test with a real instrument. Remaining polish: per-plugin
  (not global) opt-in, latency reporting for the one-block delay, sysex and
  >64-param automation for sandboxed plugins.
- **JUCE upgrade / input-device rework** — retire the CoreAudio combiner
  workaround (see engine debt below).
- **Time signatures beyond 4/4** — foundational, invasive, low value for the
  target genres.

## Engine / platform debt (carried)

- JUCE AudioIODeviceCombiner is unsafe on this machine: we start output-only
  and enable input when recording arms. Real fix is a newer JUCE or a second
  AudioDeviceManager for input. Disarming record deliberately leaves the
  input open.
- Per-note pan is stored but not applied at playback.
- The rack swing knob is not automatable (project-level property, no engine
  target kind).
- Recovery files are never garbage-collected.
