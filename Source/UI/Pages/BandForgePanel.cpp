#include "BandForgePanel.h"

namespace pg::ui
{
using namespace pg::band;

namespace
{
    const juce::Colour kBand { 0xffd9822b };      // copper: Band Forge's colour next to Song Forge's gold
    const char* kKeys[12] { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    constexpr double kRenderRate = 48000.0;

    void darkCombo (juce::ComboBox& cb)
    {
        cb.getProperties().set (props::style, "dark");
        cb.getProperties().set (props::accent, (juce::int64) kBand.getARGB());
        cb.setColour (juce::ComboBox::textColourId, colours::paper);
        cb.setColour (juce::ComboBox::arrowColourId, colours::paperDim);
    }

    juce::String instrumentsKey (Genre g) { return "bandInstruments" + juce::String ((int) g); }
}

//==============================================================================
/** Writes and plays the song on its own thread. Everything it touches is its own copy. */
class BandForgePanel::Job final : public juce::Thread
{
public:
    using Done = std::function<void (bool ok, const juce::String& error, Take)>;

    Job (Request r, juce::File where, std::shared_ptr<const SoundPack> sounds, MixFinish mixFinish, Done d)
        : juce::Thread ("Band Forge"), request (std::move (r)), folder (std::move (where)), pack (std::move (sounds)),
          finish (mixFinish), done (std::move (d)) {}
    ~Job() override { cancelFlag = true; stopThread (20000); }

    void cancel() { cancelFlag = true; }
    float getProgress() const noexcept { return progress.load(); }
    juce::String getStage() const { const juce::ScopedLock sl (stageLock); return stage; }

    void run() override
    {
        Take take;
        juce::String error;
        bool ok = false;
        {
            setStage ("Writing the song...");
            const Song song = compose (request);
            take.title = song.describe();
            take.genre = genreName (song.request.genre);
            take.keyText = juce::String (theory::keyName (song.request.keyRoot)) + (song.request.minor ? " minor" : " major");
            take.bpm = song.bpm;
            take.seconds = song.lengthSeconds (0.0);
            take.folder = folder;
            const int parts = (int) song.parts.size();
            std::vector<Stem> stems;
            int partIndex = 0;
            auto playing = [this, &song, parts] (int i)
            {
                const auto inst = song.parts[(size_t) i].instrument;
                setStage ("Playing " + juce::String (instrumentName (inst))
                          + (pack != nullptr && pack->has (inst) ? " (recorded)" : " (modeled)")
                          + "  (" + juce::String (i + 1) + " of " + juce::String (parts) + ")...");
            };
            auto onProgress = [this, &partIndex, parts, &playing] (float p)
            {
                progress = 0.05f + 0.85f * p;
                partIndex = juce::jmin (parts - 1, juce::roundToInt (p * (float) parts));
                if (partIndex < parts) playing (partIndex);
            };
            if (parts > 0) playing (0);
            progress = 0.05f;
            if (parts == 0) error = "No instruments picked.";
            else if (! renderSong (song, kRenderRate, stems, onProgress, &cancelFlag, pack != nullptr && pack->isOpen() ? pack.get() : nullptr)
                     || cancelFlag.load()) error = "Cancelled.";
            else
            {
                setStage ("Saving the stems...");
                take.stems = writeStems (song, stems, kRenderRate, folder, error);
                for (auto& st : stems) (st.recorded ? take.recorded : take.modeled).add (instrumentName (st.instrument));
                ok = error.isEmpty() && ! take.stems.isEmpty();
                if (ok && finish != MixFinish::none)
                {
                    setStage (finish == MixFinish::mastered ? "Mastering the mix (-14 LUFS)..." : "Saving the clean mix...");
                    progress = 0.93f;
                    const auto mixFile = folder.getChildFile (finish == MixFinish::mastered ? "Band Forge mix (MASTERED).wav" : "Band Forge mix (CLEAN).wav");
                    if (writeMix (stems, kRenderRate, finish, mixFile, take.mixInfo, error)) take.mix = mixFile;
                    else ok = false;
                }
                progress = 1.0f;
            }
        }
        auto cb = done;
        juce::MessageManager::callAsync ([cb, ok, error, take] { if (cb) cb (ok, error, take); });
    }

private:
    void setStage (const juce::String& s) { const juce::ScopedLock sl (stageLock); stage = s; }

    Request request;
    juce::File folder;
    std::shared_ptr<const SoundPack> pack;
    MixFinish finish;
    Done done;
    std::atomic<bool> cancelFlag { false };
    std::atomic<float> progress { 0.0f };
    juce::CriticalSection stageLock;
    juce::String stage;
};

//==============================================================================
BandForgePanel::BandForgePanel (AppContext& c) : ctx (c)
{
    darkCombo (genre);
    for (int g = 0; g < kNumGenres; ++g) genre.addItem (genreName ((Genre) g), g + 1);
    genre.setSelectedItemIndex (juce::jlimit (0, kNumGenres - 1, ctx.settings.getIntValue ("bandGenre", 0)), juce::dontSendNotification);
    genre.setTooltip ("The style of band");
    genre.onChange = [this] { ctx.settings.setValue ("bandGenre", genre.getSelectedItemIndex()); genreChanged (true); };

    darkCombo (key);
    for (int k = 0; k < 12; ++k) key.addItem (juce::String ("Key: ") + kKeys[k], k + 1);
    key.setSelectedItemIndex (juce::jlimit (0, 11, ctx.settings.getIntValue ("bandKey", 7)), juce::dontSendNotification);
    key.onChange = [this] { ctx.settings.setValue ("bandKey", key.getSelectedItemIndex()); };
    key.setTooltip ("Pick the key your vocal is in (Stem Scope tells you the key of a song)");

    darkCombo (mode);
    mode.addItem ("Major (happy, open)", 1);
    mode.addItem ("Minor (dark, sad)", 2);
    mode.setSelectedItemIndex (ctx.settings.getBoolValue ("bandMinor", false) ? 1 : 0, juce::dontSendNotification);
    mode.onChange = [this] { ctx.settings.setValue ("bandMinor", mode.getSelectedItemIndex() == 1); };

    darkCombo (length);
    length.addItem ("Full song (intro, verses, choruses, outro)", 1);
    length.addItem ("Short (about a minute)", 2);
    length.setSelectedItemIndex (ctx.settings.getBoolValue ("bandShort", false) ? 1 : 0, juce::dontSendNotification);
    length.onChange = [this] { ctx.settings.setValue ("bandShort", length.getSelectedItemIndex() == 1); };

    tempo.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    tempo.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    tempo.setRange (0.0, 200.0, 1.0);
    tempo.setValue (ctx.settings.getDoubleValue ("bandBpm", 0.0), juce::dontSendNotification);
    tempo.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 110, 20);
    tempo.setColour (juce::Slider::textBoxTextColourId, kBand.brighter (0.5f));
    tempo.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff0d1015));
    tempo.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    tempo.getProperties().set (props::accent, (juce::int64) kBand.getARGB());
    tempo.setTooltip ("Tempo. All the way down = the style's usual tempo");
    tempo.textFromValueFunction = [this] (double v)
    {
        if (v >= 40.0) return juce::String ((int) v) + " BPM";
        return "AUTO " + juce::String (juce::roundToInt (defaultBpm ((Genre) juce::jmax (0, genre.getSelectedItemIndex())))) + " BPM";
    };
    tempo.valueFromTextFunction = [] (const juce::String& t) { const double v = t.retainCharacters ("0123456789.").getDoubleValue(); return v >= 40.0 ? v : 0.0; };
    tempo.onValueChange = [this] { ctx.settings.setValue ("bandBpm", tempo.getValue() >= 40.0 ? tempo.getValue() : 0.0); };
    addAndMakeVisible (tempo);

    for (auto* cb : { &genre, &key, &mode, &length }) addAndMakeVisible (*cb);

    autoAdd.getProperties().set (props::accent, (juce::int64) kBand.getARGB());
    autoAdd.setToggleState (ctx.settings.getBoolValue ("bandAutoAdd", true), juce::dontSendNotification);
    autoAdd.onClick = [this] { ctx.settings.setValue ("bandAutoAdd", autoAdd.getToggleState()); };
    autoAdd.setTooltip ("When the band is done, every instrument goes into the session as its own track");
    addAndMakeVisible (autoAdd);

    darkCombo (mixChoice);
    mixChoice.addItem ("STEMS ONLY", 1);
    mixChoice.addItem ("STEMS + CLEAN MIX (no mastering, -3 dB peaks)", 2);
    mixChoice.addItem ("STEMS + MASTERED MIX (loud, -14 LUFS)", 3);
    mixChoice.setSelectedItemIndex (juce::jlimit (0, 2, ctx.settings.getIntValue ("bandMix", 2)), juce::dontSendNotification);
    mixChoice.onChange = [this] { ctx.settings.setValue ("bandMix", mixChoice.getSelectedItemIndex()); };
    mixChoice.setTooltip ("Also save the whole band as one WAV next to the stems.\n"
                          "CLEAN: just the sum, for mastering somewhere else or uploading to another app.\n"
                          "MASTERED: finished by Elite Master to streaming loudness (-14 LUFS, peaks under -1 dB).");
    addAndMakeVisible (mixChoice);

    forgeButton.onClick = [this] { forge(); };
    forgeButton.setTooltip ("Write and play a new band. Every press is a new take with different parts.");
    cancelButton.onClick = [this] { cancel(); };
    for (auto* b : { &forgeButton, &cancelButton }) addAndMakeVisible (*b);

    progressLabel.setFont (PlaygroundLookAndFeel::font (13.5f, false));
    progressLabel.setColour (juce::Label::textColourId, colours::paper);
    progressLabel.setJustificationType (juce::Justification::topLeft);
    soundPack = SoundPack::findInstalled();
    progressLabel.setText ("Pick a style, key and instruments, then press FORGE. Each instrument comes out as its own track "
                           "(plus a MIDI file) - no internet, nothing leaves this computer.\n"
                           + (soundPack->isOpen() ? "Real recorded instruments: " + soundPack->describe() + ". The rest are modeled."
                                                  : juce::String ("HEM Sound Pack not found, so every instrument is modeled.")),
                           juce::dontSendNotification);
    addAndMakeVisible (progressLabel);
    progressBar.setColour (juce::ProgressBar::foregroundColourId, kBand);
    progressBar.setColour (juce::ProgressBar::backgroundColourId, juce::Colour (0xff0d1015));
    progressBar.setPercentageDisplay (false);
    addChildComponent (progressBar);

    genreChanged (false);
    startTimerHz (8);
}

BandForgePanel::~BandForgePanel()
{
    stopTimer();
    job.reset();   // cancels and waits for the render thread
}

//==============================================================================
void BandForgePanel::genreChanged (bool resetInstruments)
{
    const auto g = (Genre) juce::jmax (0, genre.getSelectedItemIndex());
    shownInstruments = availableInstruments (g);
    const auto defaults = defaultInstruments (g);

    juce::Array<int> chosen;
    const auto saved = ctx.settings.getValue (instrumentsKey (g));
    if (! resetInstruments && saved.isNotEmpty())
        for (auto& s : juce::StringArray::fromTokens (saved, ",", {})) chosen.add (s.getIntValue());
    else if (saved.isNotEmpty())
        for (auto& s : juce::StringArray::fromTokens (saved, ",", {})) chosen.add (s.getIntValue());
    else
        for (auto i : defaults) chosen.add ((int) i);

    instrumentBoxes.clear();
    for (auto inst : shownInstruments)
    {
        const bool rec = soundPack != nullptr && soundPack->has (inst);
        auto* b = instrumentBoxes.add (new juce::ToggleButton (juce::String (instrumentName (inst)).toUpperCase() + (rec ? "  REC" : "")));
        b->setTooltip (rec ? "Played from real recordings (HEM Sound Pack)" : "Modeled instrument (made by code, no samples)");
        b->getProperties().set (props::accent, (juce::int64) kBand.getARGB());
        b->setToggleState (chosen.contains ((int) inst), juce::dontSendNotification);
        b->onClick = [this] { saveInstrumentChoice(); };
        addAndMakeVisible (b);
    }
    tempo.updateText();
    resized();
    repaint();
}

void BandForgePanel::saveInstrumentChoice()
{
    juce::StringArray on;
    for (int i = 0; i < instrumentBoxes.size() && i < (int) shownInstruments.size(); ++i)
        if (instrumentBoxes[i]->getToggleState()) on.add (juce::String ((int) shownInstruments[(size_t) i]));
    ctx.settings.setValue (instrumentsKey ((Genre) juce::jmax (0, genre.getSelectedItemIndex())), on.joinIntoString (","));
}

Request BandForgePanel::makeRequest() const
{
    Request r;
    r.genre = (Genre) juce::jmax (0, genre.getSelectedItemIndex());
    r.keyRoot = juce::jmax (0, key.getSelectedItemIndex());
    r.minor = mode.getSelectedItemIndex() == 1;
    r.bpm = tempo.getValue() >= 40.0 ? tempo.getValue() : 0.0;
    r.fullSong = length.getSelectedItemIndex() != 1;
    for (int i = 0; i < instrumentBoxes.size() && i < (int) shownInstruments.size(); ++i)
        if (instrumentBoxes[i]->getToggleState()) r.instruments.push_back (shownInstruments[(size_t) i]);
    r.seed = (uint32_t) juce::Random::getSystemRandom().nextInt (0x7fffffff) | 1u;   // every press = a new take
    return r;
}

void BandForgePanel::forge()
{
    if (job != nullptr) return;
    const auto req = makeRequest();
    if (req.instruments.empty())
    {
        progressLabel.setText ("Tick at least one instrument first.", juce::dontSendNotification);
        return;
    }
    const auto folder = AppContext::songsFolder().getChildFile ("Band Forge")
                            .getChildFile (juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S") + " " + genreName (req.genre));
    juce::Component::SafePointer<BandForgePanel> safe (this);
    soundPack = SoundPack::findInstalled();   // looked up every time, so a pack installed while the app runs is used
    const auto finish = mixChoice.getSelectedItemIndex() == 0 ? MixFinish::none
                      : mixChoice.getSelectedItemIndex() == 1 ? MixFinish::clean : MixFinish::mastered;
    job = std::make_unique<Job> (req, folder, soundPack, finish, [safe] (bool ok, const juce::String& err, Take t) { if (safe != nullptr) safe->finished (ok, err, std::move (t)); });
    progress = 0.0;
    progressBar.setVisible (true);
    job->startThread (juce::Thread::Priority::normal);
    if (ctx.status) ctx.status ("BAND FORGE: making a " + juce::String (genreName (req.genre)) + " band...");
    timerCallback();
}

void BandForgePanel::cancel() { if (job != nullptr) job->cancel(); }

void BandForgePanel::finished (bool ok, const juce::String& error, Take take)
{
    job.reset();
    progressBar.setVisible (false);
    if (! ok)
    {
        progressLabel.setText (error.isNotEmpty() ? error : juce::String ("Something went wrong making the band."), juce::dontSendNotification);
        if (ctx.status) ctx.status ("BAND FORGE: " + error);
        timerCallback();
        return;
    }
    takes.insert (0, take);
    takesPanel.refresh();
    const int secs = juce::roundToInt (take.seconds);
    progressLabel.setText ("Done: " + take.title + ", " + juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2) + ", "
                               + juce::String (take.stems.size()) + " instruments.\n"
                               + (take.recorded.isEmpty() ? juce::String()
                                                          : "Recorded: " + take.recorded.joinIntoString (", ")
                                                                + (take.modeled.isEmpty() ? juce::String() : ".  Modeled: " + take.modeled.joinIntoString (", ")) + ".\n")
                               + (take.mix.existsAsFile() ? "Mix: " + take.mix.getFileName() + " (" + take.mixInfo + ").\n" : juce::String())
                               + "Don't like it? Press FORGE again for a new take.",
                           juce::dontSendNotification);
    if (ctx.status) ctx.status ("BAND FORGE: " + juce::String (take.stems.size()) + " stems saved in " + take.folder.getFullPathName());
    if (autoAdd.getToggleState()) addTake (0);
    timerCallback();
}

juce::String BandForgePanel::sunoStyle (const Take& t)
{
    juce::StringArray inst;
    for (auto& n : t.recorded) inst.add (n.toLowerCase());
    for (auto& n : t.modeled) inst.add (n.toLowerCase());
    return t.genre.toLowerCase() + " instrumental, " + juce::String (juce::roundToInt (t.bpm)) + " BPM, " + t.keyText
           + (inst.isEmpty() ? juce::String() : ", " + inst.joinIntoString (", ")) + ", live band feel, warm mix";
}

void BandForgePanel::forSuno (int index)
{
    if (! juce::isPositiveAndBelow (index, takes.size())) return;
    const auto& t = takes.getReference (index);
    const auto style = sunoStyle (t);
    juce::SystemClipboard::copyTextToClipboard (style);
    const auto show = t.mix.existsAsFile() ? t.mix : t.folder;
    if (show.exists()) show.revealToUser();
    progressLabel.setText ("Suno style copied:\n" + style + "\n"
                           + (t.mix.existsAsFile() ? "Upload " + t.mix.getFileName() + " in Suno and paste the style. "
                                                   : juce::String ("No mix file for this take (pick a MIX choice before FORGE); the stems are in its folder. "))
                           + "Bring what Suno makes back into the LIBRARY.", juce::dontSendNotification);
    if (ctx.status) ctx.status ("BAND FORGE: Suno style copied to the clipboard");
}

void BandForgePanel::addTake (int index)
{
    if (! juce::isPositiveAndBelow (index, takes.size())) return;
    const auto& t = takes.getReference (index);
    juce::Array<juce::File> present;
    for (auto& f : t.stems) if (f.existsAsFile()) present.add (f);
    if (present.isEmpty())
    {
        if (ctx.status) ctx.status ("BAND FORGE: the stem files are gone (" + t.folder.getFullPathName() + ")");
        return;
    }
    if (t.bpm > 0.0 && ctx.engine.getNumUsedTracks() == 0)
        if (auto* p = ctx.engine.getAPVTS().getParameter (PlaygroundEngine::Ids::bpm))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) t.bpm));   // empty session: take the band's tempo
    if (ctx.importFiles) ctx.importFiles (present);
}

//==============================================================================
void BandForgePanel::timerCallback()
{
    forgeButton.setEnabled (job == nullptr);
    cancelButton.setEnabled (job != nullptr);
    for (auto* c : std::initializer_list<juce::Component*> { &genre, &key, &mode, &length, &tempo, &mixChoice }) c->setEnabled (job == nullptr);
    for (auto* b : instrumentBoxes) b->setEnabled (job == nullptr);
    if (job != nullptr)
    {
        progress = job->getProgress();
        const auto s = job->getStage();
        if (s != progressLabel.getText()) progressLabel.setText (s, juce::dontSendNotification);
    }
}

//==============================================================================
void BandForgePanel::resized()
{
    auto r = getLocalBounds().reduced (14, 6);
    headerArea = r.removeFromTop (30);
    r.removeFromTop (6);

    auto right = r.removeFromRight (juce::jmin (420, r.getWidth() / 3));
    r.removeFromRight (14);
    optionsArea = right;

    // left: style + instruments
    auto row = r.removeFromTop (34);
    genre.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (2));
    length.setBounds (row.reduced (2));
    r.removeFromTop (4);
    row = r.removeFromTop (34);
    key.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (2));
    mode.setBounds (row.reduced (2));
    r.removeFromTop (10);
    instrumentsCaption = r.removeFromTop (22);
    const int cols = r.getWidth() > 520 ? 3 : 2;
    const int rows = juce::jmax (1, (instrumentBoxes.size() + cols - 1) / cols);
    const int rowH = juce::jlimit (26, 40, r.getHeight() / juce::jmax (1, rows));
    for (int i = 0; i < instrumentBoxes.size(); ++i)
    {
        const int cx = i % cols, cy = i / cols;
        instrumentBoxes[i]->setBounds (r.getX() + cx * r.getWidth() / cols, r.getY() + cy * rowH, r.getWidth() / cols - 4, rowH - 4);
    }

    // right: tempo, options, FORGE
    tempo.setBounds (right.removeFromTop (juce::jmin (130, right.getHeight() / 3)).withSizeKeepingCentre (130, juce::jmin (130, right.getHeight() / 3)));
    right.removeFromTop (8);
    autoAdd.setBounds (right.removeFromTop (30).reduced (2));
    right.removeFromTop (4);
    mixChoice.setBounds (right.removeFromTop (32).reduced (2));
    right.removeFromTop (8);
    row = right.removeFromTop (56);
    cancelButton.setBounds (row.removeFromRight (110).reduced (2));
    forgeButton.setBounds (row.reduced (2));
    right.removeFromTop (8);
    progressBar.setBounds (right.removeFromTop (10).reduced (2, 0));
    right.removeFromTop (6);
    progressLabel.setBounds (right.removeFromTop (juce::jmin (110, right.getHeight())));
}

void BandForgePanel::paint (juce::Graphics& g)
{
    PlaygroundLookAndFeel::drawPanelText (g, "BAND FORGE  -  a band written and played on this computer", headerArea.toFloat(),
                                          juce::Justification::centredLeft, 15.0f, kBand.brighter (0.4f));
    PlaygroundLookAndFeel::drawPanelText (g, "INSTRUMENTS  (each one becomes its own track)", instrumentsCaption.toFloat(),
                                          juce::Justification::centredLeft, 13.0f, kBand.brighter (0.4f));
    g.setColour (juce::Colour (0xff12151a).withAlpha (0.55f));
    g.fillRoundedRectangle (optionsArea.toFloat().expanded (8.0f, 6.0f), 6.0f);
}

//==============================================================================
BandForgePanel::TakesPanel::TakesPanel (BandForgePanel& o) : owner (o)
{
    list.setModel (this);
    list.setRowHeight (44);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff0d1015).withAlpha (0.85f));
    addAndMakeVisible (list);
    addButton.onClick = [this] { owner.addTake (juce::jmax (0, list.getSelectedRow())); };
    addButton.setTooltip ("Put every instrument of the selected take into the session");
    folderButton.onClick = [this]
    {
        const int r = juce::jmax (0, list.getSelectedRow());
        if (juce::isPositiveAndBelow (r, owner.takes.size()) && owner.takes.getReference (r).folder.isDirectory())
            owner.takes.getReference (r).folder.revealToUser();
        else
            AppContext::songsFolder().getChildFile ("Band Forge").revealToUser();
    };
    sunoButton.setTooltip ("Copies a Suno style for this take and shows its mix: upload the mix in Suno and paste the style");
    sunoButton.onClick = [this] { owner.forSuno (juce::jmax (0, list.getSelectedRow())); };
    libraryButton.setTooltip ("Put this take's mix (or its stems) in your LIBRARY");
    libraryButton.onClick = [this]
    {
        const int r = juce::jmax (0, list.getSelectedRow());
        if (! juce::isPositiveAndBelow (r, owner.takes.size()) || owner.onSendToLibrary == nullptr) return;
        const auto& t = owner.takes.getReference (r);
        juce::Array<juce::File> files;
        if (t.mix.existsAsFile()) files.add (t.mix); else files.addArray (t.stems);
        owner.onSendToLibrary (files, t.genre, t.folder);
    };
    for (auto* b : { &addButton, &folderButton, &sunoButton, &libraryButton }) addAndMakeVisible (*b);
}

void BandForgePanel::TakesPanel::refresh()
{
    list.updateContent();
    list.selectRow (0);
    list.repaint();
    repaint();
}

void BandForgePanel::TakesPanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    r.removeFromTop (34);
    auto bottom = r.removeFromBottom (110);
    list.setBounds (r);
    bottom.removeFromTop (6);
    addButton.setBounds (bottom.removeFromTop (34));
    bottom.removeFromTop (4);
    auto pair = bottom.removeFromTop (30);
    sunoButton.setBounds (pair.removeFromLeft (pair.getWidth() / 2).withTrimmedRight (2));
    libraryButton.setBounds (pair.withTrimmedLeft (2));
    bottom.removeFromTop (4);
    folderButton.setBounds (bottom.removeFromTop (30));
}

void BandForgePanel::TakesPanel::paint (juce::Graphics& g)
{
    PlaygroundLookAndFeel::drawPanelText (g, "BAND TAKES", getLocalBounds().removeFromTop (40).toFloat().reduced (10.0f, 0.0f), juce::Justification::centredLeft, 15.0f);
    for (auto* b : { &addButton, &sunoButton, &libraryButton }) b->setEnabled (owner.takes.size() > 0);
}

void BandForgePanel::TakesPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (! juce::isPositiveAndBelow (row, owner.takes.size())) return;
    const auto& t = owner.takes.getReference (row);
    g.setColour (selected ? kBand.withAlpha (0.3f) : juce::Colours::transparentBlack);
    g.fillRect (0, 0, w, h);
    g.setColour (kBand.brighter (0.4f));
    g.setFont (PlaygroundLookAndFeel::font (13.0f));
    g.drawText ("TAKE " + juce::String (owner.takes.size() - row), 8, 2, w - 12, h / 2, juce::Justification::centredLeft);
    g.setColour (colours::paper);
    g.setFont (PlaygroundLookAndFeel::font (11.5f, false));
    const int secs = juce::roundToInt (t.seconds);
    g.drawText (t.title + "  " + juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2) + "  " + juce::String (t.stems.size()) + " tracks",
                8, h / 2, w - 12, h / 2 - 2, juce::Justification::centredLeft, true);
}
} // namespace pg::ui
