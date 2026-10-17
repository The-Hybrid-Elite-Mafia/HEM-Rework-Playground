#include "MainComponent.h"
#include "Engine/MediaLoader.h"
#include "Session/SessionFile.h"
#include "PlaygroundBinaryData.h"

namespace pg::ui
{
using E = PlaygroundEngine;

namespace
{
    // Main faceplate geometry (4K pixels), measured on Main-Faceplate.png
    constexpr float kW = 3840.0f, kH = 2160.0f;
    // circle-fit to the chrome rings: centre of each recess, radius = inner chrome edge
    constexpr float kSocketX[6] { 1400.0f, 1607.0f, 1815.0f, 2024.0f, 2232.0f, 2441.0f };
    constexpr float kSocketY = 347.0f, kSocketR = 84.0f;
    constexpr float kTabX[4] { 108.0f, 1024.0f, 1940.0f, 2857.0f };
    constexpr float kTabY = 503.0f, kTabW = 873.0f, kTabH = 175.0f;

    enum MenuIds
    {
        mNew = 1, mOpen, mSave, mSaveAs, mImport, mExport, mAudio, mQuit, mClearRecent,
        mRename = 20, mRemoveTrack, mClearSolo, mClearMute, mResetFader,
        mUndo = 30, mRedo,
        mTabEdit = 40, mTabMix, mTabForge, mTabMaster, mZoomIn, mZoomOut, mFit, mCloseWindows,
        mPluginBase = 60,
        mPresetsFolder = 80, mSongsFolder,
        mShortcuts = 90, mAbout,
        mRecentBase = 100
    };

    void noKeyboardFocus (juce::Component& c)
    {
        for (auto* child : c.getChildren())
        {
            if (dynamic_cast<juce::Button*> (child) != nullptr || dynamic_cast<juce::Slider*> (child) != nullptr
                || dynamic_cast<juce::ComboBox*> (child) != nullptr || dynamic_cast<juce::ListBox*> (child) != nullptr)
                child->setWantsKeyboardFocus (false);
            noKeyboardFocus (*child);
        }
    }

    juce::String timeText (double s)
    {
        const int total = (int) s;
        return juce::String (total / 60).paddedLeft ('0', 2) + ":" + juce::String (total % 60).paddedLeft ('0', 2) + "." + juce::String ((int) (std::fmod (s, 1.0) * 10.0));
    }
}

MainComponent::MainComponent (PlaygroundEngine& e, juce::AudioDeviceManager& d, juce::PropertiesFile& s)
    : engine (e), devices (d), settings (s), lnf (colours::glow),
      ctx { engine, devices, settings, waves, windows }
{
    setLookAndFeel (&lnf);
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
    setWantsKeyboardFocus (true);
    logo = juce::ImageCache::getFromMemory (PlaygroundBinary::HEMIcon1024_png, PlaygroundBinary::HEMIcon1024_pngSize);

    ctx.status = [this] (const juce::String& t) { status (t); };
    ctx.structureChanged = [this, safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        // deferred: the caller may be a control that the refresh is about to rebuild
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->refreshAll(); });
    };
    ctx.importFiles = [this] (const juce::Array<juce::File>& f) { importFiles (f); };
    ctx.showTab = [this] (int t) { showTab (t); };
    ctx.selectedStrip = E::kMaster;
    waves.onReady = [this] { if (timeline != nullptr) timeline->repaint(); };

    addAndMakeVisible (menuBar);

    using I = TransportButton::Icon;
    const std::pair<I, const char*> tb[6] { { I::toStart, "Back to the start (Enter)" }, { I::play, "Play / pause (Space)" }, { I::stop, "Stop and go back to where play started" },
                                            { I::loop, "Loop on / off (L). Drag on the timeline's ruler to set the loop." },
                                            { I::beforeAfter, "BEFORE / AFTER (B): hear the song with every plugin off, then back on" },
                                            { I::exportWav, "Export a WAV (Ctrl+E)" } };
    for (int i = 0; i < 6; ++i)
    {
        transport[(size_t) i] = std::make_unique<TransportButton> (tb[i].first, tb[i].second);
        addAndMakeVisible (*transport[(size_t) i]);
    }
    transport[0]->onClick = [this] { engine.setPositionSeconds (0.0); };
    transport[1]->onClick = [this] { togglePlay(); };
    transport[2]->onClick = [this] { engine.stop(); engine.setPositionSeconds (0.0); };
    transport[3]->onClick = [this] { toggleLoop(); };
    transport[4]->onClick = [this] { engine.setBeforeMode (! engine.isBeforeMode()); status (engine.isBeforeMode() ? "BEFORE: every plugin is OFF" : "AFTER: plugins back ON"); };
    transport[5]->onClick = [this] { showTab (tabs::master); masterPage->startExport(); };
    transport[4]->ledColour = juce::Colour (0xffffb02e);
    transport[3]->ledColour = colours::glow;
    transport[5]->ledColour = juce::Colour (0xffcfa63a);

    const char* tabNames[tabs::count] { "EDIT", "MIX", "SONG FORGE", "MASTER" };
    for (int i = 0; i < tabs::count; ++i)
    {
        auto b = std::make_unique<juce::TextButton> (tabNames[i]);
        b->getProperties().set (props::style, "tab");
        b->onClick = [this, i] { showTab (i); };
        addAndMakeVisible (*b);
        tabButtons[(size_t) i] = std::move (b);
    }

    bpmLabel.setEditable (false, true, false);
    bpmLabel.setJustificationType (juce::Justification::centredRight);
    bpmLabel.setColour (juce::Label::textColourId, colours::glow.brighter (0.6f));
    bpmLabel.setTooltip ("Song tempo: double-click to type it (Elite Delay syncs to this)");
    bpmLabel.onTextChange = [this]
    {
        const float v = bpmLabel.getText().retainCharacters ("0123456789.").getFloatValue();
        if (v >= 40.0f && v <= 240.0f)
            if (auto* p = engine.getAPVTS().getParameter (E::Ids::bpm)) p->setValueNotifyingHost (p->convertTo0to1 (v));
    };
    addAndMakeVisible (bpmLabel);

    editView.changed = [this] { if (trackList) trackList->layoutRows(); if (timeline) timeline->repaint(); };
    trackList = std::make_unique<TrackList> (ctx, editView);
    timeline = std::make_unique<Timeline> (ctx, editView);
    mixPage = std::make_unique<MixPage> (ctx);
    library = std::make_unique<PluginLibrary> (ctx);
    forgePage = std::make_unique<ForgePage> (ctx);
    masterPage = std::make_unique<MasterPage> (ctx);
    for (juce::Component* c : { (juce::Component*) trackList.get(), (juce::Component*) timeline.get(), (juce::Component*) mixPage.get(),
                                (juce::Component*) library.get(), (juce::Component*) forgePage.get(), &forgePage->getTakesPanel(),
                                (juce::Component*) masterPage.get(), &masterPage->getGuidePanel() })
        addChildComponent (c);
    masterMeter.horizontal = true;
    masterMeter.setTooltip ("Master level (click to clear the red clip light)");
    addAndMakeVisible (masterMeter);

    recent.setMaxNumberOfItems (10);
    recent.restoreFromString (settings.getValue ("recentSessions"));
    devices.addChangeListener (this);

    markSaved();
    showTab (tabs::edit);
    // cloud smoke test: HEM_START_TAB=master opens straight onto the MASTER page (3D meter on screen)
    if (juce::SystemStats::getEnvironmentVariable ("HEM_START_TAB", {}) == "master") showTab (tabs::master);
    noKeyboardFocus (*this);
    setSize (1600, 900);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    alive->store (false);
    loader.removeAllJobs (true, 10000);
    devices.removeChangeListener (this);
    windows.closeAll();
    settings.setValue ("recentSessions", recent.toString());
    masterPage.reset(); forgePage.reset(); library.reset(); mixPage.reset(); timeline.reset(); trackList.reset();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

juce::Rectangle<float> MainComponent::fp (float x, float y, float w, float h) const
{
    const float sx = (float) getWidth() / kW, sy = (float) getHeight() / kH;
    return { x * sx, y * sy, w * sx, h * sy };
}

//==============================================================================
void MainComponent::resized()
{
    if (getWidth() <= 0 || getHeight() <= 0) return;
    const auto& full = FaceplateKit::get().fullImage();
    if (full.isValid())
    {
        // pre-scaled in steps (2:1 box filters are inside the kit's mips): sharp and cheap to paint
        const float ds = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr
                           ? (float) juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale : 1.0f;
        background = juce::Image (juce::Image::RGB, juce::roundToInt ((float) getWidth() * ds), juce::roundToInt ((float) getHeight() * ds), false);
        juce::Graphics g (background);
        FaceplateKit::get().draw (g, FaceplateKit::Piece { { 0, 0, 3840, 2160 }, {}, 0.0f }, background.getBounds().toFloat(), (float) background.getWidth() / kW);
    }

    const float sx = (float) getWidth() / kW;
    menuBar.setBounds (fp (250.0f, 62.0f, 3450.0f, 125.0f).toNearestInt());
    for (int i = 0; i < 6; ++i)
        transport[(size_t) i]->setBounds (juce::Rectangle<float> (2.0f * kSocketR * sx, 2.0f * kSocketR * sx)
                                              .withCentre (fp (kSocketX[i], kSocketY, 0.0f, 0.0f).getPosition()).toNearestInt());
    for (int i = 0; i < tabs::count; ++i)
        tabButtons[(size_t) i]->setBounds (fp (kTabX[i], kTabY, kTabW, kTabH).toNearestInt());
    bpmLabel.setBounds (fp (3330.0f, 270.0f, 370.0f, 80.0f).toNearestInt());
    bpmLabel.setFont (PlaygroundLookAndFeel::font (juce::jmax (12.0f, 56.0f * sx)));

    const auto left = fp (92.0f, 708.0f, 596.0f, 1220.0f).toNearestInt();
    const auto main = fp (746.0f, 708.0f, 3004.0f, 1220.0f).toNearestInt();
    for (auto* c : { (juce::Component*) trackList.get(), (juce::Component*) library.get(), &forgePage->getTakesPanel(), &masterPage->getGuidePanel() })
        c->setBounds (left);
    for (auto* c : { (juce::Component*) timeline.get(), (juce::Component*) mixPage.get(), (juce::Component*) forgePage.get(), (juce::Component*) masterPage.get() })
        c->setBounds (main);
    editView.rowHeight = juce::jlimit (48, 90, main.getHeight() / 8);
    masterMeter.setBounds (fp (3060.0f, 2000.0f, 650.0f, 70.0f).toNearestInt());
}

void MainComponent::paint (juce::Graphics& g)
{
    if (background.isValid()) g.drawImage (background, getLocalBounds().toFloat());
    else g.fillAll (colours::navy);

    const float sx = (float) getWidth() / kW;
    // HEM logo in the corner socket
    if (logo.isValid())
    {
        const auto r = juce::Rectangle<float> (2.0f * 56.0f * sx, 2.0f * 56.0f * sx).withCentre (fp (158.0f, 125.0f, 0.0f, 0.0f).getPosition());
        juce::Graphics::ScopedSaveState s (g);
        juce::Path clip; clip.addEllipse (r);
        g.reduceClipRegion (clip);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (logo, r, juce::RectanglePlacement::fillDestination);
    }

    // song plate: song name + details, engraved
    auto plate = fp (165.0f, 272.0f, 980.0f, 145.0f);
    PlaygroundLookAndFeel::drawEngraved (g, songName() + (dirtyCached ? " *" : ""), plate.removeFromTop (plate.getHeight() * 0.6f),
                                         juce::Justification::bottomLeft, juce::jmax (12.0f, 64.0f * sx));
    const double sr = engine.getEngineSampleRate();
    PlaygroundLookAndFeel::drawEngraved (g, juce::String (engine.getNumUsedTracks()) + " TRACKS  -  " + juce::String (sr / 1000.0, 1) + " kHz  -  HEM REWORK PLAYGROUND",
                                         plate, juce::Justification::centredLeft, juce::jmax (9.0f, 34.0f * sx));

    // counter glass
    auto glass = fp (2700.0f, 262.0f, 640.0f, 165.0f);
    const double pos = engine.getPositionSeconds();
    const double bpm = engine.getAPVTS().getRawParameterValue (E::Ids::bpm)->load();
    const double beats = pos * bpm / 60.0;
    g.setFont (PlaygroundLookAndFeel::font (juce::jmax (14.0f, 92.0f * sx)));
    g.setColour (colours::glow.brighter (0.7f));
    g.drawText (timeText (pos), glass.removeFromTop (glass.getHeight() * 0.62f), juce::Justification::centredLeft, false);
    g.setFont (PlaygroundLookAndFeel::font (juce::jmax (9.0f, 36.0f * sx)));
    g.setColour (colours::paperDim);
    g.drawText ("BAR " + juce::String ((int) (beats / 4.0) + 1) + "  BEAT " + juce::String ((int) std::fmod (beats, 4.0) + 1)
                    + (engine.isLooping() ? "   LOOP " + timeText (engine.getLoopStart()) + " - " + timeText (engine.getLoopEnd()) : juce::String()),
                glass, juce::Justification::centredLeft, false);
    if (engine.isBeforeMode())
    {
        g.setColour (juce::Colour (0xffffb02e));
        g.drawText ("BEFORE", fp (3330.0f, 355.0f, 370.0f, 60.0f), juce::Justification::centredRight, false);
    }

    // bottom strip: status
    PlaygroundLookAndFeel::drawEngraved (g, statusText, fp (110.0f, 1985.0f, 2950.0f, 100.0f), juce::Justification::centredLeft, juce::jmax (10.0f, 40.0f * sx));
}

void MainComponent::paintOverChildren (juce::Graphics& g)
{
    if (loadingCount > 0 || openingSession)
    {
        auto r = fp (746.0f, 708.0f, 3004.0f, 1220.0f);
        g.setColour (colours::navy.withAlpha (0.55f));
        g.fillRect (r);
        PlaygroundLookAndFeel::drawPanelText (g, openingSession ? "OPENING SESSION..." : "LOADING " + juce::String (loadingCount) + " FILE(S)...",
                                              r, juce::Justification::centred, 26.0f);
    }
}

//==============================================================================
void MainComponent::showTab (int tab)
{
    currentTab = juce::jlimit (0, tabs::count - 1, tab);
    for (int i = 0; i < tabs::count; ++i)
        tabButtons[(size_t) i]->setToggleState (i == currentTab, juce::dontSendNotification);
    trackList->setVisible (currentTab == tabs::edit);
    timeline->setVisible (currentTab == tabs::edit);
    mixPage->setVisible (currentTab == tabs::mix);
    library->setVisible (currentTab == tabs::mix);
    forgePage->setVisible (currentTab == tabs::forge);
    forgePage->getTakesPanel().setVisible (currentTab == tabs::forge);
    masterPage->setVisible (currentTab == tabs::master);
    masterPage->getGuidePanel().setVisible (currentTab == tabs::master);
    menuItemsChanged();
    grabKeyboardFocus();
}

void MainComponent::refreshAll()
{
    trackList->rebuild();
    mixPage->rebuild();
    masterPage->rebuild();
    masterPage->setSongName (songName());
    if (editView.autoFit) timeline->fitToWidth();
    timeline->repaint();
    library->repaint();
    juce::Array<SampleData*> inUse;
    for (int t = 0; t < E::kMaxTracks; ++t)
        if (auto m = engine.getTrackMedia (t)) { inUse.add (m.get()); waves.get (m); }   // start drawing waveforms now
    waves.prune (inUse);
    noKeyboardFocus (*this);
    menuItemsChanged();
    repaint();
}

void MainComponent::status (const juce::String& t)
{
    statusText = t;
    statusTime = juce::Time::getMillisecondCounter();
    repaint (fp (60.0f, 1966.0f, 3060.0f, 140.0f).toNearestInt());
}

juce::String MainComponent::songName() const
{
    return sessionFile == juce::File() ? juce::String ("Untitled Song") : sessionFile.getFileNameWithoutExtension();
}

void MainComponent::timerCallback()
{
    static int tick = 0;
    ++tick;
    transport[1]->lit = engine.isPlaying();
    transport[3]->lit = engine.isLooping();
    transport[4]->lit = engine.isBeforeMode();
    transport[5]->lit = masterPage->isExporting();
    for (auto& t : transport) t->repaint();
    masterMeter.update (engine.getPeak (E::kMaster, 0), engine.getPeak (E::kMaster, 1));
    repaint (fp (2640.0f, 250.0f, 1090.0f, 186.0f).toNearestInt());   // counter glass
    if (! bpmLabel.isBeingEdited())
        bpmLabel.setText (juce::String (engine.getAPVTS().getRawParameterValue (E::Ids::bpm)->load(), 1) + " BPM", juce::dontSendNotification);
    if (tick % 8 == 0) engine.collectGarbage();
    if (tick % 6 == 0 && ! openingSession) history.poll();
    if (const bool p = engine.isPlaying(); p != lastPlaying)
    {
        lastPlaying = p;
        if (statusTime == 0) { status (p ? juce::String ("Playing") : juce::String ("Stopped")); statusTime = 0; }
    }
    if (tick % 30 == 0)
    {
        const bool d = isDirty();
        if (d != dirtyCached || tick == 30)
        {
            dirtyCached = d;
            repaint (fp (100.0f, 250.0f, 1100.0f, 190.0f).toNearestInt());   // song plate (unsaved star)
            if (auto* w = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent()))
                w->setName ("HEM Rework Playground  -  " + songName() + (d ? " *" : ""));
        }
    }
    if (statusTime != 0 && juce::Time::getMillisecondCounter() - statusTime > 12000)
    {
        statusTime = 0;
        status (engine.isPlaying() ? juce::String ("Playing") : juce::String ("Ready"));
        statusTime = 0;
    }
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (auto xml = devices.createStateXml())
        settings.setValue ("audioDevice", xml.get());
    repaint();
}

//==============================================================================
void MainComponent::togglePlay()
{
    if (engine.isPlaying()) engine.stop();
    else
    {
        if (! engine.isLooping() && engine.getPositionSeconds() >= engine.getSongLengthSeconds() - 0.05) engine.setPositionSeconds (0.0);
        engine.play();
    }
}

void MainComponent::toggleLoop()
{
    if (! engine.isLooping() && engine.getLoopEnd() - engine.getLoopStart() < 0.1)
        engine.setLoop (true, 0.0, juce::jmax (1.0, engine.getSongLengthSeconds()));
    else
        engine.setLoop (! engine.isLooping(), engine.getLoopStart(), engine.getLoopEnd());
    timeline->repaint();
}

bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    const auto cmd = juce::ModifierKeys::commandModifier;
    if (k == juce::KeyPress::spaceKey) { togglePlay(); return true; }
    if (k == juce::KeyPress::returnKey) { engine.setPositionSeconds (0.0); return true; }
    if (k == juce::KeyPress ('l')) { toggleLoop(); return true; }
    if (k == juce::KeyPress ('b')) { transport[4]->triggerClick(); return true; }
    if (k == juce::KeyPress ('z', cmd, 0)) { undoRedo (false); return true; }
    if (k == juce::KeyPress ('y', cmd, 0) || k == juce::KeyPress ('z', cmd | juce::ModifierKeys::shiftModifier, 0)) { undoRedo (true); return true; }
    if (k == juce::KeyPress ('s', cmd, 0)) { save(); return true; }
    if (k == juce::KeyPress ('s', cmd | juce::ModifierKeys::shiftModifier, 0)) { saveAs(); return true; }
    if (k == juce::KeyPress ('o', cmd, 0)) { checkSaveThen ([this] { openDialog(); }); return true; }
    if (k == juce::KeyPress ('n', cmd, 0)) { checkSaveThen ([this] { newSession(); }); return true; }
    if (k == juce::KeyPress ('i', cmd, 0)) { trackList->setVisible (true); menuItemSelected (mImport, 0); return true; }
    if (k == juce::KeyPress ('e', cmd, 0)) { transport[5]->triggerClick(); return true; }
    for (int i = 0; i < tabs::count; ++i)
        if (k == juce::KeyPress ('1' + i, cmd, 0)) { showTab (i); return true; }
    return false;
}

//==============================================================================
juce::StringArray MainComponent::getMenuBarNames() { return { "File", "Edit", "View", "Track", "Plugins", "Help" }; }

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu m;
    const bool trackSelected = E::isTrack (ctx.selectedStrip) && engine.isTrackUsed (ctx.selectedStrip);
    switch (index)
    {
        case 0:
        {
            m.addItem (mNew, "New Session", true, false);
            m.addItem (mOpen, "Open Session...");
            juce::PopupMenu rec;
            recent.createPopupMenuItems (rec, mRecentBase, false, true);
            if (rec.getNumItems() > 0) { rec.addSeparator(); rec.addItem (mClearRecent, "Clear list"); }
            m.addSubMenu ("Open Recent", rec, rec.getNumItems() > 0);
            m.addItem (mSave, "Save        Ctrl+S");
            m.addItem (mSaveAs, "Save As...  Ctrl+Shift+S");
            m.addSeparator();
            m.addItem (mImport, "Import Audio (stems)...   Ctrl+I");
            m.addItem (mExport, "Export WAV...   Ctrl+E");
            m.addSeparator();
            m.addItem (mAudio, "Audio Settings (sound card)...");
            m.addSeparator();
            m.addItem (mQuit, "Quit");
            break;
        }
        case 1:
            m.addItem (mUndo, "Undo      Ctrl+Z", history.canUndo());
            m.addItem (mRedo, "Redo      Ctrl+Y", history.canRedo());
            m.addSeparator();
            m.addItem (mRename, "Rename Selected Track...", trackSelected);
            m.addItem (mRemoveTrack, "Remove Selected Track", trackSelected);
            m.addSeparator();
            m.addItem (mClearSolo, "Clear All Solos");
            m.addItem (mClearMute, "Clear All Mutes");
            m.addItem (mResetFader, "Selected Fader to 0 dB");
            break;
        case 2:
            m.addItem (mTabEdit, "Edit      Ctrl+1", true, currentTab == tabs::edit);
            m.addItem (mTabMix, "Mix       Ctrl+2", true, currentTab == tabs::mix);
            m.addItem (mTabForge, "Song Forge   Ctrl+3", true, currentTab == tabs::forge);
            m.addItem (mTabMaster, "Master    Ctrl+4", true, currentTab == tabs::master);
            m.addSeparator();
            m.addItem (mZoomIn, "Zoom In  (Ctrl + mouse wheel)");
            m.addItem (mZoomOut, "Zoom Out");
            m.addItem (mFit, "Fit Whole Song");
            m.addSeparator();
            m.addItem (mCloseWindows, "Close All Plugin Windows", windows.count() > 0);
            break;
        case 3:
            m.addItem (mImport, "Import Audio (stems)...");
            m.addItem (mRename, "Rename Selected Track...", trackSelected);
            m.addItem (mRemoveTrack, "Remove Selected Track", trackSelected);
            break;
        case 4:
        {
            m.addSectionHeader ("Add to: " + engine.getStripName (ctx.selectedStrip));
            const auto& all = PluginRegistry::all();
            for (int i = 0; i < (int) all.size(); ++i)
                m.addColouredItem (mPluginBase + i, all[(size_t) i]->name, juce::Colour (all[(size_t) i]->accent), engine.canHost (ctx.selectedStrip, all[(size_t) i]->id));
            m.addSeparator();
            m.addItem (mPresetsFolder, "Open My Presets Folder");
            break;
        }
        case 5:
            m.addItem (mShortcuts, "Keyboard Shortcuts");
            m.addItem (mSongsFolder, "Open My Songs Folder");
            m.addItem (mAbout, "About");
            break;
        default: break;
    }
    return m;
}

void MainComponent::menuItemSelected (int id, int)
{
    if (id >= mRecentBase && id < mRecentBase + 20)
    {
        const auto f = recent.getFile (id - mRecentBase);
        checkSaveThen ([this, f] { openSession (f); });
        return;
    }
    if (id >= mPluginBase && id < mPluginBase + (int) PluginRegistry::all().size())
    {
        ctx.addPluginToStrip (ctx.selectedStrip, PluginRegistry::all()[(size_t) (id - mPluginBase)]->id);
        return;
    }
    switch (id)
    {
        case mNew: checkSaveThen ([this] { newSession(); }); break;
        case mOpen: checkSaveThen ([this] { openDialog(); }); break;
        case mClearRecent: recent.clear(); settings.setValue ("recentSessions", {}); break;
        case mSave: save(); break;
        case mSaveAs: saveAs(); break;
        case mImport:
        {
            auto chooser = std::make_shared<juce::FileChooser> ("Import audio (stems)", AppContext::songsFolder(), MediaLoader::wildcard());
            juce::Component::SafePointer<MainComponent> safe (this);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                                  [safe, chooser] (const juce::FileChooser& fc) { if (safe != nullptr && ! fc.getResults().isEmpty()) safe->importFiles (fc.getResults()); });
            break;
        }
        case mExport: transport[5]->triggerClick(); break;
        case mAudio: showAudioSettings(); break;
        case mQuit: juce::JUCEApplication::getInstance()->systemRequestedQuit(); break;
        case mRename:
            showTab (tabs::edit);
            status ("Right-click the track name on the left, or double-click its name in the MIX tab");
            break;
        case mRemoveTrack: if (E::isTrack (ctx.selectedStrip)) ctx.removeTrack (ctx.selectedStrip); break;
        case mClearSolo: case mClearMute:
            for (int t = 0; t < E::kMaxTracks; ++t)
                if (auto* p = engine.getAPVTS().getParameter (E::Ids::track (t, id == mClearSolo ? "solo" : "mute"))) p->setValueNotifyingHost (0.0f);
            if (id == mClearMute)
                for (int b = 0; b < E::kNumBuses; ++b)
                    if (auto* p = engine.getAPVTS().getParameter (E::Ids::bus (b, "mute"))) p->setValueNotifyingHost (0.0f);
            break;
        case mUndo: undoRedo (false); break;
        case mRedo: undoRedo (true); break;
        case mResetFader:
        {
            const auto vid = E::isTrack (ctx.selectedStrip) ? E::Ids::track (ctx.selectedStrip, "vol")
                           : (E::isBus (ctx.selectedStrip) ? E::Ids::bus (ctx.selectedStrip - E::kFirstBus, "vol") : juce::String (E::Ids::masterVol));
            if (auto* p = engine.getAPVTS().getParameter (vid)) p->setValueNotifyingHost (p->convertTo0to1 (0.0f));
            break;
        }
        case mTabEdit: showTab (tabs::edit); break;
        case mTabMix: showTab (tabs::mix); break;
        case mTabForge: showTab (tabs::forge); break;
        case mTabMaster: showTab (tabs::master); break;
        case mZoomIn: showTab (tabs::edit); timeline->zoom (1.5, engine.getPositionSeconds()); break;
        case mZoomOut: showTab (tabs::edit); timeline->zoom (1.0 / 1.5, engine.getPositionSeconds()); break;
        case mFit: showTab (tabs::edit); timeline->fitToWidth(); trackList->layoutRows(); break;
        case mCloseWindows: windows.closeAll(); mixPage->repaint(); break;
        case mPresetsFolder:
        {
            auto f = Presets::userFolder ("x").getParentDirectory();
            f.createDirectory();
            f.revealToUser();
            break;
        }
        case mSongsFolder: AppContext::songsFolder().revealToUser(); break;
        case mShortcuts: showShortcuts(); break;
        case mAbout:
            juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("HEM Rework Playground")
                                              .withMessage ("Version " HEM_VERSION_STRING "\n\nThe Hybrid Elite Mafia Rework Playground\n"
                                                            "Stems in, 8 inserts per track, buses, sends, the Elite plugins, Song Forge and WAV export.\n\n"
                                                            "Built with JUCE 8.")
                                              .withButton ("OK"), nullptr);
            break;
        default: break;
    }
}

void MainComponent::showShortcuts()
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Keyboard shortcuts").withMessage (
        "Space  -  play / pause\n"
        "Enter  -  back to the start\n"
        "L  -  loop on / off (drag on the ruler to set the loop)\n"
        "B  -  BEFORE / AFTER (all plugins off / on)\n"
        "Ctrl+1 / 2 / 3 / 4  -  Edit / Mix / Song Forge / Master\n"
        "Ctrl+S  -  save,  Ctrl+Shift+S  -  save as\n"
        "Ctrl+O  -  open,  Ctrl+N  -  new session\n"
        "Ctrl+I  -  import stems,  Ctrl+E  -  export WAV\n"
        "Ctrl + mouse wheel  -  zoom the timeline,  Shift + wheel  -  scroll sideways\n"
        "Double-click a knob or fader  -  back to its default").withButton ("OK"), nullptr);
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (devices, 0, 0, 2, 2, false, false, true, false);
    selector->setSize (560, 420);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (selector);
    o.dialogTitle = "Audio Settings";
    o.dialogBackgroundColour = juce::Colour (0xff1b1f26);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

//==============================================================================
bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (MediaLoader::isAudioFile (juce::File (f)) || juce::File (f).hasFileExtension (SessionFile::kExtension)) return true;
    return false;
}

void MainComponent::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension (SessionFile::kExtension))
        {
            const juce::File sf (f);
            checkSaveThen ([this, sf] { openSession (sf); });
            return;
        }
    juce::Array<juce::File> audio;
    for (auto& f : files) if (MediaLoader::isAudioFile (juce::File (f))) audio.add (juce::File (f));
    importFiles (audio);
}

void MainComponent::importFiles (const juce::Array<juce::File>& files)
{
    if (files.isEmpty()) return;
    const int free = E::kMaxTracks - engine.getNumUsedTracks() - loadingCount;
    if (free <= 0) { status ("All 32 tracks are in use: remove a track first."); return; }
    const int n = juce::jmin (free, files.size());
    if (n < files.size()) status ("Only " + juce::String (n) + " of " + juce::String (files.size()) + " files fit (32 tracks max).");
    showTab (currentTab == tabs::forge ? tabs::forge : tabs::edit);
    // decode in parallel, but add the tracks in the order the files were given (01 Drums, 02 Bass...)
    struct Batch
    {
        juce::Array<juce::File> files;
        std::vector<SampleData::Ptr> media;
        std::vector<juce::String> errors;
        std::vector<bool> done;
        int next = 0;
    };
    auto batch = std::make_shared<Batch>();
    for (int i = 0; i < n; ++i) batch->files.add (files[i]);
    batch->media.resize ((size_t) n);
    batch->errors.resize ((size_t) n);
    batch->done.assign ((size_t) n, false);
    for (int i = 0; i < n; ++i)
    {
        const auto file = files[i];
        ++loadingCount;
        auto flag = alive;
        juce::Component::SafePointer<MainComponent> safe (this);
        loader.addJob ([safe, file, flag, batch, i]
        {
            juce::String err;
            auto media = MediaLoader::decode (file, err);
            juce::MessageManager::callAsync ([safe, flag, media, err, batch, i]
            {
                if (! flag->load() || safe == nullptr) return;
                batch->media[(size_t) i] = media;
                batch->errors[(size_t) i] = err;
                batch->done[(size_t) i] = true;
                while (batch->next < batch->files.size() && batch->done[(size_t) batch->next])
                {
                    const int k = batch->next++;
                    const auto& f = batch->files.getReference (k);
                    --safe->loadingCount;
                    auto m = batch->media[(size_t) k];
                    if (m == nullptr) { safe->status (batch->errors[(size_t) k]); continue; }
                    const int t = safe->engine.addTrack (m, f.getFileNameWithoutExtension());
                    if (t < 0) safe->status ("All 32 tracks are in use.");
                    else
                    {
                        safe->ctx.selectedStrip = t;
                        safe->status ("Loaded " + f.getFileName() + "  (track " + juce::String (t + 1) + ")");
                    }
                    batch->media[(size_t) k].reset();
                }
                safe->refreshAll();
                safe->repaint();
            });
        });
    }
    repaint();
}

//==============================================================================
void MainComponent::undoRedo (bool redo)
{
    if (openingSession) return;
    history.beforeStructuralRestore = [this] { windows.closeAll(); };
    const bool done = redo ? history.redo() : history.undo();
    if (! done) { status (redo ? "Nothing to redo." : "Nothing to undo."); return; }
    if (history.lastRestoreWasStructural()) refreshAll();
    repaint();
    status (redo ? "Redo." : "Undo.");
}

namespace
{
    /** Session as text for the "unsaved changes" star, with parameter values rounded to 4 decimals:
        a value that went through a knob or an undo can come back 0.000001 off, which is not a change. */
    juce::String comparable (juce::ValueTree t)
    {
        t.removeProperty ("position", nullptr);
        std::function<void (juce::ValueTree)> round = [&round] (juce::ValueTree node)
        {
            if (node.hasProperty ("value") && node.hasProperty ("id"))
            {
                const double v = node["value"];
                node.setProperty ("value", std::round (v * 1.0e4) / 1.0e4, nullptr);
            }
            for (auto c : node) round (c);
        };
        round (t);
        return t.toXmlString();
    }
}

bool MainComponent::isDirty() const
{
    return comparable (engine.createSessionTree()) != savedState;
}

void MainComponent::markSaved()
{
    savedState = comparable (engine.createSessionTree());
    dirtyCached = false;
    if (auto* w = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent()))
        w->setName ("HEM Rework Playground  -  " + songName());
}

void MainComponent::checkSaveThen (std::function<void()> then)
{
    if (! isDirty()) { then(); return; }
    juce::Component::SafePointer<MainComponent> safe (this);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Save changes?")
                                      .withMessage ("\"" + songName() + "\" has changes that are not saved.")
                                      .withButton ("SAVE").withButton ("DON'T SAVE").withButton ("CANCEL"),
                                  [safe, then] (int r)
                                  {
                                      if (safe == nullptr) return;
                                      if (r == 1) safe->save ([then] (bool ok) { if (ok) then(); });
                                      else if (r == 2) then();
                                  });
}

void MainComponent::newSession()
{
    windows.closeAll();
    engine.stop();
    engine.newSession();
    sessionFile = juce::File();
    ctx.selectedStrip = E::kMaster;
    editView.autoFit = true;
    editView.scrollY = 0;
    markSaved();
    history.reset();
    refreshAll();
    status ("New session: Elite Verb is on the Reverb bus, Elite Delay on the Delay bus, Elite Master on the master.");
}

void MainComponent::openDialog()
{
    auto chooser = std::make_shared<juce::FileChooser> ("Open session", AppContext::songsFolder(), "*" + juce::String (SessionFile::kExtension));
    juce::Component::SafePointer<MainComponent> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe, chooser] (const juce::FileChooser& fc) { if (safe != nullptr && fc.getResult() != juce::File()) safe->openSession (fc.getResult()); });
}

void MainComponent::openSession (const juce::File& file)
{
    juce::String err;
    auto tree = SessionFile::read (file, err);
    if (! tree.isValid()) { status (err); return; }
    openingSession = true;
    repaint();
    const auto paths = E::mediaFilesIn (tree);
    auto flag = alive;
    juce::Component::SafePointer<MainComponent> safe (this);
    loader.addJob ([safe, file, tree, paths, flag]
    {
        juce::StringArray failed;
        auto media = MediaLoader::decodeAll (paths, failed, [flag] { return ! flag->load(); });
        juce::MessageManager::callAsync ([safe, file, tree, media, failed, flag]
        {
            if (! flag->load() || safe == nullptr) return;
            safe->openingSession = false;
            safe->windows.closeAll();
            safe->engine.stop();
            juce::StringArray missing;
            safe->engine.applySessionTree (tree, media, &missing);
            safe->sessionFile = file;
            safe->recent.addFile (file);
            safe->settings.setValue ("recentSessions", safe->recent.toString());
            safe->ctx.selectedStrip = E::kMaster;
            safe->editView.autoFit = true;
            safe->editView.scrollY = 0;
            safe->markSaved();
            safe->history.reset();
            safe->refreshAll();
            safe->status (missing.isEmpty() ? "Opened " + file.getFileName()
                                            : "Opened " + file.getFileName() + "  -  " + juce::String (missing.size()) + " audio file(s) not found: "
                                                  + juce::File (missing[0]).getFileName() + (missing.size() > 1 ? " ..." : ""));
        });
    });
}

bool MainComponent::saveTo (const juce::File& f)
{
    juce::String err;
    if (! SessionFile::save (engine, f, err)) { status ("Could not save: " + err); return false; }
    sessionFile = f.withFileExtension (SessionFile::kExtension);
    recent.addFile (sessionFile);
    settings.setValue ("recentSessions", recent.toString());
    markSaved();
    masterPage->setSongName (songName());
    status ("Saved " + sessionFile.getFullPathName());
    repaint();
    return true;
}

void MainComponent::save (std::function<void (bool)> done)
{
    if (sessionFile == juce::File()) { saveAs (std::move (done)); return; }
    const bool ok = saveTo (sessionFile);
    if (done) done (ok);
}

void MainComponent::saveAs (std::function<void (bool)> done)
{
    auto start = sessionFile != juce::File() ? sessionFile : AppContext::songsFolder().getChildFile (songName() + SessionFile::kExtension);
    auto chooser = std::make_shared<juce::FileChooser> ("Save session as", start, "*" + juce::String (SessionFile::kExtension));
    juce::Component::SafePointer<MainComponent> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe, chooser, done] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr) return;
                              const bool ok = fc.getResult() != juce::File() && safe->saveTo (fc.getResult());
                              if (done) done (ok);
                          });
}
} // namespace pg::ui
