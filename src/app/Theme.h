#pragma once

#include <juce_graphics/juce_graphics.h>

// Eurydice's own dark identity: graphite surfaces, amber primary accent,
// teal secondary. FL-like layout, original skin.
namespace theme
{
inline const juce::Colour windowBg      { 0xff17191d };
inline const juce::Colour desktopBg     { 0xff121316 };
inline const juce::Colour panelBg       { 0xff22252b };
inline const juce::Colour panelHeader   { 0xff2b2f37 };
inline const juce::Colour raised        { 0xff31353f };
inline const juce::Colour sunken        { 0xff1a1c20 };
inline const juce::Colour outline       { 0xff0d0e10 };
inline const juce::Colour outlineLight  { 0xff3d424d };

inline const juce::Colour textPrimary   { 0xffd6d9de };
inline const juce::Colour textDim       { 0xff8b909a };
inline const juce::Colour textFaint     { 0xff5b606a };

inline const juce::Colour accent        { 0xffffa726 };   // amber — playback, primary highlights
inline const juce::Colour accentDim     { 0xff8a5c17 };
inline const juce::Colour secondary     { 0xff41c7b9 };   // teal — selections, links
inline const juce::Colour record        { 0xffe5534b };
inline const juce::Colour ledOff        { 0xff3a3e46 };
inline const juce::Colour ledGreen      { 0xff7dd069 };

// Step sequencer cells: beat groups alternate tint, like FL's 4-step blocks.
inline const juce::Colour stepEvenBg    { 0xff2e333c };
inline const juce::Colour stepOddBg     { 0xff262a31 };
inline const juce::Colour stepOn        { 0xffffa726 };
inline const juce::Colour stepOnDim     { 0xffb87817 };

inline const juce::Colour pianoBlackKey { 0xff23262c };
inline const juce::Colour pianoWhiteKey { 0xff3a3f48 };
inline const juce::Colour noteFill      { 0xff5ad2c4 };
inline const juce::Colour noteOutline   { 0xff9ceee4 };
inline const juce::Colour ghostNote     { 0x40aab2bd };

inline juce::Font uiFont (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions ("Helvetica Neue", height,
                                          bold ? juce::Font::bold : juce::Font::plain));
}
} // namespace theme
