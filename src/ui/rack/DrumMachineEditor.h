#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "app/AppServices.h"
#include "model/DrumPads.h"
#include "ui/common/LabelledKnob.h"

// The pad bank of the drum-machine editor. The grid shape is whatever the
// channel says (padRows x padCols), so it can be laid out to mirror the
// hardware on the desk: 4x4 for an MPD/FPC, 2x8 for a Maschine Mikro row
// pair, 8x8 for a Launchpad. Pads light when they play, whatever triggered
// them (pattern, clicks, live MIDI).
class DrumPadGrid : public juce::Component,
                    public juce::FileDragAndDropTarget,
                    private juce::Timer
{
public:
    DrumPadGrid (AppServices&, juce::ValueTree channel);

    std::function<void (int pad)> onPadSelected;
    std::function<void (int pad)> onLoadSampleRequested;

    int getSelectedPad() const { return selectedPad; }
    void setSelectedPad (int pad);
    void gridShapeChanged();

    int preferredWidth() const;
    int preferredHeight() const;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;

    void triggerPad (int pad);
    void loadSampleOntoPad (int pad, const juce::File&);
    void clearPad (int pad);

    static constexpr int cellSize = 62;
    static constexpr int gap = 6;

private:
    void timerCallback() override;
    int padAtPosition (juce::Point<int>) const;
    juce::Rectangle<int> padBounds (int pad) const;
    void showPadMenu (int pad);

    AppServices& services;
    juce::ValueTree channel;
    int selectedPad = 0;
    int dropHighlightPad = -1;
    std::array<float, drumpads::maxPads> flashLevel {};
    std::array<std::uint32_t, drumpads::maxPads> lastTriggerCount {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumPadGrid)
};

// FPC-style drum machine editor: grid-shape controls to match the MIDI
// controller on the desk, the pad bank, and a strip editing the selected pad
// (sample, level, pan, tune, choke group, MIDI note with learn).
class DrumMachineEditor : public juce::Component,
                          private juce::ValueTree::Listener,
                          private AppServices::LiveNoteListener
{
public:
    DrumMachineEditor (AppServices&, juce::ValueTree channel);
    ~DrumMachineEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void configureGrid (int rows, int cols);
    void rebuildPadStrip();
    void refreshPadStrip();
    void updateWindowSize();

    void liveNoteOn (int key, float velocity) override;
    void liveNoteOff (int key) override { juce::ignoreUnused (key); }
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;

    AppServices& services;
    juce::ValueTree channel;

    void showKitMenu();

    juce::TextButton kitButton { "Kit..." };
    juce::ComboBox rowsBox, colsBox;
    juce::Label gridLabel, baseLabel;
    juce::Slider baseNoteSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::TextButton mapButton { "Map notes" };
    juce::ToggleButton learnButton { "Learn" };

    DrumPadGrid padGrid;

    // Selected-pad strip, rebuilt whenever the selection moves.
    juce::Label padNameLabel, padNoteLabel, chokeCaption;
    juce::TextButton loadButton { "Load sample..." }, clearButton { "Clear" };
    juce::ComboBox chokeBox;
    std::vector<std::unique_ptr<LabelledKnob>> padKnobs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumMachineEditor)
};
