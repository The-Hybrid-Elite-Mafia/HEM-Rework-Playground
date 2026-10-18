#include "MixPage.h"
#include "UI/PluginWindows.h"

namespace pg::ui
{
using E = PlaygroundEngine;

//==============================================================================
void InsertSlot::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f, 1.0f);
    auto* p = ctx.engine.getPlugin (stripIndex, slotIndex);
    const bool over = isMouseOver();
    if (p == nullptr)
    {
        g.setColour (juce::Colour (0xff15181d).withAlpha (over ? 0.95f : 0.75f));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (over ? 0.5f : 0.18f));
        g.setFont (PlaygroundLookAndFeel::font (r.getHeight() * 0.62f));
        g.drawText ("+", r, juce::Justification::centred, false);
        return;
    }
    const auto acc = juce::Colour (p->getInfo().accent);
    const bool bypassed = p->getAPVTS().getRawParameterValue ("bypass")->load() > 0.5f;
    juce::ColourGradient grad (acc.brighter (0.15f), r.getX(), r.getY(), acc.darker (0.55f), r.getX(), r.getBottom(), false);
    g.setGradientFill (grad);
    g.setOpacity (bypassed ? 0.35f : 1.0f);
    g.fillRoundedRectangle (r, 3.0f);
    g.setOpacity (1.0f);
    if (ctx.windows.isOpen (p))
    {
        g.setColour (juce::Colours::white);
        g.drawRoundedRectangle (r, 3.0f, 1.5f);
    }
    g.setColour (bypassed ? colours::paperDim : juce::Colours::white);
    g.setFont (PlaygroundLookAndFeel::font (juce::jmin (13.0f, r.getHeight() * 0.62f)));
    g.drawText (r.getWidth() > 160.0f ? juce::String (p->getInfo().name).toUpperCase() : juce::String (p->getInfo().shortName),
                r.reduced (4.0f, 0.0f), juce::Justification::centred, false);
    if (bypassed)
    {
        g.setColour (colours::meterRed.withAlpha (0.8f));
        g.drawLine (r.getX() + 4.0f, r.getCentreY(), r.getRight() - 4.0f, r.getCentreY(), 1.2f);
    }
}

void InsertSlot::mouseUp (const juce::MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition())) return;
    ctx.selectedStrip = stripIndex;
    auto* p = ctx.engine.getPlugin (stripIndex, slotIndex);
    if (p == nullptr) { ctx.showInsertMenu (stripIndex, slotIndex, this); return; }
    if (e.mods.isPopupMenu()) { showSlotMenu(); return; }
    ctx.openPlugin (stripIndex, slotIndex);
    repaint();
}

void InsertSlot::showSlotMenu()
{
    auto* p = ctx.engine.getPlugin (stripIndex, slotIndex);
    if (p == nullptr) return;
    const bool bypassed = p->getAPVTS().getRawParameterValue ("bypass")->load() > 0.5f;
    juce::PopupMenu m;
    m.addSectionHeader (juce::String (p->getInfo().name) + "  (slot " + juce::String (slotIndex + 1) + ")");
    m.addItem (1, "Open window");
    m.addItem (2, bypassed ? "Turn on (un-bypass)" : "Bypass");
    m.addItem (3, "Replace with...");
    m.addItem (4, "Move up", slotIndex > 0);
    m.addItem (5, "Move down", slotIndex < E::kSlots - 1);
    m.addSeparator();
    m.addItem (6, "Remove");
    juce::Component::SafePointer<InsertSlot> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, bypassed] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& c = safe->ctx;
        const int s = safe->stripIndex, sl = safe->slotIndex;
        switch (r)
        {
            case 1: c.openPlugin (s, sl); break;
            case 2: if (auto* pl = c.engine.getPlugin (s, sl)) if (auto* b = pl->getAPVTS().getParameter ("bypass")) b->setValueNotifyingHost (bypassed ? 0.0f : 1.0f); break;
            case 3: c.showInsertMenu (s, sl, safe.getComponent()); break;
            case 4: c.movePlugin (s, sl, sl - 1); break;
            case 5: c.movePlugin (s, sl, sl + 1); break;
            case 6: c.removePlugin (s, sl); break;
            default: break;
        }
    });
}

//==============================================================================
ChannelStrip::ChannelStrip (AppContext& c, int s) : ctx (c), strip (s)
{
    auto& apvts = ctx.engine.getAPVTS();
    const bool track = E::isTrack (strip), bus = E::isBus (strip);
    const juce::Colour acc = track ? ctx.engine.getTrackColour (strip) : (bus ? colours::glow : juce::Colour (0xffcfa63a));

    name.setText (ctx.engine.getStripName (strip), juce::dontSendNotification);
    name.setJustificationType (juce::Justification::centred);
    name.setFont (PlaygroundLookAndFeel::font (13.0f));
    name.setColour (juce::Label::textColourId, colours::paper);
    name.setEditable (false, strip != E::kMaster, false);
    name.setTooltip (strip == E::kMaster ? "Master" : "Double-click to rename");
    name.onTextChange = [this]
    {
        const auto t = name.getText().trim().substring (0, 24);
        if (E::isTrack (strip)) ctx.engine.setTrackName (strip, t);
        else if (E::isBus (strip)) ctx.engine.setBusName (strip - E::kFirstBus, t);
        if (ctx.structureChanged) ctx.structureChanged();
    };
    name.setInterceptsMouseClicks (true, false);
    addAndMakeVisible (name);

    for (int i = 0; i < E::kSlots; ++i)
    {
        auto sl = std::make_unique<InsertSlot> (ctx, strip, i);
        sl->setTooltip ("Insert " + juce::String (i + 1) + ": click to add / open, right-click for more");
        addAndMakeVisible (*sl);
        slots.push_back (std::move (sl));
    }

    auto knob = [&] (juce::Slider& k, const juce::String& tip)
    {
        k.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
        k.getProperties().set (props::accent, (juce::int64) acc.getARGB());
        k.setTooltip (tip);
        k.setPopupDisplayEnabled (true, true, this);
        addAndMakeVisible (k);
    };
    auto combo = [&] (juce::ComboBox& cb, const juce::StringArray& items, const juce::String& tip)
    {
        cb.getProperties().set (props::style, "dark");
        cb.getProperties().set (props::accent, (juce::int64) acc.getARGB());
        cb.setColour (juce::ComboBox::textColourId, colours::paper);
        cb.setColour (juce::ComboBox::arrowColourId, colours::paperDim);
        cb.addItemList (items, 1);
        cb.setTooltip (tip);
        addAndMakeVisible (cb);
    };

    juce::StringArray busNames;
    for (int b = 0; b < E::kNumBuses; ++b) busNames.add (ctx.engine.getBusName (b));

    if (track)
    {
        combo (sendABus, busNames, "Send A goes to this bus");
        combo (sendBBus, busNames, "Send B goes to this bus");
        auto outs = busNames; outs.insert (0, "Master");
        combo (output, outs, "Where this track's fader goes (Master, or a bus like the Drum Bus)");
        knob (sendA, "Send A level (after the fader)");
        knob (sendB, "Send B level (after the fader)");
        comboAtts.push_back (std::make_unique<CA> (apvts, E::Ids::track (strip, "sa_bus"), sendABus));
        comboAtts.push_back (std::make_unique<CA> (apvts, E::Ids::track (strip, "sb_bus"), sendBBus));
        comboAtts.push_back (std::make_unique<CA> (apvts, E::Ids::track (strip, "out"), output));
        sliderAtts.push_back (std::make_unique<SA> (apvts, E::Ids::track (strip, "sa"), sendA));
        sliderAtts.push_back (std::make_unique<SA> (apvts, E::Ids::track (strip, "sb"), sendB));
    }
    if (strip != E::kMaster)
    {
        knob (pan, "Pan (double-click = centre)");
        sliderAtts.push_back (std::make_unique<SA> (apvts, track ? E::Ids::track (strip, "pan") : E::Ids::bus (strip - E::kFirstBus, "pan"), pan));
        pan.setDoubleClickReturnValue (true, 0.0);
    }

    fader.setSliderStyle (juce::Slider::LinearVertical);
    fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    fader.getProperties().set (props::accent, (juce::int64) acc.getARGB());
    fader.setTooltip ("Volume (double-click = 0 dB)");
    addAndMakeVisible (fader);
    const auto volId = track ? E::Ids::track (strip, "vol") : (bus ? E::Ids::bus (strip - E::kFirstBus, "vol") : juce::String (E::Ids::masterVol));
    sliderAtts.push_back (std::make_unique<SA> (apvts, volId, fader));
    fader.setDoubleClickReturnValue (true, 0.0);
    fader.onValueChange = [this] { faderValue.setText (dbText ((float) fader.getValue()), juce::dontSendNotification); };
    faderValue.setText (dbText ((float) fader.getValue()), juce::dontSendNotification);
    faderValue.setJustificationType (juce::Justification::centred);
    faderValue.setFont (PlaygroundLookAndFeel::font (12.0f));
    faderValue.setColour (juce::Label::textColourId, acc.brighter (0.5f));
    faderValue.setColour (juce::Label::backgroundColourId, juce::Colour (0xff0d1015));
    addAndMakeVisible (faderValue);
    addAndMakeVisible (meter);

    auto ms = [&] (juce::TextButton& b, juce::Colour on, const juce::String& id, const juce::String& tip)
    {
        b.setClickingTogglesState (true);
        b.getProperties().set (props::style, "ms");
        b.getProperties().set (props::accent, (juce::int64) on.getARGB());
        b.setTooltip (tip);
        addAndMakeVisible (b);
        buttonAtts.push_back (std::make_unique<BA> (apvts, id, b));
    };
    if (track)
    {
        ms (mute, juce::Colour (0xffffb02e), E::Ids::track (strip, "mute"), "Mute");
        ms (solo, colours::hemGreen, E::Ids::track (strip, "solo"), "Solo (only soloed tracks play)");
    }
    else if (bus)
        ms (mute, juce::Colour (0xffffb02e), E::Ids::bus (strip - E::kFirstBus, "mute"), "Mute bus");
}

ChannelStrip::~ChannelStrip()
{
    sliderAtts.clear(); buttonAtts.clear(); comboAtts.clear();
}

void ChannelStrip::mouseDown (const juce::MouseEvent&)
{
    ctx.selectedStrip = strip;
    if (auto* p = getParentComponent()) p->repaint();
}

void ChannelStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f, 0.0f);
    const bool selected = ctx.selectedStrip == strip;
    g.setColour (juce::Colour (0xff12151a).withAlpha (0.82f));
    g.fillRoundedRectangle (r, 5.0f);
    const juce::Colour acc = E::isTrack (strip) ? ctx.engine.getTrackColour (strip) : (E::isBus (strip) ? colours::glow : juce::Colour (0xffcfa63a));
    g.setColour (acc);
    g.fillRoundedRectangle (r.withHeight (5.0f), 2.0f);
    g.setColour (selected ? acc.withAlpha (0.9f) : juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, selected ? 1.6f : 1.0f);

    // small captions
    g.setFont (PlaygroundLookAndFeel::font (9.5f));
    g.setColour (colours::paperDim);
    auto caption = [&] (juce::Component& c, const juce::String& text)
    {
        if (! c.isVisible() || c.getHeight() == 0) return;
        const int ch = getHeight() < 640 ? 10 : 12;
        g.drawText (text, c.getX(), c.getY() - ch, c.getWidth(), ch, juce::Justification::centredLeft, false);
    };
    if (E::isTrack (strip))
    {
        caption (sendABus, "SEND A");
        caption (sendBBus, "SEND B");
        caption (output, "OUTPUT");
    }
    if (strip == E::kMaster)
    {
        g.setColour (colours::paperDim);
        g.drawText ("MASTER", getLocalBounds().removeFromBottom (24), juce::Justification::centred, false);
    }
}

void ChannelStrip::resized()
{
    // compact mode for short windows (laptop screens): smaller slots and captions, pan beside the
    // fader readout, so the fader keeps a usable throw
    const bool compact = getHeight() < 640;
    const int cap = compact ? 10 : 12;
    auto r = getLocalBounds().reduced (6, compact ? 4 : 6);
    r.removeFromTop (3);
    name.setBounds (r.removeFromTop (compact ? 18 : 20));
    r.removeFromTop (2);
    const int slotH = compact ? 15 : juce::jlimit (17, 24, getHeight() / 30);
    for (auto& s : slots) s->setBounds (r.removeFromTop (slotH));
    r.removeFromTop (compact ? 3 : 6);

    if (E::isTrack (strip))
    {
        const int rowH = compact ? 18 : juce::jlimit (18, 24, getHeight() / 30);
        auto sendRow = [&] (juce::ComboBox& cb, juce::Slider& k)
        {
            r.removeFromTop (cap);
            auto row = r.removeFromTop (rowH + (compact ? 2 : 6));
            k.setBounds (row.removeFromRight (rowH + (compact ? 2 : 6)));
            cb.setBounds (row.withSizeKeepingCentre (row.getWidth() - 2, rowH));
        };
        sendRow (sendABus, sendA);
        sendRow (sendBBus, sendB);
        r.removeFromTop (cap);
        output.setBounds (r.removeFromTop (rowH));
        r.removeFromTop (compact ? 2 : 4);
    }
    auto bottom = r.removeFromBottom (strip == E::kMaster ? 24 : 22);
    if (E::isTrack (strip))
    {
        mute.setBounds (bottom.removeFromLeft (bottom.getWidth() / 2).reduced (2, 0));
        solo.setBounds (bottom.reduced (2, 0));
    }
    else if (E::isBus (strip))
        mute.setBounds (bottom.withSizeKeepingCentre (bottom.getWidth() / 2, bottom.getHeight()));
    r.removeFromBottom (4);
    if (compact && strip != E::kMaster)
    {
        auto row = r.removeFromBottom (28);
        pan.setBounds (row.removeFromLeft (28));
        faderValue.setBounds (row.reduced (4, 5));
    }
    else
    {
        faderValue.setBounds (r.removeFromBottom (18).reduced (6, 0));
        r.removeFromBottom (4);
        if (strip != E::kMaster)
        {
            const int pk = juce::jmin (r.getWidth() / 2, 40);
            pan.setBounds (r.removeFromTop (pk).withSizeKeepingCentre (pk, pk));
            r.removeFromTop (4);
        }
    }
    r.removeFromBottom (2);
    meter.setBounds (r.removeFromRight (14));
    r.removeFromRight (4);
    fader.setBounds (r);
}

void ChannelStrip::updateMeter()
{
    meter.update (ctx.engine.getPeak (strip, 0), ctx.engine.getPeak (strip, 1));
    for (auto& s : slots) s->repaint();
}

//==============================================================================
MixPage::MixPage (AppContext& c) : ctx (c)
{
    viewport.setViewedComponent (&strips, false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);
    rebuild();
    startTimerHz (30);
}

MixPage::~MixPage()
{
    stopTimer();
    viewport.setViewedComponent (nullptr, false);
}

void MixPage::rebuild()
{
    trackStrips.clear();
    for (int t = 0; t < E::kMaxTracks; ++t)
        if (ctx.engine.isTrackUsed (t))
            trackStrips.push_back (std::make_unique<ChannelStrip> (ctx, t));
    for (int b = 0; b < E::kNumBuses; ++b)
        trackStrips.push_back (std::make_unique<ChannelStrip> (ctx, E::busStrip (b)));
    for (auto& s : trackStrips) strips.addAndMakeVisible (*s);
    masterStrip = std::make_unique<ChannelStrip> (ctx, E::kMaster);
    addAndMakeVisible (*masterStrip);
    resized();
}

void MixPage::paint (juce::Graphics& g)
{
    // divider between tracks and buses
    juce::ignoreUnused (g);
}

void MixPage::resized()
{
    auto r = getLocalBounds();
    if (masterStrip != nullptr)
    {
        masterStrip->setBounds (r.removeFromRight (kStripWidth + 10).withTrimmedBottom (10));
        r.removeFromRight (8);
    }
    viewport.setBounds (r);
    const int h = r.getHeight() - viewport.getScrollBarThickness();
    int x = 0;
    for (auto& s : trackStrips)
    {
        if (E::isBus (s->getStrip()) && s->getStrip() == E::kFirstBus) x += 10;   // gap before the buses
        s->setBounds (x, 0, kStripWidth, h);
        x += kStripWidth;
    }
    strips.setSize (juce::jmax (x, r.getWidth()), h);
}

void MixPage::timerCallback()
{
    if (! isShowing()) return;
    for (auto& s : trackStrips) s->updateMeter();
    if (masterStrip != nullptr) masterStrip->updateMeter();
}

//==============================================================================
PluginLibrary::PluginLibrary (AppContext& c) : ctx (c) {}

juce::Rectangle<int> PluginLibrary::rowBounds (int i) const
{
    const int top = 40;
    const int h = juce::jmin (64, (getHeight() - top - 70) / (int) PluginRegistry::all().size());
    return { 8, top + i * h, getWidth() - 16, h - 6 };
}

int PluginLibrary::rowAt (juce::Point<int> p) const
{
    for (int i = 0; i < (int) PluginRegistry::all().size(); ++i)
        if (rowBounds (i).contains (p)) return i;
    return -1;
}

void PluginLibrary::paint (juce::Graphics& g)
{
    PlaygroundLookAndFeel::drawPanelText (g, "ELITE PLUGINS", getLocalBounds().removeFromTop (34).toFloat().reduced (10.0f, 0.0f),
                                          juce::Justification::centredLeft, 15.0f);
    const auto& all = PluginRegistry::all();
    for (int i = 0; i < (int) all.size(); ++i)
    {
        const auto r = rowBounds (i).toFloat();
        const auto acc = juce::Colour (all[(size_t) i]->accent);
        g.setColour (juce::Colour (0xff12151a).withAlpha (hover == i ? 0.95f : 0.75f));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (acc);
        g.fillRoundedRectangle (r.withWidth (5.0f), 2.0f);
        g.setColour (hover == i ? acc : juce::Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
        auto t = r.reduced (12.0f, r.getHeight() < 40.0f ? 2.0f : 4.0f);
        const bool compact = r.getHeight() < 40.0f;   // 19 plugins on a short window: name + one line of blurb
        g.setFont (PlaygroundLookAndFeel::font (compact ? 12.5f : 14.0f));
        g.setColour (colours::paper);
        g.drawText (all[(size_t) i]->name, t.removeFromTop (t.getHeight() * (compact ? 0.52f : 0.48f)), juce::Justification::bottomLeft, true);
        g.setFont (PlaygroundLookAndFeel::font (compact ? 9.5f : 10.5f, false));
        g.setColour (colours::paperDim);
        g.drawFittedText (all[(size_t) i]->blurb, t.toNearestInt(), juce::Justification::topLeft, compact ? 1 : 2, 0.85f);
    }
    auto foot = getLocalBounds().removeFromBottom (60).reduced (10, 4);
    g.setFont (PlaygroundLookAndFeel::font (11.0f, false));
    g.setColour (colours::paperDim);
    g.drawFittedText ("Click a plugin to add it to the selected strip:  " + ctx.engine.getStripName (ctx.selectedStrip),
                      foot, juce::Justification::centredLeft, 3);
}

void PluginLibrary::mouseMove (const juce::MouseEvent& e)
{
    const int r = rowAt (e.getPosition());
    if (r != hover) { hover = r; repaint(); }
}

void PluginLibrary::mouseUp (const juce::MouseEvent& e)
{
    const int r = rowAt (e.getPosition());
    if (r < 0) return;
    ctx.addPluginToStrip (ctx.selectedStrip, PluginRegistry::all()[(size_t) r]->id);
    repaint();
}
} // namespace pg::ui
