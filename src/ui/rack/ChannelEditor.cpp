#include "ChannelEditor.h"
#include "engine/SamplerGenerator.h"
#include "engine/SynthOsc.h"
#include "DrumMachineEditor.h"
#include "EditorParts.h"
#include "KickEditor.h"
#include "SynthDisplays.h"
#include "model/UndoGesture.h"
#include "plugins/PluginGenerator.h"
#include "sandbox/SandboxedGenerator.h"
#include "model/ChannelParams.h"

namespace
{
void buildKnobs (KnobGrid& grid, juce::Component& owner,
                 AppServices& services, juce::ValueTree channel)
{
    for (const auto& descriptor : channelparams::forChannelType (channel[ids::type].toString()))
    {
        if (descriptor.section.isNotEmpty())
            grid.beginSection (descriptor.section);

        auto knob = makeParamKnob (services, channel, descriptor);
        owner.addAndMakeVisible (*knob);
        grid.adopt (std::move (knob));
    }
}
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

// ================= SynthModule =================

SynthModule::SynthModule (juce::String titleText, std::unique_ptr<juce::Component> displayComponent)
    : title (std::move (titleText)), display (std::move (displayComponent))
{
    if (display != nullptr)
        addAndMakeVisible (*display);
}

void SynthModule::addKnob (std::unique_ptr<LabelledKnob> knob)
{
    addAndMakeVisible (*knob);
    knobs.push_back (std::move (knob));
}

void SynthModule::refreshKnobs()
{
    for (auto& knob : knobs)
        knob->refresh();
}

int SynthModule::preferredWidth() const
{
    return juce::jmax (2, (int) knobs.size()) * (LabelledKnob::preferredWidth + 2)
           + padding * 2;
}

void SynthModule::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds();
    g.setColour (theme::raised.withAlpha (0.35f));
    g.fillRoundedRectangle (r.toFloat(), 4.0f);

    auto header = r.withHeight (titleHeight);
    g.setColour (theme::panelHeader);
    g.fillRoundedRectangle (header.toFloat(), 4.0f);
    g.fillRect (header.withTop (header.getBottom() - 4));

    g.setColour (theme::textDim);
    g.setFont (theme::uiFont (9.5f, true));
    g.drawText (title, header.reduced (8, 0), juce::Justification::centredLeft);

    g.setColour (theme::outline);
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 4.0f, 1.0f);
}

void SynthModule::resized()
{
    auto r = getLocalBounds().reduced (padding);
    r.removeFromTop (titleHeight);

    auto knobRow = r.removeFromBottom (LabelledKnob::preferredHeight);
    if (display != nullptr)
        display->setBounds (r.reduced (0, padding / 2));

    int x = knobRow.getX();
    for (auto& knob : knobs)
    {
        knob->setBounds (x, knobRow.getY(), LabelledKnob::preferredWidth,
                         LabelledKnob::preferredHeight);
        x += LabelledKnob::preferredWidth + 2;
    }
}

// ================= SynthEditor =================

SynthEditor::SynthEditor (AppServices& s, juce::ValueTree ch)
    : services (s), channel (ch)
{
    using namespace synthdisplays;

    // Row 1: sound sources.
    addModule ("OSC   -2 SIN  -1 TRI  0 SAW  1 SQR", std::make_unique<OscDisplay> (channel),
               { ids::oscShape, ids::oscWarp, ids::osc2Semi, ids::osc2Detune, ids::osc2Mix });
    addModule ("UNISON", std::make_unique<UnisonDisplay> (channel),
               { ids::unisonVoices, ids::unisonDetune, ids::unisonWidth });
    addModule ("LAYERS", std::make_unique<LayersDisplay> (channel),
               { ids::subLevel, ids::noiseLevel });

    // Row 2: filter and modulation.
    addModule ("FILTER   0 LP  1 BP  2 HP", std::make_unique<FilterResponseDisplay> (channel),
               { ids::filterType, ids::cutoff, ids::resonance, ids::filterKey, ids::filterEnvAmt });
    addModule ("LFO", std::make_unique<LfoDisplay> (channel),
               { ids::lfoRate, ids::lfoAmount, ids::lfoTarget });
    addModule ("VOICE", std::make_unique<GlideDisplay> (channel), { ids::glide });

    // Row 3: envelopes.
    addModule ("FILTER ENV", std::make_unique<EnvelopeDisplay> (channel,
                   ids::fenvAttack, ids::fenvDecay, ids::fenvSustain, ids::fenvRelease,
                   theme::secondary, 0.2),
               { ids::fenvAttack, ids::fenvDecay, ids::fenvSustain, ids::fenvRelease });
    addModule ("AMP ENV", std::make_unique<EnvelopeDisplay> (channel,
                   ids::attack, ids::decay, ids::sustain, ids::release,
                   theme::accent, 0.7),
               { ids::attack, ids::decay, ids::sustain, ids::release });

    keyboard.setAvailableRange (36, 96);
    keyboard.setWantsKeyboardFocus (false);
    addAndMakeVisible (keyboard);

    bridge = std::make_unique<KeyboardBridge> (services, channel);
    keyboardState.addListener (bridge.get());
    services.liveNoteListeners.add (this);

    setSize (700, 3 * (SynthModule::preferredHeight() + 8) + 56 + 28);
}

SynthEditor::~SynthEditor()
{
    services.liveNoteListeners.remove (this);
}

// Reflects live input on the on-screen keys. The bridge is detached first so
// the echo doesn't route the note straight back into the engine, which has
// already played it; the keyboard component keeps its own subscription and
// repaints.
void SynthEditor::echoLiveNote (int channelId, int key, float velocity, bool on)
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

void SynthEditor::liveNoteOn (int channelId, int key, float velocity)
{
    echoLiveNote (channelId, key, velocity, true);
}

void SynthEditor::liveNoteOff (int channelId, int key)
{
    echoLiveNote (channelId, key, 0.0f, false);
}

SynthModule& SynthEditor::addModule (const juce::String& title,
                                     std::unique_ptr<juce::Component> display,
                                     std::initializer_list<juce::Identifier> params)
{
    auto module = std::make_unique<SynthModule> (title, std::move (display));
    for (const auto& id : params)
        if (const auto* descriptor = channelparams::find ("synth", id.toString()))
            module->addKnob (makeParamKnob (services, channel, *descriptor));

    addAndMakeVisible (*module);
    modules.push_back (std::move (module));
    return *modules.back();
}

void SynthEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelBg);
}

void SynthEditor::resized()
{
    auto r = getLocalBounds().reduced (10);

    auto keyboardArea = r.removeFromBottom (56);
    // Size the keys so the range exactly fills the width (36..96 spans 36 white keys).
    keyboard.setKeyWidth (juce::jmax (8.0f, (float) keyboardArea.getWidth() / 36.0f));
    keyboard.setBounds (keyboardArea);
    r.removeFromBottom (8);

    // Three rows of modules; the first module of each row absorbs the slack.
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
                    name + " / " + hosted->getDescription().name, std::move (notes), channelId);
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
    else if (type == "drums")
        window->setContentOwned (new DrumMachineEditor (services, channel), true);
    else
        window->setContentOwned (new SynthEditor (services, channel), true);

    if (typingKeys != nullptr)
        window->addKeyListener (typingKeys);
    window->centreWithSize (window->getWidth(), window->getHeight());
    window->setVisible (true);
    windows[channelId] = std::move (window);
}
