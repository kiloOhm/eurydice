#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ProjectModel.h"

// One gesture on a control = one undo step.
//
// juce::UndoManager keeps appending to the open transaction until someone asks
// for a new one, so a gesture has to both open a transaction when it starts and
// close it when it ends: opening alone would let the *next* edit merge into the
// drag that just finished.
namespace undoGesture
{

inline void begin (ProjectModel& model, const juce::String& name)
{
    model.getUndoManager().beginNewTransaction (name);
}

inline void end (ProjectModel& model)
{
    model.getUndoManager().beginNewTransaction();
}

// A discrete edit — a button click, a menu item, an OK'd dialog — that should
// be its own undo step rather than merge into whatever came before it.
class Scoped
{
public:
    Scoped (ProjectModel& projectModel, const juce::String& name) : model (projectModel)
    {
        begin (model, name);
    }

    ~Scoped() { end (model); }

private:
    ProjectModel& model;

    JUCE_DECLARE_NON_COPYABLE (Scoped)
};

// juce::Slider brackets every user-driven change — drag, click, wheel, text
// entry, double-click reset — with drag-start/end, so this covers all of them.
// Leaves onValueChange alone: attach it before or after, either works.
inline void attach (juce::Slider& slider, ProjectModel& model, juce::String name)
{
    slider.onDragStart = [&model, gestureName = std::move (name)]
    {
        begin (model, gestureName);
    };
    slider.onDragEnd = [&model] { end (model); };
}

}
