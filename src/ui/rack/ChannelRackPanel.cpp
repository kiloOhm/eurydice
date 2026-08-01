#include "ChannelRackPanel.h"
#include "app/Theme.h"

ChannelRackPanel::ChannelRackPanel (AppServices& s)
    : services (s)
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
    addAndMakeVisible (patternBox);

    addPatternButton.setWantsKeyboardFocus (false);
    addPatternButton.onClick = [this]
    {
        auto& project = services.project;
        auto p = project.addPattern ("Pattern " + juce::String (project.numPatterns() + 1));
        project.getRoot().setProperty (ids::activePattern, (int) p[ids::id], nullptr);
        refreshHeader();
    };
    addAndMakeVisible (addPatternButton);

    lengthBox.addItem ("16 steps", 16);
    lengthBox.addItem ("32 steps", 32);
    lengthBox.addItem ("64 steps", 64);
    lengthBox.onChange = [this]
    {
        auto pat = activePattern();
        if (pat.isValid() && lengthBox.getSelectedId() > 0)
            pat.setProperty (ids::lengthTicks, lengthBox.getSelectedId() * ids::ticksPerStep,
                             &services.project.getUndoManager());
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
    swingKnob.onValueChange = [this] { services.project.setSwing (swingKnob.getValue()); };
    addAndMakeVisible (swingKnob);

    swingLabel.setFont (theme::uiFont (9.0f, true));
    swingLabel.setColour (juce::Label::textColourId, theme::textFaint);
    swingLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (swingLabel);

    viewport.setViewedComponent (&rowContainer, false);
    viewport.setScrollBarsShown (true, true);
    addAndMakeVisible (viewport);

    addChannelButton.setWantsKeyboardFocus (false);
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
    swingKnob.setValue (project.getSwing(), juce::dontSendNotification);
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
        rowContainer.addAndMakeVisible (*row);
        rows.push_back (std::move (row));
    }

    rowContainer.setSize (rowContainerWidth(),
                          (int) rows.size() * (ChannelRow::rowHeight + 2));
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
            if (result == 1)
                project.addChannel ("sampler", "Sampler " + juce::String (project.numChannels() + 1));
            else if (result == 2)
                project.addChannel ("synth", "Synth " + juce::String (project.numChannels() + 1));
            else if (result == 3)
                services.plugins.startScan ([] {});
            else if (result >= 1000)
            {
                const auto desc = instruments[result - 1000];
                auto channel = project.addChannel ("plugin", desc.name);
                channel.setProperty (ids::pluginId, desc.createIdentifierString(),
                                     &project.getUndoManager());
            }
        });
}

void ChannelRackPanel::openChannelEditor (juce::ValueTree channel)
{
    if (channel[ids::type].toString() != "plugin")
        return;   // sampler/synth editors come later

    if (auto gen = std::dynamic_pointer_cast<PluginGenerator> (services.generators.getOrCreate (channel)))
        if (auto hosted = gen->getPlugin())
            services.pluginWindows.showEditorFor (hosted, channel[ids::name].toString());
}

void ChannelRackPanel::showChannelMenu (juce::ValueTree channel)
{
    juce::PopupMenu insertMenu;
    for (int i = 0; i < services.project.numInserts(); ++i)
        insertMenu.addItem (1000 + i,
                            services.project.getInsert (i)[ids::name].toString(),
                            true,
                            (int) channel[ids::insertIndex] == i);

    juce::PopupMenu automationMenu;
    automationMenu.addItem (10, "Volume");
    automationMenu.addItem (11, "Pan");
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
    menu.addItem (1, "Rename...");
    menu.addItem (2, "Delete channel");
    menu.addSeparator();
    menu.addSubMenu ("Route to mixer insert", insertMenu);
    menu.addSubMenu ("Create automation clip", automationMenu);

    menu.showMenuAsync ({}, [this, channel] (int result) mutable
    {
        auto& undo = services.project.getUndoManager();
        if (result == 1)
        {
            auto* window = new juce::AlertWindow ("Rename channel", {}, juce::MessageBoxIconType::NoIcon);
            window->addTextEditor ("name", channel[ids::name].toString());
            window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            window->enterModalState (true, juce::ModalCallbackFunction::create (
                [window, channel, &undo] (int r) mutable
                {
                    if (r == 1)
                        channel.setProperty (ids::name, window->getTextEditorContents ("name"), &undo);
                    delete window;
                }));
        }
        else if (result == 2)
        {
            services.project.removeChannel (channel);
        }
        else if (result == 10 || result == 11)
        {
            const bool isPan = result == 11;
            services.createAutomationWithClip ("channel", channel[ids::id],
                isPan ? "pan" : "volume",
                channel[ids::name].toString() + (isPan ? " pan" : " volume"),
                isPan ? ((double) channel[ids::pan] + 1.0) * 0.5 : (double) channel[ids::volume]);
        }
        else if (result >= 100 && result < 1000)
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
            services.createAutomationWithClip ("plugin-channel", channel[ids::id],
                juce::String (paramIndex),
                channel[ids::name].toString() + " " + paramName, current);
        }
        else if (result >= 1000)
        {
            channel.setProperty (ids::insertIndex, result - 1000, &undo);
        }
    });
}

void ChannelRackPanel::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& prop)
{
    if (tree.hasType (ids::CHANNEL))
    {
        for (auto& row : rows)
            if (row->getChannelTree() == tree)
                row->refreshFromModel();
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
        refreshHeader();
    else if (child.hasType (ids::NOTE) || child.hasType (ids::LANE))
        for (auto& row : rows)
            row->repaint();
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

    patternBox.setBounds (header.removeFromLeft (150));
    header.removeFromLeft (4);
    addPatternButton.setBounds (header.removeFromLeft (26));
    header.removeFromLeft (10);
    lengthBox.setBounds (header.removeFromLeft (100));
    header.removeFromLeft (10);
    swingKnob.setBounds (header.removeFromLeft (26));
    swingLabel.setBounds (header.removeFromLeft (40));

    auto bottom = r.removeFromBottom (30).reduced (6, 3);
    addChannelButton.setBounds (bottom.removeFromLeft (110));

    viewport.setBounds (r.reduced (2));
}
