#!/usr/bin/env python3
"""Build a full schranz track in Eurydice: sounds, effects chains, arrangement.

Unlike make_genre_projects.py (bare drum patterns), this builds a finished
piece — hosted plugins for the sounds, an effect chain per bus, sidechain
ducking, automation, and an eight-section arrangement — then renders it.

Usage: python3 scripts/make_schranz_track.py [output-dir]
"""
import json
import os
import socket
import subprocess
import sys
import time
from pathlib import Path

PORT = int(os.environ.get("EURYDICE_CONTROL_PORT", "44899"))
STEP, BEAT, BAR = 240, 960, 3840
TEMPO = 148

REPO = Path(__file__).resolve().parent.parent
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path.home() / "Music/Eurydice Starters"


class Daw:
    def __init__(self, port):
        self.f = socket.create_connection(("127.0.0.1", port), timeout=180).makefile("rw")
        self.plugins = None

    def __call__(self, method, params=None):
        self.f.write(json.dumps({"jsonrpc": "2.0", "id": 1,
                                 "method": method, "params": params or {}}) + "\n")
        self.f.flush()
        r = json.loads(self.f.readline())
        if "error" in r:
            raise RuntimeError(f"{method}: {r['error']}")
        return r["result"]

    def plugin(self, name, fmt="VST3"):
        if self.plugins is None:
            self.plugins = self("plugins.list")
        for p in self.plugins:
            if p["name"] == name and p["format"] == fmt:
                return p["id"]
        return None


def grid(pattern, key=60, length=STEP, accent=1.0, ghost=0.45):
    """'x' hit, '-' ghost, '.' rest. One char per 16th."""
    out = []
    for i, c in enumerate(pattern.replace(" ", "")):
        if c in "x-":
            out.append({"key": key, "start": i * STEP, "length": length,
                        "velocity": round(accent if c == "x" else ghost, 3)})
    return out


def build(daw):
    daw("project.new")
    daw("transport.set", {"tempo": TEMPO})

    state = daw("state.get")
    ch = {c["name"]: c["id"] for c in state["channels"]}
    main_pattern = state["activePatternId"]
    daw("pattern.select", {"patternId": main_pattern})

    # ---- inserts: one per element so each gets its own processing ----
    buses = {"Kick": 1, "Rumble": 2, "Hats": 3, "Clap": 4, "Stab": 5, "FX": 6}
    for name, index in buses.items():
        daw("mixer.setInsert", {"insert": index, "name": name, "volume": 0.8})

    # ---- sounds ----
    # ChowKick for the body; the built-in sampler channels cover percussion.
    kick_plugin = daw.plugin("ChowKick")
    if kick_plugin:
        kick_id = daw("channel.add", {"type": "plugin", "name": "Kick",
                                      "pluginId": kick_plugin})["id"]
        daw("channel.remove", {"channelId": ch["Kick"]})
    else:
        kick_id = ch["Kick"]
    daw("channel.set", {"channelId": kick_id, "insert": buses["Kick"], "volume": 0.85})

    # Surge XT twice: a sub rumble under the kick, and the schranz stab.
    surge = daw.plugin("Surge XT")
    rumble_id = (daw("channel.add", {"type": "plugin", "name": "Rumble",
                                     "pluginId": surge})["id"] if surge
                 else daw("channel.add", {"type": "synth", "name": "Rumble"})["id"])
    stab_id = (daw("channel.add", {"type": "plugin", "name": "Stab",
                                   "pluginId": surge})["id"] if surge
               else daw("channel.add", {"type": "synth", "name": "Stab"})["id"])
    daw("channel.set", {"channelId": rumble_id, "insert": buses["Rumble"], "volume": 0.55})
    daw("channel.set", {"channelId": stab_id, "insert": buses["Stab"], "volume": 0.5})

    daw("channel.set", {"channelId": ch["Hat"], "insert": buses["Hats"], "volume": 0.6})
    daw("channel.set", {"channelId": ch["Clap"], "insert": buses["Clap"], "volume": 0.7})
    daw("channel.set", {"channelId": ch["Snare"], "insert": buses["Hats"], "volume": 0.5})

    # ---- effect chains ----
    missing = []

    def fx(insert, slot, plugin_name):
        pid = daw.plugin(plugin_name)
        if pid is None:
            # Silently skipping produced empty chains once already; be loud.
            missing.append(plugin_name)
            return False
        daw("mixer.setEffect", {"insert": insert, "slot": slot, "pluginId": pid})
        return True

    # Kick: distortion then clipping then glue — the schranz signature.
    fx(buses["Kick"], 0, "BYOD")
    fx(buses["Kick"], 1, "peakeater")
    fx(buses["Kick"], 2, "TDR Nova")
    # Rumble: ducked under the kick, filtered.
    fx(buses["Rumble"], 0, "Pumpit")
    fx(buses["Rumble"], 1, "TDR Nova")
    # Hats: bright saturation and a touch of room.
    fx(buses["Hats"], 0, "Free Clip 2")
    fx(buses["Hats"], 1, "ValhallaSupermassive")
    # Clap: compression then reverb.
    fx(buses["Clap"], 0, "Free Comp")
    fx(buses["Clap"], 1, "ValhallaSupermassive")
    # Stab: OTT then the frequency-shifting echo for the metallic tail.
    fx(buses["Stab"], 0, "OTT")
    fx(buses["Stab"], 1, "ValhallaFreqEcho")
    # Master: glue, then limit.
    fx(0, 0, "TDR Nova")
    fx(0, 1, "LoudMax")

    # ---- patterns ----
    # A: the loop.  B: stripped back.  C: fill.
    daw("notes.set", {"patternId": main_pattern, "channelId": kick_id,
                      "notes": grid("x...x...x...x...")})
    daw("notes.set", {"patternId": main_pattern, "channelId": ch["Hat"],
                      "notes": grid("..x-..x-..x-..x-", length=120)})
    daw("notes.set", {"patternId": main_pattern, "channelId": ch["Clap"],
                      "notes": grid("....x.......x...")})
    daw("notes.set", {"patternId": main_pattern, "channelId": ch["Snare"],
                      "notes": grid("...-...-...-..-x")})
    # Rumble follows the kick an octave down, sustained between hits.
    daw("notes.set", {"patternId": main_pattern, "channelId": rumble_id,
                      "notes": grid("x...x...x...x...", key=24, length=BEAT)})
    # Offbeat stab.
    daw("notes.set", {"patternId": main_pattern, "channelId": stab_id,
                      "notes": grid("..x...x...x...x.", key=51, length=STEP)})

    intro = daw("pattern.create", {"name": "Intro", "lengthTicks": BAR})["id"]
    daw("notes.set", {"patternId": intro, "channelId": kick_id,
                      "notes": grid("x...x...x...x...")})
    daw("notes.set", {"patternId": intro, "channelId": ch["Hat"],
                      "notes": grid("..x...x...x...x.", length=120)})

    fill = daw("pattern.create", {"name": "Fill", "lengthTicks": BAR})["id"]
    daw("notes.set", {"patternId": fill, "channelId": kick_id,
                      "notes": grid("x...x...x...x.xx")})
    daw("notes.set", {"patternId": fill, "channelId": ch["Snare"],
                      "notes": grid("x-x-x-x-xxxxxxxx", length=120)})
    daw("notes.set", {"patternId": fill, "channelId": ch["Clap"],
                      "notes": grid("....x.......x.x.")})

    # ---- arrangement: 32 bars ----
    daw("playlist.clear")
    layout = ([(intro, b) for b in range(0, 4)]
              + [(main_pattern, b) for b in range(4, 11)] + [(fill, 11)]
              + [(main_pattern, b) for b in range(12, 19)] + [(fill, 19)]
              + [(main_pattern, b) for b in range(20, 27)] + [(fill, 27)]
              + [(intro, b) for b in range(28, 32)])
    for pattern_id, bar in layout:
        daw("playlist.addClip", {"track": 0, "patternId": pattern_id, "start": bar * BAR})

    # ---- automation: filter the stab open across the track, duck the intro ----
    stab_auto = daw("automation.create", {
        "targetType": "channel", "targetId": stab_id, "paramId": "volume",
        "name": "Stab rise", "initialValue": 0.0})["id"]
    daw("automation.setPoints", {"automationId": stab_auto, "points": [
        {"pos": 0, "value": 0.0},
        {"pos": 4 * BAR, "value": 0.0},
        {"pos": 12 * BAR, "value": 0.5, "tension": 0.3},
        {"pos": 27 * BAR, "value": 0.5},
        {"pos": 32 * BAR, "value": 0.0}]})

    # Insert-volume automation is normalised so 1.0 == 1.25x gain; a value of
    # 0.44 is the 0.55 gain the master sits at, not 0.55.
    def insert_gain(gain):
        return round(gain / 1.25, 4)

    master_auto = daw("automation.create", {
        "targetType": "insert", "targetId": 0, "paramId": "volume",
        "name": "Master fade", "initialValue": insert_gain(0.55)})["id"]
    daw("automation.setPoints", {"automationId": master_auto, "points": [
        {"pos": 0, "value": insert_gain(0.30)},
        {"pos": 4 * BAR, "value": insert_gain(0.55)},
        {"pos": 28 * BAR, "value": insert_gain(0.55)},
        {"pos": 31 * BAR, "value": insert_gain(0.28)},
        {"pos": 32 * BAR, "value": insert_gain(0.05)}]})

    # Automation clips are created four bars long; stretch them over the whole
    # arrangement, otherwise the curves only apply to the first four bars.
    for track_index, track in enumerate(daw("playlist.get")):
        for clip_index, clip in enumerate(track["clips"]):
            if clip["type"] == "automation":
                daw("playlist.setClip", {"track": track_index, "index": clip_index,
                                         "start": 0, "length": 32 * BAR})

    daw("mixer.setInsert", {"insert": 0, "volume": 0.55})
    daw("transport.set", {"songMode": True, "loopStart": 4 * BAR,
                          "loopEnd": 12 * BAR, "loopEnabled": True})
    return {"bars": 32, "missing": missing}


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    binary = next((p for p in (REPO / "build").rglob("Eurydice")
                   if p.parent.name == "MacOS"), None)
    if binary is None:
        sys.exit("Eurydice binary not found — build first")

    app = subprocess.Popen([str(binary)],
                           env=dict(os.environ, EURYDICE_CONTROL_PORT=str(PORT)),
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.time() + 40
    while time.time() < deadline:
        if app.poll() is not None:
            sys.exit(f"app exited during startup (rc={app.returncode})")
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
        info = build(daw)
        # Hosted plugins instantiate asynchronously; let them settle before render.
        time.sleep(12)
        project = OUT / "Schranz Full.eury"
        daw("project.save", {"path": str(project)})

        # The engine honours the loop range during offline render, so a song
        # render would wrap at the loop end and never reach the outro. Save the
        # project with the loop armed for working, then disarm it to render.
        daw("transport.set", {"loopEnabled": False})
        wav = OUT / "Schranz Full.wav"
        result = daw("render.export", {"path": str(wav), "tailSeconds": 2.0})
        print(f"  {info['bars']} bars @ {TEMPO} BPM")
        if info["missing"]:
            print(f"  WARNING missing plugins (chain incomplete): {', '.join(info['missing'])}")
        print(f"  project: {project}")
        for f in result["files"]:
            print(f"  render:  {f}")
    finally:
        app.terminate()
    return 0


if __name__ == "__main__":
    sys.exit(main())
