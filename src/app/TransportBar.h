#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Commands.h"

// Top strip: play/stop/record, pattern/song mode, BPM, position readout.
// Wired to the engine through std::function hooks so it stays engine-agnostic.
class TransportBar : public juce::Component,
                     private juce::Timer
{
public:
    TransportBar();

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onPlay, onStop, onRecordToggled;
    std::function<void (double)> onTempoChanged;
    std::function<void (bool)> onSongModeChanged;

    // Panel toggle buttons route through the command manager so the menu,
    // shortcuts and buttons can never disagree about state.
    std::function<void (juce::CommandID)> onPanelToggled;
    std::function<bool (juce::CommandID)> isPanelVisible;
    void refreshPanelButtons();

    // Polled at UI rate for the readouts.
    std::function<double()> getBeatPosition;   // in quarter notes
    std::function<bool()>   getIsPlaying;
    std::function<bool()>   getIsRecording;

    void setTempoDisplay (double bpm);
    void setSongMode (bool songMode);
    void setRecordArmed (bool armed);

    static constexpr int preferredHeight = 44;

private:
    void timerCallback() override;

    juce::TextButton playButton  { juce::CharPointer_UTF8 ("\xe2\x96\xb6") };
    juce::TextButton stopButton  { juce::CharPointer_UTF8 ("\xe2\x96\xa0") };
    juce::TextButton recordButton { juce::CharPointer_UTF8 ("\xe2\x97\x8f") };
    juce::TextButton patButton  { "PAT" };
    juce::TextButton songButton { "SONG" };
    juce::Slider tempoSlider;
    juce::Label positionLabel;

    struct PanelButton { juce::TextButton button; juce::CommandID command; };
    std::vector<std::unique_ptr<PanelButton>> panelButtons;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};
