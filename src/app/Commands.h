#pragma once

// Application command IDs. Everything the user can trigger lives here so the
// menu bar, keyboard shortcuts and toolbar buttons all stay in sync.
namespace CommandIDs
{
enum
{
    fileNew = 0x2000,
    fileOpen,
    fileSave,
    fileSaveAs,
    fileExport,

    editUndo,
    editRedo,

    viewPlaylist,
    viewChannelRack,
    viewPianoRoll,
    viewMixer,
    viewBrowser,

    transportPlayStop,
    transportRewind,
    transportToggleSongMode,
    transportToggleRecord,

    optionsAudioSettings,
    optionsScanPlugins,
};
}
