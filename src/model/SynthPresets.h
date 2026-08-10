#pragma once

#include <optional>
#include <vector>
#include <juce_data_structures/juce_data_structures.h>
#include "Ids.h"

// The synth channel's preset system: a factory bank browsed by genre, plus
// whatever the user has saved next to it. A preset is an absolute starting
// point — applying one writes every synth parameter, falling back to the
// descriptor default for anything it does not name, so switching presets can
// never leave a stray knob from the last one behind.
//
// Deliberately left alone by a preset load: everything that belongs to the
// channel rather than to the sound — its name, colour, volume, pan and mixer
// routing.
//
// User patches are one XML file each under ~/Music/Eurydice/Presets/Synth:
//
//   <EURYPRESET type="synth" name="My Reese" oscShape="0" cutoff="700" .../>
//
// one attribute per parameter, so a patch stays readable and editable by hand
// and gains new parameters (as defaults) as the synth grows.
namespace synthpresets
{
// The category user patches are filed under, and the file extension they use.
inline const juce::String userCategory { "User" };
inline const juce::String fileExtension { ".eurypreset" };

struct Preset
{
    juce::String name;
    juce::String category;
    // What the patch is for, shown next to the preset box.
    juce::String description;
    std::vector<std::pair<juce::Identifier, double>> values;
};

// The built-in bank, in browse order.
const std::vector<Preset>& factory();

inline juce::File userDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
               .getChildFile ("Eurydice").getChildFile ("Presets").getChildFile ("Synth");
}

// The user's saved patches, rescanned from disk on every call so a file
// dropped into the folder shows up without a restart.
std::vector<Preset> user();

// Factory bank first, then the user's patches.
std::vector<Preset> all();

// Categories in bank order, deduplicated; "User" last when there are any.
juce::StringArray categories();

std::optional<Preset> find (const juce::String& name);

// True when the name belongs to the factory bank, which a user patch must not
// shadow — the browser would then show two entries that load the same sound.
bool isFactoryName (const juce::String&);

// Writes a preset onto a synth channel in the caller's undo transaction.
void apply (juce::ValueTree channel, const Preset&, juce::UndoManager*);

// Saves a channel's synth parameters as <name>.eurypreset, replacing any file
// of that name. Returns the file written, or an invalid File if it could not
// be written.
juce::File save (const juce::ValueTree& channel, const juce::String& name);

// The single-file half of the two above, for callers that own the location.
std::optional<Preset> readFile (const juce::File&);
bool writeFile (const juce::File&, const juce::ValueTree& channel, const juce::String& name);
} // namespace synthpresets
