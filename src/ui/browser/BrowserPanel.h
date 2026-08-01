#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "app/AppServices.h"

// FL-style left browser: Samples tab (folder tree, click to preview,
// double-click to add as sampler channel) and Plugins tab (search + list,
// double-click instrument to add it to the rack).
class BrowserPanel : public juce::Component,
                     private juce::FileBrowserListener
{
public:
    explicit BrowserPanel (AppServices&);
    ~BrowserPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // --- samples tab ---
    void selectionChanged() override {}
    void fileClicked (const juce::File&, const juce::MouseEvent&) override;
    void fileDoubleClicked (const juce::File&) override;
    void browserRootChanged (const juce::File&) override {}
    void previewFile (const juce::File&);
    void addFolder();
    void setRootFolder (const juce::File&);

    // --- plugins tab ---
    struct PluginListModel : juce::ListBoxModel
    {
        BrowserPanel& owner;
        explicit PluginListModel (BrowserPanel& o) : owner (o) {}
        int getNumRows() override { return owner.filteredPlugins.size(); }
        void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    };
    void refreshPluginFilter();

    AppServices& services;

    juce::TextButton samplesTab { "Samples" }, pluginsTab { "Plugins" };

    // samples
    juce::TimeSliceThread scanThread { "BrowserScan" };
    juce::WildcardFileFilter audioFilter { "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.ogg;*.m4a", "*", "audio" };
    std::unique_ptr<juce::DirectoryContentsList> dirContents;
    std::unique_ptr<juce::FileTreeComponent> fileTree;
    juce::ComboBox folderBox;
    juce::TextButton addFolderButton { "+" };

    // preview
    juce::AudioFormatManager formats;
    juce::AudioTransportSource transport;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioSourcePlayer sourcePlayer;
    juce::File previewedFile;

    // plugins
    juce::TextEditor searchBox;
    juce::TextButton scanButton { "Scan for plugins" };
    juce::ListBox pluginList;
    PluginListModel pluginModel { *this };
    juce::Array<juce::PluginDescription> filteredPlugins;

    bool showingSamples = true;
    std::unique_ptr<juce::PropertiesFile> settings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrowserPanel)
};
