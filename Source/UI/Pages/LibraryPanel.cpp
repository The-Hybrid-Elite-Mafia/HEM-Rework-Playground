#include "LibraryPanel.h"
#include "Engine/MediaLoader.h"
#include "Band/BandTypes.h"

namespace pg::ui
{
namespace
{
    const juce::Colour kLib { 0xff4fb3d9 };   // library blue next to Band Forge's copper
    enum Col { cStars = 1, cTitle, cKind, cGenre, cBpm, cKey, cLength, cLufs, cAdded };

    void darkCombo (juce::ComboBox& cb)
    {
        cb.getProperties().set (props::style, "dark");
        cb.getProperties().set (props::accent, (juce::int64) kLib.getARGB());
        cb.setColour (juce::ComboBox::textColourId, colours::paper);
        cb.setColour (juce::ComboBox::arrowColourId, colours::paperDim);
    }

    void darkEditor (juce::TextEditor& t)
    {
        t.setFont (PlaygroundLookAndFeel::font (14.0f, false));
        t.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0d1015));
        t.setColour (juce::TextEditor::textColourId, colours::paper);
        t.setColour (juce::TextEditor::outlineColourId, kLib.withAlpha (0.4f));
        t.setColour (juce::TextEditor::focusedOutlineColourId, kLib);
    }

    juce::String timeText (double s)
    {
        const int t = juce::roundToInt (s);
        return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2);
    }

    juce::String starText (int n) { return n <= 0 ? juce::String ("-") : juce::String (n) + (n == 1 ? " STAR" : " STARS"); }

    bool isAudio (const juce::File& f)
    {
        return f.hasFileExtension ("wav;mp3;flac;aif;aiff;ogg;m4a");
    }
}

//==============================================================================
LibraryPanel::KindsPanel::KindsPanel (LibraryPanel& o) : owner (o)
{
    folderButton.onClick = [this] { owner.lib.getFolder().createDirectory(); owner.lib.getFolder().revealToUser(); };
    addAndMakeVisible (folderButton);
}

juce::Rectangle<int> LibraryPanel::KindsPanel::rowArea (int i) const
{
    return { 8, 48 + i * 40, getWidth() - 16, 36 };
}

void LibraryPanel::KindsPanel::resized()
{
    folderButton.setBounds (getLocalBounds().reduced (8).removeFromBottom (32));
}

void LibraryPanel::KindsPanel::paint (juce::Graphics& g)
{
    PlaygroundLookAndFeel::drawPanelText (g, "LIBRARY  (private)", getLocalBounds().removeFromTop (40).toFloat().reduced (10.0f, 0.0f),
                                          juce::Justification::centredLeft, 15.0f);
    juce::StringArray names { "EVERYTHING" };
    juce::StringArray filters { "" };
    for (auto& k : library::Library::kinds()) { names.add (k.toUpperCase() + "S"); filters.add (k); }
    for (int i = 0; i < names.size(); ++i)
    {
        int count = 0;
        for (auto& e : owner.lib.all()) if (filters[i].isEmpty() || e.kind == filters[i]) ++count;
        const auto r = rowArea (i);
        const bool on = owner.kindFilter == filters[i];
        g.setColour (on ? kLib.withAlpha (0.3f) : juce::Colour (0xff0d1015).withAlpha (0.6f));
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
        PlaygroundLookAndFeel::drawPanelText (g, names[i], r.toFloat().reduced (10.0f, 0.0f), juce::Justification::centredLeft, 13.0f,
                                              on ? kLib.brighter (0.5f) : colours::paper);
        PlaygroundLookAndFeel::drawPanelText (g, juce::String (count), r.toFloat().reduced (10.0f, 0.0f), juce::Justification::centredRight, 13.0f,
                                              colours::paperDim);
    }
    auto hint = juce::Rectangle<float> (10.0f, (float) rowArea (names.size()).getY() + 10.0f, (float) getWidth() - 20.0f, 120.0f);
    g.setColour (colours::paperDim);
    g.setFont (PlaygroundLookAndFeel::font (12.0f, false));
    g.drawFittedText ("Your own beats and songs: Band Forge parts you took through Suno and brought back. "
                      "Kept on this computer only.", hint.toNearestInt(), juce::Justification::topLeft, 6);
}

void LibraryPanel::KindsPanel::mouseUp (const juce::MouseEvent& e)
{
    juce::StringArray filters { "" };
    filters.addArray (library::Library::kinds());
    for (int i = 0; i < filters.size(); ++i)
        if (rowArea (i).contains (e.getPosition()))
        {
            owner.kindFilter = filters[i];
            owner.refresh();
            repaint();
        }
}

//==============================================================================
LibraryPanel::LibraryPanel (AppContext& c) : ctx (c)
{
    juce::String err;
    if (! lib.load (err) && ctx.status) ctx.status ("LIBRARY: " + err);

    darkEditor (search);
    search.setTextToShowWhenEmpty ("Search: a word, a genre, a key (\"minor\"), or a tempo (\"140\")", colours::paperDim);
    search.onTextChange = [this] { refresh(); };
    addAndMakeVisible (search);

    addButton.setTooltip ("Copy songs / beats / stems into the library (you can also drop files here)");
    addButton.onClick = [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Add to the library",
                                                            juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads"),
                                                            MediaLoader::wildcard());
        juce::Component::SafePointer<LibraryPanel> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [safe, chooser] (const juce::FileChooser& fc)
                              {
                                  if (safe != nullptr && ! fc.getResults().isEmpty()) safe->addFiles (fc.getResults());
                              });
    };
    addAndMakeVisible (addButton);

    auto& h = table.getHeader();
    const int flags = juce::TableHeaderComponent::visible | juce::TableHeaderComponent::sortable;
    h.addColumn ("RATING", cStars, 70, 50, 100, flags);
    h.addColumn ("TITLE", cTitle, 300, 120, 900, flags);
    h.addColumn ("KIND", cKind, 70, 50, 120, flags);
    h.addColumn ("GENRE", cGenre, 120, 60, 200, flags);
    h.addColumn ("BPM", cBpm, 64, 50, 100, flags);
    h.addColumn ("KEY", cKey, 90, 60, 140, flags);
    h.addColumn ("LENGTH", cLength, 66, 50, 100, flags);
    h.addColumn ("LUFS", cLufs, 60, 50, 100, flags);
    h.addColumn ("ADDED", cAdded, 110, 60, 200, flags);
    h.setSortColumnId (cAdded, false);
    h.setColour (juce::TableHeaderComponent::backgroundColourId, juce::Colour (0xff181c22));
    h.setColour (juce::TableHeaderComponent::textColourId, kLib.brighter (0.4f));
    h.setStretchToFitActive (true);
    table.setRowHeight (30);
    table.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff0d1015).withAlpha (0.85f));
    table.setMultipleSelectionEnabled (false);
    addAndMakeVisible (table);

    status.setFont (PlaygroundLookAndFeel::font (12.5f, false));
    status.setColour (juce::Label::textColourId, colours::paperDim);
    addAndMakeVisible (status);

    // details
    darkEditor (title);
    title.onTextChange = [this] { detailsEdited(); };
    darkEditor (notes);
    notes.setMultiLine (true, true);
    notes.setReturnKeyStartsNewLine (true);
    notes.setTextToShowWhenEmpty ("Notes: what you did in Suno, which take, what to try next...", colours::paperDim);
    notes.onTextChange = [this] { detailsEdited(); };
    darkCombo (kind);
    kind.addItemList (library::Library::kinds(), 1);
    kind.onChange = [this] { detailsEdited(); };
    darkCombo (genre);
    genre.setEditableText (true);
    for (int g = 0; g < band::kNumGenres; ++g) genre.addItem (band::genreName ((band::Genre) g), g + 1);
    genre.onChange = [this] { detailsEdited(); };
    darkCombo (stars);
    for (int s = 0; s <= 5; ++s) stars.addItem (s == 0 ? juce::String ("NOT RATED") : starText (s), s + 1);
    stars.onChange = [this] { detailsEdited(); };
    darkCombo (madeFrom);
    madeFrom.setTooltip ("The Band Forge take this came from (the parts you took into Suno)");
    madeFrom.onChange = [this] { detailsEdited(); };

    playButton.onClick = [this] { togglePreview(); };
    sessionButton.onClick = [this] { addToSession(); };
    sessionButton.setTooltip ("Put it in the session as a track");
    showButton.onClick = [this] { if (auto* e = current()) lib.fileOf (*e).revealToUser(); };
    doubleButton.setTooltip ("The tempo read half as fast as it really is? Double it.");
    halveButton.setTooltip ("The tempo read twice as fast as it really is? Halve it.");
    doubleButton.onClick = [this] { if (auto* e = current(); e != nullptr && e->facts.bpm > 0.0) { e->facts.bpm *= 2.0; saveSoon(); refresh(); showDetails(); } };
    halveButton.onClick = [this] { if (auto* e = current(); e != nullptr && e->facts.bpm > 0.0) { e->facts.bpm *= 0.5; saveSoon(); refresh(); showDetails(); } };
    takeButton.onClick = [this] { if (auto* e = current(); e != nullptr && juce::File (e->madeFrom).isDirectory()) juce::File (e->madeFrom).revealToUser(); };
    removeButton.setTooltip ("Take it out of the library (the file is moved to Library/Removed, not deleted)");
    removeButton.onClick = [this] { removeSelected(); };

    position.setSliderStyle (juce::Slider::LinearBar);
    position.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    position.setRange (0.0, 1.0);
    position.setColour (juce::Slider::trackColourId, kLib.withAlpha (0.6f));
    position.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff0d1015));
    position.onDragEnd = [this] { if (previewId == currentId()) ctx.engine.seekPreview (position.getValue()); };

    for (auto* comp : std::initializer_list<juce::Component*> { &title, &notes, &kind, &genre, &stars, &madeFrom, &playButton, &sessionButton,
                                                                &showButton, &doubleButton, &halveButton, &takeButton, &removeButton, &position })
        addChildComponent (comp);

    refresh();
    // anything added but never measured (the app closed while measuring) gets measured now
    for (auto& e : lib.all()) if (! e.measured) measure (e.id);
    startTimerHz (10);
}

LibraryPanel::~LibraryPanel()
{
    alive->store (false);
    stopping->store (true);
    stopTimer();
    worker.removeAllJobs (true, 20000);
    if (dirty) { juce::String err; lib.save (err); }
    if (previewId.isNotEmpty()) ctx.engine.stopPreview();
}

//==============================================================================
int LibraryPanel::addFiles (const juce::Array<juce::File>& files, const juce::String& genreHint, const juce::File& from,
                            const juce::String& kindHint, const juce::String& titleHint)
{
    int added = 0;
    juce::String lastId, err;
    for (auto& f : files)
    {
        if (! isAudio (f)) continue;
        const auto id = lib.addFile (f, err);
        if (id.isEmpty()) { if (ctx.status) ctx.status ("LIBRARY: " + err); continue; }
        if (auto* e = lib.find (id))
        {
            if (genreHint.isNotEmpty()) e->genre = genreHint;
            if (from != juce::File()) e->madeFrom = from.getFullPathName();
            if (kindHint.isNotEmpty()) e->kind = kindHint;
            if (titleHint.isNotEmpty() && files.size() == 1) e->title = titleHint;
        }
        measure (id);
        lastId = id;
        ++added;
    }
    if (added > 0)
    {
        if (! lib.save (err) && ctx.status) ctx.status ("LIBRARY: " + err);
        if (kindFilter.isNotEmpty() && lib.find (lastId) != nullptr && lib.find (lastId)->kind != kindFilter) kindFilter.clear();
        refresh();
        select (lastId);
        if (ctx.status) ctx.status ("LIBRARY: added " + juce::String (added) + (added == 1 ? " file" : " files") + ", measuring tempo and key...");
    }
    return added;
}

void LibraryPanel::measure (const juce::String& id)
{
    const auto* e = lib.find (id);
    if (e == nullptr) return;
    const auto file = lib.fileOf (*e);
    ++pendingMeasures;
    juce::Component::SafePointer<LibraryPanel> safe (this);
    auto alivePtr = alive;
    auto stopPtr = stopping;
    worker.addJob ([safe, alivePtr, stopPtr, id, file]
    {
        if (! alivePtr->load()) return;
        juce::String err;
        library::AudioFacts facts;
        bool ok = false;
        if (auto data = MediaLoader::decode (file, err))
        {
            facts = library::measure (data->getBuffer(), data->getSourceRate(), stopPtr.get());
            ok = alivePtr->load();
        }
        juce::MessageManager::callAsync ([safe, id, facts, ok, err]
        {
            if (safe == nullptr) return;
            --safe->pendingMeasures;
            if (auto* entry = safe->lib.find (id))
            {
                if (ok) { entry->facts = facts; entry->measured = true; }
                else if (safe->ctx.status) safe->ctx.status ("LIBRARY: can't read " + entry->title + (err.isNotEmpty() ? " (" + err + ")" : juce::String()));
                safe->saveSoon();
                safe->refresh();
                if (safe->currentId() == id) safe->showDetails();
            }
        });
    });
}

void LibraryPanel::refresh()
{
    const auto keep = currentId();
    rows = lib.query (search.getText(), kindFilter, sortColumn, sortForwards);
    table.updateContent();
    const int idx = rows.indexOf (keep);
    if (idx >= 0) table.selectRow (idx, true, true);
    else if (rows.size() > 0 && table.getSelectedRow() < 0) table.selectRow (0);
    else if (rows.isEmpty()) table.deselectAllRows();
    table.repaint();
    kindsPanel.repaint();
    status.setText (lib.size() == 0 ? juce::String ("Nothing here yet. Drop your Suno downloads on this page, or press ADD FILES.")
                                    : juce::String (rows.size()) + " of " + juce::String (lib.size()) + " shown"
                                          + (pendingMeasures > 0 ? "  -  measuring " + juce::String (pendingMeasures) + "..." : juce::String()),
                    juce::dontSendNotification);
    showDetails();
}

void LibraryPanel::select (const juce::String& id)
{
    const int idx = rows.indexOf (id);
    if (idx >= 0) { table.selectRow (idx); table.scrollToEnsureRowIsOnscreen (idx); }
}

juce::String LibraryPanel::currentId() const
{
    const int r = table.getSelectedRow();
    return juce::isPositiveAndBelow (r, rows.size()) ? rows[r] : juce::String();
}

library::Entry* LibraryPanel::current() { return lib.find (currentId()); }

void LibraryPanel::showDetails()
{
    auto* e = current();
    for (auto* comp : std::initializer_list<juce::Component*> { &title, &notes, &kind, &genre, &stars, &madeFrom, &playButton, &sessionButton,
                                                                &showButton, &doubleButton, &halveButton, &takeButton, &removeButton, &position })
        comp->setVisible (e != nullptr);
    if (e == nullptr) { repaint(); return; }
    showingDetails = true;   // the setters below must not count as edits
    if (title.getText() != e->title) title.setText (e->title, false);
    if (notes.getText() != e->notes) notes.setText (e->notes, false);
    kind.setSelectedItemIndex (juce::jmax (0, library::Library::kinds().indexOf (e->kind)), juce::dontSendNotification);
    genre.setText (e->genre, juce::dontSendNotification);
    stars.setSelectedItemIndex (e->stars, juce::dontSendNotification);
    takeFolders = library::Library::bandForgeTakes();
    madeFrom.clear (juce::dontSendNotification);
    madeFrom.addItem ("NOT FROM A BAND FORGE TAKE", 1);
    int sel = 0;
    for (int i = 0; i < takeFolders.size(); ++i)
    {
        madeFrom.addItem ("FROM: " + takeFolders[i].getFileName(), i + 2);
        if (takeFolders[i].getFullPathName() == e->madeFrom) sel = i + 1;
    }
    if (sel == 0 && e->madeFrom.isNotEmpty())   // a take folder that has since moved
    {
        takeFolders.add (juce::File (e->madeFrom));
        madeFrom.addItem ("FROM: " + juce::File (e->madeFrom).getFileName() + " (moved?)", takeFolders.size() + 1);
        sel = takeFolders.size();
    }
    madeFrom.setSelectedItemIndex (sel, juce::dontSendNotification);
    takeButton.setEnabled (juce::File (e->madeFrom).isDirectory());
    doubleButton.setEnabled (e->facts.bpm > 0.0);
    halveButton.setEnabled (e->facts.bpm > 0.0);
    showingDetails = false;
    repaint();
}

void LibraryPanel::detailsEdited()
{
    if (showingDetails) return;
    auto* e = current();
    if (e == nullptr) return;
    e->title = title.getText().trim().isEmpty() ? e->title : title.getText();
    e->notes = notes.getText();
    e->kind = library::Library::kinds()[juce::jmax (0, kind.getSelectedItemIndex())];
    e->genre = genre.getText().trim();
    e->stars = juce::jlimit (0, 5, stars.getSelectedItemIndex());
    const int m = madeFrom.getSelectedItemIndex();
    e->madeFrom = m <= 0 || m - 1 >= takeFolders.size() ? juce::String() : takeFolders[m - 1].getFullPathName();
    takeButton.setEnabled (juce::File (e->madeFrom).isDirectory());
    saveSoon();
    table.repaintRow (table.getSelectedRow());
    kindsPanel.repaint();
}

void LibraryPanel::togglePreview()
{
    const auto id = currentId();
    auto* e = current();
    if (e == nullptr) return;
    if (previewId == id && ctx.engine.getPreviewMedia() != nullptr)
    {
        if (ctx.engine.isPreviewPlaying()) ctx.engine.stopPreview();
        else ctx.engine.resumePreview();
        return;
    }
    // decode on the worker, then hand it to the engine
    decodingPreview = id;
    playButton.setButtonText ("LOADING...");
    const auto file = lib.fileOf (*e);
    juce::Component::SafePointer<LibraryPanel> safe (this);
    auto alivePtr = alive;
    worker.addJob ([safe, alivePtr, id, file]
    {
        if (! alivePtr->load()) return;
        juce::String err;
        auto data = MediaLoader::decode (file, err);
        juce::MessageManager::callAsync ([safe, id, data, err]
        {
            if (safe == nullptr || safe->decodingPreview != id) return;
            safe->decodingPreview.clear();
            if (data == nullptr) { if (safe->ctx.status) safe->ctx.status ("LIBRARY: can't play it (" + err + ")"); return; }
            safe->ctx.engine.startPreview (data);
            safe->previewId = id;
        });
    });
}

void LibraryPanel::addToSession()
{
    if (auto* e = current())
    {
        const auto f = lib.fileOf (*e);
        if (! f.existsAsFile()) { if (ctx.status) ctx.status ("LIBRARY: the file is gone (" + f.getFullPathName() + ")"); return; }
        if (ctx.importFiles) ctx.importFiles ({ f });
    }
}

void LibraryPanel::removeSelected()
{
    const auto id = currentId();
    if (id.isEmpty()) return;
    if (previewId == id) { ctx.engine.stopPreview(); previewId.clear(); }
    juce::String err;
    const auto name = lib.find (id)->title;
    if (! lib.remove (id, err)) { if (ctx.status) ctx.status ("LIBRARY: " + err); return; }
    lib.save (err);
    refresh();
    if (ctx.status) ctx.status ("LIBRARY: took out " + name + " (the file is in Library/Removed)");
}

//==============================================================================
void LibraryPanel::timerCallback()
{
    if (dirty)
    {
        juce::String err;
        if (! lib.save (err) && ctx.status) ctx.status ("LIBRARY: " + err);
        dirty = false;
    }
    const bool mine = previewId.isNotEmpty() && previewId == currentId();
    const bool playing = mine && ctx.engine.isPreviewPlaying();
    if (decodingPreview.isEmpty()) playButton.setButtonText (playing ? "STOP" : "PLAY");
    if (mine)
    {
        if (auto m = ctx.engine.getPreviewMedia())
        {
            const double len = m->getNumFrames() / m->getSourceRate();
            if (std::abs (position.getMaximum() - len) > 1.0e-3) position.setRange (0.0, juce::jmax (0.1, len));
            if (! position.isMouseButtonDown()) position.setValue (ctx.engine.getPreviewPosition(), juce::dontSendNotification);
        }
    }
    else if (auto* e = current())
    {
        position.setRange (0.0, juce::jmax (0.1, e->facts.seconds));
        position.setValue (0.0, juce::dontSendNotification);
    }
    if (pendingMeasures == 0 && status.getText().contains ("measuring")) refresh();
}

//==============================================================================
void LibraryPanel::paintRowBackground (juce::Graphics& g, int row, int w, int h, bool selected)
{
    g.setColour (selected ? kLib.withAlpha (0.28f) : (row % 2 ? juce::Colour (0xff12161c) : juce::Colour (0xff0f1217)));
    g.fillRect (0, 0, w, h);
    if (rows[row] == previewId && ctx.engine.isPreviewPlaying())
    {
        g.setColour (kLib);
        g.fillRect (0, 0, 3, h);
    }
}

void LibraryPanel::paintCell (juce::Graphics& g, int row, int column, int w, int h, bool)
{
    const auto* e = lib.find (rows[row]);
    if (e == nullptr) return;
    juce::String t;
    auto just = juce::Justification::centredLeft;
    switch (column)
    {
        case cStars:  t = e->stars > 0 ? juce::String::repeatedString ("*", e->stars) : juce::String(); break;
        case cTitle:  t = e->title; break;
        case cKind:   t = e->kind; break;
        case cGenre:  t = e->genre; break;
        case cBpm:    t = ! e->measured ? juce::String ("...") : e->facts.bpm > 0.0 ? juce::String (e->facts.bpm, e->facts.bpm == std::floor (e->facts.bpm) ? 0 : 1) : juce::String ("-"); just = juce::Justification::centredRight; break;
        case cKey:    t = ! e->measured ? juce::String ("...") : e->facts.keyRoot >= 0 ? e->facts.keyName() : juce::String ("-"); break;
        case cLength: t = e->facts.seconds > 0.0 ? timeText (e->facts.seconds) : juce::String ("..."); just = juce::Justification::centredRight; break;
        case cLufs:   t = e->measured && e->facts.lufs > -70.0f ? juce::String (e->facts.lufs, 1) : juce::String ("-"); just = juce::Justification::centredRight; break;
        case cAdded:  t = juce::Time (e->added).formatted ("%Y-%m-%d"); break;
        default: break;
    }
    g.setColour (column == cStars ? kLib.brighter (0.5f) : colours::paper);
    g.setFont (PlaygroundLookAndFeel::font (13.0f, column == cTitle));
    g.drawText (t, 6, 0, w - 12, h, just, true);
}

void LibraryPanel::sortOrderChanged (int columnId, bool forwards)
{
    sortColumn = juce::jlimit (0, 8, columnId - 1);
    sortForwards = forwards;
    refresh();
}

void LibraryPanel::selectedRowsChanged (int) { showDetails(); }

void LibraryPanel::cellDoubleClicked (int, int, const juce::MouseEvent&) { togglePreview(); }

bool LibraryPanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (isAudio (juce::File (f))) return true;
    return false;
}

void LibraryPanel::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    juce::Array<juce::File> list;
    for (auto& f : files) list.add (juce::File (f));
    addFiles (list);
    repaint();
}

//==============================================================================
void LibraryPanel::resized()
{
    auto r = getLocalBounds().reduced (14, 6);
    headerArea = r.removeFromTop (30);
    r.removeFromTop (6);
    auto right = r.removeFromRight (juce::jmin (380, r.getWidth() / 3));
    r.removeFromRight (14);
    detailsArea = right;

    auto row = r.removeFromTop (34);
    addButton.setBounds (row.removeFromRight (150).reduced (2));
    search.setBounds (row.reduced (2));
    r.removeFromTop (6);
    status.setBounds (r.removeFromBottom (22));
    table.setBounds (r);

    auto d = right.reduced (8, 4);
    factsArea = d.removeFromTop (44);
    auto line = [&d] (int h) { auto x = d.removeFromTop (h); d.removeFromTop (6); return x; };
    title.setBounds (line (30));
    auto two = line (30);
    kind.setBounds (two.removeFromLeft (two.getWidth() / 2).reduced (0, 0).withTrimmedRight (3));
    genre.setBounds (two.withTrimmedLeft (3));
    two = line (30);
    stars.setBounds (two.removeFromLeft (two.getWidth() / 2).withTrimmedRight (3));
    auto bpmButtons = two.withTrimmedLeft (3);
    doubleButton.setBounds (bpmButtons.removeFromLeft (bpmButtons.getWidth() / 2).withTrimmedRight (2));
    halveButton.setBounds (bpmButtons.withTrimmedLeft (2));
    two = line (30);
    takeButton.setBounds (two.removeFromRight (100).withTrimmedLeft (4));
    madeFrom.setBounds (two);
    auto buttons = d.removeFromBottom (36);
    removeButton.setBounds (buttons.removeFromRight (90).withTrimmedLeft (4));
    showButton.setBounds (buttons.removeFromRight (100).withTrimmedLeft (4));
    sessionButton.setBounds (buttons);
    d.removeFromBottom (6);
    auto play = d.removeFromBottom (44);
    playButton.setBounds (play.removeFromLeft (100));
    position.setBounds (play.withTrimmedLeft (8).withSizeKeepingCentre (play.getWidth() - 8, 16));
    d.removeFromBottom (6);
    notes.setBounds (d);
}

void LibraryPanel::paint (juce::Graphics& g)
{
    PlaygroundLookAndFeel::drawPanelText (g, "LIBRARY  -  your own beats and songs, only on this computer", headerArea.toFloat(),
                                          juce::Justification::centredLeft, 15.0f, kLib.brighter (0.4f));
    g.setColour (juce::Colour (0xff12151a).withAlpha (0.55f));
    g.fillRoundedRectangle (detailsArea.toFloat().expanded (8.0f, 6.0f), 6.0f);
    if (auto* e = const_cast<LibraryPanel*> (this)->current())
    {
        auto fa = factsArea.toFloat();
        PlaygroundLookAndFeel::drawPanelText (g, e->measured ? e->summary() : e->kind + "  -  measuring...", fa.removeFromTop (22.0f),
                                              juce::Justification::centredLeft, 14.0f, kLib.brighter (0.5f));
        juce::String more;
        if (e->measured && e->facts.lufs > -70.0f) more << juce::String (e->facts.lufs, 1) << " LUFS   peak " << juce::String (e->facts.peakDb, 1) << " dB";
        if (e->measured && e->facts.bpm > 0.0 && e->facts.tempoConfidence < 0.3f) more << "   (tempo is a rough guess)";
        PlaygroundLookAndFeel::drawPanelText (g, more, fa, juce::Justification::centredLeft, 12.0f, colours::paperDim);
    }
    else
        PlaygroundLookAndFeel::drawPanelText (g, "Pick something on the list", factsArea.toFloat(), juce::Justification::centredLeft, 13.0f, colours::paperDim);
    if (dragOver)
    {
        g.setColour (kLib.withAlpha (0.18f));
        g.fillRect (table.getBounds());
        g.setColour (kLib);
        g.drawRect (table.getBounds(), 2);
        PlaygroundLookAndFeel::drawPanelText (g, "DROP TO ADD TO THE LIBRARY", table.getBounds().toFloat(), juce::Justification::centred, 18.0f, kLib.brighter (0.5f));
    }
}
} // namespace pg::ui
