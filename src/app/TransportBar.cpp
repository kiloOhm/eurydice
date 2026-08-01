#include "TransportBar.h"
#include "Theme.h"

TransportBar::TransportBar()
{
    playButton.setWantsKeyboardFocus (false);
    stopButton.setWantsKeyboardFocus (false);
    recordButton.setWantsKeyboardFocus (false);
    patButton.setWantsKeyboardFocus (false);
    songButton.setWantsKeyboardFocus (false);

    playButton.onClick   = [this] { if (onPlay) onPlay(); };
    stopButton.onClick   = [this] { if (onStop) onStop(); };
    recordButton.setClickingTogglesState (true);
    recordButton.setColour (juce::TextButton::buttonOnColourId, theme::record);
    recordButton.onClick = [this] { if (onRecordToggled) onRecordToggled(); };

    patButton.setClickingTogglesState (true);
    songButton.setClickingTogglesState (true);
    patButton.setRadioGroupId (100);
    songButton.setRadioGroupId (100);
    patButton.setToggleState (true, juce::dontSendNotification);
    patButton.onClick  = [this] { if (onSongModeChanged) onSongModeChanged (false); };
    songButton.onClick = [this] { if (onSongModeChanged) onSongModeChanged (true); };

    tempoSlider.setSliderStyle (juce::Slider::LinearBar);
    tempoSlider.setRange (20.0, 999.0, 0.01);
    tempoSlider.setValue (140.0, juce::dontSendNotification);
    tempoSlider.setTextValueSuffix (" BPM");
    tempoSlider.setNumDecimalPlacesToDisplay (2);
    tempoSlider.setDoubleClickReturnValue (true, 140.0);
    tempoSlider.setColour (juce::Slider::trackColourId, theme::raised);
    tempoSlider.setVelocityBasedMode (true);
    tempoSlider.setVelocityModeParameters (0.5, 1, 0.09, false);
    tempoSlider.onValueChange = [this] { if (onTempoChanged) onTempoChanged (tempoSlider.getValue()); };

    positionLabel.setFont (theme::uiFont (15.0f, true));
    positionLabel.setColour (juce::Label::textColourId, theme::accent);
    positionLabel.setJustificationType (juce::Justification::centred);
    positionLabel.setText ("  1 : 1 : 00", juce::dontSendNotification);

    for (auto* c : std::initializer_list<juce::Component*> { &playButton, &stopButton, &recordButton,
                                                             &patButton, &songButton, &tempoSlider, &positionLabel })
        addAndMakeVisible (c);

    startTimerHz (30);
}

void TransportBar::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelHeader);
    g.setColour (theme::outline);
    g.fillRect (getLocalBounds().removeFromBottom (1));
}

void TransportBar::resized()
{
    auto r = getLocalBounds().reduced (8, 7);

    playButton.setBounds (r.removeFromLeft (36));
    r.removeFromLeft (4);
    stopButton.setBounds (r.removeFromLeft (36));
    r.removeFromLeft (4);
    recordButton.setBounds (r.removeFromLeft (36));
    r.removeFromLeft (16);

    patButton.setBounds (r.removeFromLeft (48));
    r.removeFromLeft (2);
    songButton.setBounds (r.removeFromLeft (48));
    r.removeFromLeft (16);

    tempoSlider.setBounds (r.removeFromLeft (110));
    r.removeFromLeft (16);

    positionLabel.setBounds (r.removeFromLeft (130));
}

void TransportBar::setTempoDisplay (double bpm)
{
    tempoSlider.setValue (bpm, juce::dontSendNotification);
}

void TransportBar::timerCallback()
{
    const bool playing = getIsPlaying ? getIsPlaying() : false;
    playButton.setColour (juce::TextButton::textColourOffId,
                          playing ? theme::ledGreen : theme::textPrimary);

    double beats = getBeatPosition ? getBeatPosition() : 0.0;
    const int bar  = (int) (beats / 4.0) + 1;
    const int beat = ((int) beats % 4) + 1;
    const int tick = (int) (std::fmod (beats, 1.0) * 100.0);
    positionLabel.setText (juce::String::formatted ("%3d : %d : %02d", bar, beat, tick),
                           juce::dontSendNotification);
    playButton.repaint();
}
