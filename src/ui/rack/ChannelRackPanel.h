#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "app/AppServices.h"
#include "ChannelRow.h"
#include "StepGraphLane.h"

// The FL-style channel rack: pattern selector + swing in the header,
// one ChannelRow per channel, add-channel button at the bottom.
class ChannelRackPanel : public juce::Component,
                         private juce::ValueTree::Listener,
                         private juce::Timer
{
public:
    explicit ChannelRackPanel (AppServices&);
    ~ChannelRackPanel() override;

    // Asks the host to bring the piano roll forward (wired to the command
    // manager by MainComponent).
    std::function<void()> onShowPianoRoll;

    // Asks the host to open this channel's editor window.
    std::function<void (juce::ValueTree)> onOpenChannelEditor;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void rebuildRows();
    void refreshHeader();
    int rowContainerWidth() const;
    juce::ValueTree activePattern() const;
    juce::ValueTree selectedChannel() const;
    void showChannelMenu (juce::ValueTree channel);
    void showInsertMenu (juce::ValueTree channel);
    void showAddChannelMenu();
    void showPatternMenu();
    void openChannelEditor (juce::ValueTree channel);
    void showPianoRollFor (juce::ValueTree channel);

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override {}
    void timerCallback() override;

    AppServices& services;
    juce::ValueTree observedRoot;

    juce::ComboBox patternBox;
    juce::TextButton addPatternButton { "+" };
    juce::TextButton patternMenuButton { "..." };
    juce::ComboBox lengthBox;
    juce::Slider swingKnob;
    juce::Label swingLabel { {}, "SWING" };
    juce::TextButton graphButton { "GRAPH" };

    struct RowContainer : juce::Component
    {
        void resized() override
        {
            int y = 0;
            for (auto* c : getChildren())
            {
                c->setBounds (0, y, getWidth(), ChannelRow::rowHeight);
                y += ChannelRow::rowHeight + 2;
            }
        }
    };

    // Reports horizontal scrolling so the graph lane can track the step grid.
    struct SyncViewport : juce::Viewport
    {
        std::function<void (juce::Rectangle<int>)> onVisibleAreaChanged;
        void visibleAreaChanged (const juce::Rectangle<int>& area) override
        {
            if (onVisibleAreaChanged)
                onVisibleAreaChanged (area);
        }
    };

    SyncViewport viewport;
    RowContainer rowContainer;
    std::vector<std::unique_ptr<ChannelRow>> rows;
    StepGraphLane graphLane;
    juce::TextButton addChannelButton { "+ Channel" };

    static constexpr int headerHeight = 34;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelRackPanel)
};
