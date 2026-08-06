#include "MixerPanel.h"
#include "app/Theme.h"
#include "model/UndoGesture.h"
#include "ui/automation/AutomationMenu.h"
#include "effects/CompressorEffect.h"

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
    undoGesture::attach (fader, owner.services.project, "Insert volume");
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
    undoGesture::attach (panKnob, owner.services.project, "Insert pan");
    addAndMakeVisible (panKnob);

    muteButton.setTooltip ("Mute this insert");
    muteButton.setClickingTogglesState (true);
    muteButton.setWantsKeyboardFocus (false);
    muteButton.setColour (juce::TextButton::buttonOnColourId, theme::record.darker (0.2f));
    muteButton.onClick = [this]
    {
        const undoGesture::Scoped step (owner.services.project, "Mute insert");
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

    addInsertButton.setTooltip ("Add a mixer insert");
    addInsertButton.setWantsKeyboardFocus (false);
    addInsertButton.onClick = [this]
    {
        const undoGesture::Scoped step (services.project, "Add insert");
        if (auto insert = services.project.addInsert(); insert.isValid())
            selectInsert (services.project.numInserts() - 1);
    };
    stripContainer.addAndMakeVisible (addInsertButton);

    rebuildStrips();
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
        b.setTooltip ("Effect slot: click to load, edit, bypass or remove");
        b.onClick = [this, slot] { showEffectSlotMenu (slot); };
        addAndMakeVisible (b);
    }

    addSendButton.setTooltip ("Send this insert's signal into another insert (bus / sidechain)");
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
        const auto pluginId = slotTree.isValid() ? slotTree[ids::pluginId].toString() : juce::String();
        if (pluginId.isNotEmpty())
        {
            const juce::String prefix = (bool) slotTree[ids::bypass] ? "[off] " : "";
            if (const auto* builtin = fx::findBuiltin (pluginId))
                label = prefix + builtin->name;
            else if (auto desc = services.plugins.findByIdentifier (pluginId))
                label = prefix + desc->name;
            else
                label = "(missing)";
            if (services.effects.isCrashed (selectedInsert, slot))
                label = "[crashed] " + label;
            else if (services.effects.peekSandboxed (selectedInsert, slot) != nullptr)
                label = label + " \xe2\xa7\x89";   // sandboxed marker
        }
        effectSlots[(size_t) slot].setButtonText (label);

        // The slot tooltip names the cost of sandboxing honestly.
        if (services.effects.peekSandboxed (selectedInsert, slot) != nullptr)
        {
            const double ms = 1000.0 * services.engine.getBlockSize()
                                     / juce::jmax (1.0, services.engine.getSampleRate());
            effectSlots[(size_t) slot].setTooltip (
                "Runs in its own process (a crash can't take the DAW down). Adds one "
                "audio block of latency (" + juce::String (ms, 1) + " ms at the current "
                "buffer size).");
        }
        else
        {
            effectSlots[(size_t) slot].setTooltip (
                "Effect slot: click to load, edit, bypass or remove");
        }
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
        undoGesture::attach (row->level, services.project, "Send level");
        addAndMakeVisible (row->level);

        row->remove.setWantsKeyboardFocus (false);
        const bool isDefaultMasterSend = selectedInsert != 0 && dest == 0 && i == 0;
        row->remove.setEnabled (! isDefaultMasterSend);
        row->remove.onClick = [this, sendCopy]() mutable
        {
            {
                const undoGesture::Scoped step (services.project, "Remove send");
                sendCopy.getParent().removeChild (sendCopy, &services.project.getUndoManager());
            }
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

    // Effect params for each filled slot: built-ins first, then hosted plugins.
    for (int slot = 0; slot < 10; ++slot)
    {
        if (auto builtin = services.builtinEffects.peek (insertIndex, slot))
        {
            juce::PopupMenu paramMenu;
            const auto& params = builtin->getParamSpecs();
            for (int i = 0; i < (int) params.size(); ++i)
                if (params[(size_t) i].choices.isEmpty() && ! params[(size_t) i].insertChooser)
                    paramMenu.addItem (5000 + slot * 100 + i, params[(size_t) i].name);
            const auto* entry = fx::findBuiltin (getSlotTree (insertIndex, slot, false)[ids::pluginId].toString());
            automationMenu.addSubMenu (entry != nullptr ? entry->name : juce::String ("Built-in"), paramMenu);
        }
        else if (auto hosted = services.effects.peek (insertIndex, slot))
        {
            juce::PopupMenu paramMenu;
            const auto& params = hosted->getInstance()->getParameters();
            for (int i = 0; i < juce::jmin (params.size(), 64); ++i)
                paramMenu.addItem (1000 + slot * 100 + i, params[i]->getName (48));
            automationMenu.addSubMenu (hosted->getDescription().name, paramMenu);
        }
        else if (auto sandboxed = services.effects.peekSandboxed (insertIndex, slot))
        {
            juce::PopupMenu paramMenu;
            const auto& names = sandboxed->getParamNames();
            for (int i = 0; i < juce::jmin (names.size(), 64); ++i)
                paramMenu.addItem (1000 + slot * 100 + i, names[i]);
            automationMenu.addSubMenu (sandboxed->getName(), paramMenu);
        }
    }

    const auto routedChannels = services.project.channelsRoutedTo (insertIndex);

    juce::PopupMenu menu;
    menu.addSectionHeader (insertTree (insertIndex)[ids::name].toString());
    menu.addItem (3, "Rename...");
    menu.addItem (4,
                  routedChannels.size() == 1
                      ? "Name after channel \"" + routedChannels[0] + "\""
                      : juce::String ("Name after channel"),
                  routedChannels.size() == 1);
    menu.addSeparator();

    // Post-fader routing to other inserts. Master stays the final bus: the
    // engine never sends from index 0. Items that would close a feedback
    // loop are disabled (unless already routed, so they can be unticked).
    if (insertIndex != 0)
    {
        juce::PopupMenu routeMenu;
        for (int i = 0; i < services.project.numInserts(); ++i)
        {
            if (i == insertIndex)
                continue;
            const bool routed = services.project.hasSend (insertIndex, i);
            const bool cycles = ! routed && services.project.sendWouldCycle (insertIndex, i);
            routeMenu.addItem (30000 + i,
                               (i == 0 ? "Master" : insertTree (i)[ids::name].toString())
                                   + (cycles ? " (would feed back)" : ""),
                               ! cycles, routed);
        }
        menu.addSubMenu ("Route to", routeMenu);
    }

    // One-click sidechain pump: a compressor keyed from another insert.
    juce::PopupMenu duckMenu;
    const bool haveFreeSlot = firstFreeSlot (insertIndex) >= 0;
    for (int i = 0; i < services.project.numInserts(); ++i)
        if (i != insertIndex)
            duckMenu.addItem (20000 + i, "from " + insertTree (i)[ids::name].toString(),
                              haveFreeSlot);
    menu.addSubMenu ("Sidechain duck", duckMenu);
    menu.addSeparator();
    menu.addSubMenu ("Create automation clip", automationMenu);

    menu.showMenuAsync ({}, [this, insertIndex, routedChannels] (int result)
    {
        if (result == 0)
            return;
        const auto insertName = insertTree (insertIndex)[ids::name].toString();
        auto ins = insertTree (insertIndex);

        if (result == 3)
        {
            auto* window = new juce::AlertWindow ("Rename insert", {},
                                                  juce::MessageBoxIconType::NoIcon);
            window->addTextEditor ("name", insertName);
            window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            auto& model = services.project;
            window->enterModalState (true, juce::ModalCallbackFunction::create (
                [window, ins, &model] (int r) mutable
                {
                    const auto typed = window->getTextEditorContents ("name").trim();
                    if (r == 1 && typed.isNotEmpty())
                    {
                        const undoGesture::Scoped step (model, "Rename insert");
                        ins.setProperty (ids::name, typed, &model.getUndoManager());
                    }
                    delete window;
                }));
            return;
        }
        if (result == 4)
        {
            if (routedChannels.size() == 1)
            {
                const undoGesture::Scoped step (services.project, "Rename insert");
                ins.setProperty (ids::name, routedChannels[0],
                                 &services.project.getUndoManager());
            }
            return;
        }
        if (result >= 30000)
        {
            const int dest = result - 30000;
            const undoGesture::Scoped step (services.project, "Route insert");
            services.project.setSendEnabled (insertIndex, dest,
                                             ! services.project.hasSend (insertIndex, dest));
            return;
        }
        if (result >= 20000)
        {
            createDuck (insertIndex, result - 20000);
            return;
        }

        const undoGesture::Scoped step (services.project, "Create automation");

        if (result == 1)
            services.createAutomationWithClip ("insert", insertIndex, "volume",
                insertName + " volume", (double) ins[ids::volume] / 1.25);
        else if (result == 2)
            services.createAutomationWithClip ("insert", insertIndex, "pan",
                insertName + " pan", ((double) ins[ids::pan] + 1.0) * 0.5);
        else if (result >= 5000)
        {
            const int slot = (result - 5000) / 100;
            const int paramIndex = (result - 5000) % 100;
            auto builtin = services.builtinEffects.peek (insertIndex, slot);
            if (builtin == nullptr)
                return;
            const auto& params = builtin->getParamSpecs();
            if (paramIndex < 0 || paramIndex >= (int) params.size())
                return;

            const auto& spec = params[(size_t) paramIndex];
            const juce::NormalisableRange<double> range (spec.minValue, spec.maxValue, 0.0, spec.skew);
            const double current = (double) getSlotTree (insertIndex, slot, false)
                                       .getProperty (spec.id, spec.defaultValue);
            services.createAutomationWithClip ("builtin-insert", insertIndex,
                juce::String (slot) + ":" + spec.id.toString(),
                insertName + " " + spec.name, range.convertTo0to1 (current));
        }
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
        if (services.effects.isCrashed (selectedInsert, slotIndex))
            menu.addItem (5, "Restart crashed plugin");
        menu.addItem (1, "Show editor");
        menu.addItem (2, "Bypass", true, (bool) slotTree[ids::bypass]);
        const bool isBuiltin = fx::findBuiltin (slotTree[ids::pluginId].toString()) != nullptr;
        if (! isBuiltin)
        {
            const bool sandboxNow = slotTree.hasProperty (ids::sandboxed)
                                        ? (bool) slotTree[ids::sandboxed]
                                        : services.effects.isSandboxEnabled();
            menu.addItem (6, "Run sandboxed (reloads the plugin)", true, sandboxNow);
        }
        menu.addItem (3, "Remove");
        menu.addSeparator();
    }

    const auto& builtins = fx::builtinEffects();
    juce::PopupMenu builtinList;
    for (int i = 0; i < (int) builtins.size(); ++i)
        builtinList.addItem (2000 + i, builtins[(size_t) i].name);
    menu.addSubMenu ("Built-in", builtinList);

    const auto plugins = services.plugins.getEffectsForDisplay();
    juce::PopupMenu pluginList;
    for (int i = 0; i < plugins.size(); ++i)
        pluginList.addItem (1000 + i, plugins[i].name + "  (" + plugins[i].pluginFormatName + ")");
    menu.addSubMenu (filled ? "Replace with plugin" : "Plugins", pluginList, plugins.size() > 0);
    menu.addSeparator();
    menu.addItem (4, services.plugins.isScanning() ? "Scanning..." : "Scan for plugins...",
                  ! services.plugins.isScanning());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (effectSlots[(size_t) slotIndex]),
        [this, slotIndex, plugins] (int result)
        {
            if (result == 0)
                return;
            auto& undo = services.project.getUndoManager();

            if (result == 1)
            {
                showEditorForSlot (slotIndex);
            }
            else if (result == 2)
            {
                const undoGesture::Scoped step (services.project, "Bypass effect");
                auto slot = getSlotTree (selectedInsert, slotIndex, true);
                slot.setProperty (ids::bypass, ! (bool) slot[ids::bypass], &undo);
                rebuildDetail();
            }
            else if (result == 3)
            {
                const undoGesture::Scoped step (services.project, "Remove effect");
                clearSlot (slotIndex);
                auto slot = getSlotTree (selectedInsert, slotIndex, false);
                if (slot.isValid())
                    slot.getParent().removeChild (slot, &undo);
                rebuildDetail();
            }
            else if (result == 5)
            {
                // Relaunch from the last state the project captured.
                const auto slot = getSlotTree (selectedInsert, slotIndex, false);
                services.effects.restartSandboxed (selectedInsert, slotIndex,
                    slot.isValid() ? slot[ids::pluginState].toString() : juce::String());
                rebuildDetail();
            }
            else if (result == 6)
            {
                // Flip the per-slot override and reload through the pool so
                // the plugin moves between processes with its state.
                auto slot = getSlotTree (selectedInsert, slotIndex, true);
                const bool now = slot.hasProperty (ids::sandboxed)
                                     ? (bool) slot[ids::sandboxed]
                                     : services.effects.isSandboxEnabled();
                const undoGesture::Scoped step (services.project, "Sandbox effect");
                slot.setProperty (ids::sandboxed, ! now, &undo);
                clearSlot (slotIndex);
                slot.setProperty (ids::pluginId, slot[ids::pluginId].toString(), &undo);
                rebuildDetail();
            }
            else if (result == 4)
            {
                services.plugins.startScan ([this] { rebuildDetail(); });
            }
            else if (result >= 2000)
            {
                const auto& builtin = fx::builtinEffects()[(size_t) (result - 2000)];
                clearSlot (slotIndex);
                auto slot = getSlotTree (selectedInsert, slotIndex, true);
                slot.setProperty (ids::pluginId, builtin.id, &undo);
                slot.setProperty (ids::pluginState, juce::String(), nullptr);
                BuiltinEffect::writeDefaults (slot, builtin.specs, &undo);
                rebuildDetail();
            }
            else if (result >= 1000)
            {
                const auto desc = plugins[result - 1000];
                const undoGesture::Scoped step (services.project, "Add effect");
                clearSlot (slotIndex);
                auto slot = getSlotTree (selectedInsert, slotIndex, true);
                slot.setProperty (ids::pluginId, desc.createIdentifierString(), &undo);
                slot.setProperty (ids::pluginState, juce::String(), nullptr);
                rebuildDetail();
            }
        });
}

// Drops whatever the slot currently holds — instance, editor window and all.
void MixerPanel::clearSlot (int slotIndex)
{
    if (auto plugin = services.effects.peek (selectedInsert, slotIndex))
        services.pluginWindows.closeFor (plugin.get());
    services.builtinEditors.closeFor (selectedInsert, slotIndex);
    services.effects.remove (selectedInsert, slotIndex);
    services.builtinEffects.remove (selectedInsert, slotIndex);
}

void MixerPanel::showEditorForSlot (int slotIndex)
{
    auto slot = getSlotTree (selectedInsert, slotIndex, false);
    if (! slot.isValid())
        return;

    const auto insertName = insertTree (selectedInsert)[ids::name].toString();
    const auto pluginId = slot[ids::pluginId].toString();

    if (const auto* builtin = fx::findBuiltin (pluginId))
    {
        services.builtinEditors.show (services.project, slot, *builtin, selectedInsert, slotIndex,
                                      insertName + " / " + builtin->name,
                                      services.builtinEffects.peek (selectedInsert, slotIndex));
        return;
    }

    if (auto plugin = services.effects.peek (selectedInsert, slotIndex))
    {
        services.pluginWindows.showEditorFor (plugin, insertName + " / "
                                                          + plugin->getDescription().name);
        return;
    }

    // Sandboxed: the editor lives in the helper's own window.
    if (auto sandboxed = services.effects.peekSandboxed (selectedInsert, slotIndex))
        sandboxed->showEditor (insertName + " / " + sandboxed->getName());
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
            const undoGesture::Scoped step (services.project, "Add send");
            auto targetSends = insertTree (selectedInsert).getChildWithName (ids::SENDS);
            juce::ValueTree newSend (ids::SEND);
            newSend.setProperty (ids::destInsert, result - 100, nullptr);
            newSend.setProperty (ids::level, 0.8, nullptr);
            targetSends.appendChild (newSend, &services.project.getUndoManager());
            rebuildDetail();
        });
}

void MixerPanel::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree.hasType (ids::INSERT))
    {
        for (auto& strip : strips)
            if (insertTree (strip->insertIndex) == tree)
                strip->refresh();
    }
    // Only the two properties the slot buttons show — parameter tweaks land on
    // the same tree and must not drag a full rebuild along with them.
    else if (tree.hasType (ids::SLOT) && (property == ids::pluginId || property == ids::bypass))
    {
        rebuildDetail();
    }
}

void MixerPanel::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    if (parent.hasType (ids::SENDS) || child.hasType (ids::SLOT))
        rebuildDetail();
    else if (child.hasType (ids::INSERT))
        rebuildStrips();
}

void MixerPanel::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    if (parent.hasType (ids::SENDS) || child.hasType (ids::SLOT))
        rebuildDetail();
    else if (child.hasType (ids::INSERT))
        rebuildStrips();
}

int MixerPanel::firstFreeSlot (int insertIndex) const
{
    for (int s = 0; s < (int) effectSlots.size(); ++s)
    {
        auto slot = const_cast<MixerPanel*> (this)->getSlotTree (insertIndex, s, false);
        if (! slot.isValid() || slot[ids::pluginId].toString().isEmpty())
            return s;
    }
    return -1;
}

// One-click genre pump: a compressor in the first free slot, keyed from
// another insert, with fast-attack / deep-ratio ducking settings rather than
// the gentle bus defaults.
void MixerPanel::createDuck (int insertIndex, int sourceInsert)
{
    const int slotIndex = firstFreeSlot (insertIndex);
    if (slotIndex < 0)
        return;

    auto& undo = services.project.getUndoManager();
    const undoGesture::Scoped step (services.project, "Sidechain duck");
    CompressorEffect::configureDuckSlot (getSlotTree (insertIndex, slotIndex, true),
                                         sourceInsert, &undo);
    selectInsert (insertIndex);
    rebuildDetail();
}

// (Re)creates one strip per insert; also runs at construction. Rebuilding on
// every count change keeps undo of "add insert" trivially correct.
void MixerPanel::rebuildStrips()
{
    strips.clear();
    for (int i = 0; i < services.project.numInserts(); ++i)
    {
        auto strip = std::make_unique<Strip> (*this, i);
        stripContainer.addAndMakeVisible (*strip);
        strips.push_back (std::move (strip));
    }
    selectedInsert = juce::jmin (selectedInsert, services.project.numInserts() - 1);
    addInsertButton.setEnabled (services.project.numInserts() < ProjectModel::maxInserts);
    resized();
    rebuildDetail();
}

void MixerPanel::timerCallback()
{
    if (++healthTick >= 24)
    {
        healthTick = 0;
        if (services.effects.checkHealth())
            rebuildDetail();   // crashed slots get their label
    }

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
    stripContainer.setSize ((int) strips.size() * stripW + 30, stripViewport.getHeight());
    int x = 0;
    for (auto& strip : strips)
    {
        strip->setBounds (x, 0, stripW, stripContainer.getHeight());
        x += stripW;
    }
    addInsertButton.setBounds (x + 4, 20, 22, 22);
}
