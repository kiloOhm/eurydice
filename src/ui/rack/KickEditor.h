#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "app/AppServices.h"
#include "ui/rack/ChannelEditor.h"
#include "ui/rack/KickDisplays.h"

// The kick designer's envelope graph: the pitch and the amplitude envelope on
// one time ruler, the selected one editable by dragging its points and bending
// its segments, the other drawn behind it for reference.
//
// A role that carries no drawn points shows its analytic decay instead and
// refuses edits until DRAW converts it — that is how a kick saved before the
// curve editor keeps its exact sound until someone deliberately changes it.
class KickEnvelopeCanvas : public juce::Component,
                           private juce::Timer
{
public:
    KickEnvelopeCanvas (AppServices&, juce::ValueTree channel);

    void setRole (const juce::String& role);
    const juce::String& getRole() const { return role; }

    bool isDrawn() const;
    // Materialises the analytic shape as draggable points, or throws the
    // points away and goes back to the analytic decay.
    void setDrawn (bool shouldBeDrawn);

    // Fired when the mode or the selection changes, so the editor's buttons
    // can follow along.
    std::function<void()> onStateChanged;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    juce::Rectangle<int> plotArea() const;
    double axisSpanSeconds() const;
    double roleOffsetSeconds (const juce::String& forRole) const;
    double roleSpanSeconds (const juce::String& forRole) const;

    float xForTime (double seconds) const;
    double timeForX (float x) const;
    float yForValue (float value) const;
    float valueForY (float y) const;

    void paintCurve (juce::Graphics&, const juce::String& forRole, bool active);
    void paintAxes (juce::Graphics&);

    int pointAt (juce::Point<int>) const;
    int segmentAt (juce::Point<int>) const;
    void commit (const kickdsp::Envelope&, bool asOneGesture);
    void showMenu (int pointIndex);

    AppServices& services;
    juce::ValueTree channel;
    juce::String role { kickenv::ampRole };

    kickdsp::Envelope editing;      // the live copy while a drag is running
    int draggedPoint = -1;
    int tensionSegment = -1;
    int tensionStartY = 0;
    float tensionStart = 0.0f;
    std::vector<double> shownState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickEnvelopeCanvas)
};

// Editor for the synthesised kick channel: a preset browser over the factory
// bank, the envelope graph and a live render of the hit next to it, then the
// four layers and the output chain as module boxes. The keyboard is the tuning
// tool — the body sweep tracks the note, so auditioning across keys is how the
// kick gets tuned to the track.
class KickEditor : public juce::Component,
                   public juce::FileDragAndDropTarget,
                   private juce::Timer
{
public:
    KickEditor (AppServices&, juce::ValueTree channel);

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    static constexpr int preferredWidth = 980;

private:
    void timerCallback() override;

    SynthModule& addModule (const juce::String& title, std::unique_ptr<juce::Component> display,
                            std::initializer_list<juce::Identifier> params);

    void buildPresetBar();
    void refreshPresetLists (bool keepSelection);
    void applyPreset (const juce::String& name);
    void stepPreset (int delta);
    void exportRender();
    void loadClickSample (const juce::File&);
    void refreshEnvelopeButtons();

    AppServices& services;
    juce::ValueTree channel;

    juce::TextButton prevButton { "<" }, nextButton { ">" };
    juce::ComboBox categoryBox, presetBox;
    juce::TextButton previewButton { juce::CharPointer_UTF8 ("\xe2\x96\xb6") };
    juce::TextButton exportButton { "Export..." };
    juce::Label readoutLabel;
    juce::String shownPresetName;

    juce::TextButton ampTab { "AMP" }, pitchTab { "PITCH" }, drawButton { "Draw" };
    std::unique_ptr<KickEnvelopeCanvas> canvas;
    kickdisplays::OutputDisplay* output = nullptr;   // owned by the module list

    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard { keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };
    std::vector<std::unique_ptr<SynthModule>> modules;
    std::unique_ptr<juce::Component> outputHolder;
    std::unique_ptr<juce::MidiKeyboardState::Listener> bridge;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickEditor)
};
