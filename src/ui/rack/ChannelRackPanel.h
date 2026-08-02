#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "app/AppServices.h"
#include "app/Theme.h"
#include "ChannelRow.h"
#include "RackReorder.h"
#include "StepGraphLane.h"
#include "ui/common/SampleDrop.h"

// The FL-style channel rack: pattern selector + swing in the header,
// one ChannelRow per channel, add-channel button at the bottom.
// Sample files (browser drag or Finder) drop onto it: a sampler row swallows
// the sample, anywhere else inserts a new sampler channel.
class ChannelRackPanel : public juce::Component,
                         public juce::FileDragAndDropTarget,
                         public juce::DragAndDropTarget,
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
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int x, int y) override;
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

private:
    sampledrop::RackTarget dropTargetAt (juce::Point<int> posInPanel) const;
    void updateDropHover (juce::Point<int> posInPanel);
    void clearDropHover();
    void performDrop (const juce::StringArray& files, juce::Point<int> posInPanel);
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
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;
    void timerCallback() override;

    int rowIndexOf (const ChannelRow&) const;
    int reorderDropIndex (const juce::MouseEvent&);   // non-const: getEventRelativeTo

    AppServices& services;
    juce::ValueTree observedRoot;
    int sandboxHealthTick = 0;

    juce::ComboBox patternBox;
    juce::TextButton addPatternButton { "+" };
    juce::TextButton patternMenuButton { "..." };
    juce::ComboBox lengthBox;
    juce::Slider swingKnob;
    juce::Label swingLabel { {}, "SWING" };
    juce::TextButton graphButton { "GRAPH" };

    static constexpr int rowGap = 2;

    struct RowContainer : juce::Component
    {
        // One row plus the gap below it; drop-target and reorder maths share it.
        static constexpr int rowPitch = ChannelRow::rowHeight + rowGap;

        void resized() override
        {
            int y = 0;
            for (auto* c : getChildren())
            {
                c->setBounds (0, y, getWidth(), ChannelRow::rowHeight);
                y += rowPitch;
            }
        }

        // Insertion indicator for a row-reorder drag; -1/-1 = idle.
        void setDropIndicator (int sourceIndex, int targetIndex)
        {
            if (dragSource != sourceIndex || dragTarget != targetIndex)
            {
                dragSource = sourceIndex;
                dragTarget = targetIndex;
                repaint();
            }
        }

        void paintOverChildren (juce::Graphics& g) override
        {
            if (dragTarget < 0 || dragTarget == dragSource)
                return;
            const int y = rackreorder::indicatorYForDrop (dragTarget, dragSource,
                                                          rowPitch, ChannelRow::rowHeight);
            g.setColour (theme::accent);
            g.fillRect (0, y, getWidth(), 2);
        }

        int dragSource = -1, dragTarget = -1;
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

    // sample-drop hover indicator
    bool dropHoverActive = false;
    sampledrop::RackTarget dropTarget;

    static constexpr int headerHeight = 34;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelRackPanel)
};
