#include "KickEditor.h"
#include "EditorParts.h"
#include "engine/WavWriter.h"
#include "model/KickPresets.h"
#include "model/UndoGesture.h"

namespace
{
constexpr int handleRadius = 4;
constexpr int grabRadius = 8;

// The output render, made draggable: dropping it on a DAW track, a browser or
// the desktop hands over a WAV of exactly what the display is showing.
class DraggableOutput : public kickdisplays::OutputDisplay
{
public:
    DraggableOutput (juce::ValueTree channelTree, juce::String channelName)
        : kickdisplays::OutputDisplay (std::move (channelTree)), name (std::move (channelName))
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTooltip ("Drag out to export this hit as a WAV");
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 8)
            return;
        dragging = true;

        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile (juce::File::createLegalFileName (name) + ".wav");
        if (writeTo (file))
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() },
                                                                        false, this,
                                                                        [this] { dragging = false; });
        else
            dragging = false;
    }

    // Renders the channel at 44.1 kHz / 24 bit. Returns false if the file
    // could not be opened for writing.
    bool writeTo (const juce::File& file) const
    {
        const auto rendered = kickchannel::render (channel, 44100.0);
        auto writer = wavwriter::forFile (file, 44100.0, 2, 24);
        if (writer == nullptr)
            return false;
        writer->writeFromAudioSampleBuffer (rendered, 0, rendered.getNumSamples());
        return true;
    }

private:
    juce::String name;
    bool dragging = false;
};

// A module box that takes a file drop — the CLICK layer's sample slot.
class DropModule : public SynthModule,
                   public juce::FileDragAndDropTarget
{
public:
    DropModule (juce::String titleText, std::unique_ptr<juce::Component> display,
                std::function<void (const juce::File&)> onDrop)
        : SynthModule (std::move (titleText), std::move (display)), drop (std::move (onDrop))
    {
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (const auto& f : files)
            if (juce::File (f).hasFileExtension ("wav;aif;aiff;mp3;flac;ogg;m4a"))
                return true;
        return false;
    }

    void filesDropped (const juce::StringArray& files, int, int) override
    {
        for (const auto& f : files)
            if (juce::File (f).hasFileExtension ("wav;aif;aiff;mp3;flac;ogg;m4a"))
            {
                if (drop)
                    drop (juce::File (f));
                return;
            }
    }

private:
    std::function<void (const juce::File&)> drop;
};
} // namespace

// ================= KickEnvelopeCanvas =================

KickEnvelopeCanvas::KickEnvelopeCanvas (AppServices& s, juce::ValueTree ch)
    : services (s), channel (ch)
{
    startTimerHz (12);
}

void KickEnvelopeCanvas::setRole (const juce::String& newRole)
{
    role = newRole;
    if (onStateChanged)
        onStateChanged();
    repaint();
}

bool KickEnvelopeCanvas::isDrawn() const
{
    return kickenv::isDrawn (channel, role);
}

void KickEnvelopeCanvas::setDrawn (bool shouldBeDrawn)
{
    const undoGesture::Scoped step (services.project,
                                    shouldBeDrawn ? "Draw kick envelope" : "Reset kick envelope");
    auto* undo = &services.project.getUndoManager();

    if (shouldBeDrawn)
    {
        // The analytic pitch decay keeps falling past its time constant; the
        // drawn one stops at the end of its span, so stretch the span to cover
        // the same audible sweep.
        if (role == kickenv::pitchRole)
        {
            const auto* descriptor = channelparams::find ("kick", ids::kickPitchDecay.toString());
            const double now = (double) channel.getProperty (ids::kickPitchDecay, 0.035);
            const double stretched = now * kickenv::pitchSpanFactor;
            channel.setProperty (ids::kickPitchDecay,
                                 descriptor != nullptr
                                     ? juce::jlimit (descriptor->range.start, descriptor->range.end, stretched)
                                     : stretched,
                                 undo);
        }
        kickenv::write (channel, role, kickenv::defaultFor (role), undo);
    }
    else
    {
        if (role == kickenv::pitchRole)
        {
            const auto* descriptor = channelparams::find ("kick", ids::kickPitchDecay.toString());
            const double now = (double) channel.getProperty (ids::kickPitchDecay, 0.035);
            const double shrunk = now / kickenv::pitchSpanFactor;
            channel.setProperty (ids::kickPitchDecay,
                                 descriptor != nullptr
                                     ? juce::jlimit (descriptor->range.start, descriptor->range.end, shrunk)
                                     : shrunk,
                                 undo);
        }
        kickenv::write (channel, role, {}, undo);
    }

    if (onStateChanged)
        onStateChanged();
    repaint();
}

void KickEnvelopeCanvas::timerCallback()
{
    // Anything that moves the graph — knobs, presets, automation, undo — lands
    // on the tree, so poll the values the graph is drawn from.
    std::vector<double> now {
        (double) channel.getProperty (ids::kickPitchDecay, 0.035),
        (double) channel.getProperty (ids::kickAmpDecay, 0.5),
        (double) channel.getProperty (ids::kickHold, 0.0),
        (double) channel.getProperty (ids::kickStartFreq, 240.0),
        (double) channel.getProperty (ids::kickEndFreq, 48.0),
        (double) channel.getProperty (ids::envShape, 1.0) };
    for (const auto* r : { &kickenv::pitchRole, &kickenv::ampRole })
        for (const auto& point : kickenv::read (channel, *r).points)
        {
            now.push_back (point.pos);
            now.push_back (point.value);
            now.push_back (point.tension);
        }

    if (now != shownState)
    {
        const bool modeChanged = now.size() != shownState.size();
        shownState = std::move (now);
        if (modeChanged && onStateChanged)
            onStateChanged();
        repaint();
    }
}

juce::Rectangle<int> KickEnvelopeCanvas::plotArea() const
{
    return getLocalBounds().reduced (2).withTrimmedLeft (36).withTrimmedBottom (14);
}

double KickEnvelopeCanvas::roleOffsetSeconds (const juce::String& forRole) const
{
    return forRole == kickenv::ampRole ? juce::jmax (0.0, (double) channel.getProperty (ids::kickHold, 0.0))
                                       : 0.0;
}

double KickEnvelopeCanvas::roleSpanSeconds (const juce::String& forRole) const
{
    return kickdisplays::envelopeSpanSeconds (channel, forRole);
}

double KickEnvelopeCanvas::axisSpanSeconds() const
{
    // The ruler follows whichever envelope is being edited, so its points
    // always spread across the full width. The other one is drawn on the same
    // real-time ruler, which is how their scales stay comparable — a 30 ms
    // sweep really is a sliver next to a 600 ms decay.
    return juce::jmax (0.02, roleOffsetSeconds (role) + roleSpanSeconds (role));
}

float KickEnvelopeCanvas::xForTime (double seconds) const
{
    const auto area = plotArea();
    return (float) area.getX()
           + (float) (juce::jlimit (0.0, 1.0, seconds / axisSpanSeconds()) * area.getWidth());
}

double KickEnvelopeCanvas::timeForX (float x) const
{
    const auto area = plotArea();
    return juce::jlimit (0.0, 1.0, (double) (x - area.getX()) / juce::jmax (1, area.getWidth()))
           * axisSpanSeconds();
}

float KickEnvelopeCanvas::yForValue (float value) const
{
    const auto area = plotArea();
    return (float) area.getBottom() - juce::jlimit (0.0f, 1.0f, value) * (float) area.getHeight();
}

float KickEnvelopeCanvas::valueForY (float y) const
{
    const auto area = plotArea();
    return juce::jlimit (0.0f, 1.0f,
                         ((float) area.getBottom() - y) / (float) juce::jmax (1, area.getHeight()));
}

void KickEnvelopeCanvas::paintAxes (juce::Graphics& g)
{
    const auto area = plotArea();
    const double span = axisSpanSeconds();

    g.setColour (theme::outlineLight.withAlpha (0.22f));
    for (int i = 1; i < 4; ++i)
        g.drawHorizontalLine (area.getY() + area.getHeight() * i / 4,
                              (float) area.getX(), (float) area.getRight());

    g.setFont (theme::uiFont (8.5f));
    for (int i = 0; i <= 4; ++i)
    {
        const double t = span * i / 4.0;
        const int x = area.getX() + area.getWidth() * i / 4;
        if (i > 0 && i < 4)
        {
            g.setColour (theme::outlineLight.withAlpha (0.22f));
            g.drawVerticalLine (x, (float) area.getY(), (float) area.getBottom());
        }
        g.setColour (theme::textFaint);
        g.drawText (t < 1.0 ? juce::String (t * 1000.0, t < 0.1 ? 1 : 0) + " ms"
                            : juce::String (t, 2) + " s",
                    juce::jlimit (0, getWidth() - 56, x - 28), area.getBottom() + 1,
                    56, 12, juce::Justification::centred);
    }

    // Value gutter: Hz for the pitch envelope, per cent for the amplitude.
    g.setColour (theme::textFaint);
    if (role == kickenv::pitchRole)
    {
        const double startFreq = (double) channel.getProperty (ids::kickStartFreq, 240.0);
        const double endFreq = (double) channel.getProperty (ids::kickEndFreq, 48.0);
        for (int i = 0; i <= 2; ++i)
        {
            const double hz = endFreq + (startFreq - endFreq) * (1.0 - i * 0.5);
            g.drawText (juce::String (hz, 0), 0, area.getY() + area.getHeight() * i / 2 - 6,
                        32, 12, juce::Justification::centredRight);
        }
    }
    else
    {
        for (int i = 0; i <= 2; ++i)
            g.drawText (juce::String (100 - i * 50) + "%", 0,
                        area.getY() + area.getHeight() * i / 2 - 6, 32, 12,
                        juce::Justification::centredRight);
    }

    // The hold plateau, if any, is part of the amplitude picture.
    const double hold = roleOffsetSeconds (kickenv::ampRole);
    if (hold > 0.0)
    {
        g.setColour (theme::secondary.withAlpha (0.10f));
        g.fillRect (juce::Rectangle<float> ((float) area.getX(), (float) area.getY(),
                                            xForTime (hold) - (float) area.getX(),
                                            (float) area.getHeight()));
    }
}

void KickEnvelopeCanvas::paintCurve (juce::Graphics& g, const juce::String& forRole, bool active)
{
    const double offset = roleOffsetSeconds (forRole);
    const double span = roleSpanSeconds (forRole);
    const double axis = axisSpanSeconds();
    const bool drawn = kickenv::isDrawn (channel, forRole);

    // Drawn across the whole ruler in real time, so the two envelopes can be
    // read against each other however differently they are scaled.
    constexpr int steps = 320;
    juce::Path path;
    for (int i = 0; i < steps; ++i)
    {
        const double t = axis * i / (double) (steps - 1);
        const float value = t < offset
                                ? 1.0f
                                : kickdisplays::envelopeValueAt (channel, forRole, t - offset);
        const float x = xForTime (t);
        const float y = yForValue (value);
        if (i == 0)
            path.startNewSubPath (x, y);
        else
            path.lineTo (x, y);
    }

    const auto colour = forRole == kickenv::pitchRole ? theme::secondary : theme::accent;
    g.setColour (active ? colour : colour.withAlpha (0.25f));
    g.strokePath (path, juce::PathStrokeType (active ? 2.0f : 1.2f));

    if (! active || ! drawn)
        return;

    const auto envelope = kickenv::read (channel, forRole);
    for (size_t i = 0; i < envelope.points.size(); ++i)
    {
        const auto& point = envelope.points[i];
        const float x = xForTime (offset + span * point.pos);
        const float y = yForValue (point.value);
        g.setColour (theme::panelBg);
        g.fillEllipse (x - handleRadius, y - handleRadius, handleRadius * 2.0f, handleRadius * 2.0f);
        g.setColour (colour);
        g.drawEllipse (x - handleRadius, y - handleRadius, handleRadius * 2.0f, handleRadius * 2.0f, 1.6f);
    }
}

void KickEnvelopeCanvas::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds();
    g.setColour (theme::sunken);
    g.fillRoundedRectangle (area.toFloat(), 3.0f);

    paintAxes (g);
    paintCurve (g, role == kickenv::ampRole ? kickenv::pitchRole : kickenv::ampRole, false);
    paintCurve (g, role, true);

    g.setColour (theme::outline);
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 3.0f, 1.0f);

    if (! isDrawn())
    {
        g.setColour (theme::textFaint);
        g.setFont (theme::uiFont (9.5f, true));
        g.drawText ("ANALYTIC DECAY  /  press DRAW to edit points",
                    plotArea().reduced (6, 4), juce::Justification::topRight);
    }
}

int KickEnvelopeCanvas::pointAt (juce::Point<int> position) const
{
    const auto envelope = kickenv::read (channel, role);
    const double offset = roleOffsetSeconds (role);
    const double span = roleSpanSeconds (role);

    for (size_t i = 0; i < envelope.points.size(); ++i)
    {
        const auto& point = envelope.points[i];
        const juce::Point<float> handle { xForTime (offset + span * point.pos),
                                          yForValue (point.value) };
        if (handle.getDistanceFrom (position.toFloat()) <= grabRadius)
            return (int) i;
    }
    return -1;
}

int KickEnvelopeCanvas::segmentAt (juce::Point<int> position) const
{
    const auto envelope = kickenv::read (channel, role);
    if (envelope.points.size() < 2)
        return -1;

    const double offset = roleOffsetSeconds (role);
    const double span = roleSpanSeconds (role);
    const double t = timeForX ((float) position.x);
    const double u = span > 0.0 ? (t - offset) / span : 0.0;

    for (size_t i = 1; i < envelope.points.size(); ++i)
        if (u <= envelope.points[i].pos)
            return (int) i - 1;
    return -1;
}

void KickEnvelopeCanvas::commit (const kickdsp::Envelope& envelope, bool asOneGesture)
{
    juce::ignoreUnused (asOneGesture);
    kickenv::write (channel, role, envelope, &services.project.getUndoManager());
    repaint();
}

void KickEnvelopeCanvas::showMenu (int pointIndex)
{
    const auto current = kickenv::read (channel, role);
    const bool canDelete = pointIndex > 0 && pointIndex + 1 < (int) current.points.size();

    juce::PopupMenu menu;
    if (isDrawn())
    {
        menu.addItem (1, "Delete point", canDelete);
        menu.addItem (2, "Flatten segment curves");
        menu.addItem (3, "Reset to the default curve");
        menu.addItem (4, "Back to the analytic decay");
    }
    else
    {
        menu.addItem (5, "Draw this envelope");
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
        [this, pointIndex] (int result)
        {
            if (result == 0)
                return;
            if (result == 4) { setDrawn (false); return; }
            if (result == 5) { setDrawn (true); return; }

            const undoGesture::Scoped step (services.project, "Edit kick envelope");
            if (result == 3)
            {
                commit (kickenv::defaultFor (role), true);
                return;
            }

            auto envelope = kickenv::read (channel, role);
            if (result == 1 && pointIndex > 0 && pointIndex + 1 < (int) envelope.points.size())
                envelope.points.erase (envelope.points.begin() + pointIndex);
            else if (result == 2)
                for (auto& point : envelope.points)
                    point.tension = 0.0f;
            commit (envelope, true);
        });
}

void KickEnvelopeCanvas::mouseDown (const juce::MouseEvent& e)
{
    draggedPoint = -1;
    tensionSegment = -1;

    if (e.mods.isPopupMenu())
    {
        showMenu (pointAt (e.getPosition()));
        return;
    }
    if (! isDrawn())
        return;

    editing = kickenv::read (channel, role);
    draggedPoint = pointAt (e.getPosition());
    if (draggedPoint >= 0)
    {
        undoGesture::begin (services.project, "Move envelope point");
        return;
    }

    tensionSegment = segmentAt (e.getPosition());
    if (tensionSegment >= 0)
    {
        tensionStart = editing.points[(size_t) tensionSegment].tension;
        tensionStartY = e.getPosition().y;
        undoGesture::begin (services.project, "Bend envelope curve");
    }
}

void KickEnvelopeCanvas::mouseDrag (const juce::MouseEvent& e)
{
    const double offset = roleOffsetSeconds (role);
    const double span = juce::jmax (1.0e-6, roleSpanSeconds (role));

    if (draggedPoint >= 0 && draggedPoint < (int) editing.points.size())
    {
        auto& point = editing.points[(size_t) draggedPoint];
        point.value = valueForY ((float) e.getPosition().y);

        const bool isEnd = draggedPoint == 0
                           || draggedPoint + 1 == (int) editing.points.size();
        if (! isEnd)
        {
            const float lower = editing.points[(size_t) draggedPoint - 1].pos + 0.01f;
            const float upper = editing.points[(size_t) draggedPoint + 1].pos - 0.01f;
            point.pos = juce::jlimit (lower, juce::jmax (lower, upper),
                                      (float) ((timeForX ((float) e.getPosition().x) - offset) / span));
        }
        commit (editing, true);
    }
    else if (tensionSegment >= 0 && tensionSegment < (int) editing.points.size())
    {
        const float delta = (float) (tensionStartY - e.getPosition().y) / 80.0f;
        editing.points[(size_t) tensionSegment].tension = juce::jlimit (-1.0f, 1.0f,
                                                                        tensionStart + delta);
        commit (editing, true);
    }
}

void KickEnvelopeCanvas::mouseUp (const juce::MouseEvent&)
{
    if (draggedPoint >= 0 || tensionSegment >= 0)
        undoGesture::end (services.project);
    draggedPoint = -1;
    tensionSegment = -1;
}

void KickEnvelopeCanvas::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! isDrawn())
    {
        setDrawn (true);
        return;
    }
    if (pointAt (e.getPosition()) >= 0)
        return;

    const double offset = roleOffsetSeconds (role);
    const double span = juce::jmax (1.0e-6, roleSpanSeconds (role));
    const float pos = juce::jlimit (0.01f, 0.99f,
                                    (float) ((timeForX ((float) e.getPosition().x) - offset) / span));

    auto envelope = kickenv::read (channel, role);
    size_t insertAt = envelope.points.size();
    for (size_t i = 0; i < envelope.points.size(); ++i)
        if (envelope.points[i].pos > pos)
        {
            insertAt = i;
            break;
        }
    envelope.points.insert (envelope.points.begin() + (long) insertAt,
                            { pos, valueForY ((float) e.getPosition().y), 0.0f });

    const undoGesture::Scoped step (services.project, "Add envelope point");
    commit (envelope, true);
}

// ================= KickEditor =================

KickEditor::KickEditor (AppServices& s, juce::ValueTree ch)
    : services (s), channel (ch)
{
    buildPresetBar();

    canvas = std::make_unique<KickEnvelopeCanvas> (services, channel);
    canvas->onStateChanged = [this] { refreshEnvelopeButtons(); };
    addAndMakeVisible (*canvas);

    for (auto* tab : { &ampTab, &pitchTab })
    {
        tab->setWantsKeyboardFocus (false);
        tab->setClickingTogglesState (false);
        addAndMakeVisible (*tab);
    }
    ampTab.onClick   = [this] { canvas->setRole (kickenv::ampRole); refreshEnvelopeButtons(); };
    pitchTab.onClick = [this] { canvas->setRole (kickenv::pitchRole); refreshEnvelopeButtons(); };

    drawButton.setWantsKeyboardFocus (false);
    drawButton.setTooltip ("Convert this envelope into draggable points, or drop back to the "
                           "analytic decay");
    drawButton.onClick = [this] { canvas->setDrawn (! canvas->isDrawn()); };
    addAndMakeVisible (drawButton);

    auto rendered = std::make_unique<DraggableOutput> (channel, channel[ids::name].toString());
    output = rendered.get();
    outputHolder = std::move (rendered);
    addAndMakeVisible (*outputHolder);

    using namespace kickdisplays;

    // Row 1: the body and what shapes it.
    addModule ("BODY", std::make_unique<BodyDisplay> (channel),
               { ids::rootNote, ids::kickStartFreq, ids::kickEndFreq, ids::kickPitchDecay,
                 ids::kickBodyShape, ids::kickBodyHarm, ids::kickBodyPhase, ids::kickBodyLevel });
    addModule ("AMP", std::make_unique<AmpDisplay> (channel),
               { ids::kickAmpDecay, ids::kickHold, ids::envShape, ids::kickPunch });
    addModule ("SUB", std::make_unique<SubDisplay> (channel),
               { ids::kickSubLevel, ids::kickSubTune, ids::kickSubDecay });

    // Row 2: the transient layers and the distortion.
    {
        auto module = std::make_unique<DropModule> (channelparams::clickSection,
                                                    std::make_unique<ClickDisplay> (channel),
                                                    [this] (const juce::File& f) { loadClickSample (f); });
        for (const auto& id : { ids::kickClickLevel, ids::kickClickDecay,
                                ids::kickClickFreq, ids::kickClickType })
            if (auto knob = makeParamKnob (services, channel, "kick", id))
                module->addKnob (std::move (knob));
        addAndMakeVisible (*module);
        modules.push_back (std::move (module));
    }
    addModule ("NOISE", std::make_unique<NoiseDisplay> (channel),
               { ids::kickNoiseLevel, ids::kickNoiseDecay, ids::kickNoiseTone });
    addModule ("DRIVE", std::make_unique<DriveDisplay> (channel),
               { ids::drive, ids::driveCurve });

    // Row 3: the output chain.
    addModule ("EQ", std::make_unique<EqDisplay> (channel),
               { ids::kickEqLowFreq, ids::kickEqLowGain, ids::kickEqMidFreq,
                 ids::kickEqMidGain, ids::kickEqHighFreq, ids::kickEqHighGain });
    addModule ("OUT", std::make_unique<OutDisplay> (channel),
               { ids::kickComp, ids::kickLimit, ids::kickOutput });

    keyboard.setAvailableRange (24, 72);
    keyboard.setWantsKeyboardFocus (false);
    addAndMakeVisible (keyboard);

    bridge = std::make_unique<KeyboardBridge> (services, channel);
    keyboardState.addListener (bridge.get());
    services.liveNoteListeners.add (this);

    refreshEnvelopeButtons();
    startTimerHz (4);
    setSize (preferredWidth,
             10 + 26 + 6 + 196 + 8 + 3 * SynthModule::preferredHeight() + 2 * 8 + 8 + 52 + 10);
}

KickEditor::~KickEditor()
{
    services.liveNoteListeners.remove (this);
}

// Reflects live input on the on-screen keys. The bridge is detached first so
// the echo doesn't route the note straight back into the engine, which has
// already played it; the keyboard component keeps its own subscription and
// repaints.
void KickEditor::echoLiveNote (int channelId, int key, float velocity, bool on)
{
    if (channelId != (int) channel[ids::id])
        return;

    keyboardState.removeListener (bridge.get());
    if (on)
        keyboardState.noteOn (1, key, velocity);
    else
        keyboardState.noteOff (1, key, 0.0f);
    keyboardState.addListener (bridge.get());
}

void KickEditor::liveNoteOn (int channelId, int key, float velocity)
{
    echoLiveNote (channelId, key, velocity, true);
}

void KickEditor::liveNoteOff (int channelId, int key)
{
    echoLiveNote (channelId, key, 0.0f, false);
}

SynthModule& KickEditor::addModule (const juce::String& title,
                                    std::unique_ptr<juce::Component> display,
                                    std::initializer_list<juce::Identifier> params)
{
    auto module = std::make_unique<SynthModule> (title, std::move (display));
    for (const auto& id : params)
        if (auto knob = makeParamKnob (services, channel, "kick", id))
            module->addKnob (std::move (knob));

    addAndMakeVisible (*module);
    modules.push_back (std::move (module));
    return *modules.back();
}

// ---------------------------------------------------------------------------
// presets
// ---------------------------------------------------------------------------

void KickEditor::buildPresetBar()
{
    for (auto* button : { &prevButton, &nextButton })
    {
        button->setWantsKeyboardFocus (false);
        addAndMakeVisible (*button);
    }
    prevButton.setTooltip ("Previous preset in this category");
    nextButton.setTooltip ("Next preset in this category");
    prevButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (1); };

    categoryBox.setWantsKeyboardFocus (false);
    categoryBox.addItem ("All categories", 1);
    int id = 2;
    for (const auto& category : kickpresets::categories())
        categoryBox.addItem (category, id++);
    categoryBox.setSelectedId (1, juce::dontSendNotification);
    categoryBox.onChange = [this] { refreshPresetLists (false); };
    addAndMakeVisible (categoryBox);

    presetBox.setWantsKeyboardFocus (false);
    presetBox.setTextWhenNothingSelected ("Preset...");
    presetBox.onChange = [this]
    {
        if (presetBox.getSelectedId() > 0)
            applyPreset (presetBox.getText());
    };
    addAndMakeVisible (presetBox);

    previewButton.setWantsKeyboardFocus (false);
    previewButton.setTooltip ("Preview at the root note");
    previewButton.onClick = [this]
    {
        services.engine.previewNote (channel[ids::id],
                                     (int) channel.getProperty (ids::rootNote, 60), 1.0f, 200);
    };
    addAndMakeVisible (previewButton);

    exportButton.setWantsKeyboardFocus (false);
    exportButton.setTooltip ("Write this hit to a WAV (or drag it out of the render display)");
    exportButton.onClick = [this] { exportRender(); };
    addAndMakeVisible (exportButton);

    readoutLabel.setFont (theme::uiFont (10.5f));
    readoutLabel.setColour (juce::Label::textColourId, theme::textDim);
    readoutLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (readoutLabel);

    refreshPresetLists (true);
}

void KickEditor::refreshPresetLists (bool keepSelection)
{
    const auto wanted = keepSelection ? channel[ids::presetName].toString() : juce::String();
    shownPresetName = channel[ids::presetName].toString();
    const auto category = categoryBox.getSelectedId() <= 1 ? juce::String()
                                                           : categoryBox.getText();

    presetBox.clear (juce::dontSendNotification);
    int id = 1;
    for (const auto& preset : kickpresets::all())
        if (category.isEmpty() || preset.category == category)
            presetBox.addItem (preset.name, id++);

    if (wanted.isNotEmpty())
        for (int i = 0; i < presetBox.getNumItems(); ++i)
            if (presetBox.getItemText (i) == wanted)
            {
                presetBox.setSelectedItemIndex (i, juce::dontSendNotification);
                return;
            }
    presetBox.setSelectedId (0, juce::dontSendNotification);
}

void KickEditor::applyPreset (const juce::String& name)
{
    const auto* preset = kickpresets::find (name);
    if (preset == nullptr)
        return;

    const undoGesture::Scoped step (services.project, "Load kick preset");
    kickpresets::apply (channel, *preset, &services.project.getUndoManager());
    refreshEnvelopeButtons();
}

void KickEditor::stepPreset (int delta)
{
    if (presetBox.getNumItems() == 0)
        return;
    const int current = presetBox.getSelectedItemIndex();
    const int next = juce::jlimit (0, presetBox.getNumItems() - 1,
                                   current < 0 ? (delta > 0 ? 0 : presetBox.getNumItems() - 1)
                                               : current + delta);
    presetBox.setSelectedItemIndex (next, juce::sendNotificationSync);
}

// ---------------------------------------------------------------------------

void KickEditor::exportRender()
{
    const auto suggested = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                               .getChildFile (juce::File::createLegalFileName (
                                   channel[ids::name].toString()) + ".wav");
    auto chooser = std::make_shared<juce::FileChooser> ("Export kick", suggested, "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, chooser] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file.getFullPathName().isEmpty())
                return;
            const auto rendered = kickchannel::render (channel, 44100.0);
            if (auto writer = wavwriter::forFile (file.withFileExtension ("wav"), 44100.0, 2, 24))
                writer->writeFromAudioSampleBuffer (rendered, 0, rendered.getNumSamples());
            else
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                    "Export kick", "Could not write " + file.getFullPathName());
        });
}

void KickEditor::loadClickSample (const juce::File& file)
{
    const undoGesture::Scoped step (services.project, "Load click sample");
    auto* undo = &services.project.getUndoManager();
    channel.setProperty (ids::samplePath, file.getFullPathName(), undo);
    channel.setProperty (ids::kickClickType, (double) (int) kickdsp::ClickType::sample, undo);
}

bool KickEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;mp3;flac;ogg;m4a"))
            return true;
    return false;
}

void KickEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;mp3;flac;ogg;m4a"))
        {
            loadClickSample (juce::File (f));
            return;
        }
}

void KickEditor::refreshEnvelopeButtons()
{
    const bool onAmp = canvas->getRole() == kickenv::ampRole;
    ampTab.setColour (juce::TextButton::buttonColourId,
                      onAmp ? theme::accent.withAlpha (0.55f) : theme::raised);
    pitchTab.setColour (juce::TextButton::buttonColourId,
                        onAmp ? theme::raised : theme::secondary.withAlpha (0.55f));
    drawButton.setButtonText (canvas->isDrawn() ? "Analytic" : "Draw");
    repaint();
}

void KickEditor::timerCallback()
{
    if (output != nullptr)
        readoutLabel.setText (output->getSummary(), juce::dontSendNotification);

    // Presets can also be loaded from the control API or undone, so follow the
    // channel rather than only the combo box. Tracking the last name seen —
    // rather than comparing against the box — keeps a preset outside the
    // chosen category from rebuilding the list on every tick.
    const auto name = channel[ids::presetName].toString();
    if (name != shownPresetName)
    {
        shownPresetName = name;
        refreshPresetLists (true);
    }
}

void KickEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelBg);
}

void KickEditor::resized()
{
    auto r = getLocalBounds().reduced (10);

    // --- preset bar ---
    auto bar = r.removeFromTop (26);
    prevButton.setBounds (bar.removeFromLeft (26));
    bar.removeFromLeft (2);
    nextButton.setBounds (bar.removeFromLeft (26));
    bar.removeFromLeft (8);
    categoryBox.setBounds (bar.removeFromLeft (140));
    bar.removeFromLeft (6);
    presetBox.setBounds (bar.removeFromLeft (190));
    bar.removeFromLeft (10);
    previewButton.setBounds (bar.removeFromLeft (32));
    bar.removeFromLeft (6);
    exportButton.setBounds (bar.removeFromLeft (84));
    readoutLabel.setBounds (bar);
    r.removeFromTop (6);

    // --- envelope graph + render ---
    auto graphRow = r.removeFromTop (196);
    auto renderArea = graphRow.removeFromRight (300);
    outputHolder->setBounds (renderArea);
    graphRow.removeFromRight (8);

    auto tabs = graphRow.removeFromTop (22);
    ampTab.setBounds (tabs.removeFromLeft (60));
    tabs.removeFromLeft (4);
    pitchTab.setBounds (tabs.removeFromLeft (60));
    tabs.removeFromLeft (8);
    drawButton.setBounds (tabs.removeFromLeft (74));
    graphRow.removeFromTop (4);
    canvas->setBounds (graphRow);
    r.removeFromTop (8);

    // --- module rows; the first module of each row absorbs the slack ---
    auto keyboardArea = r.removeFromBottom (52);
    // 24..72 spans 29 white keys.
    keyboard.setKeyWidth (juce::jmax (8.0f, (float) keyboardArea.getWidth() / 29.0f));
    keyboard.setBounds (keyboardArea);
    r.removeFromBottom (8);

    const size_t rows[3][3] = { { 0, 1, 2 }, { 3, 4, 5 }, { 6, 7, SIZE_MAX } };
    for (const auto& row : rows)
    {
        auto line = r.removeFromTop (SynthModule::preferredHeight());
        r.removeFromTop (8);

        int fixed = 0, count = 0;
        for (size_t index : row)
            if (index != SIZE_MAX && index < modules.size())
            {
                fixed += modules[index]->preferredWidth();
                ++count;
            }
        const int slack = juce::jmax (0, line.getWidth() - fixed - (count - 1) * 8);

        bool first = true;
        for (size_t index : row)
        {
            if (index == SIZE_MAX || index >= modules.size())
                continue;
            const int w = modules[index]->preferredWidth() + (first ? slack : 0);
            modules[index]->setBounds (line.removeFromLeft (w));
            line.removeFromLeft (8);
            first = false;
        }
    }
}
