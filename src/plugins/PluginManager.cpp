#include "PluginManager.h"

juce::File PluginManager::getAppDataDir()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Application Support/Eurydice");
    dir.createDirectory();
    return dir;
}

class PluginManager::ScanThread : public juce::Thread
{
public:
    ScanThread (PluginManager& pm, std::function<void()> onDone)
        : juce::Thread ("PluginScan"), owner (pm), finished (std::move (onDone)) {}

    ~ScanThread() override { stopThread (10000); }

    void run() override
    {
        for (auto* format : owner.formatManager.getFormats())
        {
            const auto deadMansPedal = getAppDataDir().getChildFile (
                "scan-crash-" + format->getName() + ".txt");

            juce::PluginDirectoryScanner scanner (owner.knownPlugins, *format,
                                                  format->getDefaultLocationsToSearch(),
                                                  true, deadMansPedal, true);
            juce::String pluginName;
            while (! threadShouldExit() && scanner.scanNextFile (true, pluginName))
                DBG ("Scanned: " + pluginName);
        }

        juce::MessageManager::callAsync ([cb = finished] { if (cb) cb(); });
    }

private:
    PluginManager& owner;
    std::function<void()> finished;
};

PluginManager::PluginManager()
{
    formatManager.addFormat (std::make_unique<juce::VST3PluginFormat>());
   #if JUCE_PLUGINHOST_AU && JUCE_MAC
    formatManager.addFormat (std::make_unique<juce::AudioUnitPluginFormat>());
   #endif

    if (auto xml = juce::parseXML (getAppDataDir().getChildFile ("plugins.xml")))
        knownPlugins.recreateFromXml (*xml);

    knownPlugins.addChangeListener (this);
}

PluginManager::~PluginManager()
{
    knownPlugins.removeChangeListener (this);
    scanThread = nullptr;
}

void PluginManager::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (auto xml = knownPlugins.createXml())
        xml->writeTo (getAppDataDir().getChildFile ("plugins.xml"));
}

bool PluginManager::isScanning() const
{
    return scanThread != nullptr && scanThread->isThreadRunning();
}

void PluginManager::startScan (std::function<void()> onFinished)
{
    if (isScanning())
        return;
    scanThread = std::make_unique<ScanThread> (*this, std::move (onFinished));
    scanThread->startThread();
}

juce::Array<juce::PluginDescription> PluginManager::getInstruments() const
{
    juce::Array<juce::PluginDescription> out;
    for (const auto& d : knownPlugins.getTypes())
        if (d.isInstrument)
            out.add (d);
    return out;
}

juce::Array<juce::PluginDescription> PluginManager::getEffects() const
{
    juce::Array<juce::PluginDescription> out;
    for (const auto& d : knownPlugins.getTypes())
        if (! d.isInstrument)
            out.add (d);
    return out;
}

juce::Array<juce::PluginDescription> PluginManager::dedupeFormats (
    const juce::Array<juce::PluginDescription>& plugins)
{
    // Key on name + manufacturer: the same plugin usually registers both
    // formats under identical strings. Prefer VST3; an AU only stays when no
    // VST3 twin exists.
    std::map<juce::String, juce::PluginDescription> byKey;
    juce::StringArray order;
    for (const auto& d : plugins)
    {
        const auto key = (d.name + "\n" + d.manufacturerName).toLowerCase();
        auto it = byKey.find (key);
        if (it == byKey.end())
        {
            byKey[key] = d;
            order.add (key);
        }
        else if (it->second.pluginFormatName != "VST3" && d.pluginFormatName == "VST3")
        {
            it->second = d;
        }
    }

    juce::Array<juce::PluginDescription> out;
    for (const auto& key : order)
        out.add (byKey[key]);
    return out;
}

std::optional<juce::PluginDescription> PluginManager::findByIdentifier (const juce::String& identifier) const
{
    for (const auto& d : knownPlugins.getTypes())
        if (d.createIdentifierString() == identifier)
            return d;
    return {};
}

void PluginManager::createInstance (const juce::PluginDescription& desc, double sampleRate, int blockSize,
                                    std::function<void (std::unique_ptr<juce::AudioPluginInstance>, const juce::String&)> callback)
{
    formatManager.createPluginInstanceAsync (desc, sampleRate, blockSize,
        [cb = std::move (callback)] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            cb (std::move (instance), error);
        });
}
