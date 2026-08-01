#!/usr/bin/env python3
"""End-to-end test: drives a real Eurydice instance through the real MCP server.

Launches the app (headless-friendly), spawns mcp/index.mjs over stdio, then
walks the full workflow an AI client would: inspect state, build a beat,
arrange it, automate it, play it (checking meters), and render it — asserting
at each step. Exits nonzero on any failure.

Usage: python3 scripts/e2e_mcp.py [path/to/Eurydice-binary]
"""
import json
import math
import os
import subprocess
import sys
import time
import wave

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT = "44899"  # dedicated port so a user session isn't disturbed


def find_app_binary():
    if len(sys.argv) > 1:
        return sys.argv[1]
    for root, _dirs, files in os.walk(os.path.join(REPO, "build")):
        if "Eurydice" in files and root.endswith("MacOS"):
            return os.path.join(root, "Eurydice")
    sys.exit("Eurydice binary not found — build first (cmake --build build)")


class McpClient:
    def __init__(self):
        env = dict(os.environ, EURYDICE_CONTROL_PORT=PORT)
        self.proc = subprocess.Popen(
            ["node", os.path.join(REPO, "mcp", "index.mjs")],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, env=env)
        self._id = 0
        self._send({"jsonrpc": "2.0", "id": self._next_id(), "method": "initialize",
                    "params": {"protocolVersion": "2024-11-05", "capabilities": {},
                               "clientInfo": {"name": "e2e", "version": "0"}}})
        self._read()
        self._send({"jsonrpc": "2.0", "method": "notifications/initialized"})

    def _next_id(self):
        self._id += 1
        return self._id

    def _send(self, obj):
        self.proc.stdin.write(json.dumps(obj) + "\n")
        self.proc.stdin.flush()

    def _read(self):
        line = self.proc.stdout.readline()
        if not line:
            sys.exit("MCP server died")
        return json.loads(line)

    def list_tools(self):
        self._send({"jsonrpc": "2.0", "id": self._next_id(), "method": "tools/list"})
        return self._read()["result"]["tools"]

    def call(self, name, arguments=None):
        self._send({"jsonrpc": "2.0", "id": self._next_id(), "method": "tools/call",
                    "params": {"name": name, "arguments": arguments or {}}})
        result = self._read()["result"]
        text = result["content"][0]["text"]
        if result.get("isError"):
            raise RuntimeError(f"{name}: {text}")
        try:
            return json.loads(text)
        except json.JSONDecodeError:
            return text

    def close(self):
        self.proc.terminate()


def check(condition, message):
    status = "ok" if condition else "FAIL"
    print(f"  [{status}] {message}")
    if not condition:
        raise AssertionError(message)


def wav_peak(path, start_sec=0.0, end_sec=None):
    with wave.open(path) as w:
        sr, n, sw = w.getframerate(), w.getnframes(), w.getsampwidth()
        a = int(start_sec * sr)
        b = n if end_sec is None else min(n, int(end_sec * sr))
        w.setpos(a)
        frames = w.readframes(b - a)
        peak = 0
        for i in range(0, len(frames) - sw, sw * 50):
            peak = max(peak, abs(int.from_bytes(frames[i:i + sw], "little", signed=True)))
        return peak / (2 ** (sw * 8 - 1))


def main():
    binary = find_app_binary()
    print(f"launching {binary} (control port {PORT})")
    app = subprocess.Popen([binary], env=dict(os.environ, EURYDICE_CONTROL_PORT=PORT),
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(3)
    mcp = McpClient()
    render_path = "/tmp/eurydice-e2e.wav"

    try:
        print("== tool discovery ==")
        tools = mcp.list_tools()
        check(len(tools) >= 20, f"{len(tools)} tools exposed")

        print("== state ==")
        mcp.call("daw_project_new")
        state = mcp.call("daw_state")
        check(state["tempo"] == 140, "default tempo 140")
        check(len(state["channels"]) == 4, "4 default channels")
        channels = {c["name"]: c["id"] for c in state["channels"]}
        pattern = state["activePatternId"]

        print("== build a beat ==")
        mcp.call("daw_notes_set", {"channelId": channels["Kick"],
                 "notes": [{"key": 60, "start": i * 240, "length": 240} for i in range(0, 16, 4)]})
        mcp.call("daw_notes_set", {"channelId": channels["Hat"],
                 "notes": [{"key": 60, "start": i * 240, "length": 120,
                            "velocity": 0.9 if i % 4 == 2 else 0.5} for i in range(16)]})
        notes = mcp.call("daw_notes_get", {"channelId": channels["Hat"]})
        check(len(notes) == 16, "16 hat notes written and read back")

        print("== arrange + automate ==")
        mcp.call("daw_playlist_clear")
        for bar in range(4):
            mcp.call("daw_playlist_add_clip",
                     {"track": 0, "patternId": pattern, "start": bar * 3840})
        playlist = mcp.call("daw_playlist_get")
        check(len(playlist[0]["clips"]) == 4, "4 pattern clips placed")

        auto = mcp.call("daw_rpc", {"method": "automation.create", "params": {
            "targetType": "channel", "targetId": channels["Kick"],
            "paramId": "volume", "name": "e2e fade", "initialValue": 1.0}})
        mcp.call("daw_rpc", {"method": "automation.setPoints", "params": {
            "automationId": auto["id"],
            "points": [{"pos": 0, "value": 1.0}, {"pos": 15360, "value": 0.05}]}})

        print("== play + meters ==")
        mcp.call("daw_transport", {"action": "set", "songMode": True, "tempo": 150})
        mcp.call("daw_transport", {"action": "play"})
        time.sleep(1.5)
        meters = mcp.call("daw_meters")
        master = meters["inserts"][0]
        mcp.call("daw_transport", {"action": "stop"})
        check(master[0] > 0.001, f"master meter alive while playing ({master[0]:.3f})")

        print("== mixer routing ==")
        mcp.call("daw_channel_set", {"channelId": channels["Hat"], "insert": 1})
        mcp.call("daw_mixer_set_insert", {"insert": 1, "name": "Hats", "volume": 0.9})
        mixer = mcp.call("daw_mixer_get")
        check(mixer[1]["name"] == "Hats", "insert rename via MCP")

        print("== render ==")
        if os.path.exists(render_path):
            os.remove(render_path)
        result = mcp.call("daw_rpc", {"method": "render.export", "params": {
            "path": render_path, "tailSeconds": 0.5}})
        check(os.path.exists(render_path), "render file written")
        # 4 bars @150bpm = 6.4 s + 0.5 tail
        with wave.open(render_path) as w:
            duration = w.getnframes() / w.getframerate()
        check(math.isclose(duration, 6.9, abs_tol=0.1), f"render duration {duration:.2f}s ≈ 6.9s")
        check(wav_peak(render_path, 0, 1.5) > 0.05, "audio at the start")
        loud, quiet = wav_peak(render_path, 0.0, 1.0), wav_peak(render_path, 5.4, 6.4)
        check(quiet < loud * 0.5, f"automation fade audible ({loud:.2f} -> {quiet:.2f})")

        print("== project round-trip ==")
        mcp.call("daw_project_save", {"path": "/tmp/eurydice-e2e.eury"})
        mcp.call("daw_project_new")
        mcp.call("daw_project_load", {"path": "/tmp/eurydice-e2e.eury"})
        state = mcp.call("daw_state")
        check(state["tempo"] == 150, "tempo survived save/load")

        print("\nE2E PASSED")
        return 0
    except (AssertionError, RuntimeError) as e:
        print(f"\nE2E FAILED: {e}")
        return 1
    finally:
        mcp.close()
        app.terminate()


if __name__ == "__main__":
    sys.exit(main())
