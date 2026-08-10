#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include "Ids.h"
#include "engine/KickDsp.h"

// The kick's drawn envelopes on the ValueTree side. A channel carries at most
// one KICKENV per role; no child at all means the classic analytic decay, so
// projects saved before the curve editor keep their exact sound.
//
//   <CHANNEL type="kick">
//     <KICKENV role="pitch">
//       <POINT pos="0" value="1" tension="0.7"/>
//       <POINT pos="1" value="0" tension="0"/>
//     </KICKENV>
//   </CHANNEL>
namespace kickenv
{
inline const juce::String pitchRole { "pitch" };
inline const juce::String ampRole { "amp" };

// The classic analytic pitch decay reaches e^-4 (near enough to zero) after
// four time constants, so a channel switching to a drawn pitch curve stretches
// its TIME knob by this much to sweep over the same audible span.
inline constexpr double pitchSpanFactor = 4.0;

inline juce::ValueTree find (const juce::ValueTree& channel, const juce::String& role)
{
    for (const auto child : channel)
        if (child.hasType (ids::KICKENV) && child[ids::role].toString() == role)
            return child;
    return {};
}

inline bool isDrawn (const juce::ValueTree& channel, const juce::String& role)
{
    return find (channel, role).getNumChildren() >= 2;
}

inline kickdsp::Envelope read (const juce::ValueTree& channel, const juce::String& role)
{
    kickdsp::Envelope envelope;
    const auto tree = find (channel, role);
    for (const auto point : tree)
        if (point.hasType (ids::POINT))
            envelope.points.push_back ({ (float) (double) point.getProperty (ids::pos, 0.0),
                                         (float) (double) point.getProperty (ids::value, 0.0),
                                         (float) (double) point.getProperty (ids::tension, 0.0) });
    envelope.tidy();
    return envelope;
}

// Replaces a role's points wholesale. An empty envelope removes the child,
// which is how the editor switches a role back to its analytic decay.
inline void write (juce::ValueTree channel, const juce::String& role,
                   const kickdsp::Envelope& envelope, juce::UndoManager* undo)
{
    auto tree = find (channel, role);

    if (envelope.points.size() < 2)
    {
        if (tree.isValid())
            channel.removeChild (tree, undo);
        return;
    }

    if (! tree.isValid())
    {
        tree = juce::ValueTree (ids::KICKENV);
        tree.setProperty (ids::role, role, nullptr);
        channel.appendChild (tree, undo);
    }
    tree.removeAllChildren (undo);

    for (const auto& p : envelope.points)
    {
        juce::ValueTree point (ids::POINT);
        point.setProperty (ids::pos, (double) p.pos, nullptr);
        point.setProperty (ids::value, (double) p.value, nullptr);
        point.setProperty (ids::tension, (double) p.tension, nullptr);
        tree.appendChild (point, undo);
    }
}

// The curve a role starts from when it is switched to drawn mode: the same
// fast-then-slow fall the analytic decay makes, as two draggable points.
inline kickdsp::Envelope defaultFor (const juce::String& role)
{
    kickdsp::Envelope envelope;
    envelope.points.push_back ({ 0.0f, 1.0f, role == pitchRole ? 0.7f : 0.75f });
    envelope.points.push_back ({ 1.0f, 0.0f, 0.0f });
    return envelope;
}
} // namespace kickenv
