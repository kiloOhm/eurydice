#include "ChannelRackPanel.h"
#include "ChannelEditor.h"
#include "app/Theme.h"
#include "model/UndoGesture.h"
#include "model/ChannelParams.h"
#include "ui/automation/AutomationMenu.h"

namespace
{
// Menu id for "New insert" in both routing menus. Far above the per-insert
// ids (1..128 and 1000..1128), so handle it before the range checks.
constexpr int newInsertMenuId = 10000;

// Channel volume is already 0..1; pan is -1..1 folded onto the same range the
// engine unfolds again when it applies the curve.
double channelKnobNormalised (const juce::ValueTree& channel, const juce::Identifier& prop)
{
    return prop == ids::pan ? ((double) channel[ids::pan] + 1.0) * 0.5
                            : (double) channel[ids::volume];
}

AutomationWriter::Target channelKnobTarget (const juce::ValueTree& channel,
                                            const juce::Identifier& prop)
{
    const bool isPan = prop == ids::pan;
    return { "channel", (int) channel[ids::id], isPan ? "pan" : "volume",
             channel[ids::name].toString() + (isPan ? " pan" : " volume") };
}
}

ChannelRackPanel::ChannelRackPanel (AppServices& s)
    : services (s), graphLane (s.project)
{
    observedRoot = services.project.getRoot();
    observedRoot.addListener (this);

    patternBox.onChange = [this]
    {
        const int patId = patternBox.getSelectedId();
        if (patId > 0)
            services.project.getRoot().setProperty (ids::activePattern, patId, nullptr);
        for (auto& row : rows)
            row->setPattern (activePattern());
    };
    patternBox.setTooltip ("Active pattern");
    addAndMakeVisible (patternBox);

    addPatternButton.setWantsKeyboardFocus (false);
    addPatternButton.setTooltip ("New pattern");
    addPatternButton.onClick = [this]
    {
        auto& project = services.project;
        auto p = project.addPattern ("Pattern " + juce::String (project.numPatterns() + 1));
        project.getRoot().setProperty (ids::activePattern, (int) p[ids::id], nullptr);
        refreshHeader();
    };
    addAndMakeVisible (addPatternButton);

    patternMenuButton.setWantsKeyboardFocus (false);
    patternMenuButton.setTooltip ("Clone, rename, reorder or delete the pattern");
    patternMenuButton.onClick = [this] { showPatternMenu(); };
    addAndMakeVisible (patternMenuButton);

    lengthBox.setTooltip ("Pattern length");
    lengthBox.addItem ("16 steps", 16);
    lengthBox.addItem ("32 steps", 32);
    lengthBox.addItem ("64 steps", 64);
    lengthBox.onChange = [this]
    {
        auto pat = activePattern();
        if (pat.isValid() && lengthBox.getSelectedId() > 0)
        {
            const undoGesture::Scoped step (services.project, "Pattern length");
            pat.setProperty (ids::lengthTicks, lengthBox.getSelectedId() * ids::ticksPerStep,
                             &services.project.getUndoManager());
        }
        for (auto& row : rows)
            row->repaint();
        rowContainer.setSize (rowContainerWidth(), rowContainer.getHeight());
    };
    addAndMakeVisible (lengthBox);

    swingKnob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    swingKnob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    swingKnob.setRange (0.0, 1.0, 0.001);
    swingKnob.setWantsKeyboardFocus (false);
    swingKnob.setDoubleClickReturnValue (true, 0.0);
    swingKnob.setTooltip ("Swing for this pattern — until you turn it, the project swing applies "
                          "(\"...\" menu to go back)");
    swingKnob.onValueChange = [this]
    {
        if (auto pat = activePattern(); pat.isValid())
            services.project.setPatternSwing (pat, swingKnob.getValue());
    };
    undoGesture::attach (swingKnob, services.project, "Swing");
    addAndMakeVisible (swingKnob);

    swingLabel.setFont (theme::uiFont (9.0f, true));
    swingLabel.setColour (juce::Label::textColourId, theme::textFaint);
    swingLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (swingLabel);

    graphButton.setWantsKeyboardFocus (false);
    graphButton.setClickingTogglesState (true);
    graphButton.setTooltip ("Show the per-step velocity / pan / pitch graph");
    graphButton.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    graphButton.onClick = [this]
    {
        graphLane.setVisible (graphButton.getToggleState());
        resized();
    };
    addAndMakeVisible (graphButton);

    viewport.setViewedComponent (&rowContainer, false);
    viewport.setScrollBarsShown (true, true);
    viewport.onVisibleAreaChanged = [this] (juce::Rectangle<int> area)
    {
        graphLane.setScrollOffset (area.getX());
    };
    addAndMakeVisible (viewport);

    addChildComponent (graphLane);

    addChannelButton.setWantsKeyboardFocus (false);
    addChannelButton.setTooltip ("Add a sampler, synth, kick or plugin channel");
    addChannelButton.onClick = [this] { showAddChannelMenu(); };
    addAndMakeVisible (addChannelButton);

    rebuildRows();
    refreshHeader();
    startTimerHz (30);
}

ChannelRackPanel::~ChannelRackPanel()
{
    observedRoot.removeListener (this);
}

juce::ValueTree ChannelRackPanel::activePattern() const
{
    return services.project.getPatternById (observedRoot[ids::activePattern]);
}

juce::ValueTree ChannelRackPanel::selectedChannel() const
{
    auto channel = services.project.getChannelById (observedRoot[ids::selectedChannel]);
    return channel.isValid() ? channel : services.project.getChannel (0);
}

void ChannelRackPanel::refreshHeader()
{
    auto& project = services.project;

    patternBox.clear (juce::dontSendNotification);
    for (int i = 0; i < project.numPatterns(); ++i)
    {
        const auto p = project.getPattern (i);
        patternBox.addItem (p[ids::name].toString(), p[ids::id]);
    }
    patternBox.setSelectedId (observedRoot[ids::activePattern], juce::dontSendNotification);

    const auto pat = activePattern();
    if (pat.isValid())
        lengthBox.setSelectedId ((int) pat[ids::lengthTicks] / ids::ticksPerStep,
                                 juce::dontSendNotification);
    swingKnob.setValue (project.getSwingForPattern (pat), juce::dontSendNotification);
    // The star marks a pattern that no longer follows the project swing.
    swingLabel.setText (project.patternOverridesSwing (pat) ? "SWING*" : "SWING",
                        juce::dontSendNotification);

    graphLane.setPattern (pat);
    graphLane.setChannel (selectedChannel());
}

void ChannelRackPanel::rebuildRows()
{
    rows.clear();
    auto& project = services.project;
    const auto pattern = activePattern();

    for (int i = 0; i < project.numChannels(); ++i)
    {
        auto row = std::make_unique<ChannelRow> (project, project.getChannel (i));
        row->setPattern (pattern);
        row->onSelected = [this] (int channelId)
        {
            services.project.getRoot().setProperty (ids::selectedChannel, channelId, nullptr);
        };
        row->onWantsContextMenu = [this] (juce::ValueTree channel) { showChannelMenu (channel); };
        row->onOpenEditor = [this] (juce::ValueTree channel) { openChannelEditor (channel); };
        row->onWantsInsertMenu = [this] (juce::ValueTree channel) { showInsertMenu (channel); };
        row->onWantsPianoRoll = [this] (juce::ValueTree channel) { showPianoRollFor (channel); };
        row->onKnobMoved = [this] (juce::ValueTree channel, juce::Identifier prop)
        {
            services.automationWriter.touch (channelKnobTarget (channel, prop),
                                             channelKnobNormalised (channel, prop));
        };
        row->onKnobContextMenu = [this] (juce::ValueTree channel, juce::Identifier prop)
        {
            const bool isPan = prop == ids::pan;
            automationmenu::show (services, channelKnobTarget (channel, prop),
                                  channelKnobNormalised (channel, prop),
                                  [channel, prop, isPan, &undo = services.project.getUndoManager()]
                                  () mutable { channel.setProperty (prop, isPan ? 0.0 : 0.78, &undo); });
        };
        rowContainer.addAndMakeVisible (*row);
        rows.push_back (std::move (row));
    }

    rowContainer.setSize (rowContainerWidth(),
                          (int) rows.size() * (ChannelRow::rowHeight + rowGap));
    rowContainer.resized();
}

int ChannelRackPanel::rowContainerWidth() const
{
    int steps = 16;
    if (auto pat = activePattern(); pat.isValid())
        steps = juce::jmax (1, (int) pat[ids::lengthTicks] / ids::ticksPerStep);
    return ChannelRow::fixedLeftWidth + steps * ChannelRow::stepWidth + 8;
}

void ChannelRackPanel::showAddChannelMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Sampler channel");
    menu.addItem (2, "Synth channel (built-in)");
    menu.addItem (4, "Kick synth channel");

    const auto instruments = services.plugins.getInstruments();
    juce::PopupMenu instrumentMenu;
    for (int i = 0; i < instruments.size(); ++i)
        instrumentMenu.addItem (1000 + i, instruments[i].name + "  (" + instruments[i].pluginFormatName + ")");
    menu.addSubMenu ("Plugin instrument", instrumentMenu, instruments.size() > 0);
    menu.addSeparator();
    menu.addItem (3, services.plugins.isScanning() ? "Scanning..." : "Scan for plugins...",
                  ! services.plugins.isScanning());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addChannelButton),
        [this, instruments] (int result)
        {
            auto& project = services.project;
            if (result == 3)
            {
                services.plugins.startScan ([] {});
                return;
            }

            const undoGesture::Scoped step (project, "Add channel");
            if (result == 1)
                project.addChannel ("sampler", "Sampler " + juce::String (project.numChannels() + 1));
            else if (result == 2)
                project.addChannel ("synth", "Synth " + juce::String (project.numChannels() + 1));
            else if (result == 4)
                project.addChannel ("kick", "Kick " + juce::String (project.numChannels() + 1));
            else if (result >= 1000)
            {
                const auto desc = instruments[result - 1000];
                auto channel = project.addChannel ("plugin", desc.name);
                channel.setProperty (ids::pluginId, desc.createIdentifierString(),
                                     &project.getUndoManager());
            }
        });
}

void ChannelRackPanel::showPatternMenu()
{
    auto& project = services.project;
    auto pattern = activePattern();
    if (! pattern.isValid())
        return;

    const int index = project.patterns().indexOf (pattern);

    juce::PopupMenu menu;
    menu.addSectionHeader (pattern[ids::name].toString());
    menu.addItem (1, "Clone");
    menu.addItem (2, "Rename...");
    menu.addItem (3, "Delete", project.numPatterns() > 1);
    menu.addSeparator();
    menu.addItem (4, "Move left", index > 0);
    menu.addItem (5, "Move right", index < project.numPatterns() - 1);
    menu.addSeparator();
    menu.addItem (6, "Follow project swing", project.patternOverridesSwing (pattern));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (patternMenuButton),
        [this, pattern, index] (int result) mutable
        {
            if (result == 1)
            {
                const undoGesture::Scoped step (services.project, "Clone pattern");
                auto copy = services.project.clonePattern ((int) pattern[ids::id]);
                services.project.getRoot().setProperty (ids::activePattern,
                                                        (int) copy[ids::id], nullptr);
            }
            else if (result == 2)
            {
                auto* window = new juce::AlertWindow ("Rename pattern", {},
                                                      juce::MessageBoxIconType::NoIcon);
                window->addTextEditor ("name", pattern[ids::name].toString());
                window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
                window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                auto& model = services.project;
                window->enterModalState (true, juce::ModalCallbackFunction::create (
                    [window, pattern, &model] (int r) mutable
                    {
                        if (r == 1)
                        {
                            const undoGesture::Scoped step (model, "Rename pattern");
                            pattern.setProperty (ids::name, window->getTextEditorContents ("name"),
                                                 &model.getUndoManager());
                        }
                        delete window;
                    }));
            }
            else if (result == 3)
            {
                const undoGesture::Scoped step (services.project, "Delete pattern");
                services.project.removePattern ((int) pattern[ids::id]);
            }
            else if (result == 4 || result == 5)
            {
                const undoGesture::Scoped step (services.project, "Move pattern");
                services.project.movePattern (index, result == 4 ? index - 1 : index + 1);
                refreshHeader();
            }
            else if (result == 6)
            {
                services.project.clearPatternSwing (pattern);
                refreshHeader();
            }
        });
}

void ChannelRackPanel::openChannelEditor (juce::ValueTree channel)
{
    if (onOpenChannelEditor)
        onOpenChannelEditor (channel);
}

void ChannelRackPanel::showPianoRollFor (juce::ValueTree channel)
{
    services.project.getRoot().setProperty (ids::selectedChannel, (int) channel[ids::id], nullptr);
    if (onShowPianoRoll)
        onShowPianoRoll();
}

void ChannelRackPanel::showInsertMenu (juce::ValueTree channel)
{
    const int current = channel[ids::insertIndex];

    juce::PopupMenu menu;
    menu.addSectionHeader ("Route \"" + channel[ids::name].toString() + "\" to");
    for (int i = 0; i < services.project.numInserts(); ++i)
    {
        const auto insert = services.project.getInsert (i);
        menu.addItem (i + 1, insert[ids::name].toString(), true, i == current);
    }
    menu.addSeparator();
    menu.addItem (newInsertMenuId, "New insert",
                  services.project.numInserts() < ProjectModel::maxInserts);

    menu.showMenuAsync (juce::PopupMenu::Options().withMinimumWidth (160),
        [this, channel] (int result) mutable
        {
            if (result == newInsertMenuId)
            {
                const undoGesture::Scoped step (services.project, "Route channel");
                if (auto insert = services.project.addInsert(); insert.isValid())
                    channel.setProperty (ids::insertIndex, services.project.numInserts() - 1,
                                         &services.project.getUndoManager());
                return;
            }
            if (result > 0)
            {
                const undoGesture::Scoped step (services.project, "Route channel");
                channel.setProperty (ids::insertIndex, result - 1,
                                     &services.project.getUndoManager());
            }
        });
}

void ChannelRackPanel::showChannelMenu (juce::ValueTree channel)
{
    juce::PopupMenu insertMenu;
    for (int i = 0; i < services.project.numInserts(); ++i)
        insertMenu.addItem (1000 + i,
                            services.project.getInsert (i)[ids::name].toString(),
                            true,
                            (int) channel[ids::insertIndex] == i);
    insertMenu.addSeparator();
    insertMenu.addItem (newInsertMenuId, "New insert",
                        services.project.numInserts() < ProjectModel::maxInserts);

    const auto& generatorParams = channelparams::forChannelType (channel[ids::type].toString());

    juce::PopupMenu automationMenu;
    automationMenu.addItem (10, "Volume");
    automationMenu.addItem (11, "Pan");
    for (size_t i = 0; i < generatorParams.size(); ++i)
        if (generatorParams[i].automatable)
            automationMenu.addItem (200 + (int) i, generatorParams[i].caption);
    if (channel[ids::type].toString() == "plugin")
    {
        if (auto gen = std::dynamic_pointer_cast<PluginGenerator> (services.generators.getOrCreate (channel)))
            if (auto hosted = gen->getPlugin())
            {
                const auto& params = hosted->getInstance()->getParameters();
                automationMenu.addSeparator();
                for (int i = 0; i < juce::jmin (params.size(), 64); ++i)
                    automationMenu.addItem (100 + i, params[i]->getName (48));
            }
    }

    juce::PopupMenu menu;
    menu.addItem (5, "Piano roll");
    menu.addItem (6, "Channel settings");
    menu.addSeparator();
    menu.addItem (1, "Rename...");
    menu.addItem (2, "Delete channel");
    menu.addSeparator();
    menu.addSubMenu ("Route to mixer insert", insertMenu);
    menu.addSubMenu ("Create automation clip", automationMenu);

    menu.showMenuAsync ({}, [this, channel] (int result) mutable
    {
        auto& project = services.project;
        if (result == 1)
        {
            auto* window = new juce::AlertWindow ("Rename channel", {}, juce::MessageBoxIconType::NoIcon);
            window->addTextEditor ("name", channel[ids::name].toString());
            window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            window->enterModalState (true, juce::ModalCallbackFunction::create (
                [window, channel, &project] (int r) mutable
                {
                    if (r == 1)
                    {
                        const undoGesture::Scoped step (project, "Rename channel");
                        channel.setProperty (ids::name, window->getTextEditorContents ("name"),
                                             &project.getUndoManager());
                    }
                    delete window;
                }));
        }
        else if (result == 2)
        {
            const undoGesture::Scoped step (project, "Delete channel");
            project.removeChannel (channel);
        }
        else if (result == 5)
        {
            showPianoRollFor (channel);
        }
        else if (result == 6)
        {
            openChannelEditor (channel);
        }
        else if (result == 10 || result == 11)
        {
            const bool isPan = result == 11;
            const undoGesture::Scoped step (project, "Create automation");
            services.createAutomationWithClip ("channel", channel[ids::id],
                isPan ? "pan" : "volume",
                channel[ids::name].toString() + (isPan ? " pan" : " volume"),
                isPan ? ((double) channel[ids::pan] + 1.0) * 0.5 : (double) channel[ids::volume]);
        }
        else if (result >= 200 && result < 1000)
        {
            const auto& params = channelparams::forChannelType (channel[ids::type].toString());
            const auto index = (size_t) (result - 200);
            if (index >= params.size())
                return;
            const auto& descriptor = params[index];
            services.createAutomationWithClip ("channel-param", channel[ids::id],
                descriptor.id.toString(),
                channel[ids::name].toString() + " " + descriptor.caption,
                descriptor.toNormalised (channel.getProperty (descriptor.id, descriptor.defaultValue)));
        }
        else if (result >= 100 && result < 200)
        {
            const int paramIndex = result - 100;
            juce::String paramName = "param " + juce::String (paramIndex);
            double current = 0.5;
            if (auto gen = std::dynamic_pointer_cast<PluginGenerator> (services.generators.getOrCreate (channel)))
                if (auto hosted = gen->getPlugin())
                    if (auto* p = hosted->getInstance()->getParameters()[paramIndex])
                    {
                        paramName = p->getName (48);
                        current = p->getValue();
                    }
            const undoGesture::Scoped step (project, "Create automation");
            services.createAutomationWithClip ("plugin-channel", channel[ids::id],
                juce::String (paramIndex),
                channel[ids::name].toString() + " " + paramName, current);
        }
        else if (result == newInsertMenuId)
        {
            const undoGesture::Scoped step (project, "Route channel");
            if (auto insert = project.addInsert(); insert.isValid())
                channel.setProperty (ids::insertIndex, project.numInserts() - 1,
                                     &project.getUndoManager());
        }
        else if (result >= 1000)
        {
            const undoGesture::Scoped step (project, "Route channel");
            channel.setProperty (ids::insertIndex, result - 1000, &project.getUndoManager());
        }
    });
}

void ChannelRackPanel::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& prop)
{
    if (tree.hasType (ids::CHANNEL))
    {
        juce::ignoreUnused (prop);
        for (auto& row : rows)
            if (row->getChannelTree() == tree)
                row->refreshFromModel();
        graphLane.repaint();
    }
    else if (tree.hasType (ids::NOTE))
    {
        graphLane.repaint();
        for (auto& row : rows)
            row->repaint();
    }
    else if (prop == ids::selectedChannel)
    {
        graphLane.setChannel (selectedChannel());
    }
    else if (prop == ids::activePattern || prop == ids::lengthTicks || prop == ids::swing
             || prop == ids::name)
    {
        refreshHeader();
        for (auto& row : rows)
            row->setPattern (activePattern());
        rowContainer.setSize (rowContainerWidth(), rowContainer.getHeight());
    }
}

void ChannelRackPanel::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    if (child.hasType (ids::CHANNEL) || parent.hasType (ids::CHANNELS))
        rebuildRows();
    else if (child.hasType (ids::PATTERN))
    {
        refreshHeader();
        // A project load replaces the pattern set wholesale; rows built before
        // that would keep drawing the pattern object they were handed.
        for (auto& row : rows)
            row->setPattern (activePattern());
    }
    else if (child.hasType (ids::NOTE) || child.hasType (ids::LANE))
    {
        for (auto& row : rows)
            row->repaint();
        graphLane.repaint();
    }
}

void ChannelRackPanel::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    valueTreeChildAdded (parent, child);
}

void ChannelRackPanel::timerCallback()
{
    int step = -1;
    if (services.engine.isPlaying() && ! services.project.isSongMode())
    {
        if (auto pat = activePattern(); pat.isValid())
        {
            const double len = (double) (int) pat[ids::lengthTicks];
            if (len > 0)
                step = (int) (std::fmod (services.engine.getPositionTicks(), len) / ids::ticksPerStep);
        }
    }
    for (auto& row : rows)
        row->setPlayStep (step);
}

// ---------- sample drops (browser drag + Finder) ----------

sampledrop::RackTarget ChannelRackPanel::dropTargetAt (juce::Point<int> pos) const
{
    const auto local = rowContainer.getLocalPoint (this, pos);
    return sampledrop::rackTargetForY (local.y, (int) rows.size(),
                                       ChannelRow::rowHeight, rowGap);
}

void ChannelRackPanel::updateDropHover (juce::Point<int> pos)
{
    dropTarget = dropTargetAt (pos);

    // Only sampler rows can take a replacement sample.
    if (dropTarget.replaceRow >= 0)
    {
        const auto channel = services.project.getChannel (dropTarget.replaceRow);
        if (! channel.isValid() || channel[ids::type].toString() != "sampler")
            dropTarget.replaceRow = -1;
    }
    dropHoverActive = true;
    repaint();
}

void ChannelRackPanel::clearDropHover()
{
    if (dropHoverActive)
    {
        dropHoverActive = false;
        repaint();
    }
}

void ChannelRackPanel::performDrop (const juce::StringArray& files, juce::Point<int> pos)
{
    updateDropHover (pos);
    auto target = dropTarget;
    clearDropHover();

    const auto audio = sampledrop::audioFilesIn (files);
    if (audio.isEmpty())
        return;

    const undoGesture::Scoped step (services.project, "Drop sample");
    for (const auto& path : audio)
    {
        const bool inserting = target.replaceRow < 0;
        sampledrop::dropOntoRack (services.project, juce::File (path), target);
        target.replaceRow = -1;   // extra files always land below the first
        if (inserting)
            ++target.insertIndex;
    }
}

bool ChannelRackPanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    return ! sampledrop::audioFilesIn (files).isEmpty();
}

void ChannelRackPanel::fileDragEnter (const juce::StringArray&, int x, int y)  { updateDropHover ({ x, y }); }
void ChannelRackPanel::fileDragMove (const juce::StringArray&, int x, int y)   { updateDropHover ({ x, y }); }
void ChannelRackPanel::fileDragExit (const juce::StringArray&)                 { clearDropHover(); }

void ChannelRackPanel::filesDropped (const juce::StringArray& files, int x, int y)
{
    performDrop (files, { x, y });
}

bool ChannelRackPanel::isInterestedInDragSource (const SourceDetails& details)
{
    return ! sampledrop::filesFromDragSource (details).isEmpty();
}

void ChannelRackPanel::itemDragEnter (const SourceDetails& details)  { updateDropHover (details.localPosition); }
void ChannelRackPanel::itemDragMove (const SourceDetails& details)   { updateDropHover (details.localPosition); }
void ChannelRackPanel::itemDragExit (const SourceDetails&)           { clearDropHover(); }

void ChannelRackPanel::itemDropped (const SourceDetails& details)
{
    performDrop (sampledrop::filesFromDragSource (details), details.localPosition);
}

void ChannelRackPanel::paintOverChildren (juce::Graphics& g)
{
    if (! dropHoverActive)
        return;

    const int pitch = ChannelRow::rowHeight + rowGap;
    const auto visible = viewport.getBounds();

    if (dropTarget.replaceRow >= 0)
    {
        const int top = getLocalPoint (&rowContainer,
                                       juce::Point<int> (0, dropTarget.replaceRow * pitch)).y;
        const auto r = juce::Rectangle<int> (visible.getX(), top,
                                             visible.getWidth(), ChannelRow::rowHeight)
                           .getIntersection (visible);
        if (! r.isEmpty())
        {
            g.setColour (theme::accent.withAlpha (0.18f));
            g.fillRect (r);
            g.setColour (theme::accent);
            g.drawRect (r, 2);
        }
    }
    else
    {
        const int y = getLocalPoint (&rowContainer,
                                     juce::Point<int> (0, dropTarget.insertIndex * pitch)).y
                      - rowGap / 2;
        if (y >= visible.getY() - 2 && y <= visible.getBottom() + 2)
        {
            g.setColour (theme::accent);
            g.fillRect (visible.getX(), y - 1, visible.getWidth(), 2);
        }
    }
}

void ChannelRackPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelBg);
    g.setColour (theme::panelHeader);
    g.fillRect (getLocalBounds().removeFromTop (headerHeight));
    g.setColour (theme::outline);
    g.drawHorizontalLine (headerHeight, 0.0f, (float) getWidth());
}

void ChannelRackPanel::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (headerHeight).reduced (6, 4);

    graphButton.setBounds (header.removeFromRight (56));

    patternBox.setBounds (header.removeFromLeft (150));
    header.removeFromLeft (4);
    addPatternButton.setBounds (header.removeFromLeft (26));
    header.removeFromLeft (2);
    patternMenuButton.setBounds (header.removeFromLeft (26));
    header.removeFromLeft (10);
    lengthBox.setBounds (header.removeFromLeft (100));
    header.removeFromLeft (10);
    swingKnob.setBounds (header.removeFromLeft (26));
    swingLabel.setBounds (header.removeFromLeft (40));

    auto bottom = r.removeFromBottom (30).reduced (6, 3);
    addChannelButton.setBounds (bottom.removeFromLeft (110));

    if (graphLane.isVisible())
        graphLane.setBounds (r.removeFromBottom (StepGraphLane::laneHeight).reduced (2, 0));

    viewport.setBounds (r.reduced (2));
}
