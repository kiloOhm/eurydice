#include <juce_audio_utils/juce_audio_utils.h>
#include "MainComponent.h"
#include "Theme.h"
#include "ui/common/PlaceholderPanel.h"
#include "ui/rack/ChannelRackPanel.h"
#include "ui/pianoroll/PianoRollPanel.h"
#include "ui/playlist/PlaylistPanel.h"
#include "ui/mixer/MixerPanel.h"
#include "control/ControlServer.h"
#include "ui/browser/BrowserPanel.h"
#include "engine/OfflineRenderer.h"
#include "AudioRecorder.h"

MainComponent::MainComponent()
{
    setLookAndFeel (&lookAndFeel);
    setWantsKeyboardFocus (true);

    controlServer = std::make_unique<ControlServer> (services);
    midiInput = std::make_unique<MidiInputManager> (services);
    recorder = std::make_unique<AudioRecorder> (services);

    addAndMakeVisible (transportBar);

    transportBar.onPlay  = [this] { transportPlay(); };
    transportBar.onStop  = [this] { transportStop(); };
    transportBar.onRecordToggled = [this]
    {
        midiInput->recordArmed.store (! midiInput->recordArmed.load());
    };
    transportBar.onTempoChanged   = [this] (double bpm) { services.project.setTempo (bpm); };
    transportBar.onSongModeChanged = [this] (bool song) { services.project.setSongMode (song); };
    transportBar.getBeatPosition  = [this] { return services.engine.getPositionBeats(); };
    transportBar.getIsPlaying     = [this] { return services.engine.isPlaying(); };
    transportBar.setTempoDisplay (services.project.getTempo());

    browser = std::make_unique<BrowserPanel> (services);
    addAndMakeVisible (*browser);

    addAndMakeVisible (desktop);

    auto makePanel = [this] (const juce::String& title)
    {
        auto panel = std::make_unique<FloatingPanel> (title, std::make_unique<PlaceholderPanel> (title));
        desktop.addAndMakeVisible (*panel);
        return panel;
    };

    playlistPanel = std::make_unique<FloatingPanel> ("Playlist",
                                                     std::make_unique<PlaylistPanel> (services));
    desktop.addAndMakeVisible (*playlistPanel);

    channelRackPanel = std::make_unique<FloatingPanel> ("Channel Rack",
                                                        std::make_unique<ChannelRackPanel> (services));
    desktop.addAndMakeVisible (*channelRackPanel);
    pianoRollPanel = std::make_unique<FloatingPanel> ("Piano Roll",
                                                      std::make_unique<PianoRollPanel> (services));
    desktop.addAndMakeVisible (*pianoRollPanel);
    mixerPanel = std::make_unique<FloatingPanel> ("Mixer",
                                                  std::make_unique<MixerPanel> (services));
    desktop.addAndMakeVisible (*mixerPanel);

    pianoRollPanel->setVisible (false);
    mixerPanel->setVisible (false);

    setSize (1440, 900);

    // Debug hook: show specific panels at startup (comma list: pianoroll,mixer)
    const auto showList = juce::SystemStats::getEnvironmentVariable ("EURYDICE_SHOW", "");
    if (showList.contains ("pianoroll")) pianoRollPanel->bringToFrontAndShow();
    if (showList.contains ("mixer"))     mixerPanel->bringToFrontAndShow();

    // Debug hook: EURYDICE_SCREENSHOT=<path.png> saves a snapshot of the UI
    // shortly after launch, for headless visual verification.
    const auto shotPath = juce::SystemStats::getEnvironmentVariable ("EURYDICE_SCREENSHOT", "");
    if (shotPath.isNotEmpty())
    {
        juce::Timer::callAfterDelay (1500, [this, shotPath]
        {
            auto image = createComponentSnapshot (getLocalBounds());
            juce::File file (shotPath);
            file.deleteFile();
            juce::FileOutputStream out (file);
            juce::PNGImageFormat png;
            png.writeImageToStream (image, out);
            std::cout << "SCREENSHOT_SAVED " << shotPath << std::endl;
        });
    }

    // Debug hook: EURYDICE_SCAN=1 scans plugins, prints them, and quits.
    if (juce::SystemStats::getEnvironmentVariable ("EURYDICE_SCAN", "") == "1")
    {
        services.plugins.startScan ([this]
        {
            for (const auto& d : services.plugins.getKnownPlugins().getTypes())
                std::cout << "PLUGIN\t" << (d.isInstrument ? "inst" : "fx") << "\t"
                          << d.pluginFormatName << "\t" << d.name << "\t"
                          << d.createIdentifierString() << std::endl;
            std::cout << "SCAN_DONE " << services.plugins.getKnownPlugins().getNumTypes() << std::endl;
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        });
    }

    // Debug hook: EURYDICE_LOADFX=<name fragment> loads that effect into
    // master slot 0 (for verifying hosted processing end to end).
    const auto loadFx = juce::SystemStats::getEnvironmentVariable ("EURYDICE_LOADFX", "");
    if (loadFx.isNotEmpty())
    {
        juce::Timer::callAfterDelay (500, [this, loadFx]
        {
            for (const auto& d : services.plugins.getEffects())
            {
                if (! d.name.containsIgnoreCase (loadFx))
                    continue;
                auto master = services.project.getInsert (0);
                juce::ValueTree slot (ids::SLOT);
                slot.setProperty (ids::slotIndex, 0, nullptr);
                slot.setProperty (ids::pluginId, d.createIdentifierString(), nullptr);
                master.appendChild (slot, nullptr);
                std::cout << "LOADFX_REQUESTED " << d.name << std::endl;
                break;
            }
        });
    }

    // Smoke-test hook: EURYDICE_AUTOPLAY=1 plays the default pattern and
    // prints master peaks so the audio path can be verified headlessly.
    if (juce::SystemStats::getEnvironmentVariable ("EURYDICE_AUTOPLAY", "") == "1")
    {
        juce::Timer::callAfterDelay (800, [this] { services.engine.play(); });
        juce::Timer::callAfterDelay (2800, [this]
        {
            std::cout << "AUTOPLAY peakL=" << services.engine.getMasterPeak (0)
                      << " peakR=" << services.engine.getMasterPeak (1)
                      << " beats=" << services.engine.getPositionBeats()
                      << " playing=" << (services.engine.isPlaying() ? 1 : 0) << std::endl;
        });
    }
}

MainComponent::~MainComponent()
{
    setLookAndFeel (nullptr);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::desktopBg);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    transportBar.setBounds (r.removeFromTop (TransportBar::preferredHeight));
    browser->setBounds (r.removeFromLeft (240));
    desktop.setBounds (r);

    if (! initialLayoutDone && desktop.getWidth() > 0)
    {
        layoutDefaultPanelPositions();
        initialLayoutDone = true;
    }
}

void MainComponent::layoutDefaultPanelPositions()
{
    const int w = desktop.getWidth();
    const int h = desktop.getHeight();

    playlistPanel->setBounds (juce::jmax (0, w - (int) (w * 0.62f) - 12), 12, (int) (w * 0.62f), (int) (h * 0.55f));
    channelRackPanel->setBounds (12, 12, 700, 460);
    pianoRollPanel->setBounds (60, 80, (int) (w * 0.7f), (int) (h * 0.65f));
    mixerPanel->setBounds (40, h - 340 - 20, w - 80, 340);
}

namespace
{
// FL-style typing piano: Z-row = lower octave from C4, Q-row = octave above.
int typingKeyToNote (juce::juce_wchar c)
{
    static const juce::String lowRow  ("zsxdcvgbhnjm");
    static const juce::String highRow ("q2w3er5t6y7ui9o0p");
    if (const int i = lowRow.indexOfChar (c); i >= 0)   return 60 + i;
    if (const int i = highRow.indexOfChar (c); i >= 0)  return 72 + i;
    return -1;
}
}

bool MainComponent::keyStateChanged (bool)
{
    bool handled = false;
    for (auto it = typingKeysDown.begin(); it != typingKeysDown.end();)
    {
        if (! juce::KeyPress::isKeyCurrentlyDown ((int) it->first))
        {
            midiInput->noteOff (it->second);
            it = typingKeysDown.erase (it);
            handled = true;
        }
        else
            ++it;
    }
    return handled;
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        if (services.engine.isPlaying()) transportStop(); else transportPlay();
        return true;
    }

    // Typing piano (ignore when modifiers are held so shortcuts still work).
    const auto c = (juce::juce_wchar) juce::CharacterFunctions::toLowerCase (
                       (juce::juce_wchar) key.getTextCharacter());
    if (! key.getModifiers().isAnyModifierKeyDown())
    {
        if (c == ',') { typingOctaveShift = juce::jmax (typingOctaveShift - 12, -36); return true; }
        if (c == '.') { typingOctaveShift = juce::jmin (typingOctaveShift + 12,  36); return true; }

        if (const int base = typingKeyToNote (c); base >= 0)
        {
            const int note = juce::jlimit (0, 127, base + typingOctaveShift);
            if (typingKeysDown.find (c) == typingKeysDown.end())
            {
                typingKeysDown[c] = note;
                midiInput->noteOn (note, 0.8f);
            }
            return true;
        }
    }

    if (key == juce::KeyPress::F5Key)  { playlistPanel->toggleVisibility();    return true; }
    if (key == juce::KeyPress::F6Key)  { channelRackPanel->toggleVisibility(); return true; }
    if (key == juce::KeyPress::F7Key)  { pianoRollPanel->toggleVisibility();   return true; }
    if (key == juce::KeyPress::F9Key)  { mixerPanel->toggleVisibility();       return true; }
    if (key == juce::KeyPress::F10Key) { showAudioSettings();                  return true; }
    if (key == juce::KeyPress ('r', juce::ModifierKeys::commandModifier, 0))
    {
        showExportDialog();
        return true;
    }
    if (key == juce::KeyPress ('s', juce::ModifierKeys::commandModifier, 0))
    {
        saveProjectInteractive();
        return true;
    }
    if (key == juce::KeyPress ('o', juce::ModifierKeys::commandModifier, 0))
    {
        openProjectInteractive();
        return true;
    }
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
    {
        services.project.getUndoManager().undo();
        return true;
    }
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier
                                     | juce::ModifierKeys::shiftModifier, 0))
    {
        services.project.getUndoManager().redo();
        return true;
    }
    return false;
}

void MainComponent::showExportDialog()
{
    auto chooser = std::make_shared<juce::FileChooser> ("Export render",
        juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile ("Untitled.wav"),
        "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File())
                return;

            OfflineRenderer::Options opts;
            opts.wavFile = file.withFileExtension (".wav");
            opts.renderMp3 = OfflineRenderer::findLameBinary() != juce::File();
            opts.renderStems = false;

            const auto result = OfflineRenderer::render (services.engine, services.project, opts);

            juce::AlertWindow::showMessageBoxAsync (
                result.ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                "Export",
                result.ok ? "Rendered:\n" + result.writtenFiles.joinIntoString ("\n")
                              + (result.error.isNotEmpty() ? "\n\n" + result.error : juce::String())
                          : result.error);
        });
}

void MainComponent::saveProjectInteractive()
{
    auto chooser = std::make_shared<juce::FileChooser> ("Save project",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Untitled.eury"),
        "*.eury");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode,
        [this, chooser] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file != juce::File())
                services.saveProject (file.withFileExtension (".eury"));
        });
}

void MainComponent::openProjectInteractive()
{
    auto chooser = std::make_shared<juce::FileChooser> ("Open project",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.eury");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file.existsAsFile())
                services.loadProject (file);
        });
}

void MainComponent::transportPlay()
{
    // Armed + song mode = record the input as an audio take.
    if (midiInput->recordArmed.load() && services.project.isSongMode()
        && ! recorder->isRecording())
        recorder->start();
    services.engine.play();
}

void MainComponent::transportStop()
{
    services.engine.stop();
    if (recorder->isRecording())
        recorder->stopAndPlaceClip();
}

void MainComponent::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (
        services.engine.getDeviceManager(), 1, 2, 2, 2, true, false, true, false);
    selector->setSize (480, 400);

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (selector.release());
    opts.dialogTitle = "Audio & MIDI Settings";
    opts.dialogBackgroundColour = theme::panelBg;
    opts.escapeKeyTriggersCloseButton = true;
    opts.resizable = false;
    opts.launchAsync();
}
