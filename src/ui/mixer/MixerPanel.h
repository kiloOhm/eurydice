#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "app/AppServices.h"
#include "ui/common/AutomatableSlider.h"

// FL-style mixer: master + insert strips with fader/pan/mute/meter, a detail
// column for the selected insert (effect slots + sends). Sends support the
// full insert-to-insert routing the engine already evaluates.
class MixerPanel : public juce::Component,
                   private juce::ValueTree::Listener,
                   private juce::Timer
{
public:
    explicit MixerPanel (AppServices&);
    ~MixerPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;


private:
    class Strip : public juce::Component
    {
    public:
        Strip (MixerPanel& owner, int insertIndex);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void refresh();
        void setPeaks (float l, float r) { peakL = l; peakR = r; repaint (meterBounds); }

        const int insertIndex;

    private:
        MixerPanel& owner;
        AutomatableSlider fader, panKnob;
        juce::TextButton muteButton { "M" };
        juce::Rectangle<int> meterBounds;
        float peakL = 0.0f, peakR = 0.0f;
    };

    juce::ValueTree insertTree (int index) const { return services.project.getInsert (index); }
    void selectInsert (int index);
    void rebuildDetail();
    void showSendMenu();
    void showEffectSlotMenu (int slotIndex);
    void showEditorForSlot (int slotIndex);
    void clearSlot (int slotIndex);
    void showStripMenu (int insertIndex);
    void showKnobMenu (int insertIndex, const juce::Identifier& prop);
    void knobMoved (int insertIndex, const juce::Identifier& prop);
    juce::ValueTree getSlotTree (int insertIndex, int slotIndex, bool createIfMissing);

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override {}
    void timerCallback() override;

    AppServices& services;
    juce::ValueTree observedRoot;

    juce::Viewport stripViewport;
    juce::Component stripContainer;
    std::vector<std::unique_ptr<Strip>> strips;

    // detail column
    int selectedInsert = 0;
    juce::Label detailName;
    std::array<juce::TextButton, 10> effectSlots;
    juce::TextButton addSendButton { "+ Send" };
    struct SendRow { juce::Label label; juce::Slider level; juce::TextButton remove { "x" }; juce::ValueTree send; };
    std::vector<std::unique_ptr<SendRow>> sendRows;

    static constexpr int stripW = 64;
    static constexpr int detailW = 190;

    friend class Strip;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerPanel)
};
