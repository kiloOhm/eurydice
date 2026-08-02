#include "TransportBar.h"
#include "Theme.h"

TransportBar::TransportBar()
{
    playButton.setWantsKeyboardFocus (false);
    stopButton.setWantsKeyboardFocus (false);
    recordButton.setWantsKeyboardFocus (false);
    patButton.setWantsKeyboardFocus (false);
    songButton.setWantsKeyboardFocus (false);
    loopButton.setWantsKeyboardFocus (false);

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

    loopButton.setClickingTogglesState (true);
    loopButton.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    loopButton.onClick = [this] { if (onLoopToggled) onLoopToggled(); };

    autoButton.setWantsKeyboardFocus (false);
    autoButton.setClickingTogglesState (true);
    autoButton.setTooltip ("Write automation: while playing, moving a knob records it");
    autoButton.setColour (juce::TextButton::buttonOnColourId, theme::record);
    autoButton.onClick = [this] { if (onAutomationWriteToggled) onAutomationWriteToggled(); };
    metronomeButton.setWantsKeyboardFocus (false);
    metronomeButton.setClickingTogglesState (true);
    metronomeButton.setTooltip ("Metronome: accented click on the bar, plain click on the beat");
    metronomeButton.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    metronomeButton.onClick = [this] { if (onMetronomeToggled) onMetronomeToggled(); };

    metronomeSlider.setSliderStyle (juce::Slider::LinearBar);
    metronomeSlider.setRange (0.0, 100.0, 1.0);
    metronomeSlider.setValue (50.0, juce::dontSendNotification);
    metronomeSlider.setTooltip ("Metronome level");
    metronomeSlider.setTextValueSuffix ("%");
    metronomeSlider.setColour (juce::Slider::trackColourId, theme::raised);
    metronomeSlider.onValueChange = [this]
    {
        if (onMetronomeLevelChanged)
            onMetronomeLevelChanged (metronomeSlider.getValue() * 0.01);
    };

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
                                                             &patButton, &songButton, &loopButton,
                                                             &autoButton, &metronomeButton,
                                                             &metronomeSlider, &tempoSlider,
                                                             &positionLabel })
        addAndMakeVisible (c);

    // Visible panel toggles: the discoverable route to every window.
    const std::vector<std::pair<juce::String, juce::CommandID>> panels {
        { "PLAYLIST", CommandIDs::viewPlaylist },
        { "RACK",     CommandIDs::viewChannelRack },
        { "PIANO",    CommandIDs::viewPianoRoll },
        { "MIXER",    CommandIDs::viewMixer },
        { "BROWSER",  CommandIDs::viewBrowser },
    };
    for (const auto& [label, command] : panels)
    {
        auto entry = std::make_unique<PanelButton>();
        entry->command = command;
        entry->button.setButtonText (label);
        entry->button.setWantsKeyboardFocus (false);
        entry->button.setClickingTogglesState (false);
        entry->button.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
        entry->button.onClick = [this, command] { if (onPanelToggled) onPanelToggled (command); };
        // TextButton eats right-clicks, so listen in and handle them here.
        entry->button.addMouseListener (this, false);
        addAndMakeVisible (entry->button);
        panelButtons.push_back (std::move (entry));
    }

    startTimerHz (30);
}

void TransportBar::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() || ! onPanelContextMenu)
        return;

    for (auto& entry : panelButtons)
        if (e.eventComponent == &entry->button)
        {
            onPanelContextMenu (entry->command);
            return;
        }
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
    r.removeFromLeft (8);

    loopButton.setBounds (r.removeFromLeft (48));
    r.removeFromLeft (2);
    autoButton.setBounds (r.removeFromLeft (48));
    r.removeFromLeft (8);

    metronomeButton.setBounds (r.removeFromLeft (52));
    r.removeFromLeft (2);
    metronomeSlider.setBounds (r.removeFromLeft (60));
    r.removeFromLeft (16);

    tempoSlider.setBounds (r.removeFromLeft (110));
    r.removeFromLeft (16);

    positionLabel.setBounds (r.removeFromLeft (130));
    r.removeFromLeft (16);

    for (auto& entry : panelButtons)
    {
        entry->button.setBounds (r.removeFromLeft (72));
        r.removeFromLeft (3);
    }
    refreshPanelButtons();
}

void TransportBar::setTempoDisplay (double bpm)
{
    tempoSlider.setValue (bpm, juce::dontSendNotification);
}

void TransportBar::setSongMode (bool songMode)
{
    patButton.setToggleState (! songMode, juce::dontSendNotification);
    songButton.setToggleState (songMode, juce::dontSendNotification);
}

void TransportBar::setRecordArmed (bool armed)
{
    recordButton.setToggleState (armed, juce::dontSendNotification);
}

void TransportBar::setMetronomeLevelDisplay (double level)
{
    metronomeSlider.setValue (level * 100.0, juce::dontSendNotification);
}

void TransportBar::refreshPanelButtons()
{
    if (! isPanelVisible)
        return;
    for (auto& entry : panelButtons)
    {
        const bool on = isPanelVisible (entry->command);
        entry->button.setColour (juce::TextButton::buttonColourId,
                                 on ? theme::accentDim : theme::raised);
        entry->button.setColour (juce::TextButton::textColourOffId,
                                 on ? theme::textPrimary : theme::textDim);
        entry->button.repaint();
    }
}

void TransportBar::timerCallback()
{
    // Poll rather than relying on toggle callbacks: panels can also be shown
    // or hidden by their own close button, startup flags, or the API.
    refreshPanelButtons();

    // The loop can also be toggled from the playlist ruler, a command or the
    // control API, so poll rather than tracking it from the click callback.
    if (getLoopEnabled)
        loopButton.setToggleState (getLoopEnabled(), juce::dontSendNotification);
    if (getAutomationWrite)
        autoButton.setToggleState (getAutomationWrite(), juce::dontSendNotification);
    if (getMetronomeEnabled)
        metronomeButton.setToggleState (getMetronomeEnabled(), juce::dontSendNotification);
    if (getSongMode)
        setSongMode (getSongMode());
    // Don't fight the user's own drag; otherwise follow the model.
    if (getTempo && ! tempoSlider.isMouseButtonDown())
        tempoSlider.setValue (getTempo(), juce::dontSendNotification);

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
