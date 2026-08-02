#include "PlaylistPanel.h"
#include "app/Theme.h"
#include "engine/EngineSnapshot.h"
#include "model/UndoGesture.h"
#include "ui/automation/AutomationEditor.h"

PlaylistPanel::PlaylistPanel (AppServices& s)
    : services (s)
{
    thumbFormats.registerBasicFormats();
    observedRoot = services.project.getRoot();
    observedRoot.addListener (this);

    snapBox.addItem ("Snap: Step", ids::ticksPerStep);
    snapBox.addItem ("Snap: Beat", ids::ticksPerQuarter);
    snapBox.addItem ("Snap: Bar",  ids::ticksPerBar);
    snapBox.setSelectedId (ids::ticksPerBar, juce::dontSendNotification);
    addAndMakeVisible (snapBox);

    startTimerHz (30);
}

PlaylistPanel::~PlaylistPanel()
{
    observedRoot.removeListener (this);
}

// ---------- geometry ----------

juce::Rectangle<int> PlaylistPanel::headerArea() const { return getLocalBounds().withHeight (headerH); }
juce::Rectangle<int> PlaylistPanel::rulerArea() const
{
    return { trackHeaderW, headerH, getWidth() - trackHeaderW, rulerH };
}
juce::Rectangle<int> PlaylistPanel::trackHeaderArea() const
{
    return { 0, headerH + rulerH, trackHeaderW, getHeight() - headerH - rulerH };
}
juce::Rectangle<int> PlaylistPanel::gridArea() const
{
    return { trackHeaderW, headerH + rulerH, getWidth() - trackHeaderW,
             getHeight() - headerH - rulerH };
}

double PlaylistPanel::xToTicks (int x) const  { return scrollTicks + (x - trackHeaderW) / pxPerTick; }
int PlaylistPanel::ticksToX (double t) const  { return trackHeaderW + (int) std::round ((t - scrollTicks) * pxPerTick); }
int PlaylistPanel::yToTrack (int y) const     { return (y - headerH - rulerH + scrollY) / trackHeight; }
int PlaylistPanel::trackTop (int i) const     { return headerH + rulerH + i * trackHeight - scrollY; }
int PlaylistPanel::snap() const               { return juce::jmax (1, snapBox.getSelectedId()); }
double PlaylistPanel::snapDown (double t) const { return std::floor (juce::jmax (0.0, t) / snap()) * snap(); }

// ---------- model ----------

juce::ValueTree PlaylistPanel::clipAt (juce::Point<int> pos, bool& overRightEdge)
{
    overRightEdge = false;
    const int trackIndex = yToTrack (pos.y);
    auto track = services.project.playlist().getChild (trackIndex);
    if (! track.isValid())
        return {};

    for (int i = track.getNumChildren(); --i >= 0;)
    {
        auto clip = track.getChild (i);
        const int x0 = ticksToX ((int) clip[ids::startTicks]);
        const int x1 = ticksToX ((int) clip[ids::startTicks] + (int) clip[ids::lengthTicks]);
        if (pos.x >= x0 && pos.x <= x1)
        {
            overRightEdge = (x1 - pos.x) <= 6;
            return clip;
        }
    }
    return {};
}

bool PlaylistPanel::loopRangeBounds (int& x0, int& x1) const
{
    const int startTicks = services.project.getLoopStart();
    const int endTicks   = services.project.getLoopEnd();
    if (endTicks <= startTicks)
        return false;

    x0 = ticksToX (startTicks);
    x1 = ticksToX (endTicks);
    return true;
}

void PlaylistPanel::addClipAt (juce::Point<int> pos)
{
    auto& project = services.project;
    const auto pattern = project.getPatternById (observedRoot[ids::activePattern]);
    if (! pattern.isValid())
        return;

    const int trackIndex = yToTrack (pos.y);
    if (trackIndex < 0 || trackIndex >= project.numPlaylistTracks())
        return;

    auto clip = project.addPlaylistClip ("pattern", trackIndex,
                                         (int) snapDown (xToTicks (pos.x)),
                                         (int) pattern[ids::lengthTicks]);
    clip.setProperty (ids::patternId, (int) pattern[ids::id], nullptr);
    dragClip = clip;
    drag = Drag::move;
    dragTickOffset = xToTicks (pos.x) - (double) (int) clip[ids::startTicks];
}

// ---------- painting ----------

void PlaylistPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelBg);

    // grid background
    const auto area = gridArea();
    g.saveState();
    g.reduceClipRegion (area);

    const int numTracks = services.project.numPlaylistTracks();
    for (int i = 0; i < numTracks; ++i)
    {
        const int y = trackTop (i);
        if (y + trackHeight < area.getY() || y > area.getBottom())
            continue;
        g.setColour (i % 2 == 0 ? theme::panelBg.brighter (0.03f) : theme::panelBg);
        g.fillRect (area.getX(), y, area.getWidth(), trackHeight);
        g.setColour (theme::outline.withAlpha (0.5f));
        g.drawHorizontalLine (y + trackHeight, (float) area.getX(), (float) area.getRight());
    }

    // bar lines
    const double firstBar = std::floor (scrollTicks / ids::ticksPerBar) * ids::ticksPerBar;
    for (double t = firstBar; ; t += ids::ticksPerBar)
    {
        const int x = ticksToX (t);
        if (x > area.getRight()) break;
        if (x >= area.getX())
        {
            const bool major = ((juce::int64) std::llround (t) / ids::ticksPerBar) % 4 == 0;
            g.setColour (major ? theme::outlineLight : theme::outline);
            g.drawVerticalLine (x, (float) area.getY(), (float) area.getBottom());
        }
    }

    if (int x0 = 0, x1 = 0; loopRangeBounds (x0, x1))
    {
        g.setColour (theme::accent.withAlpha (services.project.isLoopEnabled() ? 0.08f : 0.03f));
        g.fillRect (x0, area.getY(), juce::jmax (2, x1 - x0), area.getHeight());
    }

    paintClips (g);

    // playhead
    if (playheadTicks >= 0.0)
    {
        const int x = ticksToX (playheadTicks);
        if (x >= area.getX() && x <= area.getRight())
        {
            g.setColour (theme::accent);
            g.drawVerticalLine (x, (float) area.getY(), (float) area.getBottom());
        }
    }
    g.restoreState();

    paintRuler (g);
    paintTrackHeaders (g);

    // header strip
    g.setColour (theme::panelHeader);
    g.fillRect (headerArea());
    g.setColour (theme::outline);
    g.drawHorizontalLine (headerH - 1, 0.0f, (float) getWidth());
}

void PlaylistPanel::paintRuler (juce::Graphics& g)
{
    const auto area = rulerArea();
    g.setColour (theme::sunken);
    g.fillRect (area);

    g.saveState();
    g.reduceClipRegion (area);
    g.setFont (theme::uiFont (10.0f));

    if (int x0 = 0, x1 = 0; loopRangeBounds (x0, x1))
    {
        const bool enabled = services.project.isLoopEnabled();
        g.setColour (theme::accent.withAlpha (enabled ? 0.30f : 0.10f));
        g.fillRect (x0, area.getY(), juce::jmax (2, x1 - x0), area.getHeight());
        g.setColour (theme::accent.withAlpha (enabled ? 0.9f : 0.4f));
        g.drawVerticalLine (x0, (float) area.getY(), (float) area.getBottom());
        g.drawVerticalLine (x1, (float) area.getY(), (float) area.getBottom());
    }

    const double firstBar = std::floor (scrollTicks / ids::ticksPerBar) * ids::ticksPerBar;
    for (double t = firstBar; ; t += ids::ticksPerBar)
    {
        const int x = ticksToX (t);
        if (x > area.getRight()) break;
        if (x >= area.getX() - 30)
        {
            const int bar = (int) (std::llround (t) / ids::ticksPerBar) + 1;
            g.setColour (theme::textDim);
            g.drawText (juce::String (bar), x + 3, area.getY(), 40, area.getHeight(),
                        juce::Justification::centredLeft);
            g.setColour (theme::outlineLight);
            g.drawVerticalLine (x, (float) area.getY() + 4, (float) area.getBottom());
        }
    }

    if (playheadTicks >= 0.0)
    {
        const int x = ticksToX (playheadTicks);
        juce::Path marker;
        marker.addTriangle ((float) x - 5, (float) area.getY(),
                            (float) x + 5, (float) area.getY(),
                            (float) x, (float) area.getBottom() - 2);
        g.setColour (theme::accent);
        g.fillPath (marker);
    }
    g.restoreState();

    g.setColour (theme::outline);
    g.drawHorizontalLine (area.getBottom(), 0.0f, (float) getWidth());
}

void PlaylistPanel::paintTrackHeaders (juce::Graphics& g)
{
    const auto area = trackHeaderArea();
    g.saveState();
    g.reduceClipRegion (area);
    g.setColour (theme::panelHeader);
    g.fillRect (area);

    const auto playlistTree = services.project.playlist();
    for (int i = 0; i < playlistTree.getNumChildren(); ++i)
    {
        const int y = trackTop (i);
        if (y + trackHeight < area.getY() || y > area.getBottom())
            continue;

        const auto track = playlistTree.getChild (i);
        const bool muted = track[ids::mute];

        g.setColour (muted ? theme::ledOff : theme::ledGreen);
        g.fillEllipse (8.0f, (float) y + (float) trackHeight * 0.5f - 4.0f, 8.0f, 8.0f);

        g.setColour (muted ? theme::textFaint : theme::textPrimary);
        g.setFont (theme::uiFont (11.5f));
        g.drawText (track[ids::name].toString(), 22, y, area.getWidth() - 26, trackHeight,
                    juce::Justification::centredLeft);

        g.setColour (theme::outline.withAlpha (0.5f));
        g.drawHorizontalLine (y + trackHeight, 0.0f, (float) area.getRight());
    }

    g.setColour (theme::outline);
    g.drawVerticalLine (area.getRight() - 1, (float) area.getY(), (float) area.getBottom());
    g.restoreState();
}

void PlaylistPanel::paintClips (juce::Graphics& g)
{
    const auto playlistTree = services.project.playlist();
    auto& project = services.project;

    for (int i = 0; i < playlistTree.getNumChildren(); ++i)
    {
        const int y = trackTop (i);
        const auto track = playlistTree.getChild (i);

        for (const auto clip : track)
        {
            if (! clip.hasType (ids::CLIP))
                continue;

            const int x0 = ticksToX ((int) clip[ids::startTicks]);
            const int x1 = ticksToX ((int) clip[ids::startTicks] + (int) clip[ids::lengthTicks]);
            if (x1 < gridArea().getX() || x0 > gridArea().getRight())
                continue;

            juce::Rectangle<float> r ((float) x0, (float) y + 2.0f,
                                      (float) juce::jmax (6, x1 - x0) - 1.0f,
                                      (float) trackHeight - 4.0f);

            const bool isPattern = clip[ids::clipType].toString() == "pattern";
            auto base = isPattern ? theme::secondary : theme::accent;
            if ((bool) clip[ids::muted])
                base = base.withMultipliedSaturation (0.2f);

            g.setColour (base.withAlpha (0.28f));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (base);
            g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.2f);

            if (revealFramesLeft > 0 && clip == revealedClip)
            {
                g.setColour (juce::Colours::white.withAlpha (0.9f));
                g.drawRoundedRectangle (r.expanded (2.0f), 4.0f, 2.0f);
                g.setColour (base);
            }

            g.fillRect (r.removeFromTop (13.0f).reduced (1.0f, 1.0f));

            juce::String label = "Clip";
            if (isPattern)
            {
                const auto pattern = project.getPatternById (clip[ids::patternId]);
                if (pattern.isValid())
                    label = pattern[ids::name].toString();
            }
            else if (clip[ids::clipType].toString() == "audio")
            {
                const auto path = clip[ids::audioPath].toString();
                label = juce::File (path).getFileNameWithoutExtension();

                if (auto* thumb = getThumbnail (path); thumb != nullptr && thumb->getTotalLength() > 0.0)
                {
                    const double ratio = clip.hasProperty (ids::stretchRatio)
                                             ? (double) clip[ids::stretchRatio] : 1.0;
                    const double tps = (services.project.getTempo() / 60.0) * ids::ticksPerQuarter;
                    const double offsetSec = (double) (int) clip[ids::audioOffsetTicks] / tps / ratio;
                    const double windowSec = (double) (int) clip[ids::lengthTicks] / tps / ratio;

                    auto waveArea = juce::Rectangle<int> (x0 + 1, y + 15,
                                                          juce::jmax (4, x1 - x0) - 2,
                                                          trackHeight - 18);
                    g.setColour (theme::accent.withAlpha (0.75f));
                    thumb->drawChannels (g, waveArea, offsetSec,
                                         juce::jmin (thumb->getTotalLength(), offsetSec + windowSec),
                                         0.9f);
                }
            }
            else if (clip[ids::clipType].toString() == "automation")
            {
                const auto automation = project.getAutomationById (clip[ids::automationId]);
                if (automation.isValid())
                {
                    label = automation[ids::name].toString();

                    // curve preview
                    AutomationSnapshot snap;
                    for (const auto point : automation)
                        if (point.hasType (ids::POINT))
                            snap.points.push_back ({ (double) (int) point[ids::posTicks],
                                                     (float) (double) point[ids::value],
                                                     (float) (double) point[ids::tension] });
                    std::sort (snap.points.begin(), snap.points.end(),
                               [] (const AutomationPoint& a, const AutomationPoint& b)
                               { return a.posTicks < b.posTicks; });

                    juce::Path curve;
                    const float top = (float) y + 15.0f;
                    const float bottom = (float) y + (float) trackHeight - 3.0f;
                    bool started = false;
                    for (int px = juce::jmax (x0, gridArea().getX());
                         px <= juce::jmin (x1, gridArea().getRight()); px += 2)
                    {
                        const double local = xToTicks (px) - (double) (int) clip[ids::startTicks];
                        const float value = snap.valueAt (local);
                        const float cy = bottom - value * (bottom - top);
                        if (! started) { curve.startNewSubPath ((float) px, cy); started = true; }
                        else           curve.lineTo ((float) px, cy);
                    }
                    g.setColour (theme::accent.brighter (0.2f));
                    g.strokePath (curve, juce::PathStrokeType (1.6f));
                }
            }
            g.setColour (juce::Colours::black.withAlpha (0.8f));
            g.setFont (theme::uiFont (9.5f, true));
            g.drawText (label, (int) r.getX() + 3, y + 3, (int) r.getWidth() - 6, 11,
                        juce::Justification::centredLeft);
        }
    }
}

// ---------- interaction ----------

void PlaylistPanel::mouseDown (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();

    if (rulerArea().contains (pos))
    {
        if (e.mods.isPopupMenu())
        {
            services.project.clearLoop();
            repaint();
            return;
        }
        // Stays a seek until the mouse travels sideways; only then does it
        // become a loop-range drag, so plain clicks keep seeking.
        drag = Drag::seek;
        loopAnchorTicks = snapDown (xToTicks (pos.x));
        services.engine.setPositionTicks (loopAnchorTicks);
        return;
    }

    if (trackHeaderArea().contains (pos))
    {
        const int trackIndex = yToTrack (pos.y);
        auto track = services.project.playlist().getChild (trackIndex);
        if (track.isValid())
        {
            if (pos.x < 22)   // mute LED
            {
                const undoGesture::Scoped step (services.project, "Mute track");
                track.setProperty (ids::mute, ! (bool) track[ids::mute],
                                   &services.project.getUndoManager());
            }
            else if (e.mods.isPopupMenu() || e.getNumberOfClicks() == 2)
            {
                auto* window = new juce::AlertWindow ("Rename track", {}, juce::MessageBoxIconType::NoIcon);
                window->addTextEditor ("name", track[ids::name].toString());
                window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
                window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                auto& project = services.project;
                window->enterModalState (true, juce::ModalCallbackFunction::create (
                    [window, track, &project] (int r) mutable
                    {
                        if (r == 1)
                        {
                            const undoGesture::Scoped step (project, "Rename track");
                            track.setProperty (ids::name, window->getTextEditorContents ("name"),
                                               &project.getUndoManager());
                        }
                        delete window;
                    }));
            }
            repaint();
        }
        return;
    }

    if (! gridArea().contains (pos))
        return;

    bool overRightEdge = false;
    auto clip = clipAt (pos, overRightEdge);

    if (e.mods.isPopupMenu())
    {
        // Automation and audio clips get a menu instead of the eraser:
        // automation because the menu is the way into the curve editor,
        // audio because stretch options live there.
        if (clip.isValid() && clip[ids::clipType].toString() == "automation")
        {
            showAutomationClipMenu (clip);
            return;
        }
        if (clip.isValid() && clip[ids::clipType].toString() == "audio")
        {
            showAudioClipMenu (clip);
            return;
        }

        drag = Drag::erase;
        undoGesture::begin (services.project, "Delete clips");
        if (clip.isValid())
        {
            clip.getParent().removeChild (clip, &services.project.getUndoManager());
            repaint();
        }
        return;
    }

    if (clip.isValid())
    {
        if (e.getNumberOfClicks() == 2)
        {
            const auto type = clip[ids::clipType].toString();
            if (type == "automation")
            {
                auto automation = services.project.getAutomationById (clip[ids::automationId]);
                if (automation.isValid())
                    AutomationEditor::open (services, automation, clip[ids::lengthTicks]);
                return;
            }
            if (type == "pattern")
            {
                // FL habit: double-click a clip to edit that pattern.
                services.project.getRoot().setProperty (ids::activePattern,
                                                        (int) clip[ids::patternId], nullptr);
                if (onShowPianoRoll)
                    onShowPianoRoll();
                return;
            }
        }
        dragClip = clip;
        drag = overRightEdge ? Drag::resize : Drag::move;
        dragTickOffset = xToTicks (pos.x) - (double) (int) clip[ids::startTicks];
        undoGesture::begin (services.project, overRightEdge ? "Resize clip" : "Move clip");
    }
    else
    {
        undoGesture::begin (services.project, "Add clip");
        addClipAt (pos);
    }
    repaint();
}

void PlaylistPanel::mouseDrag (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    auto& undo = services.project.getUndoManager();

    switch (drag)
    {
        case Drag::seek:
            if (std::abs (e.getDistanceFromDragStartX()) <= 3)
            {
                services.engine.setPositionTicks (snapDown (xToTicks (pos.x)));
                return;
            }
            drag = Drag::loop;
            [[fallthrough]];

        case Drag::loop:
        {
            const double here = snapDown (xToTicks (pos.x));
            const int start = (int) juce::jmin (loopAnchorTicks, here);
            const int end   = (int) juce::jmax (loopAnchorTicks, here);
            services.project.setLoopRange (start, end > start ? end : start + snap());
            services.project.setLoopEnabled (true);
            repaint();
            return;
        }

        case Drag::erase:
        {
            bool edge;
            if (auto clip = clipAt (pos, edge); clip.isValid())
            {
                clip.getParent().removeChild (clip, &undo);
                repaint();
            }
            return;
        }

        case Drag::move:
        {
            if (! dragClip.isValid())
                return;
            const int newStart = (int) snapDown (xToTicks (pos.x) - dragTickOffset);
            dragClip.setProperty (ids::startTicks, juce::jmax (0, newStart), &undo);

            // Move across tracks too.
            const int trackIndex = juce::jlimit (0, services.project.numPlaylistTracks() - 1,
                                                 yToTrack (pos.y));
            auto targetTrack = services.project.playlist().getChild (trackIndex);
            if (targetTrack.isValid() && dragClip.getParent() != targetTrack)
            {
                auto parent = dragClip.getParent();
                auto copy = dragClip.createCopy();
                parent.removeChild (dragClip, &undo);
                targetTrack.appendChild (copy, &undo);
                dragClip = copy;
            }
            repaint();
            return;
        }

        case Drag::resize:
        {
            if (! dragClip.isValid())
                return;
            const double end = xToTicks (pos.x);
            const int start = dragClip[ids::startTicks];
            int len = (int) (std::ceil ((end - start) / snap()) * snap());
            len = juce::jmax (snap(), len);

            // Alt-resize on an audio clip = time-stretch to fit (FL stretch handle).
            if (e.mods.isAltDown() && dragClip[ids::clipType].toString() == "audio")
            {
                const double naturalSec = services.audioClips.getNaturalSeconds (
                    dragClip[ids::audioPath].toString());
                const double tps = (services.project.getTempo() / 60.0) * ids::ticksPerQuarter;
                if (naturalSec > 0.0)
                    dragClip.setProperty (ids::stretchRatio,
                        juce::jlimit (0.1, 10.0, (double) len / (naturalSec * tps)), &undo);
            }
            dragClip.setProperty (ids::lengthTicks, len, &undo);
            repaint();
            return;
        }

        case Drag::none:
            return;
    }
}

void PlaylistPanel::mouseUp (const juce::MouseEvent&)
{
    // Harmless when the gesture never wrote anything: an empty transaction is
    // never recorded.
    undoGesture::end (services.project);
    drag = Drag::none;
    dragClip = {};
}

void PlaylistPanel::mouseMove (const juce::MouseEvent& e)
{
    bool overRightEdge = false;
    if (gridArea().contains (e.getPosition()))
    {
        clipAt (e.getPosition(), overRightEdge);
        setMouseCursor (overRightEdge ? juce::MouseCursor::LeftRightResizeCursor
                                      : juce::MouseCursor::NormalCursor);
    }
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

void PlaylistPanel::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCommandDown())
    {
        const double factor = wheel.deltaY > 0 ? 1.15 : 1.0 / 1.15;
        const double mouseTicks = xToTicks (e.getPosition().x);
        pxPerTick = juce::jlimit (0.002, 0.5, pxPerTick * factor);
        scrollTicks = juce::jmax (0.0, mouseTicks - (e.getPosition().x - trackHeaderW) / pxPerTick);
    }
    else if (e.mods.isShiftDown())
    {
        scrollTicks = juce::jmax (0.0, scrollTicks - (wheel.deltaX + wheel.deltaY) * 4000.0);
    }
    else
    {
        const int contentH = services.project.numPlaylistTracks() * trackHeight;
        scrollY = juce::jlimit (0, juce::jmax (0, contentH - gridArea().getHeight()),
                                scrollY - (int) (wheel.deltaY * 120.0f));
        scrollTicks = juce::jmax (0.0, scrollTicks - wheel.deltaX * 4000.0);
    }
    repaint();
}

// ---------- listeners / timer ----------

void PlaylistPanel::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    const bool loopChanged = property == ids::loopStart || property == ids::loopEnd
                                 || property == ids::loopEnabled;

    if (tree.hasType (ids::CLIP) || tree.hasType (ids::TRACK) || tree.hasType (ids::PATTERN)
        || (tree.hasType (ids::PROJECT) && loopChanged))
        repaint();
}

void PlaylistPanel::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child)
{
    if (child.hasType (ids::CLIP) || child.hasType (ids::TRACK))
        repaint();
}

void PlaylistPanel::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int)
{
    if (child.hasType (ids::CLIP) || child.hasType (ids::TRACK))
        repaint();
}

void PlaylistPanel::revealClip (juce::ValueTree clip)
{
    if (! clip.isValid())
        return;

    revealedClip = clip;
    revealFramesLeft = 90;   // ~3 s at the 30 Hz timer

    const int startTicks = clip[ids::startTicks];
    if (startTicks < (int) scrollTicks
        || startTicks > (int) (scrollTicks + gridArea().getWidth() / pxPerTick))
        scrollTicks = juce::jmax (0.0, (double) startTicks - ids::ticksPerBar);

    const int trackIndex = services.project.playlist().indexOf (clip.getParent());
    if (trackIndex >= 0)
    {
        const int top = trackIndex * trackHeight;
        if (top < scrollY || top + trackHeight > scrollY + gridArea().getHeight())
            scrollY = juce::jmax (0, top - gridArea().getHeight() / 2);
    }
    repaint();
}

void PlaylistPanel::showAutomationClipMenu (juce::ValueTree clip)
{
    auto automation = services.project.getAutomationById (clip[ids::automationId]);

    juce::PopupMenu menu;
    menu.addSectionHeader (automation.isValid() ? automation[ids::name].toString()
                                                : juce::String ("Automation"));
    menu.addItem (1, "Open automation editor", automation.isValid());
    menu.addItem (2, (bool) clip[ids::muted] ? "Unmute clip" : "Mute clip");
    menu.addSeparator();
    menu.addItem (3, "Delete clip");

    menu.showMenuAsync ({}, [this, clip, automation] (int result) mutable
    {
        auto& undo = services.project.getUndoManager();
        if (result == 1 && automation.isValid())
            AutomationEditor::open (services, automation, clip[ids::lengthTicks]);
        else if (result == 2)
            clip.setProperty (ids::muted, ! (bool) clip[ids::muted], &undo);
        else if (result == 3)
            clip.getParent().removeChild (clip, &undo);
        repaint();
    });
}

void PlaylistPanel::showAudioClipMenu (juce::ValueTree clip)
{
    const int mode = clip[ids::stretchMode];

    juce::PopupMenu stretchMenu;
    stretchMenu.addItem (10, "Smooth", true, mode == 0);
    stretchMenu.addItem (11, "Percussive", true, mode == 1);
    stretchMenu.addItem (12, "Formant preserved", true, mode == 2);

    juce::PopupMenu menu;
    menu.addSectionHeader (juce::File (clip[ids::audioPath].toString()).getFileName());
    menu.addSubMenu ("Stretch mode", stretchMenu);
    menu.addItem (2, "Follow tempo (re-stretch on tempo change)", true,
                  (bool) clip[ids::followTempo]);
    menu.addSeparator();
    menu.addItem (3, (bool) clip[ids::muted] ? "Unmute clip" : "Mute clip");
    menu.addItem (4, "Delete clip");

    menu.showMenuAsync ({}, [this, clip] (int result) mutable
    {
        auto& project = services.project;
        auto& undo = project.getUndoManager();
        if (result >= 10 && result <= 12)
        {
            const undoGesture::Scoped step (project, "Stretch mode");
            clip.setProperty (ids::stretchMode, result - 10, &undo);
        }
        else if (result == 2)
        {
            const bool follow = ! (bool) clip[ids::followTempo];
            const undoGesture::Scoped step (project, "Follow tempo");
            clip.setProperty (ids::followTempo, follow, &undo);
            // Turning it on snaps the clip to the current tempo right away.
            if (follow)
                services.stretchFollower.refit (clip, &undo);
        }
        else if (result == 3)
        {
            const undoGesture::Scoped step (project, "Mute clip");
            clip.setProperty (ids::muted, ! (bool) clip[ids::muted], &undo);
        }
        else if (result == 4)
        {
            const undoGesture::Scoped step (project, "Delete clip");
            clip.getParent().removeChild (clip, &undo);
        }
        repaint();
    });
}

void PlaylistPanel::timerCallback()
{
    if (revealFramesLeft > 0)
    {
        --revealFramesLeft;
        if (revealFramesLeft == 0)
            repaint();
    }

    double newPlayhead = -1.0;
    if (services.project.isSongMode())
        newPlayhead = services.engine.getPositionTicks();
    if (! juce::approximatelyEqual (newPlayhead, playheadTicks))
    {
        playheadTicks = newPlayhead;
        repaint();
    }
}

juce::AudioThumbnail* PlaylistPanel::getThumbnail (const juce::String& path)
{
    if (auto it = thumbnails.find (path); it != thumbnails.end())
        return it->second.get();

    const juce::File file (path);
    if (! file.existsAsFile())
        return nullptr;

    auto thumb = std::make_unique<juce::AudioThumbnail> (256, thumbFormats, thumbCache);
    thumb->setSource (new juce::FileInputSource (file));
    auto* raw = thumb.get();
    thumbnails[path] = std::move (thumb);
    return raw;
}

bool PlaylistPanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;mp3;flac;ogg;m4a"))
            return true;
    return false;
}

void PlaylistPanel::filesDropped (const juce::StringArray& files, int x, int y)
{
    const juce::Point<int> pos (x, y);
    if (! gridArea().contains (pos))
        return;

    int trackIndex = juce::jlimit (0, services.project.numPlaylistTracks() - 1, yToTrack (pos.y));
    int startTicks = (int) snapDown (xToTicks (pos.x));

    for (const auto& path : files)
    {
        if (! juce::File (path).hasFileExtension ("wav;aif;aiff;mp3;flac;ogg;m4a"))
            continue;
        const double seconds = services.audioClips.getNaturalSeconds (path);
        if (seconds <= 0.0)
            continue;

        const double tps = (services.project.getTempo() / 60.0) * ids::ticksPerQuarter;
        const int lengthTicks = juce::jmax (ids::ticksPerStep, (int) (seconds * tps));

        auto clip = services.project.addPlaylistClip ("audio", trackIndex, startTicks, lengthTicks);
        clip.setProperty (ids::audioPath, path, nullptr);
        clip.setProperty (ids::stretchRatio, 1.0, nullptr);
        clip.setProperty (ids::audioOffsetTicks, 0, nullptr);

        trackIndex = juce::jmin (trackIndex + 1, services.project.numPlaylistTracks() - 1);
    }
    repaint();
}

void PlaylistPanel::resized()
{
    auto header = headerArea().reduced (6, 4);
    snapBox.setBounds (header.removeFromLeft (110));
}
