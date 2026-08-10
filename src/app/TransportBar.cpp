#include "TransportBar.h"
#include "Theme.h"

TransportBar::TransportBar()
{
    patButton.setWantsKeyboardFocus (false);
    songButton.setWantsKeyboardFocus (false);

    playButton.setTooltip ("Play (Space)");
    playButton.setIconColour (theme::textPrimary);
    playButton.onClick   = [this] { if (onPlay) onPlay(); };
    stopButton.setTooltip ("Stop; a second press rewinds to the start (Space while playing)");
    stopButton.setIconColour (theme::textPrimary);
    stopButton.onClick   = [this] { if (onStop) onStop(); };
    recordButton.setTooltip ("Arm recording: MIDI notes go into the pattern, audio input to a clip");
    recordButton.setIconColour (theme::record);
    recordButton.setClickingTogglesState (true);
    recordButton.onClick = [this] { if (onRecordToggled) onRecordToggled(); };

    patButton.setTooltip ("Pattern mode: loop the pattern selected in the channel rack");
    songButton.setTooltip ("Song mode: play the playlist arrangement");
    patButton.setClickingTogglesState (true);
    songButton.setClickingTogglesState (true);
    patButton.setRadioGroupId (100);
    songButton.setRadioGroupId (100);
    patButton.setToggleState (true, juce::dontSendNotification);
    patButton.onClick  = [this] { if (onSongModeChanged) onSongModeChanged (false); };
    songButton.onClick = [this] { if (onSongModeChanged) onSongModeChanged (true); };

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
    tempoSlider.setTooltip ("Tempo: drag to change, double-click to reset to 140");
    tempoSlider.onValueChange = [this] { if (onTempoChanged) onTempoChanged (tempoSlider.getValue()); };

    positionLabel.setFont (theme::uiFont (15.0f, true));
    positionLabel.setColour (juce::Label::textColourId, theme::accent);
    positionLabel.setJustificationType (juce::Justification::centred);
    positionLabel.setText ("  1 : 1 : 00", juce::dontSendNotification);

    for (auto* c : std::initializer_list<juce::Component*> { &playButton, &stopButton, &recordButton,
                                                             &patButton, &songButton, &metronomeButton,
                                                             &metronomeSlider, &tempoSlider,
                                                             &positionLabel, &masterScope })
        addAndMakeVisible (c);

    // Visible panel toggles: the discoverable route to every window. Compact
    // icons; the tooltip carries the name and shortcut.
    struct PanelSpec { const char* name; const char* tip; juce::Path icon; juce::CommandID command; };
    const PanelSpec panels[] = {
        { "playlist", "Playlist (F5)",     icons::playlist(), CommandIDs::viewPlaylist },
        { "rack",     "Channel Rack (F6)", icons::rack(),     CommandIDs::viewChannelRack },
        { "piano",    "Piano Roll (F7)",   icons::piano(),    CommandIDs::viewPianoRoll },
        { "mixer",    "Mixer (F9)",        icons::mixer(),    CommandIDs::viewMixer },
        { "browser",  "Browser",           icons::browser(),  CommandIDs::viewBrowser },
    };
    for (const auto& spec : panels)
    {
        auto entry = std::make_unique<PanelButton> (spec.name, spec.icon);
        entry->command = spec.command;
        entry->button.setTooltip (juce::String (spec.tip)
                                  + juce::String (juce::CharPointer_UTF8 (
                                      " \xe2\x80\x94 right-click for window options")));
        entry->button.setClickingTogglesState (false);
        entry->button.onClick = [this, command = spec.command]
        {
            if (onPanelToggled) onPanelToggled (command);
        };
        // Buttons eat right-clicks, so listen in and handle them here.
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

    // Faint separators between the control groups.
    for (const int x : separatorX)
        g.fillRect (x, 10, 1, getHeight() - 21);
}

void TransportBar::resized()
{
    auto r = getLocalBounds().reduced (8, 7);
    separatorX.clear();
    const auto separator = [this, &r]
    {
        separatorX.push_back (r.removeFromLeft (16).getCentreX());
    };

    playButton.setBounds (r.removeFromLeft (36));
    r.removeFromLeft (4);
    stopButton.setBounds (r.removeFromLeft (36));
    r.removeFromLeft (4);
    recordButton.setBounds (r.removeFromLeft (36));
    separator();

    patButton.setBounds (r.removeFromLeft (48));
    r.removeFromLeft (2);
    songButton.setBounds (r.removeFromLeft (48));
    separator();

    metronomeButton.setBounds (r.removeFromLeft (52));
    r.removeFromLeft (2);
    metronomeSlider.setBounds (r.removeFromLeft (48));
    separator();

    tempoSlider.setBounds (r.removeFromLeft (110));
    r.removeFromLeft (16);

    positionLabel.setBounds (r.removeFromLeft (130));
    separator();

    for (auto& entry : panelButtons)
    {
        entry->button.setBounds (r.removeFromLeft (34));
        r.removeFromLeft (3);
    }

    // The scope keeps to the right edge and gives way entirely when the
    // window leaves it too little room to be readable.
    r.removeFromLeft (8);
    masterScope.setVisible (r.getWidth() >= MasterScope::minimumWidth);
    if (masterScope.isVisible())
        masterScope.setBounds (r.removeFromRight (juce::jmin (MasterScope::preferredWidth, r.getWidth())));

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
        entry->button.setToggleState (isPanelVisible (entry->command),
                                      juce::dontSendNotification);
}

void TransportBar::timerCallback()
{
    // Poll rather than relying on toggle callbacks: panels can also be shown
    // or hidden by their own close button, startup flags, or the API.
    refreshPanelButtons();

    // These can also change from menus, commands or the control API, so poll
    // rather than tracking click callbacks.
    if (getMetronomeEnabled)
        metronomeButton.setToggleState (getMetronomeEnabled(), juce::dontSendNotification);
    if (getSongMode)
        setSongMode (getSongMode());
    // Don't fight the user's own drag; otherwise follow the model.
    if (getTempo && ! tempoSlider.isMouseButtonDown())
        tempoSlider.setValue (getTempo(), juce::dontSendNotification);

    const bool playing = getIsPlaying ? getIsPlaying() : false;
    playButton.setIconColour (playing ? theme::ledGreen : theme::textPrimary);

    double beats = getBeatPosition ? getBeatPosition() : 0.0;
    const int bar  = (int) (beats / 4.0) + 1;
    const int beat = ((int) beats % 4) + 1;
    const int tick = (int) (std::fmod (beats, 1.0) * 100.0);
    positionLabel.setText (juce::String::formatted ("%3d : %d : %02d", bar, beat, tick),
                           juce::dontSendNotification);
}
