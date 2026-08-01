Status: in-progress

# Unit tests + coverage goal

Coverage goal: >=80% lines on src/model, src/engine core, src/control (UI excluded — no GUI harness).

- [ ] Test target (GoogleTest, shared sources, no Main.cpp)
- [ ] ProjectModel: defaults, channels/lanes cleanup, notes, undo, save/load round-trip
- [ ] Snapshot build: solo/mute, note sort, send topo order + cycle fallback, clip resolution
- [ ] AutomationSnapshot::valueAt: edges, tension curves
- [ ] Engine offline: onset timing, swing, song-mode looping, automation ramp
- [ ] Sampler/synth render sanity
- [ ] OfflineRenderer duration + stems
- [ ] ControlDispatcher: every RPC method happy path + error cases
- [ ] scripts/coverage.sh with enforced threshold
