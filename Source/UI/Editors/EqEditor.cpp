#include "EqEditor.h"

namespace pg::ui
{
namespace
{
    using W = analysis::Waterfall;
    constexpr float kCurveRange = 18.0f;            // curve shows +-18 dB (the GAIN range)
    constexpr float kLevelLow = -84.0f;             // spectrum floor
    const juce::Colour kAccent (0xff2ec8e6);
    const juce::Colour kBandColours[EliteEq::kBands] { juce::Colour (0xffff5a5a), juce::Colour (0xffffa62e), juce::Colour (0xfff5e04a),
                                                       juce::Colour (0xff4ee07a), juce::Colour (0xff4aa8ff), juce::Colour (0xffc36bff) };

}

EqEditor::EqEditor (EliteEq& p) : EliteEditor (p), eq (p)
{
    shown.fill (kLevelLow);
    resized();   // the base constructor laid out before displayHeight() was ours
}

void EqEditor::tick()
{
    const double sr = eq.getSampleRate() > 0.0 ? eq.getSampleRate() : 48000.0;
    if (sr != preparedRate) { analyzer.prepare (sr); preparedRate = sr; }
    for (int got; (got = eq.readAnalyzer (scratch.data(), (int) scratch.size())) > 0;)
        analyzer.add (scratch.data(), got);
    const float* r = analyzer.row (0);
    for (int i = 0; i < W::kPoints; ++i)
        shown[(size_t) i] = r[i] > shown[(size_t) i] ? r[i] : juce::jmax (kLevelLow, shown[(size_t) i] - 0.9f);   // ~27 dB/s fall
    if (! getDisplayArea().isEmpty()) repaint (getDisplayArea().toNearestInt());
}

//==============================================================================
juce::Rectangle<float> EqEditor::plotArea() const { return getDisplayArea().reduced (14.0f * u(), 10.0f * u()).withTrimmedBottom (16.0f * u()); }
float EqEditor::xOf (float hz, juce::Rectangle<float> r) const noexcept { return r.getX() + r.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f); }
float EqEditor::hzOf (float x, juce::Rectangle<float> r) const noexcept { return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (x - r.getX()) / r.getWidth())); }
float EqEditor::yOfDb (float db, juce::Rectangle<float> r) const noexcept { return r.getCentreY() - r.getHeight() * 0.5f * juce::jlimit (-1.2f, 1.2f, db / kCurveRange); }
float EqEditor::dbOfY (float y, juce::Rectangle<float> r) const noexcept { return juce::jlimit (-kCurveRange, kCurveRange, (r.getCentreY() - y) / (r.getHeight() * 0.5f) * kCurveRange); }
float EqEditor::yOfLevel (float db, juce::Rectangle<float> r) const noexcept { return r.getBottom() - r.getHeight() * juce::jlimit (0.0f, 1.0f, (db - kLevelLow) / -kLevelLow); }

juce::RangedAudioParameter* EqEditor::param (int band, const char* what) const { return eq.getAPVTS().getParameter (EliteEq::id (band, what)); }

bool EqEditor::movesGain (int band) const
{
    const int type = (int) eq.getAPVTS().getRawParameterValue (EliteEq::id (band, "type"))->load();
    if (band == 0) return type != 0;                         // Low Cut has no gain
    if (band == EliteEq::kBands - 1) return type != 1;       // High Cut has no gain
    return type != 3;                                        // Notch has no gain
}

juce::Point<float> EqEditor::dotOf (int band, juce::Rectangle<float> r) const
{
    const float hz = eq.getAPVTS().getRawParameterValue (EliteEq::id (band, "freq"))->load();
    const float gain = movesGain (band) ? eq.getAPVTS().getRawParameterValue (EliteEq::id (band, "gain"))->load() : 0.0f;
    return { xOf (hz, r), yOfDb (gain, r) };
}

int EqEditor::bandAt (juce::Point<float> p) const
{
    const auto r = plotArea();
    int best = -1;
    float bestD = 16.0f * u();
    for (int b = 0; b < EliteEq::kBands; ++b)
        if (const float d = dotOf (b, r).getDistanceFrom (p); d < bestD) { bestD = d; best = b; }
    return best;
}

//==============================================================================
void EqEditor::mouseMove (const juce::MouseEvent& e)
{
    const int b = bandAt (e.position);
    if (b != hoverBand) { hoverBand = b; setMouseCursor (b >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor); repaint (getDisplayArea().toNearestInt()); }
}

void EqEditor::mouseDown (const juce::MouseEvent& e)
{
    dragBand = bandAt (e.position);
    if (dragBand < 0) return;
    for (auto* what : { "freq", "gain", "on" })
        if (auto* p = param (dragBand, what)) p->beginChangeGesture();
    if (auto* on = param (dragBand, "on"); on != nullptr && on->getValue() < 0.5f) on->setValueNotifyingHost (1.0f);   // grabbing a band switches it on
}

void EqEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBand < 0) return;
    const auto r = plotArea();
    if (auto* f = param (dragBand, "freq")) f->setValueNotifyingHost (f->convertTo0to1 (hzOf (e.position.x, r)));
    if (movesGain (dragBand))
        if (auto* g = param (dragBand, "gain")) g->setValueNotifyingHost (g->convertTo0to1 (dbOfY (e.position.y, r)));
    repaint (getDisplayArea().toNearestInt());
}

void EqEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragBand >= 0)
        for (auto* what : { "freq", "gain", "on" })
            if (auto* p = param (dragBand, what)) p->endChangeGesture();
    dragBand = -1;
}

void EqEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int b = bandAt (e.position);
    if (b < 0 || ! movesGain (b)) return;
    if (auto* g = param (b, "gain")) { g->beginChangeGesture(); g->setValueNotifyingHost (g->convertTo0to1 (0.0f)); g->endChangeGesture(); }
}

//==============================================================================
void EqEditor::paintDisplay (juce::Graphics& g, juce::Rectangle<float> area)
{
    const float U = u();
    g.setColour (juce::Colour (0xff0b0d12));
    g.fillRoundedRectangle (area.reduced (4.0f * U), 6.0f * U);
    const auto r = plotArea();

    // grid: frequencies and +-6 / 12 dB
    g.setFont (PlaygroundLookAndFeel::font (11.0f * U, false));
    for (const auto& [hz, text] : { std::pair<float, const char*> { 50.0f, "50" }, { 100.0f, "100" }, { 200.0f, "200" }, { 500.0f, "500" },
                                    { 1000.0f, "1k" }, { 2000.0f, "2k" }, { 5000.0f, "5k" }, { 10000.0f, "10k" } })
    {
        const float x = xOf (hz, r);
        g.setColour (juce::Colour (0xff222836));
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        g.setColour (juce::Colour (0xff6d7380));
        g.drawText (text, juce::Rectangle<float> (x - 20.0f * U, r.getBottom() + 2.0f, 40.0f * U, 14.0f * U), juce::Justification::centred);
    }
    for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
    {
        const float y = yOfDb (db, r);
        g.setColour (db == 0.0f ? juce::Colour (0xff3a4152) : juce::Colour (0xff1e2430));
        g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        g.setColour (juce::Colour (0xff6d7380));
        g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (r.getX() + 2.0f, y - 12.0f * U, 30.0f * U, 12.0f * U), juce::Justification::centredLeft);
    }

    // spectrum (what comes out of the EQ)
    {
        juce::Path fill;
        fill.startNewSubPath (r.getX(), r.getBottom());
        for (int i = 0; i < W::kPoints; ++i)
            fill.lineTo (xOf (W::pointHz (i), r), yOfLevel (shown[(size_t) i], r));
        fill.lineTo (r.getRight(), r.getBottom());
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (kAccent.withAlpha (0.35f), 0.0f, r.getY(), kAccent.withAlpha (0.05f), 0.0f, r.getBottom(), false));
        g.fillPath (fill);
    }

    // the EQ curve
    const double fs = eq.getSampleRate() > 0.0 ? eq.getSampleRate() : 48000.0;
    juce::Path curve;
    const int steps = juce::jmax (64, (int) r.getWidth() / 2);
    for (int i = 0; i <= steps; ++i)
    {
        const float x = r.getX() + r.getWidth() * (float) i / (float) steps;
        const float y = yOfDb (eq.responseDb (fs, hzOf (x, r)), r);
        if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
    }
    g.saveState();
    g.reduceClipRegion (r.toNearestInt());
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.strokePath (curve, juce::PathStrokeType (5.0f * U, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (juce::Colours::white);
    g.strokePath (curve, juce::PathStrokeType (2.0f * U, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.restoreState();

    // band dots
    for (int b = 0; b < EliteEq::kBands; ++b)
    {
        const bool on = eq.getAPVTS().getRawParameterValue (EliteEq::id (b, "on"))->load() > 0.5f;
        const auto c = dotOf (b, r);
        const float rad = (b == dragBand || b == hoverBand ? 11.0f : 9.0f) * U;
        g.setColour (kBandColours[b].withAlpha (on ? 1.0f : 0.3f));
        g.fillEllipse (juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c));
        g.setColour (juce::Colour (0xff0b0d12));
        g.setFont (PlaygroundLookAndFeel::font (12.0f * U, true));
        g.drawText (juce::String (b + 1), juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c), juce::Justification::centred);
    }
    PlaygroundLookAndFeel::drawPanelText (g, "drag a dot: left / right = FREQ, up / down = GAIN  -  double-click = 0 dB",
                                          juce::Rectangle<float> (r.getX(), area.getY() + 6.0f * U, r.getWidth() - 4.0f, 16.0f * U),
                                          juce::Justification::centredRight, 11.0f * U, juce::Colour (0xff8a8f99));
}
} // namespace pg::ui
