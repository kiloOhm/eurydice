#include <juce_audio_utils/juce_audio_utils.h>
#include "MainComponent.h"
#include "Theme.h"
#include "AudioRecorder.h"
#include "control/ControlServer.h"
#include "model/UndoGesture.h"
#include "engine/OfflineRenderer.h"
#include "ui/browser/BrowserPanel.h"
#include "ui/mixer/MixerPanel.h"
#include "ui/pianoroll/PianoRollPanel.h"
#include "ui/playlist/PlaylistPanel.h"
#include "ui/rack/ChannelEditor.h"
#include "ui/rack/ChannelRackPanel.h"
#include "ui/automation/AutomationEditor.h"
#include "ui/common/DockZones.h"
#include "KeyboardLayoutDetect.h"
#include "MicPermission.h"

namespace
{
constexpr int recentFilesBaseId = 3000;
constexpr int recentFilesClearId = 3999;

// Small toggle strip for a FloatingPanel title bar (LOOP/AUTO on the
// playlist). State is polled: these can also change from menus, shortcuts,
// the playlist ruler or the control API.
class TitleBarToggles : public juce::Component,
                        private juce::Timer
{
public:
    TitleBarToggles() { startTimerHz (10); }

    void add (const juce::String& label, const juce::String& tooltip, juce::Colour onColour,
              std::function<void()> onClick, std::function<bool()> getState)
    {
        auto item = std::make_unique<Item>();
        item->button.setButtonText (label);
        item->button.setTooltip (tooltip);
        item->button.setWantsKeyboardFocus (false);
        item->button.setClickingTogglesState (false);   // state follows the model
        item->button.setColour (juce::TextButton::buttonOnColourId, onColour);
        item->button.onClick = std::move (onClick);
        item->getState = std::move (getState);
        addAndMakeVisible (item->button);
        items.push_back (std::move (item));
        setSize ((int) items.size() * (buttonWidth + gap), 20);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        for (auto& item : items)
        {
            item->button.setBounds (r.removeFromLeft (buttonWidth));
            r.removeFromLeft (gap);
        }
    }

private:
    void timerCallback() override
    {
        for (auto& item : items)
            item->button.setToggleState (item->getState(), juce::dontSendNotification);
    }

    static constexpr int buttonWidth = 46, gap = 3;
    struct Item { juce::TextButton button; std::function<bool()> getState; };
    std::vector<std::unique_ptr<Item>> items;
};
constexpr int automationBaseId = 4000;

// Export settings sheet. Collects the render options, then hands them to the
// caller, which picks the destination and runs the render.
class ExportOptionsPanel : public juce::Component
{
public:
    ExportOptionsPanel (bool loopAvailable, bool lameAvailable)
    {
        bitDepthBox.addItemList ({ "16 bit", "24 bit", "32 bit float" }, 1);
        bitDepthBox.setSelectedItemIndex (1, juce::dontSendNotification);

        sampleRateBox.addItemList ({ "Project rate", "44100 Hz", "48000 Hz", "88200 Hz", "96000 Hz" }, 1);
        sampleRateBox.setSelectedItemIndex (0, juce::dontSendNotification);

        stemsBox.addItemList ({ "None", "Per mixer insert", "Per channel" }, 1);
        stemsBox.setSelectedItemIndex (0, juce::dontSendNotification);

        loopToggle.setButtonText ("Render the loop range only");
        loopToggle.setEnabled (loopAvailable);
        if (! loopAvailable)
            loopToggle.setTooltip ("Mark a loop range in the playlist ruler first");

        mp3Toggle.setButtonText ("Also write a 320 kbps MP3");
        mp3Toggle.setEnabled (lameAvailable);
        if (! lameAvailable)
            mp3Toggle.setTooltip ("Needs the `lame` encoder (brew install lame)");

        normaliseToggle.setButtonText ("Normalise peak to");
        normaliseToggle.onClick = [this]
        {
            normaliseSlider.setEnabled (normaliseToggle.getToggleState());
        };

        normaliseSlider.setSliderStyle (juce::Slider::LinearBar);
        normaliseSlider.setRange (-24.0, 0.0, 0.1);
        normaliseSlider.setValue (-0.3, juce::dontSendNotification);
        normaliseSlider.setTextValueSuffix (" dBFS");
        normaliseSlider.setEnabled (false);

        exportButton.onClick = [this]
        {
            if (onExport)
                onExport (buildOptions());
            closeDialog();
        };
        cancelButton.onClick = [this] { closeDialog(); };

        for (auto* c : std::initializer_list<juce::Component*> {
                 &bitDepthLabel, &sampleRateLabel, &stemsLabel,
                 &bitDepthBox, &sampleRateBox, &stemsBox,
                 &loopToggle, &mp3Toggle, &normaliseToggle, &normaliseSlider,
                 &exportButton, &cancelButton })
            addAndMakeVisible (c);
    }

    std::function<void (const OfflineRenderer::Options&)> onExport;

    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        constexpr int rowHeight = 26;
        constexpr int labelWidth = 130;

        auto row = [&r] { auto line = r.removeFromTop (rowHeight); r.removeFromTop (8); return line; };
        auto labelled = [] (juce::Rectangle<int> line, juce::Component& label,
                            juce::Component& control)
        {
            label.setBounds (line.removeFromLeft (labelWidth));
            control.setBounds (line);
        };

        labelled (row(), bitDepthLabel, bitDepthBox);
        labelled (row(), sampleRateLabel, sampleRateBox);
        labelled (row(), stemsLabel, stemsBox);
        loopToggle.setBounds (row());
        mp3Toggle.setBounds (row());

        auto normaliseRow = row();
        normaliseToggle.setBounds (normaliseRow.removeFromLeft (160));
        normaliseSlider.setBounds (normaliseRow.removeFromLeft (120));

        auto buttons = r.removeFromBottom (rowHeight + 4);
        exportButton.setBounds (buttons.removeFromRight (110));
        buttons.removeFromRight (8);
        cancelButton.setBounds (buttons.removeFromRight (90));
    }

    static constexpr int preferredWidth = 420;
    static constexpr int preferredHeight = 274;

private:
    OfflineRenderer::Options buildOptions() const
    {
        OfflineRenderer::Options options;
        options.bitDepth = std::array { 16, 24, 32 }[(size_t) juce::jlimit (0, 2, bitDepthBox.getSelectedItemIndex())];
        options.sampleRate = std::array { 0, 44100, 48000, 88200, 96000 }
                                 [(size_t) juce::jlimit (0, 4, sampleRateBox.getSelectedItemIndex())];
        options.stems = std::array { OfflineRenderer::Stems::none,
                                     OfflineRenderer::Stems::perInsert,
                                     OfflineRenderer::Stems::perChannel }
                            [(size_t) juce::jlimit (0, 2, stemsBox.getSelectedItemIndex())];
        options.loopRangeOnly = loopToggle.isEnabled() && loopToggle.getToggleState();
        options.renderMp3 = mp3Toggle.isEnabled() && mp3Toggle.getToggleState();
        options.normalise = normaliseToggle.getToggleState();
        options.normaliseTargetDb = normaliseSlider.getValue();
        return options;
    }

    void closeDialog()
    {
        if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
            dialog->exitModalState (0);
    }

    juce::Label bitDepthLabel { {}, "Bit depth" };
    juce::Label sampleRateLabel { {}, "Sample rate" };
    juce::Label stemsLabel { {}, "Stems" };
    juce::ComboBox bitDepthBox, sampleRateBox, stemsBox;
    juce::ToggleButton loopToggle, mp3Toggle, normaliseToggle;
    juce::Slider normaliseSlider;
    juce::TextButton exportButton { "Export..." }, cancelButton { "Cancel" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExportOptionsPanel)
};
}

MainComponent::MainComponent()
{
    // App-wide default so editor windows, dialogs and menus all match.
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
    setWantsKeyboardFocus (true);

    juce::PropertiesFile::Options opts;
    opts.applicationName = "Eurydice";
    opts.filenameSuffix = "settings";
    opts.folderName = "Eurydice";
    opts.osxLibrarySubFolder = "Application Support";
    settings = std::make_unique<juce::PropertiesFile> (opts);
    recentFiles.restoreFromString (settings->getValue ("recentFiles"));
    services.effects.setSandboxEnabled (settings->getBoolValue ("sandboxEffects", false));
    services.generators.setSandboxEnabled (services.effects.isSandboxEnabled());

    controlServer = std::make_unique<ControlServer> (services);
    midiInput = std::make_unique<MidiInputManager> (services);
    typingPiano = std::make_unique<TypingPiano> (
        [this] (int note, float velocity) { midiInput->noteOn (note, velocity); },
        [this] (int note) { midiInput->noteOff (note); },
        keyboardlayout::detect());
    channelEditors.typingKeys = typingPiano.get();
    services.pluginWindows.typingKeys = typingPiano.get();
    recorder = std::make_unique<AudioRecorder> (services);

    fileState.addChangeListener (this);

    // --- transport bar ---
    addAndMakeVisible (transportBar);
    transportBar.onPlay  = [this] { transportPlay(); };
    transportBar.onStop  = [this] { transportStop(); };
    transportBar.onRecordToggled = [this] { toggleRecordArm(); };
    transportBar.onTempoChanged    = [this] (double bpm) { services.project.setTempo (bpm); };
    undoGesture::attach (transportBar.getTempoSlider(), services.project, "Tempo");
    transportBar.onSongModeChanged = [this] (bool song) { services.project.setSongMode (song); };
    transportBar.onMetronomeToggled = [this]
    {
        commandManager.invokeDirectly (CommandIDs::transportToggleMetronome, false);
    };
    transportBar.onMetronomeLevelChanged = [this] (double level)
    {
        services.engine.setMetronomeLevel ((float) level);
        settings->setValue ("metronomeLevel", level);
    };
    transportBar.getMetronomeEnabled = [this] { return services.engine.isMetronomeEnabled(); };
    transportBar.getBeatPosition   = [this] { return services.engine.getPositionBeats(); };
    transportBar.getTempo          = [this] { return services.project.getTempo(); };
    transportBar.getSongMode       = [this] { return services.project.isSongMode(); };
    transportBar.getIsPlaying      = [this] { return services.engine.isPlaying(); };
    transportBar.onPanelToggled    = [this] (juce::CommandID id)
    {
        commandManager.invokeDirectly (id, false);
    };
    transportBar.onPanelContextMenu = [this] (juce::CommandID id) { showPanelContextMenu (id); };
    transportBar.isPanelVisible = [this] (juce::CommandID id)
    {
        auto* panel = panelForCommand (id);
        return id == CommandIDs::viewBrowser ? browserVisible
                                             : (panel != nullptr && panel->isVisible());
    };
    transportBar.setTempoDisplay (services.project.getTempo());

    services.engine.setMetronomeEnabled (settings->getBoolValue ("metronomeEnabled", false));
    services.engine.setMetronomeLevel ((float) settings->getDoubleValue ("metronomeLevel", 0.5));
    services.engine.setCountInBars (settings->getIntValue ("countInBars", 0));
    transportBar.setMetronomeLevelDisplay (services.engine.getMetronomeLevel());

    // --- panels ---
    {
        auto browserPanel = std::make_unique<BrowserPanel> (services);
        browserPanel->onOpenProject = [this] (const juce::File& file)
        {
            if (okToCloseProject ("opening another project"))
                loadProjectFile (file);
        };
        browser = std::move (browserPanel);
    }
    addAndMakeVisible (*browser);

    browserWidth = settings->getIntValue ("browserWidth", 240);
    browserConstrainer.setMinimumWidth (170);
    browserConstrainer.setMaximumWidth (520);
    browserResizer = std::make_unique<juce::ResizableEdgeComponent> (
        browser.get(), &browserConstrainer, juce::ResizableEdgeComponent::rightEdge);
    addAndMakeVisible (*browserResizer);
    addAndMakeVisible (desktop);

    auto rack = std::make_unique<ChannelRackPanel> (services);
    rack->onShowPianoRoll = [this] { commandManager.invokeDirectly (CommandIDs::viewPianoRoll, false); };
    rack->onOpenChannelEditor = [this] (juce::ValueTree channel)
    {
        channelEditors.show (services, channel);
    };
    channelRackPanel = std::make_unique<FloatingPanel> ("Channel Rack", std::move (rack));

    auto playlist = std::make_unique<PlaylistPanel> (services);
    playlist->onShowPianoRoll = [this] { commandManager.invokeDirectly (CommandIDs::viewPianoRoll, false); };
    playlistView = playlist.get();
    playlistPanel = std::make_unique<FloatingPanel> ("Playlist", std::move (playlist));

    // Loop and automation-write live with the playlist they act on, not in
    // the transport bar.
    {
        auto toggles = std::make_unique<TitleBarToggles>();
        toggles->add ("LOOP", "Loop the marked range during playback", theme::accentDim,
                      [this] { commandManager.invokeDirectly (CommandIDs::transportToggleLoop, false); },
                      [this] { return services.project.isLoopEnabled(); });
        toggles->add ("AUTO", "Write automation: while playing, moving a knob records it", theme::record,
                      [this] { commandManager.invokeDirectly (CommandIDs::transportToggleAutomationWrite, false); },
                      [this] { return services.automationWriter.isArmed(); });
        playlistPanel->setTitleBarComponent (std::move (toggles));
    }

    services.onSnapshotRequested = [this] (const juce::File& file) { return writeSnapshot (file); };
    services.onCloseChannelEditors = [this] { channelEditors.closeAll(); };
    services.onRecordArmRequested = [this] (bool armed)
    {
        if (midiInput->recordArmed.load() != armed)
            toggleRecordArm();
        return midiInput->recordArmed.load();
    };
    services.onShowPanelRequested = [this] (const juce::String& name)
    {
        FloatingPanel* panel = name == "playlist"  ? playlistPanel.get()
                             : name == "rack"      ? channelRackPanel.get()
                             : name == "pianoroll" ? pianoRollPanel.get()
                             : name == "mixer"     ? mixerPanel.get()
                             : nullptr;
        if (panel != nullptr)
        {
            panel->bringToFrontAndShow();
            transportBar.refreshPanelButtons();
            return true;
        }
        if (name == "browser")
        {
            if (! browserVisible)
                commandManager.invokeDirectly (CommandIDs::viewBrowser, false);
            return true;
        }
        return false;
    };

    // Creating an automation clip used to be silent; show where it landed.
    services.onAutomationClipCreated = [this] (juce::ValueTree clip)
    {
        playlistPanel->bringToFrontAndShow();
        transportBar.refreshPanelButtons();
        playlistView->revealClip (clip);
    };

    {
        auto pianoRoll = std::make_unique<PianoRollPanel> (services);
        // Live input lights the roll's keyboard column while a key is held.
        midiInput->onLiveNote = [view = pianoRoll.get()] (int key, bool on)
        {
            view->setLiveKey (key, on);
        };
        pianoRollPanel = std::make_unique<FloatingPanel> ("Piano Roll", std::move (pianoRoll));
    }
    mixerPanel = std::make_unique<FloatingPanel> ("Mixer",
                                                  std::make_unique<MixerPanel> (services));

    for (auto* panel : { playlistPanel.get(), channelRackPanel.get(),
                         pianoRollPanel.get(), mixerPanel.get() })
    {
        desktop.addAndMakeVisible (*panel);
        panel->onVisibilityToggled = [this]
        {
            transportBar.refreshPanelButtons();
            commandManager.commandStatusChanged();
        };
    }

    pianoRollPanel->setVisible (false);
    mixerPanel->setVisible (false);

    // --- commands + menu ---
    commandManager.registerAllCommandsForTarget (this);
    // Pin the target: without this, commands resolve through keyboard focus
    // and go dead whenever a plugin editor or dialog is frontmost.
    commandManager.setFirstCommandTarget (this);
    addKeyListener (commandManager.getKeyMappings());

   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (this);
   #endif
    setApplicationCommandManagerToWatch (&commandManager);

    autoSave.onBeforeSnapshot = [this] { services.capturePluginState(); };

    updateWindowTitle();
    setSize (1440, 900);

    // Deferred so the prompt lands on top of a window that already exists.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        if (safe != nullptr)
            safe->offerCrashRecovery();
    });

    // Debug hooks (used by scripts/e2e and screenshot verification).
    const auto showList = juce::SystemStats::getEnvironmentVariable ("EURYDICE_SHOW", "");
    if (showList.contains ("pianoroll")) pianoRollPanel->bringToFrontAndShow();
    if (showList.contains ("mixer"))     mixerPanel->bringToFrontAndShow();
    if (showList.contains ("export"))    juce::Timer::callAfterDelay (300, [this] { showExportDialog(); });

    // EURYDICE_EDITOR=<channel index>|synth|kick|plugin opens that channel's
    // editor. "plugin" adds a channel hosting the first instrument in the
    // database and retries until the async load lands.
    const auto editorIndex = juce::SystemStats::getEnvironmentVariable ("EURYDICE_EDITOR", "");
    if (editorIndex.isNotEmpty())
    {
        juce::Timer::callAfterDelay (400, [this, editorIndex]
        {
            if (editorIndex == "plugin")
            {
                const auto instruments = services.plugins.getInstruments();
                if (instruments.isEmpty())
                    return;

                auto channel = services.project.addChannel ("plugin", "Bass");
                channel.setProperty (ids::pluginId,
                                     instruments.getReference (0).createIdentifierString(), nullptr);
                // The instance arrives asynchronously; retry at a few offsets
                // and only call show() once it is actually there.
                for (const int delayMs : { 1500, 3000, 6000, 10000 })
                    juce::Timer::callAfterDelay (delayMs, [this, channel]
                    {
                        auto gen = std::dynamic_pointer_cast<PluginGenerator> (
                            services.generators.getOrCreate (channel));
                        const bool open = juce::TopLevelWindow::getNumTopLevelWindows() > 1;
                        if (gen != nullptr && gen->getPlugin() != nullptr && ! open)
                            channelEditors.show (services, channel);
                    });
                return;
            }
            auto channel = editorIndex == "synth" ? services.project.addChannel ("synth", "Lead")
                         : editorIndex == "kick"  ? services.project.addChannel ("kick", "Kick Synth")
                                                  : services.project.getChannel (editorIndex.getIntValue());
            channelEditors.show (services, channel);
        });
    }

    // EURYDICE_FX_EDITOR=<insert>:<slot>:<builtin id> fills that slot and opens
    // its editor window.
    const auto fxEditor = juce::SystemStats::getEnvironmentVariable ("EURYDICE_FX_EDITOR", "");
    if (fxEditor.isNotEmpty())
    {
        juce::Timer::callAfterDelay (400, [this, fxEditor]
        {
            juce::StringArray parts;
            parts.addTokens (fxEditor, ":", "");
            if (parts.size() < 4)
                return;
            const int insertIndex = parts[0].getIntValue();
            const int slotIndex = parts[1].getIntValue();
            const auto* entry = fx::findBuiltin (parts[2] + ":" + parts[3]);
            auto insert = services.project.getInsert (insertIndex);
            if (entry == nullptr || ! insert.isValid())
                return;

            juce::ValueTree slot (ids::SLOT);
            slot.setProperty (ids::slotIndex, slotIndex, nullptr);
            slot.setProperty (ids::bypass, false, nullptr);
            slot.setProperty (ids::pluginId, entry->id, nullptr);
            BuiltinEffect::writeDefaults (slot, entry->specs, nullptr);
            insert.appendChild (slot, nullptr);

            services.builtinEditors.show (services.project, slot, *entry, insertIndex, slotIndex,
                                          insert[ids::name].toString() + " / " + entry->name,
                                          services.builtinEffects.peek (insertIndex, slotIndex));
        });
    }

    const auto shotPath = juce::SystemStats::getEnvironmentVariable ("EURYDICE_SCREENSHOT", "");
    if (shotPath.isNotEmpty())
    {
        const auto shotDelay = juce::SystemStats::getEnvironmentVariable ("EURYDICE_SCREENSHOT_DELAY", "1500")
                                   .getIntValue();
        juce::Timer::callAfterDelay (shotDelay, [this, shotPath]
        {
            for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
                if (auto* w = juce::TopLevelWindow::getTopLevelWindow (i); w->isVisible())
                    std::cout << "WINDOW " << w->getName() << "\n" << std::flush;
            if (writeSnapshot (juce::File (shotPath)))
                std::cout << "SCREENSHOT_SAVED " << shotPath << "\n" << std::flush;
        });
    }

    if (juce::SystemStats::getEnvironmentVariable ("EURYDICE_SCAN", "") == "1")
    {
        services.plugins.startScan ([this]
        {
            for (const auto& d : services.plugins.getKnownPlugins().getTypes())
                std::cout << "PLUGIN\t" << (d.isInstrument ? "inst" : "fx") << "\t"
                          << d.pluginFormatName << "\t" << d.name << "\t"
                          << d.createIdentifierString() << "\n" << std::flush;
            std::cout << "SCAN_DONE " << services.plugins.getKnownPlugins().getNumTypes() << "\n" << std::flush;
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        });
    }

    // EURYDICE_DOCKCHECK=1 docks panels into zones and reports the result.
    if (juce::SystemStats::getEnvironmentVariable ("EURYDICE_DOCKCHECK", "") == "1")
    {
        juce::Timer::callAfterDelay (700, [this]
        {
            mixerPanel->setVisible (true);
            pianoRollPanel->setVisible (true);

            const auto area = desktop.getLocalBounds();
            auto dockTo = [&] (FloatingPanel* panel, docking::Zone zone)
            {
                panel->setBounds (docking::boundsForZone (zone, area));
            };
            dockTo (channelRackPanel.get(), docking::Zone::left);
            dockTo (playlistPanel.get(), docking::Zone::topRight);
            dockTo (mixerPanel.get(), docking::Zone::bottomRight);
            pianoRollPanel->setVisible (false);

            std::cout << "DESKTOP " << area.toString() << "\n"
                      << "RACK " << channelRackPanel->getBounds().toString() << "\n"
                      << "PLAYLIST " << playlistPanel->getBounds().toString() << "\n"
                      << "MIXER " << mixerPanel->getBounds().toString() << "\n" << std::flush;
        });
    }

    // EURYDICE_RESETCHECK=1 drags a panel off-screen and resets it.
    if (juce::SystemStats::getEnvironmentVariable ("EURYDICE_RESETCHECK", "") == "1")
    {
        juce::Timer::callAfterDelay (600, [this]
        {
            const auto expected = defaultBoundsFor (mixerPanel.get());
            mixerPanel->setVisible (true);
            mixerPanel->setBounds (-4000, -3000, 400, 300);
            std::cout << "LOST " << mixerPanel->getBounds().toString() << "\n" << std::flush;

            commandManager.invokeDirectly (CommandIDs::viewResetLayout, false);
            std::cout << "AFTER_RESET " << mixerPanel->getBounds().toString()
                      << " expected " << expected.toString()
                      << " match=" << (mixerPanel->getBounds() == expected ? 1 : 0)
                      << " visible=" << (mixerPanel->isVisible() ? 1 : 0) << "\n" << std::flush;
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        });
    }

    // Debug hook: EURYDICE_UICHECK=1 exercises the menu model and the
    // channel-editor route, printing what a user would be able to reach.
    if (juce::SystemStats::getEnvironmentVariable ("EURYDICE_UICHECK", "") == "1")
    {
        juce::Timer::callAfterDelay (600, [this]
        {
            for (int i = 0; i < getMenuBarNames().size(); ++i)
            {
                auto menu = getMenuForIndex (i, getMenuBarNames()[i]);
                juce::StringArray items;
                for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                    if (it.getItem().text.isNotEmpty())
                        items.add (it.getItem().text + (it.getItem().isEnabled ? "" : " (disabled)"));
                std::cout << "MENU " << getMenuBarNames()[i] << ": "
                          << items.joinIntoString (" | ") << "\n" << std::flush;
            }

            for (int i = 0; i < services.project.numChannels(); ++i)
            {
                auto channel = services.project.getChannel (i);
                channelEditors.show (services, channel);
            }
            int editorWindows = 0;
            for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
                if (juce::TopLevelWindow::getTopLevelWindow (i)->isVisible())
                    ++editorWindows;
            std::cout << "TOPLEVEL_WINDOWS " << editorWindows << "\n" << std::flush;

            commandManager.invokeDirectly (CommandIDs::viewMixer, false);
            commandManager.invokeDirectly (CommandIDs::viewPianoRoll, false);
            std::cout << "PANELS mixer=" << (mixerPanel->isVisible() ? 1 : 0)
                      << " pianoroll=" << (pianoRollPanel->isVisible() ? 1 : 0)
                      << " title=" << fileState.getWindowTitle() << "\n" << std::flush;
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        });
    }

    if (juce::SystemStats::getEnvironmentVariable ("EURYDICE_AUTOPLAY", "") == "1")
    {
        juce::Timer::callAfterDelay (800, [this] { services.engine.play(); });
        juce::Timer::callAfterDelay (2800, [this]
        {
            std::cout << "AUTOPLAY peakL=" << services.engine.getMasterPeak (0)
                      << " peakR=" << services.engine.getMasterPeak (1)
                      << " beats=" << services.engine.getPositionBeats()
                      << " playing=" << (services.engine.isPlaying() ? 1 : 0) << "\n" << std::flush;
        });
    }
}

MainComponent::~MainComponent()
{
   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);
   #endif
    // The panels die before AppServices does, so drop the callback that
    // reaches back into them.
    services.onAutomationClipCreated = nullptr;
    services.onSnapshotRequested = nullptr;
    services.onCloseChannelEditors = nullptr;
    services.onShowPanelRequested = nullptr;
    services.onRecordArmRequested = nullptr;
    midiInput->onLiveNote = nullptr;
    fileState.removeChangeListener (this);
    removeKeyListener (commandManager.getKeyMappings());
    commandManager.setFirstCommandTarget (nullptr);
    channelEditors.closeAll();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

// ---------------- menu bar ----------------

juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Edit", "View", "Automation", "Options" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu menu;

    if (index == 0)
    {
        menu.addCommandItem (&commandManager, CommandIDs::fileNew);
        menu.addCommandItem (&commandManager, CommandIDs::fileOpen);

        juce::PopupMenu recentMenu;
        recentFiles.createPopupMenuItems (recentMenu, recentFilesBaseId,
                                          false /* names, not full paths */, true);
        if (recentFiles.getNumFiles() > 0)
        {
            recentMenu.addSeparator();
            recentMenu.addItem (recentFilesClearId, "Clear Menu");
        }
        menu.addSubMenu ("Recent Projects", recentMenu, recentFiles.getNumFiles() > 0);

        menu.addSeparator();
        menu.addCommandItem (&commandManager, CommandIDs::fileSave);
        menu.addCommandItem (&commandManager, CommandIDs::fileSaveAs);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, CommandIDs::fileExport);
    }
    else if (index == 1)
    {
        menu.addCommandItem (&commandManager, CommandIDs::editUndo);
        menu.addCommandItem (&commandManager, CommandIDs::editRedo);
    }
    else if (index == 2)
    {
        menu.addCommandItem (&commandManager, CommandIDs::viewPlaylist);
        menu.addCommandItem (&commandManager, CommandIDs::viewChannelRack);
        menu.addCommandItem (&commandManager, CommandIDs::viewPianoRoll);
        menu.addCommandItem (&commandManager, CommandIDs::viewMixer);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, CommandIDs::viewBrowser);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, CommandIDs::viewResetLayout);
    }
    else if (index == 3)
    {
        menu.addCommandItem (&commandManager, CommandIDs::transportToggleAutomationWrite);
        menu.addSeparator();

        int count = 0;
        for (const auto source : services.project.automations())
            if (source.hasType (ids::AUTOMATION))
                menu.addItem (automationBaseId + count++,
                              "Edit " + source[ids::name].toString());

        if (count == 0)
            menu.addItem (automationBaseId + 999,
                          "Right-click any knob to create a clip", false, false);
    }
    else if (index == 4)
    {
        menu.addCommandItem (&commandManager, CommandIDs::transportPlayStop);
        menu.addCommandItem (&commandManager, CommandIDs::transportRewind);
        menu.addCommandItem (&commandManager, CommandIDs::transportToggleSongMode);
        menu.addCommandItem (&commandManager, CommandIDs::transportToggleLoop);
        menu.addCommandItem (&commandManager, CommandIDs::transportToggleRecord);
        menu.addCommandItem (&commandManager, CommandIDs::transportToggleMetronome);
        menu.addCommandItem (&commandManager, CommandIDs::transportCountIn);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, CommandIDs::optionsAudioSettings);
        menu.addCommandItem (&commandManager, CommandIDs::optionsScanPlugins);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, CommandIDs::optionsSandboxEffects);
    }

    return menu;
}

void MainComponent::menuItemSelected (int menuItemID, int)
{
    if (menuItemID == recentFilesClearId)
    {
        recentFiles.clear();
        settings->setValue ("recentFiles", recentFiles.toString());
        settings->saveIfNeeded();
        menuItemsChanged();
        return;
    }
    if (menuItemID >= recentFilesBaseId && menuItemID < recentFilesBaseId + 100)
    {
        const auto file = recentFiles.getFile (menuItemID - recentFilesBaseId);
        if (file.existsAsFile() && okToCloseProject ("opening another project"))
            loadProjectFile (file);
        return;
    }

    if (menuItemID >= automationBaseId && menuItemID < automationBaseId + 999)
        openAutomationEditor (menuItemID - automationBaseId);
}

// index counts only AUTOMATION children, matching how the menu was built.
void MainComponent::openAutomationEditor (int index)
{
    int count = 0;
    for (const auto source : services.project.automations())
    {
        if (! source.hasType (ids::AUTOMATION))
            continue;
        if (count++ != index)
            continue;
        const auto clip = AutomationWriter::findClip (services.project, (int) source[ids::id]);
        AutomationEditor::open (services, source,
                                clip.isValid() ? (int) clip[ids::lengthTicks] : 4 * ids::ticksPerBar);
        return;
    }
}

// ---------------- commands ----------------

void MainComponent::getAllCommands (juce::Array<juce::CommandID>& commands)
{
    commands.addArray ({
        CommandIDs::fileNew, CommandIDs::fileOpen, CommandIDs::fileSave,
        CommandIDs::fileSaveAs, CommandIDs::fileExport,
        CommandIDs::editUndo, CommandIDs::editRedo,
        CommandIDs::viewPlaylist, CommandIDs::viewChannelRack,
        CommandIDs::viewPianoRoll, CommandIDs::viewMixer, CommandIDs::viewBrowser,
        CommandIDs::viewResetLayout,
        CommandIDs::transportPlayStop, CommandIDs::transportRewind,
        CommandIDs::transportToggleSongMode, CommandIDs::transportToggleRecord,
        CommandIDs::transportToggleLoop, CommandIDs::transportToggleAutomationWrite,
        CommandIDs::transportToggleLoop, CommandIDs::transportToggleMetronome,
        CommandIDs::transportCountIn,
        CommandIDs::optionsAudioSettings, CommandIDs::optionsScanPlugins,
        CommandIDs::optionsSandboxEffects });
}

void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    const auto cmd = juce::ModifierKeys::commandModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;

    // Panel toggles get a Cmd+digit primary shortcut (macOS eats bare F-keys
    // unless the user opts into standard function keys) plus the FL-style
    // F-key as a secondary binding for muscle memory.
    auto panelCommand = [&info] (const juce::String& name, bool visible, int digit, int fKey)
    {
        info.setInfo (name, "Show or hide the " + name.toLowerCase(), "View", 0);
        info.addDefaultKeypress ((juce::juce_wchar) ('0' + digit), juce::ModifierKeys::commandModifier);
        info.addDefaultKeypress (fKey, juce::ModifierKeys::noModifiers);
        info.setTicked (visible);
    };

    switch (id)
    {
        case CommandIDs::fileNew:
            info.setInfo ("New Project", "Start an empty project", "File", 0);
            info.addDefaultKeypress ('n', cmd);
            break;
        case CommandIDs::fileOpen:
            info.setInfo ("Open...", "Open a .eury project", "File", 0);
            info.addDefaultKeypress ('o', cmd);
            break;
        case CommandIDs::fileSave:
            info.setInfo ("Save", "Save the project", "File", 0);
            info.addDefaultKeypress ('s', cmd);
            break;
        case CommandIDs::fileSaveAs:
            info.setInfo ("Save As...", "Save the project to a new file", "File", 0);
            info.addDefaultKeypress ('s', cmd | shift);
            break;
        case CommandIDs::fileExport:
            info.setInfo ("Export Audio...", "Render to WAV/MP3/stems", "File", 0);
            info.addDefaultKeypress ('r', cmd);
            break;

        case CommandIDs::editUndo:
            info.setInfo ("Undo", "Undo the last edit", "Edit", 0);
            info.addDefaultKeypress ('z', cmd);
            info.setActive (services.project.getUndoManager().canUndo());
            break;
        case CommandIDs::editRedo:
            info.setInfo ("Redo", "Redo the last undone edit", "Edit", 0);
            info.addDefaultKeypress ('z', cmd | shift);
            info.setActive (services.project.getUndoManager().canRedo());
            break;

        case CommandIDs::viewPlaylist:
            panelCommand ("Playlist", playlistPanel && playlistPanel->isVisible(), 1, juce::KeyPress::F5Key);
            break;
        case CommandIDs::viewChannelRack:
            panelCommand ("Channel Rack", channelRackPanel && channelRackPanel->isVisible(), 2, juce::KeyPress::F6Key);
            break;
        case CommandIDs::viewPianoRoll:
            panelCommand ("Piano Roll", pianoRollPanel && pianoRollPanel->isVisible(), 3, juce::KeyPress::F7Key);
            break;
        case CommandIDs::viewMixer:
            panelCommand ("Mixer", mixerPanel && mixerPanel->isVisible(), 4, juce::KeyPress::F9Key);
            break;
        case CommandIDs::viewBrowser:
            info.setInfo ("Browser", "Show or hide the browser", "View", 0);
            info.addDefaultKeypress ('b', cmd);
            info.setTicked (browserVisible);
            break;

        case CommandIDs::viewResetLayout:
            info.setInfo ("Reset Panel Positions", "Move every panel back to its default place",
                          "View", 0);
            break;

        case CommandIDs::transportPlayStop:
            info.setInfo ("Play / Stop", "Start or stop playback", "Transport", 0);
            info.addDefaultKeypress (juce::KeyPress::spaceKey, juce::ModifierKeys::noModifiers);
            break;
        case CommandIDs::transportRewind:
            info.setInfo ("Rewind to Start", "Move the playhead to the beginning", "Transport", 0);
            info.addDefaultKeypress (juce::KeyPress::homeKey, juce::ModifierKeys::noModifiers);
            break;
        case CommandIDs::transportToggleSongMode:
            info.setInfo ("Song Mode", "Play the playlist instead of the current pattern", "Transport", 0);
            info.addDefaultKeypress ('l', cmd);
            info.setTicked (services.project.isSongMode());
            break;
        case CommandIDs::transportToggleRecord:
            info.setInfo ("Arm Recording", "Record MIDI and audio input while playing", "Transport", 0);
            info.addDefaultKeypress ('e', cmd);
            info.setTicked (midiInput != nullptr && midiInput->recordArmed.load());
            break;
        case CommandIDs::transportToggleLoop:
            info.setInfo ("Toggle Loop", "Loop the range marked in the playlist ruler", "Transport", 0);
            info.addDefaultKeypress ('l', cmd | shift);
            info.setTicked (services.project.isLoopEnabled());
            break;
        case CommandIDs::transportToggleAutomationWrite:
            info.setInfo ("Write Automation (AUTO)",
                          "While playing, record every knob you move into its automation clip",
                          "Automation", 0);
            info.addDefaultKeypress ('a', cmd | shift);
            info.setTicked (services.automationWriter.isArmed());
            break;

        case CommandIDs::transportToggleMetronome:
            info.setInfo ("Metronome", "Click on every beat, accented on the bar", "Transport", 0);
            info.addDefaultKeypress ('m', cmd | shift);
            info.setTicked (services.engine.isMetronomeEnabled());
            break;
        case CommandIDs::transportCountIn:
        {
            const int bars = services.engine.getCountInBars();
            info.setInfo (bars == 0 ? "Count-in: off"
                                    : "Count-in: " + juce::String (bars) + (bars == 1 ? " bar" : " bars"),
                          "Click one or two bars before armed recording starts", "Transport", 0);
            info.setTicked (bars > 0);
            break;
        }

        case CommandIDs::optionsAudioSettings:
            info.setInfo ("Audio & MIDI Settings...", "Choose the audio device", "Options", 0);
            info.addDefaultKeypress (',', cmd);
            break;
        case CommandIDs::optionsScanPlugins:
            info.setInfo ("Scan for Plugins", "Search for VST3 and AU plugins", "Options", 0);
            info.setActive (! services.plugins.isScanning());
            break;
        case CommandIDs::optionsSandboxEffects:
            info.setInfo ("Sandbox Plugins",
                          "Load plugins (effects and instruments) in separate processes so a "
                          "crash can't take the DAW down. Applies to plugins loaded from now on.",
                          "Options", 0);
            info.setTicked (services.effects.isSandboxEnabled());
            break;
        default:
            break;
    }
}

FloatingPanel* MainComponent::panelForCommand (juce::CommandID id) const
{
    switch (id)
    {
        case CommandIDs::viewPlaylist:    return playlistPanel.get();
        case CommandIDs::viewChannelRack: return channelRackPanel.get();
        case CommandIDs::viewPianoRoll:   return pianoRollPanel.get();
        case CommandIDs::viewMixer:       return mixerPanel.get();
        default:                          return nullptr;
    }
}

bool MainComponent::perform (const juce::ApplicationCommandTarget::InvocationInfo& info)
{
    switch (info.commandID)
    {
        case CommandIDs::fileNew:     newProject(); return true;
        case CommandIDs::fileOpen:    openProjectInteractive(); return true;
        case CommandIDs::fileSave:    saveProject (false); return true;
        case CommandIDs::fileSaveAs:  saveProject (true); return true;
        case CommandIDs::fileExport:  showExportDialog(); return true;

        case CommandIDs::editUndo:
            services.project.getUndoManager().undo();
            commandManager.commandStatusChanged();
            return true;
        case CommandIDs::editRedo:
            services.project.getUndoManager().redo();
            commandManager.commandStatusChanged();
            return true;

        case CommandIDs::viewPlaylist:
        case CommandIDs::viewChannelRack:
        case CommandIDs::viewPianoRoll:
        case CommandIDs::viewMixer:
            if (auto* panel = panelForCommand (info.commandID))
            {
                panel->toggleVisibility();
                transportBar.refreshPanelButtons();
                commandManager.commandStatusChanged();
            }
            return true;

        case CommandIDs::viewBrowser:
            browserVisible = ! browserVisible;
            browser->setVisible (browserVisible);
            resized();
            transportBar.refreshPanelButtons();
            commandManager.commandStatusChanged();
            return true;

        case CommandIDs::viewResetLayout:
            layoutDefaultPanelPositions();
            for (auto* panel : { playlistPanel.get(), channelRackPanel.get(),
                                 pianoRollPanel.get(), mixerPanel.get() })
                panel->toFront (false);
            transportBar.refreshPanelButtons();
            return true;

        case CommandIDs::transportPlayStop:
            if (services.engine.isPlaying()) transportStop(); else transportPlay();
            return true;
        case CommandIDs::transportRewind:
            services.engine.setPositionTicks (0.0);
            return true;
        case CommandIDs::transportToggleSongMode:
            services.project.setSongMode (! services.project.isSongMode());
            transportBar.setSongMode (services.project.isSongMode());
            commandManager.commandStatusChanged();
            return true;
        case CommandIDs::transportToggleRecord:
            toggleRecordArm();
            commandManager.commandStatusChanged();
            return true;
        case CommandIDs::transportToggleLoop:
            services.project.setLoopEnabled (! services.project.isLoopEnabled());
            commandManager.commandStatusChanged();
            return true;
        case CommandIDs::transportToggleAutomationWrite:
            services.automationWriter.setArmed (! services.automationWriter.isArmed());
            commandManager.commandStatusChanged();
            menuItemsChanged();
            return true;

        case CommandIDs::transportToggleMetronome:
        {
            const bool on = ! services.engine.isMetronomeEnabled();
            services.engine.setMetronomeEnabled (on);
            settings->setValue ("metronomeEnabled", on);
            commandManager.commandStatusChanged();
            return true;
        }
        case CommandIDs::transportCountIn:
        {
            const int bars = (services.engine.getCountInBars() + 1) % 3;
            services.engine.setCountInBars (bars);
            settings->setValue ("countInBars", bars);
            commandManager.commandStatusChanged();
            menuItemsChanged();
            return true;
        }

        case CommandIDs::optionsAudioSettings: showAudioSettings(); return true;
        case CommandIDs::optionsScanPlugins:
            services.plugins.startScan ([this] { commandManager.commandStatusChanged(); });
            commandManager.commandStatusChanged();
            return true;

        case CommandIDs::optionsSandboxEffects:
            services.effects.setSandboxEnabled (! services.effects.isSandboxEnabled());
            services.generators.setSandboxEnabled (services.effects.isSandboxEnabled());
            settings->setValue ("sandboxEffects", services.effects.isSandboxEnabled());
            settings->saveIfNeeded();
            commandManager.commandStatusChanged();
            return true;

        default: return false;
    }
}

// ---------------- project files ----------------

bool MainComponent::okToCloseProject (const juce::String& action)
{
    if (! fileState.isDirty())
    {
        autoSave.clearRecovery();
        return true;
    }

    const int result = juce::AlertWindow::showYesNoCancelBox (
        juce::MessageBoxIconType::WarningIcon, "Unsaved changes",
        "\"" + fileState.getDisplayName() + "\" has unsaved changes.\n\nSave before " + action + "?",
        "Save", "Discard", "Cancel");

    if (result == 0)   // cancel
        return false;
    if (result == 1)   // save
    {
        saveProject (false);
        return ! fileState.isDirty();
    }
    autoSave.clearRecovery();   // discarded on purpose, so do not offer it back
    return true;
}

void MainComponent::offerCrashRecovery()
{
    AutoSaver::garbageCollect (autoSave.getDirectory());
    const auto pending = AutoSaver::findPending (autoSave.getDirectory());
    if (pending.isEmpty())
        return;

    const auto recovery = pending.getFirst();
    const auto shadowed = AutoSaver::projectShadowedBy (recovery);
    const auto displayName = shadowed == juce::File() ? juce::String ("Untitled")
                                                      : shadowed.getFileName();

    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon,
        "Recover unsaved work",
        "Eurydice did not shut down cleanly. An autosave of \"" + displayName + "\" from "
            + recovery.getLastModificationTime().toString (true, true) + " is available.\n\n"
              "Restore it?",
        "Restore", "Discard", nullptr,
        juce::ModalCallbackFunction::create (
            [safe = juce::Component::SafePointer<MainComponent> (this), recovery] (int result)
            {
                if (safe == nullptr)
                    return;
                if (result == 1)
                    safe->restoreFromRecovery (recovery);
                else
                    recovery.deleteFile();
            }));
}

void MainComponent::restoreFromRecovery (const juce::File& recoveryFile)
{
    const auto shadowed = AutoSaver::projectShadowedBy (recoveryFile);

    if (! services.loadProject (recoveryFile))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
            "Recovery failed", "Could not read the autosave file.");
        return;
    }

    services.project.getRoot().removeProperty (ids::recoveryOf, nullptr);
    fileState.markRestored (shadowed);
}

void MainComponent::newProject()
{
    if (! okToCloseProject ("starting a new project"))
        return;
    services.newProject();
    fileState.markNewProject();
}

void MainComponent::loadProjectFile (const juce::File& file)
{
    if (! services.loadProject (file))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
            "Open failed", "Could not read " + file.getFileName());
        return;
    }
    fileState.markLoaded (file);
    autoSave.clearRecovery();
    recentFiles.addFile (file);
    settings->setValue ("recentFiles", recentFiles.toString());
    settings->saveIfNeeded();
    menuItemsChanged();
}

void MainComponent::openProjectInteractive()
{
    if (! okToCloseProject ("opening another project"))
        return;

    auto chooser = std::make_shared<juce::FileChooser> ("Open project",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.eury");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser] (const juce::FileChooser& fc)
        {
            if (fc.getResult().existsAsFile())
                loadProjectFile (fc.getResult());
        });
}

void MainComponent::saveProject (bool forceChooser)
{
    const auto existing = fileState.getFile();
    if (! forceChooser && existing != juce::File())
    {
        if (services.saveProject (existing))
        {
            fileState.markSaved (existing);
            autoSave.clearRecovery();
            recentFiles.addFile (existing);
            settings->setValue ("recentFiles", recentFiles.toString());
        }
        return;
    }

    auto chooser = std::make_shared<juce::FileChooser> ("Save project",
        existing != juce::File()
            ? existing
            : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                  .getChildFile ("Untitled.eury"),
        "*.eury");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, chooser] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File())
                return;
            const auto target = file.withFileExtension (".eury");
            if (services.saveProject (target))
            {
                fileState.markSaved (target);
                autoSave.clearRecovery();
                recentFiles.addFile (target);
                settings->setValue ("recentFiles", recentFiles.toString());
                settings->saveIfNeeded();
                menuItemsChanged();
            }
        });
}

void MainComponent::showExportDialog()
{
    const bool loopAvailable = services.project.getLoopEnd() > services.project.getLoopStart();
    auto panel = std::make_unique<ExportOptionsPanel> (loopAvailable,
                                                       OfflineRenderer::findLameBinary() != juce::File());
    panel->setSize (ExportOptionsPanel::preferredWidth, ExportOptionsPanel::preferredHeight);
    panel->onExport = [this] (const OfflineRenderer::Options& options) { chooseExportFile (options); };

    juce::DialogWindow::LaunchOptions dialog;
    dialog.content.setOwned (panel.release());
    dialog.dialogTitle = "Export Audio";
    dialog.dialogBackgroundColour = theme::panelBg;
    dialog.escapeKeyTriggersCloseButton = true;
    dialog.resizable = false;
    dialog.launchAsync();
}

void MainComponent::chooseExportFile (const OfflineRenderer::Options& options)
{
    auto chooser = std::make_shared<juce::FileChooser> ("Export render",
        juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
            .getChildFile (fileState.getDisplayName() + ".wav"),
        "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser, options] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File())
                return;

            auto renderOptions = options;
            renderOptions.wavFile = file.withFileExtension (".wav");

            const auto result = OfflineRenderer::render (services.engine, services.project, renderOptions);
            juce::AlertWindow::showMessageBoxAsync (
                result.ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                "Export",
                result.ok ? "Rendered:\n" + result.writtenFiles.joinIntoString ("\n")
                              + (result.error.isNotEmpty() ? "\n\n" + result.error : juce::String())
                          : result.error);
        });
}

void MainComponent::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (
        services.engine.getDeviceManager(), 1, 2, 2, 2, true, false, true, false);
    selector->setSize (480, 400);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = "Audio & MIDI Settings";
    options.dialogBackgroundColour = theme::panelBg;
    options.escapeKeyTriggersCloseButton = true;
    options.resizable = false;
    options.launchAsync();
}

// ---------------- transport ----------------

void MainComponent::transportPlay()
{
    const bool armed = midiInput->recordArmed.load();
    if (! armed)
    {
        services.engine.play();
        return;
    }

    services.engine.playWithCountIn();

    const bool wantsTake = services.project.isSongMode() && ! recorder->isRecording();
    // The take must start where the music does, so it waits out the count-in.
    const int countInMs = (int) (services.engine.getCountInBars() * 4.0 * 60000.0
                                 / juce::jmax (1.0, services.project.getTempo()));
    if (wantsTake && countInMs <= 0)
        recorder->start();
    else if (wantsTake)
        juce::Timer::callAfterDelay (countInMs, [safeThis = juce::Component::SafePointer (this)]
        {
            if (safeThis == nullptr)
                return;
            if (safeThis->services.engine.isPlaying() && ! safeThis->recorder->isRecording())
                safeThis->recorder->start();
        });
}

void MainComponent::toggleRecordArm()
{
    const bool arming = ! midiInput->recordArmed.load();

    // Arm first: MIDI recording and playback must not wait on the mic.
    midiInput->recordArmed.store (arming);
    transportBar.setRecordArmed (arming);

    if (! arming)
        return;

    // The device runs output-only until recording needs the input (opening the
    // mic at startup meant a combined device and a crashy CoreAudio race).
    // Permission has to be settled BEFORE the reopen: the CoreAudio HAL raises
    // the TCC prompt from inside the blocking device open, which froze all
    // audio — playback and preview included — until the dialog was answered.
    micpermission::request ([safeThis = juce::Component::SafePointer (this)] (bool granted)
    {
        if (safeThis == nullptr || ! safeThis->midiInput->recordArmed.load())
            return;   // disarmed while the prompt was up

        if (! granted)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                "Record", "Microphone access is off for Eurydice.\n"
                          "MIDI recording still works; for audio recording allow the microphone in "
                          "System Settings > Privacy & Security > Microphone.");
            return;
        }

        const auto err = safeThis->services.engine.setInputEnabled (true);
        if (err.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                "Record", "Could not open the audio input: " + err
                          + "\nMIDI recording still works; audio recording needs an input device.");
    });
}

void MainComponent::transportStop()
{
    services.engine.stop();
    services.automationWriter.finaliseAll();
    if (recorder->isRecording())
        recorder->stopAndPlaceClip();
}

bool MainComponent::writeSnapshot (const juce::File& file)
{
    // If a separate editor window is open, capture that instead — it is what
    // the caller is almost always asking about.
    juce::Component* target = this;
    for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
    {
        auto* window = juce::TopLevelWindow::getTopLevelWindow (i);
        if (window != nullptr && window->isVisible()
            && window != findParentComponentOfClass<juce::DocumentWindow>())
            target = window;
    }

    const auto image = target->createComponentSnapshot (target->getLocalBounds());
    if (! image.isValid())
        return false;

    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::FileOutputStream out (file);
    if (! out.openedOk())
        return false;

    juce::PNGImageFormat png;
    return png.writeImageToStream (image, out);
}

// ---------------- window / layout ----------------

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateWindowTitle();
}

void MainComponent::updateWindowTitle()
{
    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
        window->setName (fileState.getWindowTitle());
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::desktopBg);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    transportBar.setBounds (r.removeFromTop (TransportBar::preferredHeight));
    if (browserVisible)
        browser->setBounds (r.removeFromLeft (browserWidth));
    if (browserResizer != nullptr)
    {
        browserResizer->setVisible (browserVisible);
        if (browserVisible)
            browserResizer->setBounds (browser->getRight() - 3, r.getY(), 6, r.getHeight());
    }
    desktop.setBounds (r);

    if (! initialLayoutDone && desktop.getWidth() > 0)
    {
        layoutDefaultPanelPositions();
        initialLayoutDone = true;
        updateWindowTitle();
    }
}

void MainComponent::childBoundsChanged (juce::Component* child)
{
    // The browser's edge handle resizes the browser directly; fold the new
    // width back into the layout so the desktop reflows with it.
    if (child == browser.get() && browserVisible
        && browser->getWidth() != browserWidth && browser->getWidth() > 0)
    {
        browserWidth = browser->getWidth();
        settings->setValue ("browserWidth", browserWidth);
        resized();
    }
}

// The typing piano. Command shortcuts are handled by the command manager's
// key listener first, so only unbound plain keys reach here. The logic lives
// in TypingPiano so editor windows can share it.
bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    return typingPiano->keyPressed (key, this);
}

bool MainComponent::keyStateChanged (bool isKeyDown)
{
    return typingPiano->keyStateChanged (isKeyDown, this);
}

juce::Rectangle<int> MainComponent::defaultBoundsFor (const FloatingPanel* panel) const
{
    const int w = desktop.getWidth();
    const int h = desktop.getHeight();
    const auto wf = (float) w;
    const auto hf = (float) h;

    if (panel == playlistPanel.get())
        return { juce::jmax (0, w - (int) (wf * 0.62f) - 12), 12,
                 (int) (wf * 0.62f), (int) (hf * 0.55f) };
    if (panel == channelRackPanel.get())
        return { 12, 12, 700, 460 };
    if (panel == pianoRollPanel.get())
        return { 60, 80, (int) (wf * 0.7f), (int) (hf * 0.65f) };
    if (panel == mixerPanel.get())
        return { 40, juce::jmax (0, h - 360), w - 80, 340 };

    return { 40, 40, 640, 420 };
}

void MainComponent::layoutDefaultPanelPositions()
{
    for (auto* panel : { playlistPanel.get(), channelRackPanel.get(),
                         pianoRollPanel.get(), mixerPanel.get() })
        panel->setBounds (defaultBoundsFor (panel));
}

void MainComponent::resetPanelPosition (FloatingPanel* panel)
{
    if (panel == nullptr)
        return;
    panel->setBounds (defaultBoundsFor (panel));
    panel->bringToFrontAndShow();
    transportBar.refreshPanelButtons();
}

void MainComponent::showPanelContextMenu (juce::CommandID commandID)
{
    auto* panel = panelForCommand (commandID);

    juce::PopupMenu menu;
    if (panel != nullptr)
    {
        menu.addItem (1, panel->isVisible() ? "Hide" : "Show");
        menu.addSeparator();
        menu.addItem (2, "Reset position");
    }
    menu.addItem (3, "Reset all panel positions");

    menu.showMenuAsync ({}, [this, commandID, panel] (int result)
    {
        if (result == 1)
            commandManager.invokeDirectly (commandID, false);
        else if (result == 2)
            resetPanelPosition (panel);
        else if (result == 3)
            commandManager.invokeDirectly (CommandIDs::viewResetLayout, false);
    });
}
