#include "ChannelEditor.h"
#include "engine/SamplerGenerator.h"
#include "engine/SynthOsc.h"
#include "model/UndoGesture.h"
#include "plugins/PluginGenerator.h"
#include "sandbox/SandboxedGenerator.h"
#include "model/ChannelParams.h"
#include "ui/automation/AutomationMenu.h"

namespace
{
// Builds a generator's knob row from the shared parameter table and wires each
// knob into the automation layer: moving one records while the write arm is
// on, right-clicking one offers to create or edit its clip.
void buildKnobs (KnobGrid& grid, juce::Component& owner,
                 AppServices& services, juce::ValueTree channel)
{
    const int channelId = channel[ids::id];
    const auto channelName = channel[ids::name].toString();

    for (const auto& descriptor : channelparams::forChannelType (channel[ids::type].toString()))
    {
        if (descriptor.section.isNotEmpty())
            grid.beginSection (descriptor.section);

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
        grid.adopt (std::move (knob));
    }
}
}

namespace
{
// The drive curve is an index, not a continuous value; the section caption
// spells the choices out because a rotary can only show the number.
const juce::String driveSectionCaption { "DRIVE   0 SOFT   1 HARD   2 FOLD" };

// Routes an on-screen keyboard through the engine's preview path.
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
} // namespace

// ================= KnobGrid =================

void KnobGrid::adopt (std::unique_ptr<LabelledKnob> knob)
{
    knobs.push_back (std::move (knob));
}

void KnobGrid::beginSection (const juce::String& caption)
{
    sections.emplace_back (caption, (int) knobs.size());
}

void KnobGrid::add (juce::Component& owner, const juce::String& caption, ProjectModel& model,
                    juce::ValueTree channel, const juce::Identifier& property,
                    juce::NormalisableRange<double> range, double defaultValue,
                    const juce::String& suffix, int decimals)
{
    auto knob = std::make_unique<LabelledKnob> (caption, model, channel, property, range,
                                                defaultValue, suffix, decimals);
    owner.addAndMakeVisible (*knob);
    knobs.push_back (std::move (knob));
}

int KnobGrid::layout (juce::Rectangle<int> area)
{
    captionPositions.clear();
    if (knobs.empty())
        return 0;

    const int knobStep = LabelledKnob::preferredWidth + 2;
    int x = area.getX();
    int y = area.getY();

    for (size_t s = 0; s < sections.size(); ++s)
    {
        const int first = sections[s].second;
        const int last = s + 1 < sections.size() ? sections[s + 1].second : (int) knobs.size();
        if (first >= last)
            continue;

        if (x > area.getX() && x + (last - first) * knobStep > area.getRight())
        {
            x = area.getX();
            y += rowHeight() + rowGap;
        }

        captionPositions.emplace_back (sections[s].first, juce::Point<int> (x, y));

        for (int i = first; i < last; ++i)
        {
            knobs[(size_t) i]->setBounds (x, y + captionHeight,
                                          LabelledKnob::preferredWidth, LabelledKnob::preferredHeight);
            x += knobStep;
        }
        x += sectionGap;
    }

    return y + rowHeight() - area.getY();
}

void KnobGrid::paintCaptions (juce::Graphics& g) const
{
    g.setColour (theme::textFaint);
    g.setFont (theme::uiFont (9.5f, true));
    for (const auto& [caption, position] : captionPositions)
        g.drawText (caption, position.x, position.y, 240, captionHeight,
                    juce::Justification::centredLeft);
}

void KnobGrid::refresh()
{
    for (auto& knob : knobs)
        knob->refresh();
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
    oneShotButton.setTooltip ("Ignore note-offs and play the sample out");
    oneShotButton.setToggleState (channel.getProperty (ids::oneShot, true),
                                  juce::dontSendNotification);
    oneShotButton.onClick = [this]
    {
        const undoGesture::Scoped step (services.project, "One-shot");
        channel.setProperty (ids::oneShot, oneShotButton.getToggleState(),
                             &services.project.getUndoManager());
    };
    addAndMakeVisible (oneShotButton);

    reverseButton.setWantsKeyboardFocus (false);
    reverseButton.setToggleState (channel.getProperty (ids::reverse, false),
                                  juce::dontSendNotification);
    reverseButton.onClick = [this]
    {
        channel.setProperty (ids::reverse, reverseButton.getToggleState(),
                             &services.project.getUndoManager());
    };
    addAndMakeVisible (reverseButton);

    pathLabel.setFont (theme::uiFont (11.0f));
    pathLabel.setColour (juce::Label::textColourId, theme::textDim);
    pathLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (pathLabel);

    buildKnobs (grid, *this, services, channel);

    refreshWaveform();
    startTimerHz (4);
    setSize (680, 344);
}

void SamplerEditor::loadSample (const juce::File& file)
{
    const undoGesture::Scoped step (services.project, "Load sample");
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
    reverseButton.setToggleState (channel.getProperty (ids::reverse, false),
                                  juce::dontSendNotification);
    grid.refresh();
    repaint (waveformArea());
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

        // Grey out whatever the start/end trim excludes.
        const auto startX = (float) juce::jlimit (0.0, 1.0, (double) channel.getProperty (ids::sampleStart, 0.0));
        const auto endX   = (float) juce::jlimit (0.0, 1.0, (double) channel.getProperty (ids::sampleEnd, 1.0));
        g.setColour (theme::panelBg.withAlpha (0.72f));
        g.fillRect ((float) wave.getX(), (float) wave.getY(),
                    startX * (float) wave.getWidth(), (float) wave.getHeight());
        g.fillRect ((float) wave.getX() + endX * (float) wave.getWidth(), (float) wave.getY(),
                    (1.0f - endX) * (float) wave.getWidth(), (float) wave.getHeight());
    }

    g.setColour (theme::outline);
    g.drawRoundedRectangle (wave.toFloat(), 3.0f, 1.0f);

    grid.paintCaptions (g);
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
    auto toggles = r.removeFromTop (22);
    oneShotButton.setBounds (toggles.removeFromLeft (100));
    toggles.removeFromLeft (8);
    reverseButton.setBounds (toggles.removeFromLeft (90));

    r.removeFromTop (78 + 10);   // waveform (painted) + gap

    grid.layout (r);
}

// ================= SynthEditor =================

SynthEditor::SynthEditor (AppServices& s, juce::ValueTree ch)
    : services (s), channel (ch)
{
    buildKnobs (grid, *this, services, channel);

    keyboard.setAvailableRange (36, 96);
    keyboard.setWantsKeyboardFocus (false);
    addAndMakeVisible (keyboard);

    bridge = std::make_unique<KeyboardBridge> (services, channel);
    keyboardState.addListener (bridge.get());

    startTimerHz (15);
    setSize (760, 505);
}

juce::Rectangle<int> SynthEditor::waveArea() const
{
    return getLocalBounds().reduced (10).removeFromTop (56);
}

void SynthEditor::timerCallback()
{
    const float morph = (float) (double) channel.getProperty (ids::oscShape, 0.0);
    const float warp  = (float) (double) channel.getProperty (ids::oscWarp, 0.0);
    if (! juce::approximatelyEqual (morph, shownMorph)
        || ! juce::approximatelyEqual (warp, shownWarp))
    {
        shownMorph = morph;
        shownWarp = warp;
        repaint (waveArea());
    }
}

void SynthEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelBg);

    // One cycle of oscillator 1, rendered through the same function the audio
    // thread uses, so the preview is the sound.
    const auto wave = waveArea();
    g.setColour (theme::sunken);
    g.fillRoundedRectangle (wave.toFloat(), 3.0f);

    constexpr int points = 256;
    juce::Path path;
    const float midY = (float) wave.getCentreY();
    const float halfH = (float) wave.getHeight() * 0.42f;
    for (int i = 0; i < points; ++i)
    {
        const float value = synthosc::sample (shownMorph, shownWarp,
                                              (double) i / points, 1.0 / points);
        const float x = (float) wave.getX() + 2.0f
                        + (float) i / (points - 1) * ((float) wave.getWidth() - 4.0f);
        const float y = midY - value * halfH;
        if (i == 0)
            path.startNewSubPath (x, y);
        else
            path.lineTo (x, y);
    }
    g.setColour (theme::accent);
    g.strokePath (path, juce::PathStrokeType (1.6f));

    g.setColour (theme::outline);
    g.drawRoundedRectangle (wave.toFloat(), 3.0f, 1.0f);

    grid.paintCaptions (g);
}

void SynthEditor::resized()
{
    auto r = getLocalBounds().reduced (10);
    r.removeFromTop (56 + 10);   // wave preview (painted) + gap

    auto keyboardArea = r.removeFromBottom (56);
    // Size the keys so the range exactly fills the width (36..96 spans 36 white keys).
    keyboard.setKeyWidth (juce::jmax (8.0f, (float) keyboardArea.getWidth() / 36.0f));
    keyboard.setBounds (keyboardArea);
    r.removeFromBottom (8);

    grid.layout (r);
}

// ================= KickEditor =================

KickEditor::KickEditor (AppServices& s, juce::ValueTree ch)
    : services (s), channel (ch)
{
    auto& model = services.project;

    previewButton.setWantsKeyboardFocus (false);
    previewButton.setTooltip ("Preview at the root note");
    previewButton.onClick = [this]
    {
        services.engine.previewNote (channel[ids::id],
                                     (int) channel.getProperty (ids::rootNote, 60), 1.0f, 200);
    };
    addAndMakeVisible (previewButton);

    grid.beginSection ("BODY");
    grid.add (*this, "ROOT",  model, channel, ids::rootNote,       { 0.0, 127.0, 1.0 }, 60.0, {}, 0);
    grid.add (*this, "FROM",  model, channel, ids::kickStartFreq,  { 40.0, 2000.0, 0.0, 0.4 }, 240.0, " Hz", 0);
    grid.add (*this, "TO",    model, channel, ids::kickEndFreq,    { 20.0, 400.0, 0.0, 0.5 }, 48.0, " Hz", 1);
    grid.add (*this, "PDEC",  model, channel, ids::kickPitchDecay, { 0.001, 1.0, 0.0, 0.35 }, 0.035, " s", 3);
    grid.add (*this, "ADEC",  model, channel, ids::kickAmpDecay,   { 0.02, 4.0, 0.0, 0.4 }, 0.5, " s", 2);
    grid.add (*this, "SHAPE", model, channel, ids::kickBodyShape,  { 0.0, 1.0 }, 0.0, {}, 2);

    grid.beginSection ("CLICK");
    grid.add (*this, "LEVEL", model, channel, ids::kickClickLevel, { 0.0, 1.0 }, 0.3, {}, 2);
    grid.add (*this, "CDEC",  model, channel, ids::kickClickDecay, { 0.0005, 0.2, 0.0, 0.35 }, 0.004, " s", 4);

    grid.beginSection ("NOISE");
    grid.add (*this, "LEVEL", model, channel, ids::kickNoiseLevel, { 0.0, 1.0 }, 0.12, {}, 2);
    grid.add (*this, "NDEC",  model, channel, ids::kickNoiseDecay, { 0.001, 0.5, 0.0, 0.35 }, 0.02, " s", 3);

    grid.beginSection (driveSectionCaption);
    grid.add (*this, "CURVE",  model, channel, ids::driveCurve, { 0.0, 2.0, 1.0 }, 0.0, {}, 0);
    grid.add (*this, "DRIVE",  model, channel, ids::drive,      { 0.0, 1.0 }, 0.25, {}, 2);
    grid.add (*this, "ENVSHP", model, channel, ids::envShape,   { 0.0, 1.0 }, 1.0, {}, 2);

    keyboard.setAvailableRange (24, 72);
    keyboard.setWantsKeyboardFocus (false);
    addAndMakeVisible (keyboard);

    bridge = std::make_unique<KeyboardBridge> (services, channel);
    keyboardState.addListener (bridge.get());

    startTimerHz (4);
    setSize (700, 340);
}

void KickEditor::timerCallback()
{
    grid.refresh();
}

void KickEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelBg);
    grid.paintCaptions (g);
}

void KickEditor::resized()
{
    auto r = getLocalBounds().reduced (10);

    auto top = r.removeFromTop (26);
    previewButton.setBounds (top.removeFromLeft (32));
    r.removeFromTop (6);

    auto keyboardArea = r.removeFromBottom (56);
    // 24..72 spans 29 white keys.
    keyboard.setKeyWidth (juce::jmax (8.0f, (float) keyboardArea.getWidth() / 29.0f));
    keyboard.setBounds (keyboardArea);
    r.removeFromBottom (8);

    grid.layout (r);
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
        if (services.generators.isSandboxCrashed (channelId))
        {
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon,
                name, "The plugin crashed. Restart it?", "Restart", "Cancel", nullptr,
                juce::ModalCallbackFunction::create ([&services, channel] (int result) mutable
                {
                    if (result == 1)
                        services.generators.restartSandboxed (channel);
                }));
            return;
        }
        if (auto sandboxGen = std::dynamic_pointer_cast<SandboxedGenerator> (
                services.generators.getOrCreate (channel)))
        {
            if (auto sandboxed = sandboxGen->getPlugin())
            {
                // The editor opens in the helper's own window (no shell piano
                // there; the typing keyboard still plays via the preview path).
                sandboxed->showEditor (name + " / " + sandboxed->getName());
            }
            else
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                    name, "The plugin is still loading (or failed to load). Try again in a moment.");
            return;
        }
        if (auto gen = std::dynamic_pointer_cast<PluginGenerator> (services.generators.getOrCreate (channel)))
        {
            if (auto hosted = gen->getPlugin())
            {
                // Instruments get the shell's piano, wired through the same
                // preview path the editors' keyboards use.
                PluginEditorShell::NoteSink notes {
                    [&services, channelId] (int note, float velocity)
                    { services.engine.previewNote (channelId, note, velocity, 0); },
                    [&services, channelId] (int note)
                    { services.engine.previewNoteOff (channelId, note); } };
                services.pluginWindows.showEditorFor (hosted,
                    name + " / " + hosted->getDescription().name, std::move (notes));
            }
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
    else if (type == "kick")
        window->setContentOwned (new KickEditor (services, channel), true);
    else
        window->setContentOwned (new SynthEditor (services, channel), true);

    if (typingKeys != nullptr)
        window->addKeyListener (typingKeys);
    window->centreWithSize (window->getWidth(), window->getHeight());
    window->setVisible (true);
    windows[channelId] = std::move (window);
}
