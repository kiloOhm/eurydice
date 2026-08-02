#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

// FL-style typing piano: bottom letter row = lower octave from C4, Q-row =
// the octave above, ','/'.' shift octaves. One instance is shared as a
// KeyListener by the main window and the channel-editor windows, so the
// laptop keyboard plays the selected instrument wherever the focus happens
// to be.
//
// The mapping is by PHYSICAL key position, so layouts whose characters move
// (QWERTZ swaps y/z) need their own character rows — key events only carry
// the translated character, not the scan code.
//
// Sinks are plain functions so the class stays testable without an engine.
class TypingPiano : public juce::KeyListener
{
public:
    enum class Layout { qwerty, qwertz };

    TypingPiano (std::function<void (int note, float velocity)> onNoteOn,
                 std::function<void (int note)> onNoteOff,
                 Layout keyboardLayout = Layout::qwerty)
        : noteOn (std::move (onNoteOn)), noteOff (std::move (onNoteOff)),
          layout (keyboardLayout) {}

    static int keyToNote (juce::juce_wchar c, Layout layout = Layout::qwerty)
    {
        // Same physical keys on every layout; only the characters differ.
        static const juce::String lowQwerty  ("zsxdcvgbhnjm");
        static const juce::String highQwerty ("q2w3er5t6y7ui9o0p");
        static const juce::String lowQwertz  ("ysxdcvgbhnjm");
        static const juce::String highQwertz ("q2w3er5t6z7ui9o0p");

        const auto& lowRow  = layout == Layout::qwertz ? lowQwertz  : lowQwerty;
        const auto& highRow = layout == Layout::qwertz ? highQwertz : highQwerty;
        if (const int i = lowRow.indexOfChar (c); i >= 0)   return 60 + i;
        if (const int i = highRow.indexOfChar (c); i >= 0)  return 72 + i;
        return -1;
    }

    bool keyPressed (const juce::KeyPress& key, juce::Component*) override
    {
        if (key.getModifiers().isAnyModifierKeyDown())
            return false;

        const auto c = (juce::juce_wchar) juce::CharacterFunctions::toLowerCase (
                           (juce::juce_wchar) key.getTextCharacter());

        if (c == ',') { octaveShift = juce::jmax (octaveShift - 12, -36); return true; }
        if (c == '.') { octaveShift = juce::jmin (octaveShift + 12,  36); return true; }

        if (const int base = keyToNote (c, layout); base >= 0)
        {
            const int note = juce::jlimit (0, 127, base + octaveShift);
            if (keysDown.find (c) == keysDown.end())   // key repeat: no retrigger
            {
                keysDown[c] = note;
                noteOn (note, 0.8f);
            }
            return true;
        }
        return false;
    }

    bool keyStateChanged (bool, juce::Component*) override
    {
        bool handled = false;
        for (auto it = keysDown.begin(); it != keysDown.end();)
        {
            if (! juce::KeyPress::isKeyCurrentlyDown ((int) it->first))
            {
                noteOff (it->second);
                it = keysDown.erase (it);
                handled = true;
            }
            else
                ++it;
        }
        return handled;
    }

private:
    std::function<void (int, float)> noteOn;
    std::function<void (int)> noteOff;
    Layout layout = Layout::qwerty;
    int octaveShift = 0;
    std::map<juce::juce_wchar, int> keysDown;   // typed char -> sounding note

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TypingPiano)
};
