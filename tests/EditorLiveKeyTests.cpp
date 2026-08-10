#include <gtest/gtest.h>
#include "app/AppServices.h"
#include "ui/rack/ChannelEditor.h"

namespace
{
bool pixelsDiffer (const juce::Image& a, const juce::Image& b)
{
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt (x, y) != b.getPixelAt (x, y))
                return true;
    return false;
}

// Fires a live note the way MidiInputManager does after routing it to chId.
void broadcastLiveNote (AppServices& services, int chId, int key, bool on)
{
    services.liveNoteListeners.call ([chId, key, on] (AppServices::LiveNoteListener& l)
    {
        if (on)
            l.liveNoteOn (chId, key, 0.8f);
        else
            l.liveNoteOff (chId, key);
    });
}
} // namespace

// MIDI / typing-piano input lights the key on a built-in instrument's keyboard.
TEST (EditorLiveKeys, SynthKeyboardFollowsLiveNotes)
{
    AppServices services (false);
    auto channel = services.project.addChannel ("synth", "Lead");
    const int channelId = channel[ids::id];

    SynthEditor editor (services, channel);   // the constructor sizes and lays out

    // The keyboard is the bottom strip inside the editor's 10 px margin.
    const juce::Rectangle<int> keys { 10, editor.getHeight() - 66, editor.getWidth() - 20, 56 };
    auto snapshot = [&editor, keys] { return editor.createComponentSnapshot (keys); };

    const auto idle = snapshot();
    broadcastLiveNote (services, channelId, 60, true);
    const auto held = snapshot();
    broadcastLiveNote (services, channelId, 60, false);
    const auto released = snapshot();

    EXPECT_TRUE (pixelsDiffer (idle, held)) << "live note did not light the key";
    EXPECT_FALSE (pixelsDiffer (idle, released)) << "released key stayed lit";
}

// Notes routed to a different channel must leave this editor's keys alone.
TEST (EditorLiveKeys, SynthKeyboardIgnoresOtherChannels)
{
    AppServices services (false);
    auto lead = services.project.addChannel ("synth", "Lead");
    auto other = services.project.addChannel ("synth", "Pad");

    SynthEditor editor (services, lead);

    const juce::Rectangle<int> keys { 10, editor.getHeight() - 66, editor.getWidth() - 20, 56 };
    auto snapshot = [&editor, keys] { return editor.createComponentSnapshot (keys); };

    const auto idle = snapshot();
    broadcastLiveNote (services, (int) other[ids::id], 60, true);

    EXPECT_FALSE (pixelsDiffer (idle, snapshot()))
        << "a note routed to another channel lit this editor's keys";
}

// The kick editor's keyboard is the tuning tool, so it follows live input too.
TEST (EditorLiveKeys, KickKeyboardFollowsLiveNotes)
{
    AppServices services (false);
    auto channel = services.project.addChannel ("kick", "Kick Synth");
    const int channelId = channel[ids::id];

    KickEditor editor (services, channel);

    const juce::Rectangle<int> keys { 10, editor.getHeight() - 66, editor.getWidth() - 20, 56 };
    auto snapshot = [&editor, keys] { return editor.createComponentSnapshot (keys); };

    const auto idle = snapshot();
    broadcastLiveNote (services, channelId, 48, true);
    const auto held = snapshot();
    broadcastLiveNote (services, channelId, 48, false);
    const auto released = snapshot();

    EXPECT_TRUE (pixelsDiffer (idle, held)) << "live note did not light the key";
    EXPECT_FALSE (pixelsDiffer (idle, released)) << "released key stayed lit";
}
