#!/usr/bin/env python3
"""Generate starter projects with genre-typical beats.

Tempo in Eurydice is a project-level setting, and these genres live at very
different tempos, so this writes one project per genre rather than stuffing
every pattern into a single file at a compromise BPM.

Usage: python3 scripts/make_genre_projects.py [output-dir]
Requires a running Eurydice (the script drives its JSON-RPC control socket).
"""
import json
import os
import socket
import subprocess
import sys
import time
from pathlib import Path

PORT = int(os.environ.get("EURYDICE_CONTROL_PORT", "44899"))
STEP = 240          # one 16th note
BAR = 3840          # 4/4 bar

REPO = Path(__file__).resolve().parent.parent
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path.home() / "Music/Eurydice Starters"


class Daw:
    def __init__(self, port):
        self.f = socket.create_connection(("127.0.0.1", port), timeout=120).makefile("rw")

    def __call__(self, method, params=None):
        self.f.write(json.dumps({"jsonrpc": "2.0", "id": 1,
                                 "method": method, "params": params or {}}) + "\n")
        self.f.flush()
        r = json.loads(self.f.readline())
        if "error" in r:
            raise RuntimeError(f"{method}: {r['error']}")
        return r["result"]


def steps(pattern: str):
    """'x...x...' -> tick offsets. 'x' hit, '-' ghost (quieter), '.' rest."""
    out = []
    for i, c in enumerate(pattern.replace(" ", "")):
        if c == "x":
            out.append((i * STEP, 1.0))
        elif c == "-":
            out.append((i * STEP, 0.45))
    return out


def notes(pattern: str, key=60, length=STEP):
    return [{"key": key, "start": t, "length": length, "velocity": round(0.55 + 0.45 * v, 3)}
            for t, v in steps(pattern)]


# Each genre: tempo, pattern length in bars, and per-channel step patterns.
# 16 characters = one bar of 16ths.
GENRES = {
    "Schranz": dict(
        tempo=148, bars=1, note="Distorted straight kick, offbeat hats, rolling toms. "
                                "Put a clipper + OTT on the kick bus.",
        parts={
            "Kick":  "x...x...x...x...",
            "Hat":   "..x...x...x...x.",
            "Clap":  "....x.......x...",
            "Snare": "...-...-...-..-x",
        }),
    "Tekk": dict(
        tempo=155, bars=1, note="Hard kick with an offbeat bass stab. Sidechain the bass "
                                "from the kick with Pumpit.",
        parts={
            "Kick":  "x...x...x...x...",
            "Clap":  "....x.......x...",
            "Hat":   "..x...x...x...x.",
            "Snare": "...............x",
        }),
    "Hardtekk": dict(
        tempo=160, bars=1, note="Tekk backbone with room for a melodic screech lead "
                                "(Surge XT) over the top.",
        parts={
            "Kick":  "x...x...x...x...",
            "Clap":  "....x.......x...",
            "Hat":   "..x-..x-..x-..x-",
            "Snare": ".............x.x",
        }),
    "DnB": dict(
        tempo=174, bars=1, note="Two-step amen skeleton: kick on 1 and the 'and' of 3, "
                                "snare on 2 and 4. Chop a break in TX16Wx over it.",
        parts={
            "Kick":  "x........x......",
            "Snare": "....x.......x...",
            "Hat":   "..x...x...x...x.",
            "Clap":  "..............-.",
        }),
    "Uptempo": dict(
        tempo=200, bars=1, note="Relentless kick with a fill on the last beat. Kick wants "
                                "heavy distortion (BYOD) then a clipper.",
        parts={
            "Kick":  "x.x.x.x.x.x.xxxx",
            "Clap":  "....x.......x...",
            "Hat":   ".x.x.x.x.x.x.x.x",
            "Snare": "...............x",
        }),
    "Hardcore": dict(
        tempo=175, bars=1, note="Gabber four-to-the-floor with offbeat kick accents. "
                                "Distort the kick until it is the bassline.",
        parts={
            "Kick":  "x...x...x...x...",
            "Clap":  "....x.......x...",
            "Hat":   "..x...x...x...x.",
            "Snare": "..........x....x",
        }),
    "Frenchcore": dict(
        tempo=210, bars=1, note="Rolling kick on every 8th with 16th rolls. The kick is "
                                "the track: pitch-envelope it and layer.",
        parts={
            "Kick":  "x.x.x.x.x.x.x.xx",
            "Clap":  "....x.......x...",
            "Hat":   "..x...x...x...x.",
            "Snare": "..............x.",
        }),
}


def build(daw, name, spec, out_dir):
    daw("project.new")
    daw("transport.set", {"tempo": spec["tempo"]})

    channels = {c["name"]: c["id"] for c in daw("state.get")["channels"]}
    pattern_id = daw("state.get")["activePatternId"]

    # Rename the starting pattern after the genre, and give every drum its own
    # mixer insert so there is somewhere to hang distortion per element.
    for insert, (part, grid) in enumerate(spec["parts"].items(), start=1):
        channel_id = channels.get(part)
        if channel_id is None:
            continue
        daw("notes.set", {"patternId": pattern_id, "channelId": channel_id,
                          "notes": notes(grid)})
        daw("channel.set", {"channelId": channel_id, "insert": insert})
        daw("mixer.setInsert", {"insert": insert, "name": part})

    # Dense kick patterns at these tempos overlap heavily and hit full scale,
    # so leave headroom rather than shipping a project that clips on load.
    daw("mixer.setInsert", {"insert": 0, "volume": 0.55})

    # Arrange 8 bars so there is something to play in song mode, and loop it.
    daw("playlist.clear")
    for bar in range(8):
        daw("playlist.addClip", {"track": 0, "patternId": pattern_id, "start": bar * BAR})
    daw("transport.set", {"songMode": True, "loopStart": 0,
                          "loopEnd": 4 * BAR, "loopEnabled": True})

    path = out_dir / f"{name}.eury"
    daw("project.save", {"path": str(path)})
    return path


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    binary = next((p for p in (REPO / "build").rglob("Eurydice")
                   if p.parent.name == "MacOS"), None)
    if binary is None:
        sys.exit("Eurydice binary not found — build first")

    app = subprocess.Popen([str(binary)],
                           env=dict(os.environ, EURYDICE_CONTROL_PORT=str(PORT)),
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # A freshly built binary can take seconds to clear Gatekeeper, and the app
    # is single-instance, so report a startup exit rather than spinning.
    deadline = time.time() + 40
    while time.time() < deadline:
        if app.poll() is not None:
            sys.exit(f"app exited during startup (rc={app.returncode}) — "
                     "is another Eurydice instance already running?")
        try:
            socket.create_connection(("127.0.0.1", PORT), timeout=0.5).close()
            break
        except OSError:
            time.sleep(0.4)
    else:
        app.terminate()
        sys.exit("app never opened its control port")

    try:
        daw = Daw(PORT)
        for name, spec in GENRES.items():
            path = build(daw, name, spec, OUT)
            print(f"  {name:<12} {spec['tempo']:>3} BPM  ->  {path.name}")
            print(f"               {spec['note']}")
    finally:
        app.terminate()

    print(f"\nWritten to {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
