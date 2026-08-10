#pragma once

#include "DrumPads.h"

// Drum-machine kit presets. A kit is a folder of one-shot samples under the
// user's kits directory; each folder becomes an entry in the editor's Kit
// menu. A folder may carry a kit.json manifest naming the pads and their
// choke groups; without one, the samples land on pads in filename order.
//
//   ~/Music/Eurydice/Kits/TR-808/kit.json
//   ~/Music/Eurydice/Kits/TR-808/01 Kick.wav ...
//
// manifest: { "name": "TR-808", "pads": [
//              { "name": "Kick", "file": "01 Kick.wav", "choke": 0 }, ... ] }
namespace drumkits
{
struct KitPad
{
    juce::String name;
    juce::File file;
    int choke = 0;
};

struct Kit
{
    juce::String name;
    std::vector<KitPad> pads;
};

inline juce::File kitsDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
               .getChildFile ("Eurydice").getChildFile ("Kits");
}

// "01 Kick" -> "Kick"; plain names pass through.
inline juce::String padNameFromFile (const juce::File& file)
{
    auto name = file.getFileNameWithoutExtension();
    int i = 0;
    while (i < name.length() && juce::CharacterFunctions::isDigit (name[i]))
        ++i;
    while (i < name.length() && (name[i] == ' ' || name[i] == '-' || name[i] == '_'))
        ++i;
    return i > 0 && i < name.length() ? name.substring (i) : name;
}

inline Kit loadKit (const juce::File& folder)
{
    Kit kit;
    kit.name = folder.getFileName();

    const auto manifest = folder.getChildFile ("kit.json");
    if (manifest.existsAsFile())
    {
        const auto parsed = juce::JSON::parse (manifest.loadFileAsString());
        if (parsed.getProperty ("name", "").toString().isNotEmpty())
            kit.name = parsed.getProperty ("name", "").toString();

        if (const auto* padList = parsed.getProperty ("pads", juce::var()).getArray())
            for (const auto& entry : *padList)
            {
                const auto file = folder.getChildFile (entry.getProperty ("file", "").toString());
                if (! file.existsAsFile())
                    continue;   // a broken manifest line loses one pad, not the kit
                KitPad pad;
                pad.file = file;
                pad.name = entry.getProperty ("name", padNameFromFile (file)).toString();
                pad.choke = juce::jlimit (0, 8, (int) entry.getProperty ("choke", 0));
                kit.pads.push_back (std::move (pad));
            }
        return kit;
    }

    auto files = folder.findChildFiles (juce::File::findFiles, false,
                                        "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a");
    files.sort();
    for (const auto& file : files)
        kit.pads.push_back ({ padNameFromFile (file), file, 0 });
    return kit;
}

inline std::vector<Kit> scanKits (const juce::File& baseDirectory = kitsDirectory())
{
    std::vector<Kit> kits;
    auto folders = baseDirectory.findChildFiles (juce::File::findDirectories, false);
    folders.sort();
    for (const auto& folder : folders)
        if (auto kit = loadKit (folder); ! kit.pads.empty())
            kits.push_back (std::move (kit));
    return kits;
}

// Puts the kit's sounds on the pads: sample, name, choke, with level, pan and
// tune back at their defaults. Note mappings are untouched, so a controller
// layout survives swapping kits. Grows the grid when the kit needs more pads.
inline void applyKit (juce::ValueTree channel, const Kit& kit, juce::UndoManager* undo)
{
    const int numPads = juce::jmin ((int) kit.pads.size(), drumpads::maxPads);

    int rows = drumpads::gridRows (channel);
    int cols = drumpads::gridCols (channel);
    while (rows * cols < numPads && (rows < drumpads::maxGridSide || cols < drumpads::maxGridSide))
        (rows <= cols && rows < drumpads::maxGridSide) ? ++rows : ++cols;
    channel.setProperty (ids::padRows, rows, undo);
    channel.setProperty (ids::padCols, cols, undo);
    drumpads::ensurePadCount (channel, numPads, undo);

    for (int i = 0; i < numPads; ++i)
    {
        auto pad = drumpads::getPad (channel, i);
        const auto& kitPad = kit.pads[(size_t) i];
        pad.setProperty (ids::name, kitPad.name, undo);
        pad.setProperty (ids::samplePath, kitPad.file.getFullPathName(), undo);
        pad.setProperty (ids::choke, kitPad.choke, undo);
        pad.setProperty (ids::volume, 0.9, undo);
        pad.setProperty (ids::pan, 0.0, undo);
        pad.setProperty (ids::tune, 0.0, undo);
        if (pad.hasProperty (ids::synthDrum))
            pad.removeProperty (ids::synthDrum, undo);
    }
}
} // namespace drumkits
