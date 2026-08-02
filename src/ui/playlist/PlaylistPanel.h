#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "app/AppServices.h"

// Modern-FL playlist: free tracks, paint the active pattern as clips,
// move/resize/delete clips, ruler with seek, song-mode playhead.
// Audio and automation clips join once their engines land.
class PlaylistPanel : public juce::Component,
                      public juce::FileDragAndDropTarget,
                      private juce::ValueTree::Listener,
                      private juce::Timer
{
public:
    explicit PlaylistPanel (AppServices&);
    ~PlaylistPanel() override;

    // Asks the host to bring the piano roll forward.
    std::function<void()> onShowPianoRoll;

    // Scrolls a freshly created clip into view and rings it for a moment, so
    // "create automation clip" visibly does something.
    void revealClip (juce::ValueTree clip);

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    juce::Rectangle<int> headerArea() const;   // top controls
    juce::Rectangle<int> rulerArea() const;
    juce::Rectangle<int> trackHeaderArea() const;
    juce::Rectangle<int> gridArea() const;

    double xToTicks (int x) const;
    int    ticksToX (double ticks) const;
    int    yToTrack (int y) const;
    int    trackTop (int trackIndex) const;
    int    snap() const;
    double snapDown (double t) const;

    juce::ValueTree clipAt (juce::Point<int>, bool& overRightEdge);
    void addClipAt (juce::Point<int>);
    void showAutomationClipMenu (juce::ValueTree clip);
    bool loopRangeBounds (int& x0, int& x1) const;
    void paintTrackHeaders (juce::Graphics&);
    void paintRuler (juce::Graphics&);
    void paintClips (juce::Graphics&);

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& property) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override {}
    void timerCallback() override;

    AppServices& services;
    juce::ValueTree observedRoot;

    juce::ComboBox snapBox;

    double pxPerTick = 96.0 / 3840.0;   // one bar = 96 px
    double scrollTicks = 0.0;
    int scrollY = 0;
    int trackHeight = 34;

    enum class Drag { none, move, resize, erase, seek, loop };
    Drag drag = Drag::none;
    juce::ValueTree dragClip;
    double dragTickOffset = 0.0;
    double loopAnchorTicks = 0.0;

    double playheadTicks = -1.0;

    juce::ValueTree revealedClip;
    int revealFramesLeft = 0;   // counts down at timer rate

    juce::AudioFormatManager thumbFormats;
    juce::AudioThumbnailCache thumbCache { 64 };
    std::map<juce::String, std::unique_ptr<juce::AudioThumbnail>> thumbnails;
    juce::AudioThumbnail* getThumbnail (const juce::String& path);

    static constexpr int headerH = 30;
    static constexpr int rulerH = 22;
    static constexpr int trackHeaderW = 150;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaylistPanel)
};
