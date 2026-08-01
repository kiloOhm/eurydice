#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AppServices.h"
#include "EurydiceLookAndFeel.h"
#include "MidiInputManager.h"
#include "TransportBar.h"
#include "ui/common/FloatingPanel.h"

// The whole app surface: transport bar on top, browser docked left,
// FL-style floating panels (playlist, channel rack, piano roll, mixer)
// on the desktop area. F5/F6/F7/F9 toggle panels like FL.
class MainComponent : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

private:
    void layoutDefaultPanelPositions();
    void showAudioSettings();
    void showExportDialog();
    void saveProjectInteractive();
    void openProjectInteractive();

    EurydiceLookAndFeel lookAndFeel;
    AppServices services;
    std::unique_ptr<class ControlServer> controlServer;
    std::unique_ptr<MidiInputManager> midiInput;
    std::unique_ptr<class AudioRecorder> recorder;

    void transportPlay();
    void transportStop();

    int typingOctaveShift = 0;                 // Z/X in FL shifts octaves; we use ,/. keys
    std::map<juce::juce_wchar, int> typingKeysDown;   // char -> midi key currently sounding

    TransportBar transportBar;
    std::unique_ptr<juce::Component> browser;
    juce::Component desktop;   // hosts the floating panels

    std::unique_ptr<FloatingPanel> playlistPanel;      // F5
    std::unique_ptr<FloatingPanel> channelRackPanel;   // F6
    std::unique_ptr<FloatingPanel> pianoRollPanel;     // F7
    std::unique_ptr<FloatingPanel> mixerPanel;         // F9

    bool initialLayoutDone = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
