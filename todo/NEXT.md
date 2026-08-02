# What's next — round 3

Agreed 2026-08-02 after the UX/dynamic-mixer batch. Ordered; top section is
the active work.

## Now — finish the sound

1. **Retune compressor defaults.** A stock Clipper → EQ → Compressor → Reverb
   chain loses ~5x RMS versus Clipper alone (0.350 → 0.066); threshold/ratio
   clamp hard with no makeup. A fresh instance should be roughly unity on a
   busy bus. Verify with before/after RMS renders.
2. **One-click sidechain ducking.** Insert context menu: "Duck from <insert>"
   — drops a compressor in the first free slot, wires the sidechain source,
   sets pumping-friendly defaults (fast attack, ~100ms release, deep ratio).
   The defining genre move; today it takes four manual steps.
3. **Effect editors show what they do.** EQ/filter frequency-response curves
   and a compressor gain-reduction meter. Today they're knob grids and you
   mix blind.

## Next — the AI angle

4. **Let the AI hear.** `render.analyze` (or extend `render.export`): render a
   range offline and return RMS/peak/spectral summary per insert, so an AI
   can iterate on a mix instead of working deaf. `ui.snapshot`/`ui.showPanel`
   are the eyes; this is the ears.
5. **AI-recordable automation.** An explicit API to write automation points
   from live-style input (channel.set deliberately doesn't record while
   armed).

## Then — workflow depth

6. Channel drag-reorder, colours, grouping — the rack gets unwieldy past ~10
   channels.
7. Browser drag-and-drop onto rack and playlist (double-click works today).
8. Loop-record takes and comping.
9. Realtime time-stretch (Rubber Band is offline-only today).

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
