#include "ForgePage.h"

namespace pg::ui
{
namespace
{
    constexpr const char* kServerKey = "server";
    constexpr const char* kApiKeyKey = "apiKey";
    constexpr const char* kFolderKey = "aceFolder";
    constexpr const char* kDownloadPage = "https://github.com/ACE-Step/ACE-Step-1.5/blob/main/docs/en/INSTALL.md";
    const juce::Colour kForge { 0xffe0a526 };

    juce::StringArray keyChoices()
    {
        juce::StringArray k { "Key: Auto" };
        for (auto* n : { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" })
        {
            k.add (juce::String (n) + " major");
            k.add (juce::String (n) + " minor");
        }
        return k;
    }
    const juce::StringArray timeSigs { "Time: Auto", "4/4", "3/4", "6/8", "2/4" };
    const char* timeSigValue[] { "", "4", "3", "6", "2" };
    const std::pair<const char*, const char*> languages[] { { "en", "English" }, { "es", "Spanish" }, { "fr", "French" }, { "de", "German" },
                                                             { "it", "Italian" }, { "pt", "Portuguese" }, { "ja", "Japanese" }, { "ko", "Korean" },
                                                             { "zh", "Chinese" }, { "ru", "Russian" } };

    void styleEditor (juce::TextEditor& e)
    {
        e.setMultiLine (true, true);
        e.setReturnKeyStartsNewLine (true);
        e.setScrollbarsShown (true);
        e.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0d1015));
        e.setColour (juce::TextEditor::outlineColourId, kForge.withAlpha (0.35f));
        e.setColour (juce::TextEditor::focusedOutlineColourId, kForge);
        e.setColour (juce::TextEditor::textColourId, colours::paper);
        e.setFont (PlaygroundLookAndFeel::font (15.0f, false));
    }

    void styleKnob (juce::Slider& s, double lo, double hi, double step, double def, const juce::String& suffix, const juce::String& tip)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
        s.setRange (lo, hi, step);
        s.setValue (def, juce::dontSendNotification);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 20);
        s.setTextValueSuffix (suffix);
        s.setColour (juce::Slider::textBoxTextColourId, kForge.brighter (0.5f));
        s.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff0d1015));
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.getProperties().set (props::accent, (juce::int64) kForge.getARGB());
        s.setTooltip (tip);
    }
}

ForgePage::ForgePage (AppContext& c) : ctx (c)
{
    juce::PropertiesFile::Options o;
    o.applicationName = "SongForge";
    o.folderName = "Hybrid Elite Mafia";
    o.filenameSuffix = ".settings";
    o.osxLibrarySubFolder = "Application Support";
    aceSettings = std::make_unique<juce::PropertiesFile> (o);

    engineLabel.setFont (PlaygroundLookAndFeel::font (14.0f));
    addAndMakeVisible (engineLabel);
    startButton.onClick = [this] { startEngine(); };
    startButton.setTooltip ("Starts ACE-Step's server (start_api_server.bat) in its own window. Keep that window open while you make songs.");
    setupButton.onClick = [this] { showSetup(); };
    setupButton.setTooltip ("Where ACE-Step is installed and its address");
    for (auto* b : { &startButton, &setupButton }) addAndMakeVisible (*b);

    styleEditor (style);
    style.setTextToShowWhenEmpty ("e.g. dark trap, 140 bpm, 808s, eerie piano, male rap vocal", colours::paperDim.withAlpha (0.6f));
    styleEditor (lyrics);
    lyrics.setTextToShowWhenEmpty ("[Verse]\nfirst lines...\n\n[Chorus]\nthe hook...", colours::paperDim.withAlpha (0.6f));
    style.setText (ctx.settings.getValue ("forgeStyle"), false);
    lyrics.setText (ctx.settings.getValue ("forgeLyrics"), false);
    style.onTextChange = [this] { saveTexts(); };
    lyrics.onTextChange = [this] { saveTexts(); };
    addAndMakeVisible (style);
    addAndMakeVisible (lyrics);

    styleKnob (seconds, 10.0, 600.0, 1.0, ctx.settings.getDoubleValue ("forgeSeconds", 180.0), " s", "Song length in seconds");
    styleKnob (bpm, 0.0, 300.0, 1.0, ctx.settings.getDoubleValue ("forgeBpm", 0.0), " BPM", "Tempo (0 = ACE-Step picks)");
    styleKnob (takeCount, 1.0, 4.0, 1.0, ctx.settings.getDoubleValue ("forgeTakes", 2.0), " takes", "Versions per press. 8 GB graphics cards: 2 at most.");
    seconds.onValueChange = [this] { ctx.settings.setValue ("forgeSeconds", seconds.getValue()); };
    bpm.onValueChange = [this] { ctx.settings.setValue ("forgeBpm", bpm.getValue()); };
    takeCount.onValueChange = [this] { ctx.settings.setValue ("forgeTakes", takeCount.getValue()); };
    bpm.textFromValueFunction = [] (double v) { return v < 1.0 ? juce::String ("AUTO") : juce::String ((int) v) + " BPM"; };
    bpm.updateText();
    for (auto* s : { &seconds, &bpm, &takeCount }) addAndMakeVisible (*s);

    auto combo = [this] (juce::ComboBox& cb, const juce::StringArray& items, const juce::String& keyName)
    {
        cb.getProperties().set (props::style, "dark");
        cb.getProperties().set (props::accent, (juce::int64) kForge.getARGB());
        cb.setColour (juce::ComboBox::textColourId, colours::paper);
        cb.setColour (juce::ComboBox::arrowColourId, colours::paperDim);
        cb.addItemList (items, 1);
        cb.setSelectedItemIndex (juce::jlimit (0, items.size() - 1, ctx.settings.getIntValue (keyName, 0)), juce::dontSendNotification);
        cb.onChange = [this, &cb, keyName] { ctx.settings.setValue (keyName, cb.getSelectedItemIndex()); };
        addAndMakeVisible (cb);
    };
    juce::StringArray langs;
    for (auto& l : languages) langs.add (l.second);
    combo (key, keyChoices(), "forgeKey");
    combo (timeSig, timeSigs, "forgeTimeSig");
    combo (language, langs, "forgeLanguage");

    for (auto* t : { &smart, &instrumental })
    {
        t->getProperties().set (props::accent, (juce::int64) kForge.getARGB());
        addAndMakeVisible (*t);
    }
    smart.setToggleState (ctx.settings.getBoolValue ("forgeSmart", false), juce::dontSendNotification);
    instrumental.setToggleState (ctx.settings.getBoolValue ("forgeInstrumental", false), juce::dontSendNotification);
    smart.setTooltip ("ACE-Step plans the song first: better structure, slower. Needs more than 6 GB of graphics memory - leave OFF on 8 GB laptops.");
    instrumental.setTooltip ("No vocals: the lyrics are ignored");
    smart.onClick = [this] { ctx.settings.setValue ("forgeSmart", smart.getToggleState()); };
    instrumental.onClick = [this] { ctx.settings.setValue ("forgeInstrumental", instrumental.getToggleState()); };

    generateButton.onClick = [this] { generate(); };
    generateButton.setTooltip ("Make the song with ACE-Step on this computer");
    cancelButton.onClick = [this] { cancel(); };
    for (auto* b : { &generateButton, &cancelButton }) addAndMakeVisible (*b);

    progressLabel.setFont (PlaygroundLookAndFeel::font (13.5f, false));
    progressLabel.setColour (juce::Label::textColourId, colours::paper);
    progressLabel.setJustificationType (juce::Justification::centredLeft);
    progressLabel.setText ("Write a STYLE (and lyrics), then press GENERATE. The finished takes appear on the left.", juce::dontSendNotification);
    addAndMakeVisible (progressLabel);

    health.setServer (server(), apiKey());

    // mode buttons: BAND FORGE (our own band) / SONG FORGE (ACE-Step)
    for (auto* b : { &bandTab, &aceTab, &libraryTab })
    {
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd9822b).withAlpha (0.85f));
        addAndMakeVisible (*b);
    }
    bandTab.setTooltip ("BAND FORGE: guitar, banjo, fiddle, harmonica, drums... written and played right here, no internet");
    aceTab.setTooltip ("SONG FORGE: whole songs with vocals from ACE-Step running on this computer");
    libraryTab.setTooltip ("LIBRARY: your own beats and songs (Band Forge parts taken through Suno and back), kept on this computer");
    bandTab.onClick = [this] { setMode (Mode::band); };
    aceTab.onClick = [this] { setMode (Mode::ace); };
    libraryTab.onClick = [this] { setMode (Mode::library); };
    addChildComponent (band);
    addChildComponent (library);
    leftHolder.addChildComponent (takesPanel);
    leftHolder.addChildComponent (band.getTakesPanel());
    leftHolder.addChildComponent (library.getKindsPanel());
    band.onSendToLibrary = [this] (const juce::Array<juce::File>& files, const juce::String& genreName, const juce::File& take)
    {
        const auto kindName = files.size() == 1 ? juce::String ("Beat") : juce::String ("Stem");
        if (library.addFiles (files, genreName, take, kindName, "Band Forge  " + take.getFileName()) > 0) setMode (Mode::library);
    };
    {
        const int saved = ctx.settings.getIntValue ("forgeMode", ctx.settings.getBoolValue ("forgeBandMode", true) ? 0 : 1);
        setMode ((Mode) juce::jlimit (0, 2, saved));
    }

    startTimerHz (4);
    timerCallback();
}

std::vector<juce::Component*> ForgePage::aceControls()
{
    return { &engineLabel, &progressLabel, &startButton, &setupButton, &generateButton, &cancelButton, &style, &lyrics,
             &seconds, &bpm, &takeCount, &key, &timeSig, &language, &smart, &instrumental };
}

void ForgePage::setMode (Mode m)
{
    mode = m;
    ctx.settings.setValue ("forgeMode", (int) m);
    const std::pair<juce::TextButton*, const char*> tabsList[] { { &bandTab, "BAND FORGE" }, { &aceTab, "SONG FORGE  (ACE-STEP)" }, { &libraryTab, "LIBRARY" } };
    for (int i = 0; i < 3; ++i)
    {
        auto* t = tabsList[i].first;
        const bool on = (int) m == i;
        t->setToggleState (on, juce::dontSendNotification);
        // the chosen mode reads bright copper, the others dim
        t->setButtonText (on ? juce::String::fromUTF8 ("\xe2\x96\xb6  ") + tabsList[i].second : juce::String (tabsList[i].second));
        t->setColour (juce::TextButton::textColourOffId, on ? juce::Colour (0xffffb35c) : colours::paperDim);
        t->setColour (juce::TextButton::textColourOnId, juce::Colour (0xffffb35c));
    }
    for (auto* c : aceControls()) c->setVisible (m == Mode::ace);
    band.setVisible (m == Mode::band);
    library.setVisible (m == Mode::library);
    takesPanel.setVisible (m == Mode::ace);
    band.getTakesPanel().setVisible (m == Mode::band);
    library.getKindsPanel().setVisible (m == Mode::library);
    leftHolder.resized();
    resized();
    repaint();
}

ForgePage::~ForgePage()
{
    stopTimer();
    job.reset();
}

juce::String ForgePage::server() const { return aceSettings->getValue (kServerKey, ace::kDefaultServer); }
juce::String ForgePage::apiKey() const { return aceSettings->getValue (kApiKeyKey); }
juce::File ForgePage::aceFolder() const { return juce::File (aceSettings->getValue (kFolderKey)); }

void ForgePage::saveTexts()
{
    ctx.settings.setValue ("forgeStyle", style.getText());
    ctx.settings.setValue ("forgeLyrics", lyrics.getText());
}

//==============================================================================
void ForgePage::resized()
{
    auto all = getLocalBounds();
    auto tabsRow = all.removeFromTop (40).reduced (14, 4);
    bandTab.setBounds (tabsRow.removeFromLeft (juce::jmin (220, tabsRow.getWidth() / 3)).reduced (2, 0));
    aceTab.setBounds (tabsRow.removeFromLeft (juce::jmin (260, tabsRow.getWidth() / 2)).reduced (2, 0));
    libraryTab.setBounds (tabsRow.removeFromLeft (juce::jmin (200, tabsRow.getWidth())).reduced (2, 0));
    band.setBounds (all);
    library.setBounds (all);
    auto r = all.reduced (14, 10);
    auto top = r.removeFromTop (36);
    setupButton.setBounds (top.removeFromRight (100).reduced (2));
    startButton.setBounds (top.removeFromRight (160).reduced (2));
    top.removeFromLeft (24);
    engineLabel.setBounds (top);
    r.removeFromTop (8);

    auto right = r.removeFromRight (juce::jmin (420, r.getWidth() / 3));
    r.removeFromRight (14);
    styleCaption = r.removeFromTop (20);
    style.setBounds (r.removeFromTop (juce::jmax (60, r.getHeight() / 5)));
    r.removeFromTop (8);
    lyricsCaption = r.removeFromTop (20);
    lyrics.setBounds (r);

    optionsArea = right;
    auto knobs = right.removeFromTop (juce::jmin (130, right.getHeight() / 3));
    const int kw = knobs.getWidth() / 3;
    seconds.setBounds (knobs.removeFromLeft (kw).reduced (6, 0));
    bpm.setBounds (knobs.removeFromLeft (kw).reduced (6, 0));
    takeCount.setBounds (knobs.reduced (6, 0));
    right.removeFromTop (10);
    key.setBounds (right.removeFromTop (30).reduced (2));
    auto row = right.removeFromTop (34);
    timeSig.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (2));
    language.setBounds (row.reduced (2));
    right.removeFromTop (6);
    row = right.removeFromTop (34);
    smart.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (2));
    instrumental.setBounds (row.reduced (2));
    right.removeFromTop (12);
    row = right.removeFromTop (56);
    cancelButton.setBounds (row.removeFromRight (110).reduced (2));
    generateButton.setBounds (row.reduced (2));
    right.removeFromTop (8);
    progressLabel.setBounds (right.removeFromTop (juce::jmin (90, right.getHeight())));
}

void ForgePage::paint (juce::Graphics& g)
{
    if (mode != Mode::ace) return;
    const auto c = engineLabel.getBounds();
    PlaygroundLookAndFeel::drawLed (g, { (float) c.getX() - 12.0f, (float) c.getCentreY() }, 6.0f, engineUp ? colours::hemGreen : colours::meterRed, true);
    PlaygroundLookAndFeel::drawPanelText (g, "STYLE  (genre, instruments, mood, voice)", styleCaption.toFloat(), juce::Justification::centredLeft, 13.0f, kForge.brighter (0.4f));
    PlaygroundLookAndFeel::drawPanelText (g, "LYRICS  ([Verse] [Chorus] [Bridge] tags; empty = instrumental)", lyricsCaption.toFloat(), juce::Justification::centredLeft, 13.0f, kForge.brighter (0.4f));
    g.setColour (juce::Colour (0xff12151a).withAlpha (0.55f));
    g.fillRoundedRectangle (optionsArea.toFloat().expanded (8.0f, 6.0f), 6.0f);
}

void ForgePage::timerCallback()
{
    const bool up = health.isUp();
    if (up != engineUp || engineLabel.getText().isEmpty())
    {
        engineUp = up;
        if (up)
            engineLabel.setText ("ACE-STEP ENGINE READY", juce::dontSendNotification);
        else if (engineStartedAt != 0 && juce::Time::getMillisecondCounter() - engineStartedAt < 15u * 60u * 1000u)
            engineLabel.setText ("ACE-STEP ENGINE STARTING...  (the first start can take a few minutes)", juce::dontSendNotification);
        else
            engineLabel.setText ("ACE-STEP ENGINE NOT RUNNING  -  press START ENGINE", juce::dontSendNotification);
        engineLabel.setColour (juce::Label::textColourId, up ? colours::hemGreen.brighter (0.3f) : colours::paper);
        repaint();
    }
    generateButton.setEnabled (job == nullptr);
    cancelButton.setEnabled (job != nullptr);
    lyrics.setEnabled (! instrumental.getToggleState());
}

//==============================================================================
ace::Request ForgePage::makeRequest() const
{
    ace::Request r;
    r.style = style.getText();
    r.lyrics = lyrics.getText();
    r.instrumental = instrumental.getToggleState();
    r.seconds = seconds.getValue();
    r.bpm = juce::roundToInt (bpm.getValue());
    const int k = key.getSelectedItemIndex();
    r.key = k > 0 ? keyChoices()[k] : juce::String();
    r.timeSignature = timeSigValue[juce::jlimit (0, 4, timeSig.getSelectedItemIndex())];
    r.language = languages[juce::jlimit (0, 9, language.getSelectedItemIndex())].first;
    r.takes = juce::roundToInt (takeCount.getValue());
    r.smart = smart.getToggleState();
    return r;
}

void ForgePage::generate()
{
    if (job != nullptr) return;
    const auto req = makeRequest();
    if (req.style.trim().isEmpty())
    {
        progressLabel.setText ("Type a STYLE first (genre, instruments, mood, voice).", juce::dontSendNotification);
        return;
    }
    if (! engineUp)
        progressLabel.setText ("The engine does not answer yet - trying anyway...", juce::dontSendNotification);
    takesFolder = AppContext::songsFolder().getChildFile ("Song Forge").getChildFile (juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S"));
    takesFolder.createDirectory();
    juce::Component::SafePointer<ForgePage> safe (this);
    job = std::make_unique<ace::Job> (req, server(), apiKey(), takesFolder,
                                      [safe] (const juce::String& text) { if (safe != nullptr) safe->progressLabel.setText (text, juce::dontSendNotification); },
                                      [safe] (ace::Result r) { if (safe != nullptr) safe->finished (std::move (r)); });
    job->start();
    if (ctx.status) ctx.status ("SONG FORGE: making " + juce::String (req.takes) + " take(s), " + juce::String ((int) req.seconds) + " s each...");
    timerCallback();
}

void ForgePage::cancel() { if (job != nullptr) job->cancel(); }

void ForgePage::finished (ace::Result r)
{
    job.reset();
    if (! r.ok)
    {
        progressLabel.setText (r.cancelled ? juce::String ("Cancelled.") : "Could not make the song: " + r.error, juce::dontSendNotification);
        if (ctx.status) ctx.status ("SONG FORGE: " + (r.cancelled ? juce::String ("cancelled") : r.error));
        timerCallback();
        return;
    }
    takes = r.takes;
    takesPanel.refresh();
    const int secs = juce::roundToInt (r.secondsTaken);
    progressLabel.setText (juce::String (takes.size()) + " take(s) ready in " + juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2)
                               + ". Pick one on the left and press ADD TO SESSION.", juce::dontSendNotification);
    if (ctx.status) ctx.status ("SONG FORGE: " + juce::String (takes.size()) + " take(s) saved in " + takesFolder.getFullPathName());
    timerCallback();
}

void ForgePage::addTake (int index)
{
    if (! juce::isPositiveAndBelow (index, takes.size())) return;
    const auto& t = takes.getReference (index);
    if (! t.file.existsAsFile())
    {
        if (ctx.status) ctx.status ("SONG FORGE: the take file is gone (" + t.file.getFullPathName() + ")");
        return;
    }
    if (t.bpm > 0.0 && ctx.engine.getNumUsedTracks() == 0)
        if (auto* p = ctx.engine.getAPVTS().getParameter (PlaygroundEngine::Ids::bpm))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) t.bpm));   // first song in: take its tempo
    if (ctx.importFiles) ctx.importFiles ({ t.file });
}

//==============================================================================
void ForgePage::showSetup()
{
    auto* aw = new juce::AlertWindow ("SONG FORGE - set up ACE-Step",
        "SONG FORGE uses ACE-Step 1.5 (free) running on THIS computer.\n\n"
        "If Hybrid Studio's Song Forge already works on this PC, nothing to do here: both apps share the same setting.\n\n"
        "Otherwise: download the Windows portable package, unpack it (e.g. C:\\ACE-Step-1.5), pick that folder below, SAVE, then START ENGINE.",
        juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("folder", aceFolder().getFullPathName(), "ACE-Step folder (the one with start_api_server.bat)");
    aw->addTextEditor ("server", server(), "Server address");
    aw->addTextEditor ("key", apiKey(), "API key (leave empty unless you set one)");
    aw->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("BROWSE...", 2);
    aw->addButton ("GET ACE-STEP", 3);
    aw->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<ForgePage> safe (this);
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe, aw] (int result)
    {
        std::unique_ptr<juce::AlertWindow> owner (aw);
        if (safe == nullptr) return;
        if (result == 3) { juce::URL (kDownloadPage).launchInDefaultBrowser(); return; }
        if (result == 2)
        {
            auto chooser = std::make_shared<juce::FileChooser> ("Pick the ACE-Step folder (with start_api_server.bat)", safe->aceFolder());
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                  [safe, chooser] (const juce::FileChooser& fc)
                                  {
                                      if (safe == nullptr || fc.getResult() == juce::File()) return;
                                      safe->aceSettings->setValue (kFolderKey, fc.getResult().getFullPathName());
                                      safe->aceSettings->saveIfNeeded();
                                      safe->showSetup();
                                  });
            return;
        }
        if (result != 1) return;
        auto s = aw->getTextEditorContents ("server").trim();
        if (s.isEmpty()) s = ace::kDefaultServer;
        if (! s.startsWithIgnoreCase ("http")) s = "http://" + s;
        safe->aceSettings->setValue (kServerKey, s);
        safe->aceSettings->setValue (kApiKeyKey, aw->getTextEditorContents ("key").trim());
        safe->aceSettings->setValue (kFolderKey, aw->getTextEditorContents ("folder").trim().unquoted());
        safe->aceSettings->saveIfNeeded();
        safe->health.setServer (safe->server(), safe->apiKey());
        safe->engineLabel.setText ({}, juce::dontSendNotification);
        safe->timerCallback();
    }), false);
}

void ForgePage::startEngine()
{
    if (engineUp) { if (ctx.status) ctx.status ("SONG FORGE: the engine is already running"); return; }
    const auto folder = aceFolder();
    const auto bat = folder.getChildFile ("start_api_server.bat");
    if (! bat.existsAsFile())
    {
        if (ctx.status) ctx.status ("SONG FORGE: tell me where ACE-Step is first (SET UP)");
        showSetup();
        return;
    }
   #if JUCE_WINDOWS
    juce::ChildProcess launcher;
    const bool ok = launcher.start (juce::StringArray { "cmd.exe", "/c", "start", "ACE-Step engine", "/D", folder.getFullPathName(), bat.getFileName() }, 0);
   #else
    const bool ok = bat.startAsProcess();
   #endif
    engineStartedAt = juce::Time::getMillisecondCounter();
    engineLabel.setText ({}, juce::dontSendNotification);
    timerCallback();
    if (ctx.status) ctx.status (ok ? juce::String ("SONG FORGE: starting ACE-Step in its own window - the light turns green when it is ready")
                                   : "SONG FORGE: could not start " + bat.getFullPathName());
}

//==============================================================================
ForgePage::TakesPanel::TakesPanel (ForgePage& o) : owner (o)
{
    list.setModel (this);
    list.setRowHeight (34);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff0d1015).withAlpha (0.85f));
    addAndMakeVisible (list);
    addButton.onClick = [this] { owner.addTake (juce::jmax (0, list.getSelectedRow())); };
    addButton.setTooltip ("Put the selected take into the session as a new track");
    allButton.onClick = [this] { for (int i = 0; i < owner.takes.size(); ++i) owner.addTake (i); };
    folderButton.onClick = [this] { if (owner.takesFolder.isDirectory()) owner.takesFolder.revealToUser(); };
    for (auto* b : { &addButton, &allButton, &folderButton }) addAndMakeVisible (*b);
}


void ForgePage::TakesPanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    r.removeFromTop (34);
    auto bottom = r.removeFromBottom (110);
    list.setBounds (r);
    bottom.removeFromTop (6);
    addButton.setBounds (bottom.removeFromTop (34));
    bottom.removeFromTop (4);
    auto row = bottom.removeFromTop (34);
    allButton.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (0, 0).withTrimmedRight (2));
    folderButton.setBounds (row.withTrimmedLeft (2));
}

void ForgePage::TakesPanel::paint (juce::Graphics& g)
{
    PlaygroundLookAndFeel::drawPanelText (g, "TAKES", getLocalBounds().removeFromTop (40).toFloat().reduced (10.0f, 0.0f), juce::Justification::centredLeft, 15.0f);
    const bool any = owner.takes.size() > 0;
    addButton.setEnabled (any); allButton.setEnabled (any); folderButton.setEnabled (owner.takesFolder.isDirectory());
}

void ForgePage::TakesPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (! juce::isPositiveAndBelow (row, owner.takes.size())) return;
    const auto& t = owner.takes.getReference (row);
    g.setColour (selected ? kForge.withAlpha (0.3f) : juce::Colours::transparentBlack);
    g.fillRect (0, 0, w, h);
    g.setColour (kForge.brighter (0.4f));
    g.setFont (PlaygroundLookAndFeel::font (13.0f));
    g.drawText ("TAKE " + juce::String (row + 1), 8, 0, 70, h, juce::Justification::centredLeft);
    juce::String info;
    const int secs = juce::roundToInt (t.seconds);
    if (secs > 0) info << secs / 60 << ":" << juce::String (secs % 60).paddedLeft ('0', 2) << "  ";
    if (t.bpm > 0.0) info << juce::roundToInt (t.bpm) << " BPM  ";
    if (t.key.isNotEmpty()) info << t.key;
    g.setColour (colours::paper);
    g.setFont (PlaygroundLookAndFeel::font (12.0f, false));
    g.drawText (info, 78, 0, w - 82, h, juce::Justification::centredLeft, true);
}
} // namespace pg::ui
