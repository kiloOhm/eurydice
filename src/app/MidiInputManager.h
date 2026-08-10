#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include "AppServices.h"

// Routes hardware MIDI (all enabled inputs) to the selected channel for live
// play, and records notes into the active pattern while playing + armed.
// Also serves as the sink for the typing-keyboard piano.
class MidiInputManager : private juce::MidiInputCallback,
                         private juce::Timer
{
public:
    explicit MidiInputManager (AppServices& s) : services (s)
    {
        auto& dm = services.engine.getDeviceManager();
        for (const auto& device : juce::MidiInput::getAvailableDevices())
            dm.setMidiInputDeviceEnabled (device.identifier, true);
        dm.addMidiInputDeviceCallback ({}, this);
        startTimer (2000);   // hot-plug: keep enabling new devices
    }

    ~MidiInputManager() override
    {
        services.engine.getDeviceManager().removeMidiInputDeviceCallback ({}, this);
    }

    std::atomic<bool> recordArmed { false };

    // UI feedback for live input (piano-roll key highlight). Message thread.
    std::function<void (int key, bool on)> onLiveNote;

    // Typing keyboard + MIDI hardware funnel through these.
    void noteOn (int key, float velocity)
    {
        const int chId = selectedChannelId();
        if (chId < 0)
            return;
        services.engine.previewNote (chId, key, velocity, 0);
        if (onLiveNote)
            onLiveNote (key, true);
        services.liveNoteListeners.call ([key, velocity] (AppServices::LiveNoteListener& l)
                                         { l.liveNoteOn (key, velocity); });

        if (recordArmed.load() && services.engine.isPlaying())
        {
            const double tick = currentPatternTick();
            heldNotes[key] = tick;
            auto pattern = activePattern();
            if (pattern.isValid())
                services.project.addNote (services.project.getOrCreateLane (pattern, chId),
                                          key, (int) tick, ids::ticksPerStep, velocity);
        }
    }

    void noteOff (int key)
    {
        const int chId = selectedChannelId();
        if (chId < 0)
            return;
        services.engine.previewNoteOff (chId, key);
        if (onLiveNote)
            onLiveNote (key, false);
        services.liveNoteListeners.call ([key] (AppServices::LiveNoteListener& l)
                                         { l.liveNoteOff (key); });

        if (auto it = heldNotes.find (key); it != heldNotes.end())
        {
            // Stretch the recorded note to the release point.
            auto pattern = activePattern();
            if (pattern.isValid())
            {
                auto lane = services.project.getLane (pattern, chId);
                const double endTick = currentPatternTick();
                for (int i = lane.getNumChildren(); --i >= 0;)
                {
                    auto note = lane.getChild (i);
                    if ((int) note[ids::key] == key
                        && (int) note[ids::startTicks] == (int) it->second)
                    {
                        const int len = juce::jmax (60, (int) (endTick - it->second));
                        note.setProperty (ids::lengthTicks, len, nullptr);
                        break;
                    }
                }
            }
            heldNotes.erase (it);
        }
    }

private:
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message) override
    {
        if (message.isNoteOn())
        {
            const int key = message.getNoteNumber();
            const float vel = message.getFloatVelocity();
            juce::MessageManager::callAsync ([this, key, vel] { noteOn (key, vel); });
        }
        else if (message.isNoteOff())
        {
            const int key = message.getNoteNumber();
            juce::MessageManager::callAsync ([this, key] { noteOff (key); });
        }
    }

    void timerCallback() override
    {
        auto& dm = services.engine.getDeviceManager();
        for (const auto& device : juce::MidiInput::getAvailableDevices())
            if (! dm.isMidiInputDeviceEnabled (device.identifier))
                dm.setMidiInputDeviceEnabled (device.identifier, true);
    }

    int selectedChannelId() const
    {
        const int id = services.project.getRoot()[ids::selectedChannel];
        if (services.project.getChannelById (id).isValid())
            return id;
        if (services.project.numChannels() > 0)
            return services.project.getChannel (0)[ids::id];
        return -1;
    }

    juce::ValueTree activePattern() const
    {
        return services.project.getPatternById (services.project.getRoot()[ids::activePattern]);
    }

    double currentPatternTick() const
    {
        const auto pattern = activePattern();
        const double pos = services.engine.getPositionTicks();
        if (services.project.isSongMode() || ! pattern.isValid())
            return pos;
        const double len = (double) (int) pattern[ids::lengthTicks];
        return len > 0 ? std::fmod (pos, len) : pos;
    }

    AppServices& services;
    std::map<int, double> heldNotes;   // key -> recorded start tick (message thread)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiInputManager)
};
