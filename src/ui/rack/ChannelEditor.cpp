#include "ChannelEditor.h"
#include "engine/SamplerGenerator.h"
#include "plugins/PluginGenerator.h"
#include "model/ChannelParams.h"
#include "ui/automation/AutomationMenu.h"

namespace
{
// Builds a generator's knob row from the shared parameter table and wires each
// knob into the automation layer: moving one records while the write arm is
// on, right-clicking one offers to create or edit its clip.
void buildKnobs (std::vector<std::unique_ptr<LabelledKnob>>& knobs, juce::Component& owner,
                 AppServices& services, juce::ValueTree channel)
{
    const int channelId = channel[ids::id];
    const auto channelName = channel[ids::name].toString();

    for (const auto& descriptor : channelparams::forChannelType (channel[ids::type].toString()))
    {
        auto knob = std::make_unique<LabelledKnob> (descriptor.caption, services.project, channel,
                                                    descriptor.id, descriptor.range,
                                                    descriptor.defaultValue, descriptor.suffix,
                                                    descriptor.decimals);
        if (descriptor.automatable)
        {
            // The table is a function-local static, so &descriptor outlives
            // every editor window that captures it.
            const AutomationWriter::Target target { "channel-param", channelId,
                                                    descriptor.id.toString(),
                                                    channelName + " " + descriptor.caption };
            auto* knobPtr = knob.get();

            knob->onLiveEdit = [&services, target, &descriptor] (double value)
            {
                services.automationWriter.touch (target, descriptor.toNormalised (value));
            };
            knob->onContextMenu = [&services, target, &descriptor, knobPtr] (double value)
            {
                automationmenu::show (services, target, descriptor.toNormalised (value),
                                      [knobPtr] { knobPtr->resetToDefault(); });
            };
        }
        owner.addAndMakeVisible (*knob);
        knobs.push_back (std::move (knob));
    }
}
}

// ================= SamplerEditor =================

SamplerEditor::SamplerEditor (AppServices& s, juce::ValueTree ch)
    : services (s), channel (ch)
{
    loadButton.setWantsKeyboardFocus (false);
    loadButton.onClick = [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> (
            "Load sample", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
            "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this, chooser] (const juce::FileChooser& fc)
            {
                if (fc.getResult().existsAsFile())
                    loadSample (fc.getResult());
            });
    };
    addAndMakeVisible (loadButton);

    previewButton.setWantsKeyboardFocus (false);
    previewButton.setTooltip ("Preview at the root note");
    previewButton.onClick = [this]
    {
        services.engine.previewNote (channel[ids::id],
                                     (int) channel.getProperty (ids::rootNote, 60), 0.9f, 1200);
    };
    addAndMakeVisible (previewButton);

    oneShotButton.setWantsKeyboardFocus (false);
    oneShotButton.setToggleState (channel.getProperty (ids::oneShot, true),
                                  juce::dontSendNotification);
    oneShotButton.onClick = [this]
    {
        channel.setProperty (ids::oneShot, oneShotButton.getToggleState(),
                             &services.project.getUndoManager());
    };
    addAndMakeVisible (oneShotButton);

    pathLabel.setFont (theme::uiFont (11.0f));
    pathLabel.setColour (juce::Label::textColourId, theme::textDim);
    pathLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (pathLabel);

    buildKnobs (knobs, *this, services, channel);

    refreshWaveform();
    startTimerHz (4);
    setSize (480, 300);
}

void SamplerEditor::loadSample (const juce::File& file)
{
    channel.setProperty (ids::samplePath, file.getFullPathName(),
                         &services.project.getUndoManager());
    // Give the pool a beat to reload, then repaint the waveform.
    juce::Timer::callAfterDelay (120, [this] { refreshWaveform(); });
}

void SamplerEditor::refreshWaveform()
{
    auto generator = services.generators.getOrCreate (channel);
    if (auto* sampler = dynamic_cast<SamplerGenerator*> (generator.get()))
    {
        waveform = sampler->getWaveformOutline (juce::jmax (32, waveformArea().getWidth()));
        waveformForPath = sampler->getSamplePath();

        const auto path = channel[ids::samplePath].toString();
        pathLabel.setText (path.isEmpty() ? "(built-in " + channel[ids::name].toString().toLowerCase() + ")"
                                          : juce::File (path).getFileName(),
                           juce::dontSendNotification);
    }
    repaint();
}

void SamplerEditor::timerCallback()
{
    // Pick up samples loaded from elsewhere (browser drop, API, undo).
    if (channel[ids::samplePath].toString() != waveformForPath
        && channel[ids::samplePath].toString().isNotEmpty())
        refreshWaveform();

    oneShotButton.setToggleState (channel.getProperty (ids::oneShot, true),
                                  juce::dontSendNotification);
    for (auto& knob : knobs)
        knob->refresh();
}

juce::Rectangle<int> SamplerEditor::waveformArea() const
{
    return getLocalBounds().withTrimmedTop (66).withHeight (78).reduced (10, 0);
}

bool SamplerEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;mp3;flac;ogg;m4a"))
            return true;
    return false;
}

void SamplerEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;mp3;flac;ogg;m4a"))
        {
            loadSample (juce::File (f));
            return;
        }
}

void SamplerEditor::mouseDown (const juce::MouseEvent& e)
{
    if (waveformArea().contains (e.getPosition()))
        previewButton.triggerClick();
}

void SamplerEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelBg);

    auto wave = waveformArea();
    g.setColour (theme::sunken);
    g.fillRoundedRectangle (wave.toFloat(), 3.0f);

    if (waveform.empty())
    {
        g.setColour (theme::textFaint);
        g.setFont (theme::uiFont (11.0f));
        g.drawText ("Drop a sample here", wave, juce::Justification::centred);
    }
    else
    {
        g.setColour (theme::accent.withAlpha (0.85f));
        const float midY = (float) wave.getCentreY();
        const float halfH = (float) wave.getHeight() * 0.45f;
        const float step = (float) wave.getWidth() / (float) waveform.size();
        for (size_t i = 0; i < waveform.size(); ++i)
        {
            const float x = (float) wave.getX() + (float) i * step;
            const float h = juce::jmax (1.0f, waveform[i] * halfH);
            g.fillRect (x, midY - h, juce::jmax (1.0f, step - 0.5f), h * 2.0f);
        }
    }

    g.setColour (theme::outline);
    g.drawRoundedRectangle (wave.toFloat(), 3.0f, 1.0f);

    g.setColour (theme::textFaint);
    g.setFont (theme::uiFont (9.5f, true));
    g.drawText ("AMP ENVELOPE / FILTER", 10, wave.getBottom() + 6, 240, 12,
                juce::Justification::centredLeft);
}

void SamplerEditor::resized()
{
    auto r = getLocalBounds().reduced (10);

    auto top = r.removeFromTop (26);
    loadButton.setBounds (top.removeFromLeft (120));
    top.removeFromLeft (6);
    previewButton.setBounds (top.removeFromLeft (32));
    top.removeFromLeft (8);
    pathLabel.setBounds (top);

    r.removeFromTop (4);
    oneShotButton.setBounds (r.removeFromTop (22));

    r.removeFromTop (78 + 24);   // waveform + section caption (painted)

    auto knobRow = r.removeFromTop (LabelledKnob::preferredHeight);
    for (auto& knob : knobs)
    {
        knob->setBounds (knobRow.removeFromLeft (LabelledKnob::preferredWidth));
        knobRow.removeFromLeft (2);
    }
}

// ================= SynthEditor =================

SynthEditor::SynthEditor (AppServices& s, juce::ValueTree ch)
    : services (s), channel (ch)
{
    buildKnobs (knobs, *this, services, channel);

    // Captions sit above the knob the section starts at, so these indices
    // follow the order of channelparams::synth().
    sections.emplace_back ("OSCILLATORS", 0);
    sections.emplace_back ("FILTER", 3);
    sections.emplace_back ("ENVELOPE", 6);

    keyboard.setAvailableRange (36, 96);
    keyboard.setWantsKeyboardFocus (false);
    addAndMakeVisible (keyboard);

    // Route the on-screen keyboard through the engine's preview path.
    struct KeyboardBridge : juce::MidiKeyboardState::Listener
    {
        KeyboardBridge (AppServices& s, juce::ValueTree c) : services (s), channel (c) {}
        void handleNoteOn (juce::MidiKeyboardState*, int, int note, float vel) override
        {
            services.engine.previewNote (channel[ids::id], note, juce::jmax (0.3f, vel), 0);
        }
        void handleNoteOff (juce::MidiKeyboardState*, int, int note, float) override
        {
            services.engine.previewNoteOff (channel[ids::id], note);
        }
        AppServices& services;
        juce::ValueTree channel;
    };
    bridge = std::make_unique<KeyboardBridge> (services, channel);
    keyboardState.addListener (bridge.get());

    setSize (700, 260);
}

void SynthEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelBg);

    g.setColour (theme::textFaint);
    g.setFont (theme::uiFont (9.5f, true));
    for (const auto& [caption, firstKnob] : sections)
    {
        if (firstKnob >= (int) knobs.size())
            continue;
        const auto bounds = knobs[(size_t) firstKnob]->getBounds();
        g.drawText (caption, bounds.getX(), bounds.getY() - 16, 160, 12,
                    juce::Justification::centredLeft);
    }
}

void SynthEditor::resized()
{
    auto r = getLocalBounds().reduced (10);

    auto keyboardArea = r.removeFromBottom (56);
    // Size the keys so the range exactly fills the width (36..96 spans 36 white keys).
    keyboard.setKeyWidth (juce::jmax (8.0f, (float) keyboardArea.getWidth() / 36.0f));
    keyboard.setBounds (keyboardArea);
    r.removeFromBottom (8);
    r.removeFromTop (16);   // room for the first section caption

    auto row = r.removeFromTop (LabelledKnob::preferredHeight);
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        // Gap between sections.
        for (const auto& [caption, firstKnob] : sections)
            if (firstKnob == (int) i && i != 0)
                row.removeFromLeft (18);

        knobs[i]->setBounds (row.removeFromLeft (LabelledKnob::preferredWidth));
        row.removeFromLeft (2);
    }
}

// ================= window management =================

struct ChannelEditorManager::Window : juce::DocumentWindow
{
    Window (ChannelEditorManager& ownerRef, const juce::String& title, int idOfChannel)
        : juce::DocumentWindow (title, theme::panelHeader, closeButton),
          owner (ownerRef), channelId (idOfChannel)
    {
        setUsingNativeTitleBar (true);
    }

    void closeButtonPressed() override
    {
        // Defer: we are inside this window's own callback.
        auto* ownerPtr = &owner;
        const int id = channelId;
        juce::MessageManager::callAsync ([ownerPtr, id] { ownerPtr->close (id); });
    }

    ChannelEditorManager& owner;
    int channelId;
};

ChannelEditorManager::ChannelEditorManager() = default;
ChannelEditorManager::~ChannelEditorManager() { closeAll(); }

void ChannelEditorManager::close (int channelId) { windows.erase (channelId); }
void ChannelEditorManager::closeAll()            { windows.clear(); }

void ChannelEditorManager::show (AppServices& services, juce::ValueTree channel)
{
    if (! channel.isValid())
        return;

    const int channelId = channel[ids::id];
    const auto type = channel[ids::type].toString();
    const auto name = channel[ids::name].toString();

    if (type == "plugin")
    {
        if (auto gen = std::dynamic_pointer_cast<PluginGenerator> (services.generators.getOrCreate (channel)))
        {
            if (auto hosted = gen->getPlugin())
                services.pluginWindows.showEditorFor (hosted, name);
            else
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                    name, "The plugin is still loading (or failed to load). Try again in a moment.");
        }
        return;
    }

    if (auto it = windows.find (channelId); it != windows.end())
    {
        it->second->toFront (true);
        return;
    }

    auto window = std::make_unique<Window> (*this, name, channelId);
    if (type == "sampler")
        window->setContentOwned (new SamplerEditor (services, channel), true);
    else
        window->setContentOwned (new SynthEditor (services, channel), true);

    window->centreWithSize (window->getWidth(), window->getHeight());
    window->setVisible (true);
    windows[channelId] = std::move (window);
}
