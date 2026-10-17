#include "MasterPage.h"

namespace pg::ui
{
using E = PlaygroundEngine;

namespace { const juce::Colour kGold { 0xffcfa63a }; }

//==============================================================================
struct MasterPage::ExportJob final : juce::Thread
{
    using Done = std::function<void (bool, juce::String, float peak, float cleanGain)>;
    ExportJob (std::unique_ptr<OfflineExport> e, Done done)
        : juce::Thread ("WAV export"), exporter (std::move (e)), onDone (std::move (done)) {}
    ~ExportJob() override { cancel.store (true); stopThread (20000); }

    void run() override
    {
        juce::String err;
        const bool ok = exporter->render ([this] (double p) { progress.store (p); }, [this] { return cancel.load() || threadShouldExit(); }, err);
        const float peak = exporter->getPeak(), gain = exporter->getCleanGain();
        auto cb = onDone;
        juce::MessageManager::callAsync ([cb, ok, err, peak, gain] { cb (ok, err, peak, gain); });
    }

    std::unique_ptr<OfflineExport> exporter;
    Done onDone;
    std::atomic<double> progress { 0.0 };
    std::atomic<bool> cancel { false };
};

//==============================================================================
MasterPage::MasterPage (AppContext& c) : ctx (c)
{
    for (int i = 0; i < E::kSlots; ++i)
    {
        auto s = std::make_unique<InsertSlot> (ctx, E::kMaster, i);
        s->setTooltip ("Master insert " + juce::String (i + 1) + " (put Elite Master last)");
        addAndMakeVisible (*s);
        slots.push_back (std::move (s));
    }
    matchButton.setTooltip ("Sets Elite Master's INPUT so the song lands on the chosen loudness target (play the loudest part first)");
    matchButton.onClick = [this]
    {
        if (auto* m = findEliteMaster())
        {
            if (m->matchTarget()) { if (ctx.status) ctx.status ("MASTER: input set to hit " + EliteMaster::targets()[(int) m->getAPVTS().getRawParameterValue ("target")->load()]); }
            else if (ctx.status) ctx.status ("MASTER: play the song for a while first, so Elite Master can measure it");
        }
    };
    resetButton.onClick = [this] { if (auto* m = findEliteMaster()) m->resetMeters(); };
    addMasterButton.onClick = [this] { ctx.insertPlugin (E::kMaster, E::kSlots - 1, "elite-master"); ctx.openPlugin (E::kMaster, E::kSlots - 1); };
    for (auto* b : { &matchButton, &resetButton, &addMasterButton }) addAndMakeVisible (*b);

    fileName.setFont (PlaygroundLookAndFeel::font (15.0f, false));
    fileName.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0d1015));
    fileName.setColour (juce::TextEditor::textColourId, colours::paper);
    fileName.setColour (juce::TextEditor::outlineColourId, kGold.withAlpha (0.4f));
    fileName.setText (ctx.settings.getValue ("exportName", "My Song - master"), false);
    addAndMakeVisible (fileName);

    auto combo = [this] (juce::ComboBox& cb, const juce::StringArray& items, const juce::String& key, int def)
    {
        cb.getProperties().set (props::style, "dark");
        cb.getProperties().set (props::accent, (juce::int64) kGold.getARGB());
        cb.setColour (juce::ComboBox::textColourId, colours::paper);
        cb.setColour (juce::ComboBox::arrowColourId, colours::paperDim);
        cb.addItemList (items, 1);
        cb.setSelectedItemIndex (ctx.settings.getIntValue (key, def), juce::dontSendNotification);
        cb.onChange = [this, &cb, key] { ctx.settings.setValue (key, cb.getSelectedItemIndex()); };
        addAndMakeVisible (cb);
    };
    combo (finish, { "MASTERED  -  loud, master chain on", "CLEAN  -  master chain off, peaks at -3 dB" }, "exportFinish", 0);
    finish.setTooltip ("MASTERED: the file sounds like the MASTER page (Elite Master loudness and limiting).\n"
                       "CLEAN: the master inserts are skipped and the file keeps its dynamics, turned down only if its "
                       "peaks go over -3 dB. For mastering somewhere else, or for uploading to another app.");
    combo (bits, { "16-bit (CD / streaming upload)", "24-bit (best for mastering)", "32-bit float" }, "exportBits", 1);
    combo (rate, { "44.1 kHz", "48 kHz", "96 kHz" }, "exportRate", 1);

    tail.setSliderStyle (juce::Slider::LinearHorizontal);
    tail.setRange (0.0, 10.0, 0.5);
    tail.setValue (ctx.settings.getDoubleValue ("exportTail", 3.0), juce::dontSendNotification);
    tail.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
    tail.setTextValueSuffix (" s");
    tail.setColour (juce::Slider::textBoxTextColourId, colours::paper);
    tail.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    tail.getProperties().set (props::accent, (juce::int64) kGold.getARGB());
    tail.setTooltip ("Extra time after the last track ends, so reverb and delay can ring out");
    tail.onValueChange = [this] { ctx.settings.setValue ("exportTail", tail.getValue()); };
    addAndMakeVisible (tail);

    beforeVersion.getProperties().set (props::accent, (juce::int64) juce::Colour (0xffffb02e).getARGB());
    beforeVersion.setTooltip ("Export the BEFORE version: every plugin bypassed (to compare)");
    addAndMakeVisible (beforeVersion);

    folderButton.onClick = [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Export folder", exportFolder());
        juce::Component::SafePointer<MasterPage> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [safe, chooser] (const juce::FileChooser& fc)
        {
            if (safe != nullptr && fc.getResult() != juce::File()) { safe->ctx.settings.setValue ("exportFolder", fc.getResult().getFullPathName()); safe->repaint(); }
        });
    };
    exportButton.onClick = [this] { if (job != nullptr) job->cancel.store (true); else startExport(); };
    revealButton.onClick = [this] { if (lastExport.existsAsFile()) lastExport.revealToUser(); };
    for (auto* b : { &folderButton, &exportButton, &revealButton }) addAndMakeVisible (*b);

    rebuild();
    startTimerHz (15);
}

MasterPage::~MasterPage()
{
    stopTimer();
    job.reset();
}

void MasterPage::rebuild()
{
    masterStrip = std::make_unique<ChannelStrip> (ctx, E::kMaster);
    addAndMakeVisible (*masterStrip);
    resized();
}

juce::File MasterPage::exportFolder() const
{
    const juce::File f (ctx.settings.getValue ("exportFolder"));
    return f.isDirectory() ? f : AppContext::songsFolder().getChildFile ("Exports");
}

EliteMaster* MasterPage::findEliteMaster() const
{
    for (int s = E::kSlots - 1; s >= 0; --s)
        if (auto* m = dynamic_cast<EliteMaster*> (ctx.engine.getPlugin (E::kMaster, s))) return m;
    return nullptr;
}

void MasterPage::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    if (masterStrip != nullptr) masterStrip->setBounds (r.removeFromLeft (MixPage::kStripWidth + 10));
    r.removeFromLeft (14);
    exportArea = r.removeFromRight (juce::jmin (440, r.getWidth() / 2));
    r.removeFromRight (14);

    slotsArea = r.removeFromTop (juce::jmin (r.getHeight() / 2, 8 * 30 + 36));
    auto s = slotsArea.withTrimmedTop (30);
    const int h = s.getHeight() / E::kSlots;
    for (auto& sl : slots) sl->setBounds (s.removeFromTop (h).reduced (0, 2));
    r.removeFromTop (12);
    loudArea = r;
    auto buttons = loudArea.removeFromBottom (40);
    matchButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (2));
    resetButton.setBounds (buttons.reduced (2));
    addMasterButton.setBounds (loudArea.withSizeKeepingCentre (juce::jmin (260, loudArea.getWidth()), 44));

    auto e = exportArea.reduced (14, 12).withTrimmedTop (30);
    auto row = [&e] (int h2) { auto x = e.removeFromTop (h2); e.removeFromTop (8); return x; };
    e.removeFromTop (18);
    fileName.setBounds (row (32));
    e.removeFromTop (14);
    folderButton.setBounds (row (32).removeFromLeft (130));
    e.removeFromTop (14);
    finish.setBounds (row (30));
    bits.setBounds (row (30));
    rate.setBounds (row (30));
    e.removeFromTop (14);
    tail.setBounds (row (28));
    beforeVersion.setBounds (row (30));
    auto b = row (52);
    revealButton.setBounds (b.removeFromRight (120).reduced (2));
    exportButton.setBounds (b.reduced (2));
}

void MasterPage::paint (juce::Graphics& g)
{
    auto box = [&g] (juce::Rectangle<int> r) { g.setColour (juce::Colour (0xff12151a).withAlpha (0.7f)); g.fillRoundedRectangle (r.toFloat(), 6.0f); };
    box (slotsArea.expanded (8, 4));
    box (loudArea.expanded (8, 4).withBottom (getHeight() - 8));
    box (exportArea);
    PlaygroundLookAndFeel::drawPanelText (g, "MASTER INSERTS", slotsArea.withHeight (26).toFloat(), juce::Justification::centredLeft, 15.0f, kGold.brighter (0.4f));

    auto lr = loudArea.withTrimmedBottom (48).toFloat();
    if (auto* m = findEliteMaster())
    {
        addMasterButton.setVisible (false);
        matchButton.setVisible (true); resetButton.setVisible (true);
        PlaygroundLookAndFeel::drawPanelText (g, "LOUDNESS  (Elite Master)", lr.removeFromTop (26.0f), juce::Justification::centredLeft, 15.0f, kGold.brighter (0.4f));
        const int target = (int) m->getAPVTS().getRawParameterValue ("target")->load();
        auto val = [] (float v, const char* unit) { return v <= -69.0f ? juce::String ("--") : juce::String (v, 1) + " " + unit; };
        const std::pair<juce::String, juce::String> rows[] {
            { "INTEGRATED", val (m->getReadout (EliteMaster::integrated), "LUFS") },
            { "SHORT-TERM", val (m->getReadout (EliteMaster::shortTerm), "LUFS") },
            { "TRUE PEAK", val (m->getReadout (EliteMaster::truePeak), "dBTP") },
            { "LIMITING", juce::String (m->getReadout (EliteMaster::gr), 1) + " dB" },
            { "TARGET", EliteMaster::targets()[target] } };
        const float rh = juce::jmin (40.0f, lr.getHeight() / 5.0f);
        for (int i = 0; i < 5; ++i)
        {
            auto row = lr.removeFromTop (rh);
            PlaygroundLookAndFeel::drawPanelText (g, rows[i].first, row.removeFromLeft (row.getWidth() * 0.45f), juce::Justification::centredLeft, 13.0f, colours::paperDim);
            g.setFont (PlaygroundLookAndFeel::font (i == 0 ? 22.0f : 17.0f));
            g.setColour (i == 0 ? kGold.brighter (0.5f) : colours::paper);
            g.drawText (rows[i].second, row, juce::Justification::centredLeft, false);
        }
    }
    else
    {
        addMasterButton.setVisible (true);
        matchButton.setVisible (false); resetButton.setVisible (false);
        PlaygroundLookAndFeel::drawPanelText (g, "No Elite Master on the master yet - add it for the final limiter and loudness meter.",
                                              lr.withTrimmedBottom (lr.getHeight() * 0.55f), juce::Justification::centredBottom, 13.0f, colours::paperDim);
    }

    auto e = exportArea.reduced (14, 12).toFloat();
    PlaygroundLookAndFeel::drawPanelText (g, "EXPORT WAV", e.removeFromTop (30.0f), juce::Justification::centredLeft, 17.0f, kGold.brighter (0.4f));
    PlaygroundLookAndFeel::drawPanelText (g, "FILE NAME", e.removeFromTop (18.0f), juce::Justification::centredLeft, 11.5f, colours::paperDim);
    PlaygroundLookAndFeel::drawPanelText (g, "SAVES TO:  " + exportFolder().getFullPathName(),
                                          juce::Rectangle<float> ((float) folderButton.getRight() + 10.0f, (float) folderButton.getY(), e.getRight() - (float) folderButton.getRight() - 10.0f, (float) folderButton.getHeight()),
                                          juce::Justification::centredLeft, 11.0f, colours::paperDim);
    // progress
    auto pb = juce::Rectangle<float> ((float) exportButton.getX(), (float) exportButton.getBottom() + 14.0f, (float) (revealButton.getRight() - exportButton.getX()), 12.0f);
    g.setColour (juce::Colour (0xff0b0d10));
    g.fillRoundedRectangle (pb, 4.0f);
    if (job != nullptr || progress > 0.0)
    {
        g.setColour (kGold);
        g.fillRoundedRectangle (pb.withWidth (pb.getWidth() * (float) juce::jlimit (0.0, 1.0, progress)), 4.0f);
    }
    g.setColour (colours::paper);
    g.setFont (PlaygroundLookAndFeel::font (12.5f, false));
    g.drawFittedText (exportMessage, pb.translated (0.0f, 18.0f).withHeight (60.0f).toNearestInt(), juce::Justification::topLeft, 4);
}

void MasterPage::timerCallback()
{
    if (! isShowing() && job == nullptr) return;
    if (job != nullptr) progress = job->progress.load();
    exportButton.setButtonText (job != nullptr ? "CANCEL" : "EXPORT WAV");
    revealButton.setEnabled (lastExport.existsAsFile());
    if (masterStrip != nullptr) masterStrip->updateMeter();
    for (auto& s : slots) s->repaint();
    repaint (loudArea.expanded (8));
    repaint (exportArea);
}

void MasterPage::startExport()
{
    if (job != nullptr) return;
    if (ctx.engine.getNumUsedTracks() == 0) { exportMessage = "Nothing to export yet: load your stems first."; repaint(); return; }
    auto name = juce::File::createLegalFileName (fileName.getText().trim());
    if (name.isEmpty()) name = "Rework Playground mix";
    ctx.settings.setValue ("exportName", fileName.getText());

    ExportOptions o;
    o.file = exportFolder().getChildFile (name + ".wav");
    o.bitDepth = bits.getSelectedItemIndex() == 0 ? 16 : (bits.getSelectedItemIndex() == 2 ? 32 : 24);
    o.sampleRate = rate.getSelectedItemIndex() == 0 ? 44100.0 : (rate.getSelectedItemIndex() == 2 ? 96000.0 : 48000.0);
    o.tailSeconds = tail.getValue();
    o.beforeMode = beforeVersion.getToggleState();
    o.clean = finish.getSelectedItemIndex() == 1;

    const auto session = ctx.engine.createSessionTree();
    std::map<juce::String, SampleData::Ptr> media;
    for (int t = 0; t < E::kMaxTracks; ++t)
        if (auto m = ctx.engine.getTrackMedia (t))
            media[m->getFile().getFullPathName()] = m;   // reuse the decoded audio: no second load from disk

    auto exporter = std::make_unique<OfflineExport> (session, media, o);
    const auto target = o.file;
    juce::Component::SafePointer<MasterPage> safe (this);
    job = std::make_unique<ExportJob> (std::move (exporter), [safe, target, clean = o.clean] (bool ok, juce::String err, float peak, float cleanGain)
    {
        if (safe == nullptr) return;
        safe->job.reset();
        safe->progress = ok ? 1.0 : 0.0;
        if (ok)
        {
            safe->lastExport = target;
            safe->exportMessage = "Saved " + target.getFileName() + (clean ? " (CLEAN)" : " (MASTERED)")
                                  + ".  Peak " + juce::String (params::gainToDb (peak), 1) + " dBFS"
                                  + (clean && cleanGain < 0.999f ? ", turned down " + juce::String (-params::gainToDb (cleanGain), 1) + " dB to keep headroom" : juce::String())
                                  + (peak > 1.0f ? "  -  WARNING: it clips. Add Elite Master or lower the master fader." : "");
        }
        else
            safe->exportMessage = err;
        if (safe->ctx.status) safe->ctx.status ("EXPORT: " + safe->exportMessage);
        safe->repaint();
    });
    exportMessage = "Exporting " + o.file.getFileName() + (o.clean ? " CLEAN" : " MASTERED") + " (" + juce::String (o.bitDepth) + "-bit, " + juce::String (o.sampleRate / 1000.0, 1) + " kHz)...";
    progress = 0.0;
    job->startThread();
    repaint();
}

//==============================================================================
void MasterPage::Guide::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (12, 8).toFloat();
    PlaygroundLookAndFeel::drawPanelText (g, "LOUDNESS GUIDE", r.removeFromTop (32.0f), juce::Justification::centredLeft, 15.0f);
    const std::pair<const char*, const char*> rows[] {
        { "-14 LUFS", "Spotify, YouTube, TikTok" }, { "-16 LUFS", "Apple Music" }, { "-9 LUFS", "Loud trap / rap" },
        { "-7 LUFS", "Club / DJ" }, { "-1 dBTP", "Ceiling for streaming" } };
    for (auto& [a, b] : rows)
    {
        auto row = r.removeFromTop (44.0f);
        PlaygroundLookAndFeel::drawPanelText (g, a, row.removeFromTop (22.0f), juce::Justification::centredLeft, 15.0f, juce::Colour (0xffcfa63a).brighter (0.4f));
        PlaygroundLookAndFeel::drawPanelText (g, b, row, juce::Justification::topLeft, 12.0f, colours::paperDim);
    }
    r.removeFromTop (10.0f);
    g.setFont (PlaygroundLookAndFeel::font (12.0f, false));
    g.setColour (colours::paperDim);
    g.drawFittedText ("Play the loudest part, press MATCH TARGET, then export. Use BEFORE / AFTER (transport) to check the master really sounds better, not just louder.",
                      r.toNearestInt(), juce::Justification::topLeft, 8);
}
} // namespace pg::ui
