# Eurydice

An FL Studio–style DAW for Apple Silicon Macs. C++20 + JUCE 8, hosting VST3 and
Audio Unit plugins, with a control API that lets an AI drive the whole thing
over MCP.

Original visual identity; the *workflow* is modelled on FL Studio (channel rack
and step sequencer, pattern-based piano roll, free-form playlist, insert mixer
with arbitrary send routing).

## Build

```bash
cmake -B build -G Ninja && cmake --build build
```

Dependencies (JUCE, Rubber Band, GoogleTest) are fetched automatically.
Prerequisites: Xcode command line tools, `cmake`, `ninja`.
Optional: `brew install lame` for MP3 export.

Run: `open build/Eurydice_artefacts/RelWithDebInfo/Eurydice.app`

Builds default to `RelWithDebInfo` — an unoptimised audio engine drops buffers.

## What's in it

| Area | Notes |
|---|---|
| Channel rack | Step sequencer with 4-step group tinting, per-pattern swing, per-channel pan/volume, insert routing |
| Piano roll | Draw/paint/move/resize, right-click delete, Draw/Select tools with lasso selection, copy/cut/paste/duplicate across channels and patterns, velocity lane, ghost notes from other channels, chord stamp, scale highlighting |
| Playlist | Free tracks; pattern, audio and automation clips; drag/resize/cross-track move; alt-resize time-stretches audio |
| Mixer | Master + as many inserts as you add (`+` in the mixer or the routing menus), 10 effect slots each, insert→insert sends (buses and sidechain), live peak meters, per-insert renaming |
| Effects | Six built-ins: oversampled clipper, filter with envelope follower + synced LFO, 4-band EQ, compressor with external sidechain, tempo-synced delay, reverb |
| Generators | Sampler channel (drop a WAV in, ADSR + lowpass, one-shot or sustained, start/end trim, reverse, pitch envelope, drive), a built-in 2-osc subtractive synth, and a kick designer — all with editor windows |
| Synth presets | 35 factory patches across seven genres (Schranz, Drum & Bass, Techno, Trance, House, Hardstyle, Dubstep) plus an init patch, browsed by category in the synth editor. Save your own with `Save...`; they land in `~/Music/Eurydice/Presets/Synth` as one small XML file each and join the browser under *User* |
| Kick designer | Four layers (swept body with harmonics, tuned sub, click as tick/noise/pulse/your own WAV, filtered noise), draggable multi-point pitch and amp envelopes, and a built-in output chain: drive, three bands of EQ, compressor, limiter. 40 factory presets across nine genres, a live render of the hit with waveform, spectrum, peak and detected tuning, and drag-out or Export to WAV |
| Plugins | VST3 + AU hosting: background scan, instrument channels, effect slots, native editor windows, state saved in the project; optional per-process sandboxing (Options menu) for effects and instruments, so a crashing plugin costs one slot's sound and a one-click restart, not the DAW |
| Automation | Right-click any knob or fader → automation clip with a tension-curve editor; arm `AUTO` (playlist header) to record moves live |
| Transport | Loop range (drag the playlist ruler, `LOOP` in the playlist header), metronome with count-in, per-pattern swing |
| Samples | Browser previews on click, double-click sends to the rack; a royalty-free SampleRadar drum library (934 hits) lives in `~/Music/Samples` |
| Audio | Input recording to audio clips; Rubber Band R3 offline time-stretch |
| MIDI | CoreMIDI input with hot-plug, note recording, FL-style typing keyboard |
| Export | Dialog with bit depth, sample rate, loop-range-only, MP3 via LAME, per-insert or per-channel stems, peak normalisation |
| Metronome | Synthesised click accented on the bar, level control, optional one- or two-bar count-in before armed recording |

### Getting around

Everything is reachable three ways: the **menu bar** (File / Edit / View / Options / Help),
the **panel buttons** in the transport bar, and keyboard shortcuts.
*Help → Keyboard Shortcuts* (`⌘/`) lists all of them — including the mouse
gestures below — in-app.

`⌘1` playlist · `⌘2` channel rack · `⌘3` piano roll · `⌘4` mixer · `⌘B` browser.
The FL-style `F5`/`F6`/`F7`/`F9` bindings also work, but macOS claims those keys
for brightness and media by default — either use `Fn`+the key, or turn on
*Use F1, F2, etc. as standard function keys* in System Settings → Keyboard.

`Space` play/stop · `Home` rewind · `⌘L` song mode · `⌘E` arm recording ·
`⌘N`/`⌘O`/`⌘S`/`⇧⌘S` project · `⌘R` export · `⌘Z`/`⇧⌘Z` undo/redo ·
`⇧⌘L` loop · `⇧⌘M` metronome · `⌘,` audio settings ·
`Z`–`M` and `Q`–`P` rows play notes (QWERTZ layouts detected and mapped by physical key) · `,`/`.` shift octave.

Drag a panel near an edge to dock it to that half of the desktop, or into a
corner for a quarter — a preview shows the target region before you release,
and a panel already occupying that region is pushed into the complementary one
so the two tile instead of overlapping. Away from the edges, panels snap
magnetically to each other. Hold shift while dragging to place freely.

Right-click a panel button to reset that panel's position (or all of them);
*View → Reset Panel Positions* does the same. Panels also can't be dragged
fully off-screen, and double-clicking a title bar maximises it.

Click a channel name to open its editor (sampler: sample slot, trim, reverse,
pitch envelope, ADSR, filter, drive; synth: preset browser, oscillators,
filter, envelopes, LFO, keyboard; kick: presets, envelope graph, live render,
the four layers and the output chain, keyboard). Right-click a
channel for piano roll, routing, and automation. Double-click a pattern clip in
the playlist to edit that pattern.

## AI control (MCP)

The app hosts a JSON-RPC 2.0 server on `127.0.0.1:44890` (override with
`EURYDICE_CONTROL_PORT`). `mcp/index.mjs` bridges it to any MCP client as 43
typed tools — transport, channels, notes, patterns, playlist, mixer, plugins,
automation, meters, render, kick and synth presets, project I/O.

```bash
cd mcp && npm install
claude mcp add eurydice -- node "$PWD/index.mjs"
```

Then, with Eurydice running: *"make me a four-on-the-floor at 128, put a reverb
on the drum bus, and render it."*

Time is in ticks: 960 per quarter note, 240 per 16th step, 3840 per bar.

## Testing

```bash
cmake --build build --target EurydiceTests
./build/EurydiceTests_artefacts/RelWithDebInfo/EurydiceTests
```

348 unit tests over the model, snapshot builder, sequencer timing
(sample-accurate onsets, swing, song mode), automation curves and recording,
generators, built-in effects, renderer, export, undo gestures, autosave,
control dispatcher, socket framing, channel parameters, docking/snapping,
typing piano, project dirty-tracking, and live AU hosting (including the
plugin editor shell). They never open an audio device, so they run anywhere
in about half a second.

```bash
scripts/coverage.sh 80     # llvm-cov with an enforced line-coverage gate
scripts/e2e_mcp.py         # full workflow through the real MCP bridge
scripts/static-analysis.sh # clang-tidy + cppcheck
```

Coverage of the non-UI core currently sits at **~86%** lines (gate: 80%).
The e2e script launches the app, drives it through MCP, and asserts on the
rendered audio.

Static analysis is clean: zero clang-tidy and cppcheck findings in `src/`.
SonarQube's C++ analyzer is a commercial feature, so clang-tidy + cppcheck are
the real gate here; `sonar-project.properties` documents how to plug the reports
into a Sonar server if you want the dashboard.

## Layout

```
src/model      ValueTree project model + undo
src/engine     RT audio engine, snapshot builder, generators, renderer
src/plugins    VST3/AU scanning, hosting, editor windows
src/control    JSON-RPC dispatcher + socket server
src/ui         Panels (rack, piano roll, playlist, mixer, browser, automation)
mcp/           Node stdio MCP server
tests/         GoogleTest suite
todo/          Per-feature status notes
```

## Starter projects

```bash
python3 scripts/make_genre_projects.py
```

Writes one project per genre (schranz, tekk, hardtekk, DnB, uptempo, hardcore,
frenchcore) to `~/Music/Eurydice Starters`, each at its own tempo with a
genre-typical drum pattern, per-element mixer routing, an eight-bar arrangement
and a four-bar loop. Separate files rather than one project because tempo is a
project-level setting and these genres sit 60 BPM apart.

## Plugins

Six stock effects ship built in (see the table above), so projects open on any
machine. For synths and heavier processing, start with free VSTs:
[docs/free-plugins.md](docs/free-plugins.md) lists verified free, Apple-Silicon
native VST3/AU plugins organised by the FL Studio stock plugin they replace,
aimed at techno and adjacent hard genres. Instrument plugins open inside a
JUCE shell — title strip with a toggleable piano for plugins without their
own keyboard.

## Licence

GPLv3 — required by the VST3 SDK and Rubber Band, both used under their GPL terms.
