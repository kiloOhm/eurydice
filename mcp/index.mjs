#!/usr/bin/env node
// MCP stdio server for the Eurydice DAW.
// Bridges MCP tool calls to Eurydice's newline-delimited JSON-RPC control
// socket (127.0.0.1:44890 by default; EURYDICE_CONTROL_PORT overrides).
//
// Register in Claude Code:
//   claude mcp add eurydice -- node /path/to/eurydice/mcp/index.mjs

import net from "node:net";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";

const PORT = Number(process.env.EURYDICE_CONTROL_PORT ?? 44890);

// ---- tiny JSON-RPC client with one lazy connection ----
let socket = null;
let lineBuffer = "";
let pending = [];
let nextId = 1;

function connect() {
  return new Promise((resolve, reject) => {
    const s = net.createConnection({ host: "127.0.0.1", port: PORT }, () => resolve(s));
    s.on("error", reject);
    s.on("data", (chunk) => {
      lineBuffer += chunk.toString("utf8");
      let idx;
      while ((idx = lineBuffer.indexOf("\n")) >= 0) {
        const line = lineBuffer.slice(0, idx);
        lineBuffer = lineBuffer.slice(idx + 1);
        if (!line.trim()) continue;
        const resolver = pending.shift();
        if (resolver) resolver(line);
      }
    });
    s.on("close", () => { socket = null; pending.forEach((r) => r(null)); pending = []; });
  });
}

async function rpc(method, params = {}) {
  if (!socket) {
    try { socket = await connect(); }
    catch {
      throw new Error(
        `Cannot reach Eurydice on port ${PORT}. Is the Eurydice app running?`);
    }
  }
  const id = nextId++;
  const line = await new Promise((resolve, reject) => {
    pending.push(resolve);
    socket.write(JSON.stringify({ jsonrpc: "2.0", id, method, params }) + "\n", (err) => {
      if (err) reject(err);
    });
    setTimeout(() => resolve(null), 15000);
  });
  if (line === null) throw new Error("Eurydice connection lost or timed out");
  const response = JSON.parse(line);
  if (response.error) throw new Error(response.error.message ?? "RPC error");
  return response.result;
}

// ---- MCP server ----
const server = new McpServer({ name: "eurydice", version: "0.1.0" });

const asText = (result) => ({
  content: [{ type: "text", text: typeof result === "string" ? result : JSON.stringify(result, null, 1) }],
});

function tool(name, description, shape, method, transform = (a) => a) {
  server.tool(name, description, shape, async (args) => asText(await rpc(method, transform(args))));
}

const TIME_HELP = "Time is in ticks: 960/quarter note, 240/16th step, 3840/bar (4/4).";

tool("daw_state", "Get the full DAW state: tempo, swing, transport, channels (with ids), patterns, counts. Call this first.", {}, "state.get");

server.tool("daw_transport",
  "Control the transport: play, stop, seek, or set tempo/swing/songMode/loop range. " + TIME_HELP,
  {
    action: z.enum(["play", "stop", "seek", "set"]),
    ticks: z.number().optional().describe("seek target in ticks"),
    tempo: z.number().min(20).max(999).optional(),
    swing: z.number().min(0).max(1).optional(),
    songMode: z.boolean().optional().describe("true = play the playlist, false = loop the active pattern"),
    loopStart: z.number().min(0).optional().describe("loop range start in ticks"),
    loopEnd: z.number().min(0).optional().describe("loop range end in ticks; only a range longer than 0 loops"),
    loopEnabled: z.boolean().optional().describe("wrap back to loopStart when the transport reaches loopEnd"),
    automationWrite: z.boolean().optional().describe("arm automation write: while playing, every control the user moves is recorded into its automation clip"),
  },
  async ({ action, ...rest }) => {
    if (action === "play") return asText(await rpc("transport.play"));
    if (action === "stop") return asText(await rpc("transport.stop"));
    if (action === "seek") return asText(await rpc("transport.seek", { ticks: rest.ticks ?? 0 }));
    return asText(await rpc("transport.set", rest));
  });

tool("daw_channel_add",
  "Add a channel to the rack. type: 'sampler' (drum/one-shot, give samplePath for a wav), 'synth' (built-in subtractive synth), 'kick' (synthesised hardcore kick) or 'plugin' (needs pluginId from daw_plugins_list).",
  {
    type: z.enum(["sampler", "synth", "kick", "plugin"]),
    name: z.string(),
    pluginId: z.string().optional(),
    samplePath: z.string().optional(),
  }, "channel.add");

tool("daw_channel_set",
  "Update a channel: volume (0..1), pan (-1..1), mute, insert (mixer routing 0=master), name, rootNote, samplePath. "
  + "Sampler kick design: sampleStart/sampleEnd (0..1 trim), reverse, pitchEnvDepth (semitones) + pitchEnvDecay (s), "
  + "envShape (0 linear .. 1 exponential), drive (0..1) + driveCurve (0 soft, 1 hard, 2 fold). "
  + "Kick synth: kickStartFreq/kickEndFreq (Hz), kickPitchDecay/kickAmpDecay (s), kickBodyShape (0 sine .. 1 triangle), "
  + "kickClickLevel/kickClickDecay, kickNoiseLevel/kickNoiseDecay.",
  {
    channelId: z.number(),
    volume: z.number().optional(), pan: z.number().optional(), mute: z.boolean().optional(),
    insert: z.number().optional(), name: z.string().optional(),
    rootNote: z.number().optional(), samplePath: z.string().optional(),
    sampleStart: z.number().optional(), sampleEnd: z.number().optional(),
    reverse: z.boolean().optional(),
    pitchEnvDepth: z.number().optional(), pitchEnvDecay: z.number().optional(),
    envShape: z.number().optional(),
    drive: z.number().optional(), driveCurve: z.number().optional(),
    kickStartFreq: z.number().optional(), kickEndFreq: z.number().optional(),
    kickPitchDecay: z.number().optional(), kickAmpDecay: z.number().optional(),
    kickBodyShape: z.number().optional(),
    kickClickLevel: z.number().optional(), kickClickDecay: z.number().optional(),
    kickNoiseLevel: z.number().optional(), kickNoiseDecay: z.number().optional(),
  }, "channel.set");

tool("daw_channel_remove", "Delete a channel and its notes.", { channelId: z.number() }, "channel.remove");

tool("daw_pattern_create", "Create a pattern. " + TIME_HELP,
  { name: z.string().optional(), lengthTicks: z.number().optional() }, "pattern.create");

tool("daw_pattern_select", "Make a pattern the active one (shown in rack/piano roll, looped in pattern mode).",
  { patternId: z.number() }, "pattern.select");

tool("daw_pattern_clone",
  "Duplicate a pattern with all its notes, named '<name> (copy)' and placed right after the original. patternId defaults to the active pattern.",
  { patternId: z.number().optional() }, "pattern.clone");

tool("daw_pattern_remove",
  "Delete a pattern and every playlist clip that references it. Fails if it is the last remaining pattern. patternId defaults to the active pattern.",
  { patternId: z.number().optional() }, "pattern.remove");

tool("daw_pattern_set_swing",
  "Set swing (0..1) for one pattern, overriding the project swing. Omit `swing` to drop the override and follow the project again. patternId defaults to the active pattern.",
  { patternId: z.number().optional(), swing: z.number().min(0).max(1).optional() }, "pattern.setSwing");

tool("daw_notes_get", "List a channel's notes in a pattern. patternId defaults to the active pattern.",
  { patternId: z.number().optional(), channelId: z.number() }, "notes.get");

tool("daw_notes_set",
  "Replace a channel's notes in a pattern. Steps are notes: a 16th-step at index i = {start: i*240, length: 240}. key 60 = C4 (drums usually 60). velocity 0..1. " + TIME_HELP,
  {
    patternId: z.number().optional(),
    channelId: z.number(),
    notes: z.array(z.object({
      key: z.number(), start: z.number(), length: z.number(),
      velocity: z.number().optional(), pan: z.number().optional(),
    })),
  }, "notes.set");

tool("daw_playlist_get", "List playlist tracks and their clips.", {}, "playlist.get");

tool("daw_playlist_add_clip",
  "Place a pattern clip on a playlist track (song mode arrangement). length defaults to the pattern length; longer clips loop the pattern. " + TIME_HELP,
  {
    track: z.number(), patternId: z.number(),
    start: z.number(), length: z.number().optional(),
  }, "playlist.addClip");

tool("daw_playlist_set_clip",
  "Move, resize or mute an existing playlist clip. index counts clips within the track in the order daw_playlist_get reports them. " +
  "Audio clips also take stretchMode (0 smooth, 1 percussive, 2 formant preserved) and followTempo (re-stretch on tempo change; enabling it refits the clip immediately). " + TIME_HELP,
  {
    track: z.number(), index: z.number(),
    start: z.number().optional(), length: z.number().optional(),
    muted: z.boolean().optional(),
    stretchMode: z.number().optional().describe("audio clips: 0 smooth, 1 percussive, 2 formant preserved"),
    followTempo: z.boolean().optional().describe("audio clips: keep musical length across tempo changes"),
  }, "playlist.setClip");

tool("daw_playlist_clear", "Clear all clips (or one track's).", { track: z.number().optional() }, "playlist.clear");

tool("daw_mixer_get", "List mixer inserts with volume/pan/mute, sends, effect slots, and live peak levels.", {}, "mixer.get");

tool("daw_mixer_add_insert", "Append a new mixer insert; returns its index.",
  { name: z.string().optional().describe("insert name; defaults to 'Insert N'") }, "mixer.addInsert");

tool("daw_mixer_set_insert", "Set insert volume/pan/mute/name. Insert 0 is the master.",
  {
    insert: z.number(), volume: z.number().optional(), pan: z.number().optional(),
    mute: z.boolean().optional(), name: z.string().optional(),
  }, "mixer.setInsert");

tool("daw_mixer_add_send", "Route one insert into another (bus/sidechain routing). level 0..1.25.",
  { from: z.number(), to: z.number(), level: z.number().optional() }, "mixer.addSend");

tool("daw_mixer_set_effect", "Load an effect plugin (pluginId from daw_plugins_list) into an insert slot (0-9).",
  { insert: z.number(), slot: z.number(), pluginId: z.string() }, "mixer.setEffect");

tool("daw_mixer_remove_effect", "Remove the effect in an insert slot.",
  { insert: z.number(), slot: z.number() }, "mixer.removeEffect");

tool("daw_plugins_list", "List scanned VST3/AU plugins with their pluginId strings.", {}, "plugins.list");
tool("daw_plugins_scan", "Scan the system for VST3/AU plugins (runs in background).", {}, "plugins.scan");

tool("daw_meters", "Live peak meters for all mixer inserts (call while playing to check levels).", {}, "meters.get");

tool("daw_automation_list",
  "List every automation source: id, name, target type/id, param, and how many curve points it holds.",
  {}, "automation.list");

tool("daw_automation_create",
  "Create an automation source plus the playlist clip that plays it. targetType 'channel' (paramId volume|pan), 'insert' (volume|pan), 'channel-param' (a built-in sampler/synth knob: attack, decay, sustain, release, cutoff, resonance, osc2Detune, osc2Mix, oscShape, filterEnvAmt), 'plugin-channel' (paramId '<paramIndex>') or 'plugin-insert' (paramId '<slot>:<paramIndex>'). Values are normalised 0..1.",
  {
    targetType: z.enum(["channel", "insert", "channel-param", "plugin-channel", "plugin-insert"]),
    targetId: z.number().describe("channel id for channel/channel-param/plugin-channel, insert index for insert/plugin-insert"),
    paramId: z.string(),
    name: z.string().optional(),
    initialValue: z.number().min(0).max(1).optional(),
  }, "automation.create");

tool("daw_automation_get_points", "Read one automation source's curve points (pos in ticks, value 0..1, tension -1..1).",
  { automationId: z.number() }, "automation.getPoints");

tool("daw_automation_set_points",
  "Replace an automation source's curve. value 0..1, tension -1..1 shapes the segment to the next point. " + TIME_HELP,
  {
    automationId: z.number(),
    points: z.array(z.object({
      pos: z.number(), value: z.number(), tension: z.number().optional(),
    })),
  }, "automation.setPoints");

tool("daw_automation_remove", "Delete an automation source and every playlist clip that references it.",
  { automationId: z.number() }, "automation.remove");
tool("daw_ui_show_panel",
  "Bring a DAW panel forward so it is visible in a snapshot and to the user.",
  { panel: z.enum(["playlist", "rack", "pianoroll", "mixer", "browser"]) }, "ui.showPanel");

tool("daw_ui_snapshot",
  "Save a PNG screenshot of the Eurydice UI (the frontmost editor window if one is open, otherwise the main window). Use it to see what the user sees.",
  { path: z.string().describe("destination .png path") }, "ui.snapshot");

tool("daw_render_analyze",
  "Hear the mix without writing files: offline-render the project (or the loop range) and return peak dB, RMS dB and spectral band shares (sub/low/lowMid/highMid/high, dB relative to the target's total energy) for the master and every insert carrying signal. Use it to judge levels and balance, then adjust and re-analyze.",
  {
    loopRangeOnly: z.boolean().optional().describe("bound the analysis to the project's loop range"),
    tailSeconds: z.number().optional().describe("extra ring-out time (default 0.5)"),
  }, "render.analyze");

tool("daw_render_export",
  "Render the project to a WAV (plus optional MP3 and stems). Renders the whole arrangement in song mode, or the active pattern otherwise; an armed loop is ignored unless loopRangeOnly is set.",
  {
    path: z.string().describe("destination .wav path; stems land beside it as '<name>-<stem>.wav'"),
    mp3: z.boolean().optional().describe("also write a 320 kbps MP3 (needs the `lame` binary)"),
    stems: z.enum(["none", "insert", "channel"]).optional()
      .describe("'insert' = one wav per mixer insert, 'channel' = one wav per rack channel in isolation"),
    loopRangeOnly: z.boolean().optional().describe("bound the render to the project's loop range"),
    normalise: z.boolean().optional().describe("peak-normalise afterwards; stems get the master's gain"),
    normaliseDb: z.number().min(-24).max(0).optional().describe("normalisation target in dBFS (default -0.3)"),
    bitDepth: z.union([z.literal(16), z.literal(24), z.literal(32)]).optional(),
    sampleRate: z.number().optional().describe("output sample rate; omit or 0 to keep the engine rate"),
    tailSeconds: z.number().optional().describe("extra time so reverbs and releases ring out (default 2)"),
  }, "render.export");

tool("daw_project_save", "Save the project to a .eury file.", { path: z.string() }, "project.save");
tool("daw_project_load", "Load a .eury project file.", { path: z.string() }, "project.load");
tool("daw_project_new", "Start a fresh default project (discards unsaved changes).", {}, "project.new");

server.tool("daw_rpc",
  "Escape hatch: call any raw Eurydice JSON-RPC method with params. Use only when no dedicated tool fits.",
  { method: z.string(), params: z.record(z.any()).optional() },
  async ({ method, params }) => asText(await rpc(method, params ?? {})));

const transport = new StdioServerTransport();
await server.connect(transport);
