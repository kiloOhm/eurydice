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

6. Channel drag-reorder, colours, grouping — the rack gets unwieldy past ~10
   channels.
7. Browser drag-and-drop onto rack and playlist (double-click works today).
8. Loop-record takes and comping.
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

- **Plugin sandboxing** — out-of-process hosting so a crashing VST can't take
  the DAW down. Largest reliability win, largest change.
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
