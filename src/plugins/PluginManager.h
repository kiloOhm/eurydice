#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Plugin discovery + instantiation. The known-plugin list persists in
// ~/Library/Application Support/Eurydice/plugins.xml. Scanning runs on a
// background thread with a dead-man's-pedal file so a crashing plugin gets
// blacklisted instead of taking the scan down twice.
class PluginManager : private juce::ChangeListener
{
public:
    PluginManager();
    ~PluginManager() override;

    juce::AudioPluginFormatManager& getFormats()   { return formatManager; }
    juce::KnownPluginList& getKnownPlugins()       { return knownPlugins; }

    bool isScanning() const;
    void startScan (std::function<void()> onFinished);

    juce::Array<juce::PluginDescription> getInstruments() const;
    juce::Array<juce::PluginDescription> getEffects() const;
    std::optional<juce::PluginDescription> findByIdentifier (const juce::String& identifierString) const;

    // Asynchronous instantiation (AU requires it). Callback runs on the
    // message thread with nullptr + error message on failure.
    void createInstance (const juce::PluginDescription&, double sampleRate, int blockSize,
                         std::function<void (std::unique_ptr<juce::AudioPluginInstance>, const juce::String&)>);

    static juce::File getAppDataDir();

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;   // list changed -> save

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;

    class ScanThread;
    std::unique_ptr<ScanThread> scanThread;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginManager)
};
