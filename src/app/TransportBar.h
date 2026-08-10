#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Commands.h"
#include "MasterScope.h"
#include "ui/common/IconButton.h"

// Top strip: play/stop/record, pattern/song mode, BPM, position readout.
// Wired to the engine through std::function hooks so it stays engine-agnostic.
class TransportBar : public juce::Component,
                     private juce::Timer
{
public:
    TransportBar();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    std::function<void()> onPlay, onStop, onRecordToggled, onMetronomeToggled;
    std::function<void (double)> onTempoChanged, onMetronomeLevelChanged;
    std::function<void (bool)> onSongModeChanged;

    // Panel toggle buttons route through the command manager so the menu,
    // shortcuts and buttons can never disagree about state.
    std::function<void (juce::CommandID)> onPanelToggled;
    std::function<bool (juce::CommandID)> isPanelVisible;
    std::function<void (juce::CommandID)> onPanelContextMenu;
    void refreshPanelButtons();

    // Polled at UI rate for the readouts.
    std::function<double()> getBeatPosition;   // in quarter notes
    std::function<bool()>   getIsPlaying;
    std::function<bool()>   getIsRecording;
    std::function<bool()>   getMetronomeEnabled;
    std::function<double()> getTempo;          // tempo/mode can change under us
    std::function<bool()>   getSongMode;       // (project load, API, undo)

    // Exposed so the owner can bracket tempo drags into one undo transaction
    // without this bar needing to know about the project model.
    juce::Slider& getTempoSlider() { return tempoSlider; }

    // Exposed for wiring its engine hooks and persisting its options.
    MasterScope& getMasterScope() { return masterScope; }

    void setTempoDisplay (double bpm);
    void setSongMode (bool songMode);
    void setRecordArmed (bool armed);
    void setMetronomeLevelDisplay (double level);

    static constexpr int preferredHeight = 44;

private:
    void timerCallback() override;

    IconButton playButton   { "play", icons::play() };
    IconButton stopButton   { "stop", icons::stop() };
    IconButton recordButton { "record", icons::record(), theme::record };
    juce::TextButton patButton  { "PAT" };
    juce::TextButton songButton { "SONG" };
    juce::TextButton metronomeButton { "CLICK" };
    juce::Slider metronomeSlider;
    juce::Slider tempoSlider;
    juce::Label positionLabel;
    MasterScope masterScope;

    struct PanelButton
    {
        PanelButton (const juce::String& name, juce::Path icon) : button (name, std::move (icon)) {}
        IconButton button;
        juce::CommandID command {};
    };
    std::vector<std::unique_ptr<PanelButton>> panelButtons;
    std::vector<int> separatorX;   // group separators, computed in resized()

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};
