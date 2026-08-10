#pragma once

#include "app/AppServices.h"
#include "model/ChannelParams.h"
#include "model/Ids.h"
#include "ui/automation/AutomationMenu.h"
#include "ui/common/LabelledKnob.h"

// Builds a generator's knob from the shared parameter table and wires it into
// the automation layer: moving one records while the write arm is on,
// right-clicking one offers to create or edit its clip. Shared by every
// built-in generator's editor so a knob behaves the same wherever it appears.
inline std::unique_ptr<LabelledKnob> makeParamKnob (AppServices& services, juce::ValueTree channel,
                                                    const channelparams::Descriptor& descriptor)
{
    auto knob = std::make_unique<LabelledKnob> (descriptor.caption, services.project, channel,
                                                descriptor.id, descriptor.range,
                                                descriptor.defaultValue, descriptor.suffix,
                                                descriptor.decimals);
    if (descriptor.automatable)
    {
        // The table is a function-local static, so &descriptor outlives
        // every editor window that captures it.
        const AutomationWriter::Target target { "channel-param", (int) channel[ids::id],
                                                descriptor.id.toString(),
                                                channel[ids::name].toString() + " " + descriptor.caption };
        auto* knobPtr = knob.get();

        knob->onLiveEdit = [&services, target, &descriptor] (double value)
        {
            services.automationWriter.touch (target, descriptor.toNormalised (value));
        };
        knob->onContextMenu = [&services, target, &descriptor, knobPtr] (double value)
        {
            automationmenu::show (services, target, descriptor.toNormalised (value),
                                  [knobPtr] { knobPtr->resetToDefault(); });
        };
    }
    return knob;
}

// Looks a descriptor up by id and builds its knob, or returns nullptr when the
// generator has no such parameter.
inline std::unique_ptr<LabelledKnob> makeParamKnob (AppServices& services, juce::ValueTree channel,
                                                    const juce::String& channelType,
                                                    const juce::Identifier& paramId)
{
    if (const auto* descriptor = channelparams::find (channelType, paramId.toString()))
        return makeParamKnob (services, channel, *descriptor);
    return nullptr;
}

// Routes an editor's on-screen keyboard through the engine's preview path.
struct KeyboardBridge : juce::MidiKeyboardState::Listener
{
    KeyboardBridge (AppServices& s, juce::ValueTree c) : services (s), channel (c) {}

    void handleNoteOn (juce::MidiKeyboardState*, int, int note, float vel) override
    {
        services.engine.previewNote (channel[ids::id], note, juce::jmax (0.3f, vel), 0);
    }

    void handleNoteOff (juce::MidiKeyboardState*, int, int note, float) override
    {
        services.engine.previewNoteOff (channel[ids::id], note);
    }

    AppServices& services;
    juce::ValueTree channel;
};
