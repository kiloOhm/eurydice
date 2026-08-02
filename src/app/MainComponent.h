#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AppServices.h"
#include "AutoSaver.h"
#include "Commands.h"
#include "EurydiceLookAndFeel.h"
#include "MidiInputManager.h"
#include "ProjectFileState.h"
#include "TransportBar.h"
#include "ui/common/FloatingPanel.h"
#include "engine/OfflineRenderer.h"
#include "ui/rack/ChannelEditor.h"

// The whole app surface: menu bar, transport bar with panel toggles, browser
// docked left, and FL-style floating panels on the desktop area.
class MainComponent : public juce::Component,
                      public juce::ApplicationCommandTarget,
                      public juce::MenuBarModel,
                      private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int index, const juce::String& name) override;
    void menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

    // ApplicationCommandTarget
    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands (juce::Array<juce::CommandID>&) override;
    void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
    bool perform (const juce::ApplicationCommandTarget::InvocationInfo&) override;

    juce::ApplicationCommandManager& getCommandManager() { return commandManager; }

    // Called by the app before quitting; returns false to cancel the quit.
    bool okToCloseProject (const juce::String& action);

private:
    void layoutDefaultPanelPositions();
    juce::Rectangle<int> defaultBoundsFor (const FloatingPanel*) const;
    void resetPanelPosition (FloatingPanel*);
    void showPanelContextMenu (juce::CommandID);
    void showAudioSettings();
    void showExportDialog();
    void chooseExportFile (const OfflineRenderer::Options&);
    void saveProject (bool forceChooser);
    void openProjectInteractive();
    void newProject();
    void loadProjectFile (const juce::File&);
    void offerCrashRecovery();
    void restoreFromRecovery (const juce::File& recoveryFile);
    void transportPlay();
    void transportStop();
    void openAutomationEditor (int index);
    void updateWindowTitle();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    FloatingPanel* panelForCommand (juce::CommandID) const;

    EurydiceLookAndFeel lookAndFeel;
    AppServices services;
    ProjectFileState fileState { services.project };
    AutoSaver autoSave { services.project, fileState };
    juce::ApplicationCommandManager commandManager;
    juce::RecentlyOpenedFilesList recentFiles;
    std::unique_ptr<juce::PropertiesFile> settings;

    std::unique_ptr<class ControlServer> controlServer;
    std::unique_ptr<MidiInputManager> midiInput;
    std::unique_ptr<class AudioRecorder> recorder;
    ChannelEditorManager channelEditors;

    TransportBar transportBar;
    std::unique_ptr<juce::Component> browser;
    juce::Component desktop;

    std::unique_ptr<FloatingPanel> playlistPanel;
    class PlaylistPanel* playlistView = nullptr;   // owned by playlistPanel
    std::unique_ptr<FloatingPanel> channelRackPanel;
    std::unique_ptr<FloatingPanel> pianoRollPanel;
    std::unique_ptr<FloatingPanel> mixerPanel;

    bool initialLayoutDone = false;
    bool browserVisible = true;

    int typingOctaveShift = 0;
    std::map<juce::juce_wchar, int> typingKeysDown;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
