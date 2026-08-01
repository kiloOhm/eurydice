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
| Channel rack | Step sequencer with 4-step group tinting, swing, per-channel pan/volume, insert routing |
| Piano roll | Draw/paint/move/resize, right-click delete, marquee select, velocity lane, ghost notes from other channels, chord stamp, scale highlighting |
| Playlist | Free tracks; pattern, audio and automation clips; drag/resize/cross-track move; alt-resize time-stretches audio |
| Mixer | 32 inserts + master, 10 effect slots each, insert→insert sends (buses and sidechain), live peak meters |
| Generators | Sampler channel (drop a WAV in, ADSR + lowpass, one-shot or sustained) and a built-in 2-osc subtractive synth, both with editor windows |
| Plugins | VST3 + AU hosting: background scan, instrument channels, effect slots, native editor windows, state saved in the project |
| Automation | Right-click any parameter (including plugin params) → automation clip with a tension-curve editor |
| Audio | Input recording to audio clips; Rubber Band R3 offline time-stretch |
| MIDI | CoreMIDI input with hot-plug, note recording, FL-style typing keyboard |
| Export | WAV (16/24-bit), MP3 via LAME, per-insert stems |

### Getting around

Everything is reachable three ways: the **menu bar** (File / Edit / View / Options),
the **panel buttons** in the transport bar, and keyboard shortcuts.

`⌘1` playlist · `⌘2` channel rack · `⌘3` piano roll · `⌘4` mixer · `⌘B` browser.
The FL-style `F5`/`F6`/`F7`/`F9` bindings also work, but macOS claims those keys
for brightness and media by default — either use `Fn`+the key, or turn on
*Use F1, F2, etc. as standard function keys* in System Settings → Keyboard.

`Space` play/stop · `Home` rewind · `⌘L` song mode · `⌘E` arm recording ·
`⌘N`/`⌘O`/`⌘S`/`⇧⌘S` project · `⌘R` export · `⌘Z`/`⇧⌘Z` undo/redo ·
`⌘,` audio settings · `Z`–`M` and `Q`–`P` rows play notes · `,`/`.` shift octave.

Drag a panel near an edge to dock it to that half of the desktop, or into a
corner for a quarter — a preview shows the target region before you release,
and a panel already occupying that region is pushed into the complementary one
so the two tile instead of overlapping. Away from the edges, panels snap
magnetically to each other. Hold shift while dragging to place freely.

Right-click a panel button to reset that panel's position (or all of them);
*View → Reset Panel Positions* does the same. Panels also can't be dragged
fully off-screen, and double-clicking a title bar maximises it.

Click a channel name to open its editor (sampler: sample slot, root note, ADSR,
filter, one-shot; synth: oscillators, filter, envelope, keyboard). Right-click a
channel for piano roll, routing, and automation. Double-click a pattern clip in
the playlist to edit that pattern.

## AI control (MCP)

The app hosts a JSON-RPC 2.0 server on `127.0.0.1:44890` (override with
`EURYDICE_CONTROL_PORT`). `mcp/index.mjs` bridges it to any MCP client as 24
typed tools — transport, channels, notes, patterns, playlist, mixer, plugins,
automation, meters, render, project I/O.

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

81 unit tests over the model, snapshot builder, sequencer timing (sample-accurate
onsets, swing, song mode), automation curves, generators, renderer, control
dispatcher, socket framing, channel parameters, project dirty-tracking, and
live AU hosting. They never open an audio device, so they run anywhere in under
a second.

```bash
scripts/coverage.sh 80     # llvm-cov with an enforced line-coverage gate
scripts/e2e_mcp.py         # full workflow through the real MCP bridge
scripts/static-analysis.sh # clang-tidy + cppcheck
```

Coverage of the non-UI core currently sits at **~85%** lines (gate: 80%).
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

## Plugins

Eurydice ships no stock effects yet, so start with free VSTs:
[docs/free-plugins.md](docs/free-plugins.md) lists verified free, Apple-Silicon
native VST3/AU plugins organised by the FL Studio stock plugin they replace,
aimed at techno and adjacent hard genres.

## Licence

GPLv3 — required by the VST3 SDK and Rubber Band, both used under their GPL terms.
