Status: done

# Discoverability pass (menus, editors, navigation)

Found during review: the engine features worked but were unreachable by mouse.

- [x] macOS menu bar (File/Edit/View/Options) via ApplicationCommandManager
- [x] Recent-projects list, persisted
- [x] Panel toggle buttons in the transport bar, lit to show state
- [x] Shortcuts moved off bare F-keys (Cmd+1..4) with F-keys kept as secondary
- [x] Command target pinned so menus stay live when a plugin window has focus
- [x] Sampler editor: sample slot, waveform, root note, ADSR, filter, one-shot
- [x] Synth editor: oscillators, filter, envelope, on-screen keyboard
- [x] Channel params persisted on the CHANNEL tree and synced to generators
- [x] Right-click channel -> piano roll / settings; double-click clip -> piano roll
- [x] Window title with project name + dirty marker; save prompt on quit/new/open
- [x] Custom LookAndFeel applied app-wide (editor windows and dialogs too)
- [x] Fixed: editor windows leaked at shutdown (static map outlived the leak detector)
