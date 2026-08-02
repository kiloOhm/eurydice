#include <gtest/gtest.h>
#include "app/TypingPiano.h"

namespace
{
struct Sink
{
    std::vector<std::pair<int, float>> ons;
    std::vector<int> offs;

    TypingPiano piano {
        [this] (int n, float v) { ons.emplace_back (n, v); },
        [this] (int n) { offs.push_back (n); } };

    bool press (juce::juce_wchar c)
    {
        return piano.keyPressed (juce::KeyPress ((int) c, {}, c), nullptr);
    }
};
}

TEST (TypingPiano, MapsFlStyleRows)
{
    EXPECT_EQ (TypingPiano::keyToNote ('z'), 60);   // C4
    EXPECT_EQ (TypingPiano::keyToNote ('s'), 61);   // C#4
    EXPECT_EQ (TypingPiano::keyToNote ('m'), 71);   // B4
    EXPECT_EQ (TypingPiano::keyToNote ('q'), 72);   // C5
    EXPECT_EQ (TypingPiano::keyToNote ('p'), 88);   // E6
    EXPECT_EQ (TypingPiano::keyToNote ('a'), -1);   // not a piano key
    EXPECT_EQ (TypingPiano::keyToNote ('1'), -1);
}

TEST (TypingPiano, PressTriggersNoteOnOnceUntilReleased)
{
    Sink s;
    EXPECT_TRUE (s.press ('z'));
    EXPECT_TRUE (s.press ('z'));   // key repeat while held
    ASSERT_EQ (s.ons.size(), 1u);
    EXPECT_EQ (s.ons[0].first, 60);

    // Headless: the OS reports the key as not down, so a state change
    // releases it.
    EXPECT_TRUE (s.piano.keyStateChanged (false, nullptr));
    ASSERT_EQ (s.offs.size(), 1u);
    EXPECT_EQ (s.offs[0], 60);

    // Released keys can retrigger.
    EXPECT_TRUE (s.press ('z'));
    EXPECT_EQ (s.ons.size(), 2u);
}

TEST (TypingPiano, OctaveShiftAppliesAndClamps)
{
    Sink s;
    s.press (',');                 // one octave down
    s.press ('z');
    ASSERT_EQ (s.ons.size(), 1u);
    EXPECT_EQ (s.ons[0].first, 48);

    s.piano.keyStateChanged (false, nullptr);
    for (int i = 0; i < 10; ++i)   // clamped at -36
        s.press (',');
    s.press ('z');
    EXPECT_EQ (s.ons.back().first, 24);

    s.piano.keyStateChanged (false, nullptr);
    for (int i = 0; i < 20; ++i)   // clamped at +36
        s.press ('.');
    s.press ('z');
    EXPECT_EQ (s.ons.back().first, 96);
}

TEST (TypingPiano, IgnoresShortcutsAndUnmappedKeys)
{
    Sink s;
    const juce::KeyPress cmdZ ((int) 'z', juce::ModifierKeys::commandModifier, 'z');
    EXPECT_FALSE (s.piano.keyPressed (cmdZ, nullptr));   // undo, not a note
    EXPECT_FALSE (s.press ('a'));
    EXPECT_TRUE (s.ons.empty());
}

TEST (TypingPiano, QwertzLayoutFollowsPhysicalKeys)
{
    using L = TypingPiano::Layout;
    // Physical bottom-left key types 'y' on QWERTZ: it must be C4 there and
    // dead on QWERTY's low row (where it is a high-row key instead).
    EXPECT_EQ (TypingPiano::keyToNote ('y', L::qwertz), 60);
    EXPECT_EQ (TypingPiano::keyToNote ('z', L::qwertz), 81);   // physical top-row Y position
    EXPECT_EQ (TypingPiano::keyToNote ('y', L::qwerty), 81);
    EXPECT_EQ (TypingPiano::keyToNote ('z', L::qwerty), 60);
    // Keys whose characters don't move stay put on both layouts.
    EXPECT_EQ (TypingPiano::keyToNote ('s', L::qwertz), 61);
    EXPECT_EQ (TypingPiano::keyToNote ('m', L::qwertz), 71);
    EXPECT_EQ (TypingPiano::keyToNote ('q', L::qwertz), 72);
}
