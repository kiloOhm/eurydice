#include "MixerPanel.h"
#include "app/Theme.h"
#include "ui/automation/AutomationMenu.h"

namespace
{
// Insert volume runs 0..1.25 and pan -1..1; both fold onto the 0..1 the
// automation curve stores and the engine unfolds again.
double insertKnobNormalised (const juce::ValueTree& insert, const juce::Identifier& prop)
{
    return prop == ids::pan ? ((double) insert[ids::pan] + 1.0) * 0.5
                            : (double) insert[ids::volume] / 1.25;
}

AutomationWriter::Target insertKnobTarget (const juce::ValueTree& insert, int insertIndex,
                                           const juce::Identifier& prop)
{
    const bool isPan = prop == ids::pan;
    return { "insert", insertIndex, isPan ? "pan" : "volume",
             insert[ids::name].toString() + (isPan ? " pan" : " volume") };
}
}

// ================= Strip =================

MixerPanel::Strip::Strip (MixerPanel& o, int index)
    : insertIndex (index), owner (o)
{
    fader.setSliderStyle (juce::Slider::LinearVertical);
    fader.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    fader.setRange (0.0, 1.25, 0.001);
    fader.setDoubleClickReturnValue (true, 0.8);
    fader.setWantsKeyboardFocus (false);
    fader.onValueChange = [this]
    {
        owner.insertTree (insertIndex).setProperty (ids::volume, fader.getValue(),
                                                    &owner.services.project.getUndoManager());
        owner.knobMoved (insertIndex, ids::volume);
    };
    fader.onContextMenu = [this]
    {
        owner.selectInsert (insertIndex);
        owner.showKnobMenu (insertIndex, ids::volume);
    };
    addAndMakeVisible (fader);

    panKnob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    panKnob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    panKnob.setRange (-1.0, 1.0, 0.001);
    panKnob.setDoubleClickReturnValue (true, 0.0);
    panKnob.setWantsKeyboardFocus (false);
    panKnob.onValueChange = [this]
    {
        owner.insertTree (insertIndex).setProperty (ids::pan, panKnob.getValue(),
                                                    &owner.services.project.getUndoManager());
        owner.knobMoved (insertIndex, ids::pan);
    };
    panKnob.onContextMenu = [this]
    {
        owner.selectInsert (insertIndex);
        owner.showKnobMenu (insertIndex, ids::pan);
    };
    addAndMakeVisible (panKnob);

    muteButton.setClickingTogglesState (true);
    muteButton.setWantsKeyboardFocus (false);
    muteButton.setColour (juce::TextButton::buttonOnColourId, theme::record.darker (0.2f));
    muteButton.onClick = [this]
    {
        owner.insertTree (insertIndex).setProperty (ids::mute, muteButton.getToggleState(),
                                                    &owner.services.project.getUndoManager());
    };
    addAndMakeVisible (muteButton);

    refresh();
}

void MixerPanel::Strip::refresh()
{
    const auto tree = owner.insertTree (insertIndex);
    fader.setValue ((double) tree[ids::volume], juce::dontSendNotification);
    panKnob.setValue ((double) tree[ids::pan], juce::dontSendNotification);
    muteButton.setToggleState ((bool) tree[ids::mute], juce::dontSendNotification);
    repaint();
}

void MixerPanel::Strip::paint (juce::Graphics& g)
{
    const bool selected = owner.selectedInsert == insertIndex;
    g.fillAll (selected ? theme::raised : theme::panelBg);
    if (selected)
    {
        g.setColour (theme::accent);
        g.fillRect (getLocalBounds().removeFromTop (2));
    }

    const auto tree = owner.insertTree (insertIndex);
    g.setColour (theme::textPrimary);
    g.setFont (theme::uiFont (10.0f, insertIndex == 0));
    g.drawText (tree[ids::name].toString(), getLocalBounds().removeFromTop (18).reduced (2, 0),
                juce::Justification::centred);

    // meters
    auto drawMeter = [&g] (juce::Rectangle<int> r, float peak)
    {
        g.setColour (theme::sunken);
        g.fillRect (r);
        const float db = juce::Decibels::gainToDecibels (peak, -60.0f);
        const float norm = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        auto lit = r.removeFromBottom ((int) (norm * (float) r.getHeight()));
        g.setColour (peak > 1.0f ? theme::record : theme::ledGreen);
        g.fillRect (lit);
    };
    auto m = meterBounds;
    drawMeter (m.removeFromLeft (m.getWidth() / 2).reduced (1, 0), peakL);
    drawMeter (m.reduced (1, 0), peakR);

    g.setColour (theme::outline);
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
}

void MixerPanel::Strip::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (18);                     // name text
    panKnob.setBounds (r.removeFromTop (26).reduced ((getWidth() - 26) / 2, 0));
    muteButton.setBounds (r.removeFromBottom (20).reduced (18, 1));
    auto faderArea = r.reduced (4, 2);
    meterBounds = faderArea.removeFromRight (12);
    fader.setBounds (faderArea);
}

void MixerPanel::Strip::mouseDown (const juce::MouseEvent& e)
{
    owner.selectInsert (insertIndex);
    if (e.mods.isPopupMenu())
        owner.showStripMenu (insertIndex);
}

// ================= MixerPanel =================

MixerPanel::MixerPanel (AppServices& s)
    : services (s)
{
    observedRoot = services.project.getRoot();
    observedRoot.addListener (this);

    for (int i = 0; i < services.project.numInserts(); ++i)
    {
        auto strip = std::make_unique<Strip> (*this, i);
        stripContainer.addAndMakeVisible (*strip);
        strips.push_back (std::move (strip));
    }
    stripViewport.setViewedComponent (&stripContainer, false);
    stripViewport.setScrollBarsShown (false, true);
    addAndMakeVisible (stripViewport);

    detailName.setFont (theme::uiFont (13.0f, true));
    detailName.setColour (juce::Label::textColourId, theme::accent);
    addAndMakeVisible (detailName);

    for (int slot = 0; slot < (int) effectSlots.size(); ++slot)
    {
        auto& b = effectSlots[(size_t) slot];
        b.setButtonText ("---");
        b.setWantsKeyboardFocus (false);
        b.onClick = [this, slot] { showEffectSlotMenu (slot); };
        addAndMakeVisible (b);
    }

    addSendButton.setWantsKeyboardFocus (false);
    addSendButton.onClick = [this] { showSendMenu(); };
    addAndMakeVisible (addSendButton);

    selectInsert (0);
    startTimerHz (24);
}

MixerPanel::~MixerPanel()
{
    observedRoot.removeListener (this);
}

void MixerPanel::selectInsert (int index)
{
    selectedInsert = index;
    rebuildDetail();
    for (auto& strip : strips)
        strip->repaint();
}

void MixerPanel::rebuildDetail()
{
    const auto tree = insertTree (selectedInsert);
    detailName.setText (tree[ids::name].toString(), juce::dontSendNotification);

    for (int slot = 0; slot < (int) effectSlots.size(); ++slot)
    {
        auto slotTree = getSlotTree (selectedInsert, slot, false);
        juce::String label = "---";
        if (slotTree.isValid() && slotTree[ids::pluginId].toString().isNotEmpty())
        {
            if (auto desc = services.plugins.findByIdentifier (slotTree[ids::pluginId].toString()))
                label = ((bool) slotTree[ids::bypass] ? "[off] " : "") + desc->name;
            else
                label = "(missing)";
        }
        effectSlots[(size_t) slot].setButtonText (label);
    }

    sendRows.clear();
    const auto sends = tree.getChildWithName (ids::SENDS);
    for (int i = 0; i < sends.getNumChildren(); ++i)
    {
        auto send = sends.getChild (i);
        auto row = std::make_unique<SendRow>();
        row->send = send;

        const int dest = send[ids::destInsert];
        row->label.setText ("→ " + insertTree (dest)[ids::name].toString(),
                            juce::dontSendNotification);
        row->label.setFont (theme::uiFont (11.0f));
        addAndMakeVisible (row->label);

        row->level.setSliderStyle (juce::Slider::LinearHorizontal);
        row->level.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        row->level.setRange (0.0, 1.25, 0.001);
        row->level.setValue ((double) send[ids::level], juce::dontSendNotification);
        row->level.setWantsKeyboardFocus (false);
        auto& undo = services.project.getUndoManager();
        auto sendCopy = send;
        row->level.onValueChange = [rowPtr = row.get(), sendCopy, &undo]() mutable
        {
            sendCopy.setProperty (ids::level, rowPtr->level.getValue(), &undo);
        };
        addAndMakeVisible (row->level);

        row->remove.setWantsKeyboardFocus (false);
        const bool isDefaultMasterSend = selectedInsert != 0 && dest == 0 && i == 0;
        row->remove.setEnabled (! isDefaultMasterSend);
        row->remove.onClick = [this, sendCopy]() mutable
        {
            sendCopy.getParent().removeChild (sendCopy, &services.project.getUndoManager());
            rebuildDetail();
            resized();
        };
        addAndMakeVisible (row->remove);

        sendRows.push_back (std::move (row));
    }
    resized();
    repaint();
}

void MixerPanel::knobMoved (int insertIndex, const juce::Identifier& prop)
{
    const auto insert = insertTree (insertIndex);
    services.automationWriter.touch (insertKnobTarget (insert, insertIndex, prop),
                                     insertKnobNormalised (insert, prop));
}

void MixerPanel::showKnobMenu (int insertIndex, const juce::Identifier& prop)
{
    auto insert = insertTree (insertIndex);
    const bool isPan = prop == ids::pan;
    automationmenu::show (services, insertKnobTarget (insert, insertIndex, prop),
                          insertKnobNormalised (insert, prop),
                          [insert, prop, isPan, &undo = services.project.getUndoManager()]
                          () mutable { insert.setProperty (prop, isPan ? 0.0 : 0.8, &undo); });
}

void MixerPanel::showStripMenu (int insertIndex)
{
    juce::PopupMenu automationMenu;
    automationMenu.addItem (1, "Volume");
    automationMenu.addItem (2, "Pan");

    // Plugin params for each filled slot.
    for (int slot = 0; slot < 10; ++slot)
    {
        if (auto hosted = services.effects.peek (insertIndex, slot))
        {
            juce::PopupMenu paramMenu;
            const auto& params = hosted->getInstance()->getParameters();
            for (int i = 0; i < juce::jmin (params.size(), 64); ++i)
                paramMenu.addItem (1000 + slot * 100 + i, params[i]->getName (48));
            automationMenu.addSubMenu (hosted->getDescription().name, paramMenu);
        }
    }

    juce::PopupMenu menu;
    menu.addSubMenu ("Create automation clip", automationMenu);

    menu.showMenuAsync ({}, [this, insertIndex] (int result)
    {
        if (result == 0)
            return;
        const auto insertName = insertTree (insertIndex)[ids::name].toString();
        const auto ins = insertTree (insertIndex);

        if (result == 1)
            services.createAutomationWithClip ("insert", insertIndex, "volume",
                insertName + " volume", (double) ins[ids::volume] / 1.25);
        else if (result == 2)
            services.createAutomationWithClip ("insert", insertIndex, "pan",
                insertName + " pan", ((double) ins[ids::pan] + 1.0) * 0.5);
        else if (result >= 1000)
        {
            const int slot = (result - 1000) / 100;
            const int paramIndex = (result - 1000) % 100;
            juce::String paramName = "param";
            double current = 0.5;
            if (auto hosted = services.effects.peek (insertIndex, slot))
                if (auto* p = hosted->getInstance()->getParameters()[paramIndex])
                {
                    paramName = p->getName (48);
                    current = p->getValue();
                }
            services.createAutomationWithClip ("plugin-insert", insertIndex,
                juce::String (slot) + ":" + juce::String (paramIndex),
                insertName + " " + paramName, current);
        }
    });
}

juce::ValueTree MixerPanel::getSlotTree (int insertIndex, int slotIndex, bool createIfMissing)
{
    auto insert = insertTree (insertIndex);
    for (auto child : insert)
        if (child.hasType (ids::SLOT) && (int) child[ids::slotIndex] == slotIndex)
            return child;

    if (! createIfMissing)
        return {};

    juce::ValueTree slot (ids::SLOT);
    slot.setProperty (ids::slotIndex, slotIndex, nullptr);
    slot.setProperty (ids::bypass, false, nullptr);
    insert.appendChild (slot, &services.project.getUndoManager());
    return slot;
}

void MixerPanel::showEffectSlotMenu (int slotIndex)
{
    auto slotTree = getSlotTree (selectedInsert, slotIndex, false);
    const bool filled = slotTree.isValid() && slotTree[ids::pluginId].toString().isNotEmpty();

    juce::PopupMenu menu;
    if (filled)
    {
        menu.addItem (1, "Show editor");
        menu.addItem (2, "Bypass", true, (bool) slotTree[ids::bypass]);
        menu.addItem (3, "Remove");
        menu.addSeparator();
    }

    const auto fx = services.plugins.getEffects();
    juce::PopupMenu pluginList;
    for (int i = 0; i < fx.size(); ++i)
        pluginList.addItem (1000 + i, fx[i].name + "  (" + fx[i].pluginFormatName + ")");
    menu.addSubMenu (filled ? "Replace with" : "Select effect", pluginList, fx.size() > 0);
    menu.addSeparator();
    menu.addItem (4, services.plugins.isScanning() ? "Scanning..." : "Scan for plugins...",
                  ! services.plugins.isScanning());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (effectSlots[(size_t) slotIndex]),
        [this, slotIndex, fx] (int result)
        {
            if (result == 0)
                return;
            auto& undo = services.project.getUndoManager();

            if (result == 1)
            {
                if (auto plugin = services.effects.peek (selectedInsert, slotIndex))
                    services.pluginWindows.showEditorFor (plugin,
                        insertTree (selectedInsert)[ids::name].toString()
                        + " / " + plugin->getDescription().name);
            }
            else if (result == 2)
            {
                auto slot = getSlotTree (selectedInsert, slotIndex, true);
                slot.setProperty (ids::bypass, ! (bool) slot[ids::bypass], &undo);
                rebuildDetail();
            }
            else if (result == 3)
            {
                if (auto plugin = services.effects.peek (selectedInsert, slotIndex))
                    services.pluginWindows.closeFor (plugin.get());
                services.effects.remove (selectedInsert, slotIndex);
                auto slot = getSlotTree (selectedInsert, slotIndex, false);
                if (slot.isValid())
                    slot.getParent().removeChild (slot, &undo);
                rebuildDetail();
            }
            else if (result == 4)
            {
                services.plugins.startScan ([this] { rebuildDetail(); });
            }
            else if (result >= 1000)
            {
                const auto desc = fx[result - 1000];
                if (auto plugin = services.effects.peek (selectedInsert, slotIndex))
                    services.pluginWindows.closeFor (plugin.get());
                services.effects.remove (selectedInsert, slotIndex);
                auto slot = getSlotTree (selectedInsert, slotIndex, true);
                slot.setProperty (ids::pluginId, desc.createIdentifierString(), &undo);
                slot.setProperty (ids::pluginState, juce::String(), nullptr);
                rebuildDetail();
            }
        });
}

void MixerPanel::showSendMenu()
{
    juce::PopupMenu menu;
    const auto sends = insertTree (selectedInsert).getChildWithName (ids::SENDS);

    for (int i = 0; i < services.project.numInserts(); ++i)
    {
        if (i == selectedInsert)
            continue;
        bool exists = false;
        for (const auto send : sends)
            if ((int) send[ids::destInsert] == i)
                exists = true;
        menu.addItem (100 + i, insertTree (i)[ids::name].toString(), ! exists, exists);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addSendButton),
        [this] (int result)
        {
            if (result < 100)
                return;
            auto targetSends = insertTree (selectedInsert).getChildWithName (ids::SENDS);
            juce::ValueTree newSend (ids::SEND);
            newSend.setProperty (ids::destInsert, result - 100, nullptr);
            newSend.setProperty (ids::level, 0.8, nullptr);
            targetSends.appendChild (newSend, &services.project.getUndoManager());
            rebuildDetail();
        });
}

void MixerPanel::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&)
{
    if (tree.hasType (ids::INSERT))
        for (auto& strip : strips)
            if (insertTree (strip->insertIndex) == tree)
                strip->refresh();
}

void MixerPanel::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    if (parent.hasType (ids::SENDS) || child.hasType (ids::SLOT))
        rebuildDetail();
}

void MixerPanel::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    if (parent.hasType (ids::SENDS) || child.hasType (ids::SLOT))
        rebuildDetail();
}

void MixerPanel::timerCallback()
{
    if (auto* viewed = stripViewport.getViewedComponent())
    {
        juce::ignoreUnused (viewed);
        for (auto& strip : strips)
            strip->setPeaks (services.engine.getInsertPeak (strip->insertIndex, 0),
                             services.engine.getInsertPeak (strip->insertIndex, 1));
    }
}

void MixerPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelBg);
    g.setColour (theme::outline);
    g.drawVerticalLine (getWidth() - detailW, 0.0f, (float) getHeight());
}

void MixerPanel::resized()
{
    auto r = getLocalBounds();
    auto detail = r.removeFromRight (detailW).reduced (8, 6);

    detailName.setBounds (detail.removeFromTop (22));
    detail.removeFromTop (4);

    for (auto& b : effectSlots)
    {
        b.setBounds (detail.removeFromTop (19));
        detail.removeFromTop (2);
    }
    detail.removeFromTop (8);

    for (auto& row : sendRows)
    {
        auto line = detail.removeFromTop (20);
        row->remove.setBounds (line.removeFromRight (18));
        row->label.setBounds (line.removeFromLeft (86));
        row->level.setBounds (line);
        detail.removeFromTop (2);
    }
    detail.removeFromTop (4);
    addSendButton.setBounds (detail.removeFromTop (22).removeFromLeft (80));

    stripViewport.setBounds (r);
    stripContainer.setSize ((int) strips.size() * stripW, stripViewport.getHeight());
    int x = 0;
    for (auto& strip : strips)
    {
        strip->setBounds (x, 0, stripW, stripContainer.getHeight());
        x += stripW;
    }
}
