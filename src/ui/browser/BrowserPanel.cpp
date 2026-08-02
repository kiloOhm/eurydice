#include "BrowserPanel.h"
#include "app/Theme.h"
#include "plugins/PluginManager.h"
#include "ui/common/SampleDrop.h"

BrowserPanel::BrowserPanel (AppServices& s)
    : services (s)
{
    juce::PropertiesFile::Options opts;
    opts.applicationName = "Eurydice";
    opts.filenameSuffix = "settings";
    opts.folderName = "Eurydice";
    opts.osxLibrarySubFolder = "Application Support";
    settings = std::make_unique<juce::PropertiesFile> (opts);

    // tabs
    for (auto* tab : { &samplesTab, &pluginsTab, &projectsTab })
    {
        tab->setClickingTogglesState (true);
        tab->setRadioGroupId (200);
        tab->setWantsKeyboardFocus (false);
        addAndMakeVisible (tab);
    }
    samplesTab.setToggleState (true, juce::dontSendNotification);
    samplesTab.onClick  = [this] { activeTab = Tab::samples;  resized(); repaint(); };
    pluginsTab.onClick  = [this] { activeTab = Tab::plugins;  refreshPluginFilter(); resized(); repaint(); };
    projectsTab.onClick = [this] { activeTab = Tab::projects; refreshRecentProjects(); resized(); repaint(); };

    // --- projects ---
    recentList.setModel (&recentModel);
    recentList.setRowHeight (34);
    recentList.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    addChildComponent (recentList);

    // --- samples ---
    scanThread.startThread();
    formats.registerBasicFormats();
    sourcePlayer.setSource (&transport);
    services.engine.getDeviceManager().addAudioCallback (&sourcePlayer);

    dirContents = std::make_unique<juce::DirectoryContentsList> (&audioFilter, scanThread);
    fileTree = std::make_unique<juce::FileTreeComponent> (*dirContents);
    fileTree->addListener (this);
    // Lets rows be dragged onto the rack and playlist (MainComponent is the
    // DragAndDropContainer).
    fileTree->setDragAndDropDescription (sampledrop::browserDragDescription);
    fileTree->setColour (juce::TreeView::backgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (*fileTree);

    folderBox.onChange = [this]
    {
        const auto path = folderBox.getText();
        if (juce::File (path).isDirectory())
            setRootFolder (juce::File (path));
    };
    addAndMakeVisible (folderBox);

    addFolderButton.setWantsKeyboardFocus (false);
    addFolderButton.setTooltip ("Add a sample folder");
    addFolderButton.onClick = [this] { addFolder(); };
    addAndMakeVisible (addFolderButton);

    // restore folders
    const auto stored = settings->getValue ("sampleFolders", "");
    auto folders = juce::StringArray::fromTokens (stored, "|", "");
    folders.removeEmptyStrings();
    if (folders.isEmpty())
        folders.add (juce::File::getSpecialLocation (juce::File::userMusicDirectory).getFullPathName());
    for (const auto& f : folders)
        folderBox.addItem (f, folderBox.getNumItems() + 1);
    folderBox.setSelectedItemIndex (0, juce::dontSendNotification);
    setRootFolder (juce::File (folders[0]));

    // --- plugins ---
    searchBox.setTextToShowWhenEmpty ("Search plugins...", theme::textFaint);
    searchBox.onTextChange = [this] { refreshPluginFilter(); };
    addAndMakeVisible (searchBox);

    scanButton.setWantsKeyboardFocus (false);
    scanButton.onClick = [this]
    {
        scanButton.setEnabled (false);
        scanButton.setButtonText ("Scanning...");
        services.plugins.startScan ([this]
        {
            scanButton.setEnabled (true);
            scanButton.setButtonText ("Scan for plugins");
            refreshPluginFilter();
        });
    };
    addAndMakeVisible (scanButton);

    pluginList.setModel (&pluginModel);
    pluginList.setRowHeight (34);
    addAndMakeVisible (pluginList);

    refreshPluginFilter();
    resized();
}

BrowserPanel::~BrowserPanel()
{
    services.engine.getDeviceManager().removeAudioCallback (&sourcePlayer);
    sourcePlayer.setSource (nullptr);
    transport.setSource (nullptr);
    fileTree->removeListener (this);
}

void BrowserPanel::setRootFolder (const juce::File& folder)
{
    dirContents->setDirectory (folder, true, true);
}

void BrowserPanel::addFolder()
{
    auto chooser = std::make_shared<juce::FileChooser> ("Add sample folder",
        juce::File::getSpecialLocation (juce::File::userHomeDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this, chooser] (const juce::FileChooser& fc)
        {
            const auto folder = fc.getResult();
            if (! folder.isDirectory())
                return;
            folderBox.addItem (folder.getFullPathName(), folderBox.getNumItems() + 1);
            folderBox.setSelectedItemIndex (folderBox.getNumItems() - 1);

            juce::StringArray folders;
            for (int i = 0; i < folderBox.getNumItems(); ++i)
                folders.add (folderBox.getItemText (i));
            settings->setValue ("sampleFolders", folders.joinIntoString ("|"));
            settings->saveIfNeeded();
        });
}

void BrowserPanel::previewFile (const juce::File& file)
{
    transport.stop();
    transport.setSource (nullptr);
    readerSource = nullptr;

    if (file == previewedFile)   // second click stops
    {
        previewedFile = juce::File();
        return;
    }

    if (auto* reader = formats.createReaderFor (file))
    {
        readerSource = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
        transport.setSource (readerSource.get(), 0, nullptr, reader->sampleRate);
        transport.setPosition (0);
        transport.start();
        previewedFile = file;
    }
}

void BrowserPanel::fileClicked (const juce::File& file, const juce::MouseEvent&)
{
    if (file.existsAsFile())
        previewFile (file);
}

void BrowserPanel::fileDoubleClicked (const juce::File& file)
{
    if (! file.existsAsFile())
        return;
    // Double-click = new sampler channel with this sample, FL "send to rack".
    auto channel = services.project.addChannel ("sampler", file.getFileNameWithoutExtension());
    channel.setProperty (ids::samplePath, file.getFullPathName(), nullptr);
}

void BrowserPanel::refreshPluginFilter()
{
    filteredPlugins.clear();
    const auto needle = searchBox.getText().trim();
    for (const auto& d : PluginManager::dedupeFormats (services.plugins.getKnownPlugins().getTypes()))
        if (needle.isEmpty() || d.name.containsIgnoreCase (needle)
            || d.manufacturerName.containsIgnoreCase (needle))
            filteredPlugins.add (d);
    pluginList.updateContent();
    pluginList.repaint();
}

void BrowserPanel::PluginListModel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= owner.filteredPlugins.size())
        return;
    const auto& d = owner.filteredPlugins.getReference (row);

    if (selected)
    {
        g.setColour (theme::accentDim.withAlpha (0.4f));
        g.fillRect (0, 0, w, h);
    }
    g.setColour (d.isInstrument ? theme::secondary : theme::textPrimary);
    g.setFont (theme::uiFont (12.0f, true));
    g.drawText (d.name, 8, 2, w - 12, 16, juce::Justification::centredLeft);
    g.setColour (theme::textFaint);
    g.setFont (theme::uiFont (10.0f));
    g.drawText ((d.isInstrument ? "instrument · " : "effect · ") + d.pluginFormatName
                    + " · " + d.manufacturerName,
                8, 17, w - 12, 14, juce::Justification::centredLeft);
}

void BrowserPanel::PluginListModel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (row < 0 || row >= owner.filteredPlugins.size())
        return;
    const auto d = owner.filteredPlugins.getReference (row);
    if (d.isInstrument)
    {
        auto channel = owner.services.project.addChannel ("plugin", d.name);
        channel.setProperty (ids::pluginId, d.createIdentifierString(), nullptr);
    }
    // Effects are loaded from the mixer's slot menu.
}

void BrowserPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelBg.darker (0.15f));
    g.setColour (theme::outline);
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
}

void BrowserPanel::resized()
{
    auto r = getLocalBounds().reduced (4);
    auto tabs = r.removeFromTop (24);
    samplesTab.setBounds (tabs.removeFromLeft (tabs.getWidth() / 3));
    pluginsTab.setBounds (tabs.removeFromLeft (tabs.getWidth() / 2));
    projectsTab.setBounds (tabs);
    r.removeFromTop (4);

    const bool samples  = activeTab == Tab::samples;
    const bool plugins  = activeTab == Tab::plugins;
    const bool projects = activeTab == Tab::projects;
    fileTree->setVisible (samples);
    folderBox.setVisible (samples);
    addFolderButton.setVisible (samples);
    searchBox.setVisible (plugins);
    scanButton.setVisible (plugins);
    pluginList.setVisible (plugins);
    recentList.setVisible (projects);

    if (samples)
    {
        auto top = r.removeFromTop (24);
        addFolderButton.setBounds (top.removeFromRight (24));
        folderBox.setBounds (top);
        r.removeFromTop (4);
        fileTree->setBounds (r);
    }
    else if (plugins)
    {
        searchBox.setBounds (r.removeFromTop (24));
        r.removeFromTop (4);
        scanButton.setBounds (r.removeFromBottom (26));
        pluginList.setBounds (r);
    }
    else
    {
        recentList.setBounds (r);
    }
}

// ---------------- projects tab ----------------

void BrowserPanel::refreshRecentProjects()
{
    // Same settings key MainComponent maintains; reread on every visit so the
    // list follows loads and saves without extra plumbing.
    settings->reload();
    juce::RecentlyOpenedFilesList recent;
    recent.restoreFromString (settings->getValue ("recentFiles"));
    recent.removeNonExistentFiles();

    recentProjects.clear();
    for (int i = 0; i < recent.getNumFiles(); ++i)
        recentProjects.add (recent.getFile (i));
    recentList.updateContent();
    recentList.repaint();
}

void BrowserPanel::RecentListModel::paintListBoxItem (int row, juce::Graphics& g,
                                                      int w, int h, bool selected)
{
    if (row < 0 || row >= owner.recentProjects.size())
        return;
    const auto& file = owner.recentProjects.getReference (row);

    if (selected)
    {
        g.setColour (theme::raised);
        g.fillRect (0, 0, w, h);
    }
    g.setColour (theme::textPrimary);
    g.setFont (theme::uiFont (12.5f));
    g.drawText (file.getFileNameWithoutExtension(), 8, 2, w - 12, 16,
                juce::Justification::centredLeft);
    g.setColour (theme::textFaint);
    g.setFont (theme::uiFont (10.0f));
    g.drawText (file.getParentDirectory().getFullPathName(), 8, 17, w - 12, 14,
                juce::Justification::centredLeft);
}

void BrowserPanel::RecentListModel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < owner.recentProjects.size() && owner.onOpenProject)
        owner.onOpenProject (owner.recentProjects.getReference (row));
}
