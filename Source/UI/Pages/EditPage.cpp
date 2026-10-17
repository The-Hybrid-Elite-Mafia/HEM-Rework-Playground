#include "EditPage.h"
#include "Engine/MediaLoader.h"

namespace pg::ui
{
using E = PlaygroundEngine;

//==============================================================================
TrackRow::TrackRow (AppContext& c, int t) : ctx (c), track (t)
{
    auto& apvts = ctx.engine.getAPVTS();
    auto setup = [this] (juce::TextButton& b, juce::Colour on, const juce::String& tip)
    {
        b.setClickingTogglesState (true);
        b.getProperties().set (props::style, "ms");
        b.getProperties().set (props::accent, (juce::int64) on.getARGB());
        b.setTooltip (tip);
        addAndMakeVisible (b);
    };
    setup (mute, juce::Colour (0xffffb02e), "Mute");
    setup (solo, colours::hemGreen, "Solo");
    muteAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, E::Ids::track (track, "mute"), mute);
    soloAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, E::Ids::track (track, "solo"), solo);
    fx.getProperties().set (props::style, "dark");
    fx.setTooltip ("Plugins on this track: click to add one");
    fx.onClick = [this] { ctx.selectedStrip = track; ctx.showInsertMenu (track, ctx.firstFreeSlot (track), &fx); };
    addAndMakeVisible (fx);
}

TrackRow::~TrackRow() { muteAtt.reset(); soloAtt.reset(); }

void TrackRow::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (4.0f, 2.0f);
    const bool selected = ctx.selectedStrip == track;
    const auto col = ctx.engine.getTrackColour (track);
    g.setColour (juce::Colour (0xff12151a).withAlpha (selected ? 0.95f : 0.78f));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (col);
    g.fillRoundedRectangle (r.withWidth (6.0f), 2.0f);
    if (selected) { g.setColour (col.withAlpha (0.85f)); g.drawRoundedRectangle (r, 5.0f, 1.5f); }

    auto text = r.withTrimmedLeft (14.0f).withTrimmedRight (4.0f);
    g.setFont (PlaygroundLookAndFeel::font (14.0f));
    g.setColour (colours::paper);
    g.drawText (juce::String (track + 1) + "  " + ctx.engine.getTrackName (track), text.removeFromTop (r.getHeight() * 0.5f),
                juce::Justification::bottomLeft, true);
    int n = 0;
    for (int s = 0; s < E::kSlots; ++s) n += ctx.engine.getPlugin (track, s) != nullptr ? 1 : 0;
    fx.setButtonText (n > 0 ? "FX " + juce::String (n) : "+ FX");
    if (ctx.engine.getTrackMedia (track) == nullptr)
    {
        g.setFont (PlaygroundLookAndFeel::font (10.5f, false));
        g.setColour (colours::meterRed.brighter (0.4f));
        g.drawText ("audio file missing", text, juce::Justification::topRight, true);
    }
}

void TrackRow::resized()
{
    auto r = getLocalBounds().reduced (6, 4).withTrimmedLeft (12);
    auto bottom = r.removeFromBottom (juce::jmin (22, r.getHeight() / 2));
    mute.setBounds (bottom.removeFromLeft (26)); bottom.removeFromLeft (4);
    solo.setBounds (bottom.removeFromLeft (26)); bottom.removeFromLeft (4);
    fx.setBounds (bottom.removeFromLeft (52));
}

void TrackRow::mouseUp (const juce::MouseEvent& e)
{
    ctx.selectedStrip = track;
    if (e.mods.isPopupMenu()) showMenu();
    if (auto* p = getParentComponent()) p->repaint();
}

void TrackRow::showMenu()
{
    juce::PopupMenu m, colourMenu;
    m.addSectionHeader (ctx.engine.getTrackName (track));
    m.addItem (1, "Rename...");
    const std::pair<const char*, juce::uint32> palette[] { { "Gold", 0xffe0a526 }, { "Cyan", 0xff2ec8e6 }, { "Red", 0xffe0533c }, { "Green", 0xff5bc94a },
                                                           { "Purple", 0xffa45be0 }, { "Teal", 0xff1fb59a }, { "Pink", 0xffe05ba8 }, { "Blue", 0xff3a7be0 } };
    for (int i = 0; i < 8; ++i) colourMenu.addColouredItem (100 + i, palette[i].first, juce::Colour (palette[i].second));
    m.addSubMenu ("Colour", colourMenu);
    m.addItem (2, "Add plugin...");
    m.addSeparator();
    m.addItem (3, "Remove track");
    juce::Component::SafePointer<TrackRow> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, palette] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& c = safe->ctx;
        const int t = safe->track;
        if (r >= 100) { c.engine.setTrackColour (t, juce::Colour (palette[r - 100].second)); if (c.structureChanged) c.structureChanged(); return; }
        if (r == 1)
        {
            auto* aw = new juce::AlertWindow ("Rename track", {}, juce::MessageBoxIconType::NoIcon);
            aw->addTextEditor ("name", c.engine.getTrackName (t));
            aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            aw->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            aw->enterModalState (true, juce::ModalCallbackFunction::create ([&c, t, aw] (int res)
            {
                if (res == 1) { c.engine.setTrackName (t, aw->getTextEditorContents ("name").trim().substring (0, 24)); if (c.structureChanged) c.structureChanged(); }
            }), true);
        }
        else if (r == 2) c.showInsertMenu (t, c.firstFreeSlot (t), safe.getComponent());
        else if (r == 3) c.removeTrack (t);
    });
}

//==============================================================================
TrackList::TrackList (AppContext& c, EditView& v) : ctx (c), view (v)
{
    importButton.setTooltip ("Load WAV / MP3 / FLAC stems as tracks (you can also drag files onto the window)");
    importButton.onClick = [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Import audio (stems)", AppContext::songsFolder(), MediaLoader::wildcard());
        juce::Component::SafePointer<TrackList> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                              [safe, chooser] (const juce::FileChooser& fc)
                              {
                                  if (safe != nullptr && ! fc.getResults().isEmpty() && safe->ctx.importFiles) safe->ctx.importFiles (fc.getResults());
                              });
    };
    addAndMakeVisible (importButton);
    rebuild();
}

void TrackList::rebuild()
{
    rows.clear();
    for (int t = 0; t < E::kMaxTracks; ++t)
        if (ctx.engine.isTrackUsed (t))
        {
            auto row = std::make_unique<TrackRow> (ctx, t);
            addAndMakeVisible (*row);
            rows.push_back (std::move (row));
        }
    layoutRows();
    repaint();
}

void TrackList::layoutRows()
{
    importButton.setBounds (getWidth() - 104, 6, 96, kHeaderHeight - 12);
    int y = kHeaderHeight - view.scrollY;
    for (auto& r : rows)
    {
        r->setBounds (0, y, getWidth(), view.rowHeight);
        r->setVisible (y + view.rowHeight > kHeaderHeight && y < getHeight());
        y += view.rowHeight;
    }
}

void TrackList::paint (juce::Graphics& g)
{
    PlaygroundLookAndFeel::drawPanelText (g, "TRACKS  " + juce::String (rows.size()) + "/" + juce::String (E::kMaxTracks),
                                          juce::Rectangle<float> (10.0f, 0.0f, (float) getWidth() - 120.0f, (float) kHeaderHeight), juce::Justification::centredLeft, 15.0f);
}

void TrackList::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const int content = (int) rows.size() * view.rowHeight;
    const int visible = getHeight() - kHeaderHeight;
    view.scrollY = juce::jlimit (0, juce::jmax (0, content - visible), view.scrollY - juce::roundToInt (w.deltaY * 240.0f));
    view.notify();
}

//==============================================================================
Timeline::Timeline (AppContext& c, EditView& v) : ctx (c), view (v)
{
    hScroll.setAutoHide (false);
    hScroll.addListener (&hListener);
    addAndMakeVisible (hScroll);
    startTimerHz (30);
}

Timeline::~Timeline() { hScroll.removeListener (&hListener); }

juce::Array<int> Timeline::usedTracks() const
{
    juce::Array<int> t;
    for (int i = 0; i < E::kMaxTracks; ++i) if (ctx.engine.isTrackUsed (i)) t.add (i);
    return t;
}

void Timeline::resized()
{
    hScroll.setBounds (getLocalBounds().removeFromBottom (12));
    if (view.autoFit) fitToWidth();
    updateScrollBar();
}

void Timeline::fitToWidth()
{
    const double len = juce::jmax (10.0, ctx.engine.getSongLengthSeconds() * 1.03);
    view.pxPerSecond = juce::jmax (1.0, (double) juce::jmax (100, getWidth()) / len);
    view.startSeconds = 0.0;
    view.autoFit = true;
    updateScrollBar();
    repaint();
}

void Timeline::zoom (double factor, double around)
{
    const float ax = secondsToX (around);
    view.pxPerSecond = juce::jlimit (0.5, 4000.0, view.pxPerSecond * factor);
    view.startSeconds = juce::jmax (0.0, around - (double) ax / view.pxPerSecond);
    view.autoFit = false;
    updateScrollBar();
    view.notify();
}

void Timeline::updateScrollBar()
{
    const double total = juce::jmax (10.0, ctx.engine.getSongLengthSeconds() * 1.05);
    const double visible = (double) getWidth() / view.pxPerSecond;
    hScroll.setRangeLimits (0.0, juce::jmax (total, view.startSeconds + visible));
    hScroll.setCurrentRange (view.startSeconds, visible, juce::dontSendNotification);
}

void Timeline::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().withTrimmedBottom (12);
    const float w = (float) bounds.getWidth();
    const double bpm = ctx.engine.getAPVTS().getRawParameterValue (E::Ids::bpm)->load();
    const double barSec = 4.0 * 60.0 / juce::jmax (20.0, bpm);

    // ruler
    auto ruler = bounds.withHeight (kRulerHeight).toFloat();
    g.setColour (juce::Colour (0xff101317).withAlpha (0.9f));
    g.fillRoundedRectangle (ruler, 4.0f);
    if (ctx.engine.isLooping() || ctx.engine.getLoopEnd() > ctx.engine.getLoopStart())
    {
        const float lx0 = secondsToX (ctx.engine.getLoopStart()), lx1 = secondsToX (ctx.engine.getLoopEnd());
        g.setColour ((ctx.engine.isLooping() ? colours::glow : colours::paperDim).withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (lx0, ruler.getY(), lx1 - lx0, ruler.getHeight()));
        g.setColour (ctx.engine.isLooping() ? colours::glow : colours::paperDim);
        g.fillRect (lx0, ruler.getY(), 2.0f, ruler.getHeight());
        g.fillRect (lx1 - 2.0f, ruler.getY(), 2.0f, ruler.getHeight());
    }
    // bar lines: pick a bar step that leaves >= 60 px between labels
    int barStep = 1;
    while (barStep * barSec * view.pxPerSecond < 60.0 && barStep < 1024) barStep *= 2;
    const int firstBar = juce::jmax (0, (int) std::floor (view.startSeconds / barSec / barStep) * barStep);
    g.setFont (PlaygroundLookAndFeel::font (11.5f));
    for (int bar = firstBar;; bar += barStep)
    {
        const float x = secondsToX (bar * barSec);
        if (x > w) break;
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.fillRect (x, ruler.getY() + ruler.getHeight() * 0.5f, 1.0f, ruler.getHeight() * 0.5f);
        g.setColour (colours::paper);
        g.drawText (juce::String (bar + 1), juce::Rectangle<float> (x + 3.0f, ruler.getY() + 2.0f, 60.0f, 16.0f), juce::Justification::centredLeft, false);
        const int secs = (int) (bar * barSec);
        g.setColour (colours::paperDim);
        g.setFont (PlaygroundLookAndFeel::font (9.5f, false));
        g.drawText (juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2),
                    juce::Rectangle<float> (x + 3.0f, ruler.getY() + 18.0f, 60.0f, 14.0f), juce::Justification::centredLeft, false);
        g.setFont (PlaygroundLookAndFeel::font (11.5f));
    }

    // lanes
    auto lanes = bounds.withTrimmedTop (kRulerHeight + 2);
    juce::Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (lanes);
    const auto tracks = usedTracks();
    int y = lanes.getY() - view.scrollY;
    for (int i = 0; i < tracks.size(); ++i, y += view.rowHeight)
    {
        if (y + view.rowHeight < lanes.getY() || y > lanes.getBottom()) continue;
        const int t = tracks[i];
        const auto lane = juce::Rectangle<float> (0.0f, (float) y + 2.0f, w, (float) view.rowHeight - 4.0f);
        const auto col = ctx.engine.getTrackColour (t);
        g.setColour (juce::Colour (0xff12151a).withAlpha (ctx.selectedStrip == t ? 0.85f : 0.62f));
        g.fillRoundedRectangle (lane, 4.0f);
        // bar grid
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        for (int bar = firstBar;; bar += barStep)
        {
            const float x = secondsToX (bar * barSec);
            if (x > w) break;
            g.fillRect (x, lane.getY(), 1.0f, lane.getHeight());
        }
        auto media = ctx.engine.getTrackMedia (t);
        if (media != nullptr)
        {
            const double len = (double) media->getNumFrames() / media->getSourceRate();
            const float x0 = juce::jmax (0.0f, secondsToX (0.0)), x1 = juce::jmin (w, secondsToX (len));
            g.setColour (col.withAlpha (0.18f));
            g.fillRoundedRectangle (juce::Rectangle<float> (x0, lane.getY(), x1 - x0, lane.getHeight()), 4.0f);
            if (auto ov = ctx.waves.get (media))
            {
                const float mid = lane.getCentreY(), half = lane.getHeight() * 0.46f;
                const double bucketsPerSecond = ov->sourceRate / WaveOverview::kSamplesPerBucket;
                juce::Path wave;
                juce::RectangleList<float> columns;
                for (int px = (int) x0; px < (int) x1; ++px)
                {
                    const auto b0 = (size_t) juce::jmax (0.0, xToSeconds ((float) px) * bucketsPerSecond);
                    const auto b1 = (size_t) juce::jmax ((double) b0 + 1.0, xToSeconds ((float) px + 1.0f) * bucketsPerSecond);
                    float lo = 0.0f, hi = 0.0f;
                    for (size_t b = b0; b < b1 && b < ov->mins.size(); ++b) { lo = juce::jmin (lo, ov->mins[b]); hi = juce::jmax (hi, ov->maxs[b]); }
                    const float top = mid - juce::jmin (1.0f, hi) * half, bot = mid - juce::jmax (-1.0f, lo) * half;
                    columns.addWithoutMerging ({ (float) px, top, 1.0f, juce::jmax (1.0f, bot - top) });
                }
                g.setColour (col.brighter (0.3f));
                g.fillRectList (columns);
            }
            else
            {
                g.setColour (colours::paperDim);
                g.setFont (PlaygroundLookAndFeel::font (11.0f, false));
                g.drawText ("drawing waveform...", lane.reduced (10.0f, 0.0f), juce::Justification::centredLeft, false);
            }
        }
        g.setColour (colours::paper.withAlpha (0.9f));
        g.setFont (PlaygroundLookAndFeel::font (11.5f));
        g.drawText (ctx.engine.getTrackName (t), lane.reduced (8.0f, 4.0f), juce::Justification::topLeft, true);
    }

    if (tracks.isEmpty())
    {
        PlaygroundLookAndFeel::drawPanelText (g, "DRAG YOUR STEMS HERE", lanes.toFloat().withTrimmedBottom ((float) lanes.getHeight() * 0.5f),
                                              juce::Justification::centredBottom, 30.0f);
        PlaygroundLookAndFeel::drawPanelText (g, "WAV / MP3 / FLAC - or File > Import Audio, or make a song in SONG FORGE",
                                              lanes.toFloat().withTrimmedTop ((float) lanes.getHeight() * 0.5f + 8.0f),
                                              juce::Justification::centredTop, 15.0f, colours::paperDim);
    }
    if (dropHover)
    {
        g.setColour (colours::glow.withAlpha (0.25f));
        g.fillRect (getLocalBounds());
        g.setColour (colours::glow);
        g.drawRect (getLocalBounds(), 3);
    }
}

void Timeline::paintOverChildren (juce::Graphics& g)
{
    const float x = secondsToX (ctx.engine.getPositionSeconds());
    if (x < -2.0f || x > (float) getWidth() + 2.0f) return;
    const float h = (float) getHeight() - 12.0f;
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.fillRect (x - 1.5f, 0.0f, 4.0f, h);
    g.setColour (ctx.engine.isPlaying() ? colours::hemGreen : juce::Colours::white);
    g.fillRect (x, 0.0f, 1.5f, h);
    juce::Path head;
    head.addTriangle (x - 6.0f, 0.0f, x + 7.5f, 0.0f, x + 0.75f, 9.0f);
    g.fillPath (head);
}

void Timeline::fileDragExit (const juce::StringArray&)
{
    dropHover = false;
    repaint();
}

void Timeline::timerCallback()
{
    if (! isShowing()) return;
    const float x = secondsToX (ctx.engine.getPositionSeconds());
    // follow the play-head: page forward when it leaves the view
    if (ctx.engine.isPlaying() && ! view.autoFit && (x > (float) getWidth() - 20.0f || x < 0.0f))
    {
        view.startSeconds = juce::jmax (0.0, ctx.engine.getPositionSeconds() - 0.05 * getWidth() / view.pxPerSecond);
        updateScrollBar();
        repaint();
        view.notify();
        return;
    }
    if (std::abs (x - lastPlayheadX) >= 0.5f)
    {
        repaint (juce::Rectangle<int> ((int) lastPlayheadX - 3, 0, 7, getHeight()));
        lastPlayheadX = x;
        repaint (juce::Rectangle<int> ((int) x - 3, 0, 7, getHeight()));
    }
}

void Timeline::mouseDown (const juce::MouseEvent& e)
{
    rulerClick = e.y < kRulerHeight;
    dragStartSeconds = juce::jmax (0.0, xToSeconds ((float) e.x));
    draggingLoop = false;
    if (! rulerClick)
    {
        // click in a lane: select that track, move the play-head there
        const auto tracks = usedTracks();
        const int idx = (e.y - kRulerHeight - 2 + view.scrollY) / view.rowHeight;
        if (juce::isPositiveAndBelow (idx, tracks.size())) ctx.selectedStrip = tracks[idx];
        ctx.engine.setPositionSeconds (dragStartSeconds);
        if (ctx.structureChanged) ctx.structureChanged();
        repaint();
    }
}

void Timeline::mouseDrag (const juce::MouseEvent& e)
{
    if (! rulerClick || e.getDistanceFromDragStart() < 5) return;
    // drag on the ruler = set the loop range
    draggingLoop = true;
    const double now = juce::jmax (0.0, xToSeconds ((float) e.x));
    ctx.engine.setLoop (true, juce::jmin (dragStartSeconds, now), juce::jmax (dragStartSeconds, now));
    repaint();
}

void Timeline::mouseUp (const juce::MouseEvent& e)
{
    if (rulerClick && ! draggingLoop && e.getDistanceFromDragStart() < 5)
        ctx.engine.setPositionSeconds (dragStartSeconds);
    draggingLoop = rulerClick = false;
    repaint();
}

void Timeline::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        zoom (w.deltaY > 0 ? 1.25 : 0.8, xToSeconds ((float) e.x));
        repaint();
        return;
    }
    if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
        view.startSeconds = juce::jmax (0.0, view.startSeconds - d * 200.0 / view.pxPerSecond);
        view.autoFit = false;
        updateScrollBar();
        view.notify();
        return;
    }
    const int content = usedTracks().size() * view.rowHeight;
    const int visible = getHeight() - kRulerHeight - 14;
    view.scrollY = juce::jlimit (0, juce::jmax (0, content - visible), view.scrollY - juce::roundToInt (w.deltaY * 240.0f));
    view.notify();
}

bool Timeline::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (MediaLoader::isAudioFile (juce::File (f))) { dropHover = true; repaint(); return true; }
    return false;
}

void Timeline::filesDropped (const juce::StringArray& files, int, int)
{
    dropHover = false;
    repaint();
    juce::Array<juce::File> list;
    for (auto& f : files) if (MediaLoader::isAudioFile (juce::File (f))) list.add (juce::File (f));
    if (! list.isEmpty() && ctx.importFiles) ctx.importFiles (list);
}
} // namespace pg::ui
