Status: done

# Unit tests + coverage goal

Coverage goal: >=80% lines on src/model, src/engine core, src/control (UI excluded — no GUI harness).

- [x] Test target (GoogleTest, shared sources, no Main.cpp)
- [x] ProjectModel: defaults, channels/lanes cleanup, notes, undo, save/load round-trip
- [x] Snapshot build: solo/mute, note sort, send topo order + cycle fallback, clip resolution
- [x] AutomationSnapshot::valueAt: edges, tension curves
- [x] Engine offline: onset timing, swing, song-mode looping, automation ramp
- [x] Sampler/synth render sanity
- [x] OfflineRenderer duration + stems
- [x] ControlDispatcher: every RPC method happy path + error cases
- [x] scripts/coverage.sh with enforced threshold

Result: 69 tests, 85.5% line coverage on core (gate 80%).

Result: 69 tests, 85.5% line coverage on core (gate 80%).
