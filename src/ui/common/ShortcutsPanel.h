#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "app/Theme.h"

// Scrollable cheat sheet for Help -> Keyboard Shortcuts. The command section
// is generated from the ApplicationCommandManager so it can never drift from
// the real keymap; only the bindings that live outside the command manager
// (typing piano, per-panel keys, mouse-modifier gestures) are listed by hand.
class ShortcutsPanel : public juce::Component
{
public:
    static constexpr int preferredWidth = 560;
    static constexpr int preferredHeight = 600;

    explicit ShortcutsPanel (juce::ApplicationCommandManager& commandManager)
    {
        addCommandRows (commandManager);
        addManualRows();
        addAndMakeVisible (viewport);
        viewport.setViewedComponent (&list, false);
        viewport.setScrollBarsShown (true, false);
    }

    void resized() override
    {
        viewport.setBounds (getLocalBounds());
        list.setSize (viewport.getMaximumVisibleWidth(), list.heightNeeded());
    }

private:
    struct Row
    {
        juce::String keys;      // empty for section headers and notes
        juce::String action;
        enum class Kind { header, shortcut, note } kind = Kind::shortcut;
    };

    struct ListView : juce::Component
    {
        std::vector<Row> rows;

        static constexpr int padding = 16, headerHeight = 34, rowHeight = 21,
                             noteHeight = 30, keyColumnWidth = 150, columnGap = 12;

        int heightNeeded() const
        {
            int height = padding * 2;
            for (const auto& row : rows)
                height += row.kind == Row::Kind::header ? headerHeight
                        : row.kind == Row::Kind::note   ? noteHeight
                                                        : rowHeight;
            return height;
        }

        void paint (juce::Graphics& g) override
        {
            int y = padding;
            for (const auto& row : rows)
            {
                if (row.kind == Row::Kind::header)
                {
                    g.setColour (theme::outlineLight);
                    g.fillRect (padding, y + headerHeight - 7, getWidth() - padding * 2, 1);
                    g.setColour (theme::accent);
                    g.setFont (theme::uiFont (13.0f, true));
                    g.drawText (row.action, padding, y, getWidth() - padding * 2,
                                headerHeight - 10, juce::Justification::bottomLeft);
                    y += headerHeight;
                }
                else if (row.kind == Row::Kind::note)
                {
                    g.setColour (theme::textFaint);
                    g.setFont (theme::uiFont (11.5f));
                    g.drawFittedText (row.action, padding, y + 4, getWidth() - padding * 2,
                                      noteHeight - 8, juce::Justification::topLeft, 2);
                    y += noteHeight;
                }
                else
                {
                    g.setFont (theme::uiFont (12.5f, true));
                    g.setColour (theme::secondary);
                    g.drawText (row.keys, padding, y, keyColumnWidth,
                                rowHeight, juce::Justification::centredRight);
                    g.setFont (theme::uiFont (12.5f));
                    g.setColour (theme::textPrimary);
                    g.drawText (row.action, padding + keyColumnWidth + columnGap, y,
                                getWidth() - padding * 2 - keyColumnWidth - columnGap,
                                rowHeight, juce::Justification::centredLeft);
                    y += rowHeight;
                }
            }
        }
    };

    // One row per command that has a binding, grouped by category in the
    // order the commands were registered (which mirrors the menus).
    void addCommandRows (juce::ApplicationCommandManager& commandManager)
    {
        for (const auto& category : { juce::String ("File"), juce::String ("Edit"),
                                      juce::String ("View"), juce::String ("Transport"),
                                      juce::String ("Automation"), juce::String ("Options"),
                                      juce::String ("Help") })
        {
            bool headerAdded = false;
            for (int i = 0; i < commandManager.getNumCommands(); ++i)
            {
                const auto* info = commandManager.getCommandForIndex (i);
                if (info == nullptr || info->categoryName != category)
                    continue;

                const auto keys = commandManager.getKeyMappings()
                                      ->getKeyPressesAssignedToCommand (info->commandID);
                if (keys.isEmpty())
                    continue;

                if (! headerAdded)
                {
                    list.rows.push_back ({ {}, category, Row::Kind::header });
                    headerAdded = true;
                }

                juce::StringArray keyTexts;
                for (const auto& key : keys)
                    keyTexts.add (prettyKeyText (key));
                list.rows.push_back ({ keyTexts.joinIntoString ("  /  "),
                                       info->shortName, Row::Kind::shortcut });
            }
        }

        list.rows.push_back ({ {}, "macOS claims the bare F-keys for brightness and media: "
                                   "use Fn+F5 etc., or enable standard function keys in "
                                   "System Settings.", Row::Kind::note });
    }

    // Bindings that live outside the command manager.
    void addManualRows()
    {
        auto section = [this] (const juce::String& title) {
            list.rows.push_back ({ {}, title, Row::Kind::header });
        };
        auto row = [this] (const juce::String& keys, const juce::String& action) {
            list.rows.push_back ({ keys, action, Row::Kind::shortcut });
        };

        // juce::String's const char* constructor is ASCII-only, so the key
        // glyphs have to be spelled out as UTF-8.
        const juce::String enDash (juce::CharPointer_UTF8 ("\xe2\x80\x93"));
        const juce::String cmdKey (juce::CharPointer_UTF8 ("\xe2\x8c\x98"));
        const juce::String backspaceKey (juce::CharPointer_UTF8 ("\xe2\x8c\xab"));

        section ("Typing Piano");
        row ("Z " + enDash + " M", "Play notes, lower octave (S D G H J are the black keys)");
        row ("Q " + enDash + " P", "Play notes, upper octave (number row fills in the black keys)");
        row (",  /  .", "Octave down / up");
        list.rows.push_back ({ {}, "QWERTZ layouts are detected automatically and mapped "
                                   "by physical key.", Row::Kind::note });

        section ("Piano Roll");
        row ("Delete / " + backspaceKey, "Delete the selected notes");
        row (cmdKey + "A", "Select all notes in the current channel");
        row ("Right-click", "Tool menu on a selection; erase elsewhere (drag to keep erasing)");
        row (cmdKey + "-drag", "Marquee select");
        row ("Shift-click", "Add a note to the selection");
        row (cmdKey + "-wheel", "Zoom horizontally (around the pointer)");
        row (cmdKey + "+  /  " + cmdKey + "-", "Zoom in / out horizontally");
        row (cmdKey + "0", "Fit the whole pattern in the window");
        row ("Shift-wheel", "Scroll horizontally");

        section ("Playlist");
        row ("Right-click", "Delete a clip; rename on a track header");
        row ("Alt-resize", "Time-stretch an audio clip to fit its new length");
        row (cmdKey + "-wheel", "Zoom horizontally");
        row ("Shift-wheel", "Scroll horizontally");

        section ("Channel Rack");
        row ("Right-click / drag", "Erase steps");

        section ("Panels");
        row ("Drag near an edge", "Dock to that half of the desktop (corners take a quarter)");
        row ("Shift-drag", "Place freely, without snapping");
        row ("Double-click title", "Maximise the panel");
    }

    // JUCE spells bare keys in lowercase ("spacebar", "home"); tidy them up.
    static juce::String prettyKeyText (const juce::KeyPress& key)
    {
        auto text = key.getTextDescriptionWithIcons();
        if (text == "spacebar")
            return "Space";
        if (text.isNotEmpty() && juce::CharacterFunctions::isLowerCase (text[0]))
            return text.substring (0, 1).toUpperCase() + text.substring (1);
        return text;
    }

    ListView list;
    juce::Viewport viewport;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShortcutsPanel)
};
