#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AutomationEditor.h"
#include "app/AppServices.h"

// The right-click menu every automatable control shares. FL users reach for
// the knob itself to make an automation clip, so this hangs off the control
// rather than off the panel behind it.
//
// juce::Slider swallows right-clicks, so callers reach this by forwarding the
// slider's mouse events to their parent (addMouseListener) and dispatching on
// the event component, the way ChannelRow already does for its buttons.
namespace automationmenu
{
// `normalised` is the control's current value mapped onto the 0..1 range the
// automation curve stores. `resetToDefault` powers the last item; pass an
// empty function to grey it out.
inline void show (AppServices& services, const AutomationWriter::Target& target,
                  double normalised, std::function<void()> resetToDefault = {})
{
    const bool exists = AutomationWriter::findSource (services.project, target).isValid();

    juce::PopupMenu menu;
    menu.addSectionHeader (target.name);
    menu.addItem (1, exists ? "Edit automation" : "Create automation clip");
    menu.addItem (2, "Record automation", true, services.automationWriter.isArmed());
    menu.addSeparator();
    menu.addItem (3, "Reset to default", resetToDefault != nullptr);

    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks) — the callback's
    // ownership passes into the menu's modal manager, which the analyzer
    // cannot see across JUCE's type-erased dispatch.
    menu.showMenuAsync ({}, [&services, target, normalised, reset = std::move (resetToDefault)] (int result)
    {
        if (result == 1)
        {
            auto source = AutomationWriter::findSource (services.project, target);
            if (! source.isValid())
                source = services.createAutomationWithClip (target.type, target.id,
                                                            target.paramId, target.name,
                                                            normalised);
            if (! source.isValid())
                return;
            const auto clip = AutomationWriter::findClip (services.project, (int) source[ids::id]);
            AutomationEditor::open (services, source,
                                    clip.isValid() ? (int) clip[ids::lengthTicks]
                                                   : 4 * ids::ticksPerBar);
        }
        else if (result == 2)
        {
            const bool arm = ! services.automationWriter.isArmed();
            services.automationWriter.setArmed (arm);
            // Arming mid-playback starts this parameter's pass right away, so
            // the curve begins at the value the control is sitting on.
            if (arm)
                services.automationWriter.touch (target, normalised);
        }
        else if (result == 3 && reset)
        {
            reset();
        }
    });
}
} // namespace automationmenu
