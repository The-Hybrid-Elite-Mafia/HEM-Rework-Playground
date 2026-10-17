#include "Spectrum3D.h"
#include "PlaygroundLookAndFeel.h"
#include <cmath>

namespace pg::ui
{
namespace
{
    using W = analysis::Waterfall;
    constexpr float kXW = 1.6f;     // half width (frequency)
    constexpr float kZD = 2.4f;     // depth (time)
    constexpr float kH = 0.95f;     // height of a 0 dB peak
    constexpr float kDbLow = -84.0f;
    constexpr float kCamDist = 4.2f;

    float height (float db) noexcept { return kH * juce::jlimit (0.0f, 1.0f, (db - kDbLow) / -kDbLow); }
    float xOf (float hz) noexcept { return -kXW + 2.0f * kXW * std::log (hz / 20.0f) / std::log (1000.0f); }

    // deep blue -> violet -> HEM orange -> hot yellow-white
    juce::Colour heat (float t) noexcept
    {
        t = juce::jlimit (0.0f, 1.0f, t);
        const juce::Colour stops[] { juce::Colour (0xff0a1a3a), juce::Colour (0xff3a2a8c), juce::Colour (0xffb0368c),
                                     juce::Colour (0xffff8a2a), juce::Colour (0xffffe08a) };
        const float x = t * 4.0f;
        const int i = juce::jmin (3, (int) x);
        return stops[i].interpolatedWith (stops[i + 1], x - (float) i);
    }
}

Spectrum3D::Spectrum3D (PlaygroundEngine& e) : engine (e)
{
    setOpaque (false);
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    setTooltip ("3D spectrum of the master output: newest sound in front, lows left, highs right. Drag to turn it, double-click to reset.");
}

Spectrum3D::~Spectrum3D()
{
    stopTimer();
    if (tapping) engine.setMasterTapActive (false);
}

void Spectrum3D::visibilityChanged()
{
    const bool show = isShowing();
    if (show == tapping) return;
    tapping = show;
    engine.setMasterTapActive (show);
    if (show)
    {
        while (engine.readMasterTap (scratch.data(), (int) scratch.size()) > 0) {}   // drop stale audio
        startTimerHz (30);
    }
    else
        stopTimer();
}

void Spectrum3D::timerCallback()
{
    const double sr = engine.getSampleRate() > 0.0 ? engine.getSampleRate() : 48000.0;
    if (sr != preparedRate) { waterfall.prepare (sr); preparedRate = sr; }
    for (int got; (got = engine.readMasterTap (scratch.data(), (int) scratch.size())) > 0;)
        waterfall.add (scratch.data(), got);
    repaint();
}

void Spectrum3D::mouseDown (const juce::MouseEvent&) { dragYaw = yaw; dragPitch = pitch; }

void Spectrum3D::mouseDrag (const juce::MouseEvent& e)
{
    yaw = juce::jlimit (-0.9f, 0.9f, dragYaw + (float) e.getDistanceFromDragStartX() * 0.006f);
    pitch = juce::jlimit (0.15f, 1.1f, dragPitch + (float) e.getDistanceFromDragStartY() * 0.006f);
    repaint();
}

void Spectrum3D::mouseDoubleClick (const juce::MouseEvent&) { yaw = kDefaultYaw; pitch = kDefaultPitch; repaint(); }

juce::Point<float> Spectrum3D::project (float x, float y, float z, juce::Rectangle<float>) const noexcept
{
    const float zc = z - 0.5f * kZD;
    const float cy = std::cos (yaw), sy = std::sin (yaw), cp = std::cos (pitch), sp = std::sin (pitch);
    const float x1 = x * cy - zc * sy;
    const float z1 = x * sy + zc * cy;
    const float up = y * cp + z1 * sp;
    const float depth = z1 * cp - y * sp;
    const float s = kCamDist / (kCamDist + depth);
    return { x1 * s, -up * s };
}

void Spectrum3D::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff0b0d12));
    g.fillRoundedRectangle (area, 6.0f);
    g.setColour (juce::Colour (0xffff8a2a).withAlpha (0.25f));
    g.drawRoundedRectangle (area.reduced (0.5f), 6.0f, 1.0f);

    auto head = area.removeFromTop (24.0f);
    PlaygroundLookAndFeel::drawPanelText (g, "3D SPECTRUM", head.reduced (10.0f, 0.0f), juce::Justification::centredLeft, 14.0f, juce::Colour (0xffff8a2a).brighter (0.3f));
    PlaygroundLookAndFeel::drawPanelText (g, "drag to turn  -  double-click to reset", head.reduced (10.0f, 0.0f), juce::Justification::centredRight, 11.0f, juce::Colour (0xff8a8f99));
    auto box = area.reduced (14.0f, 10.0f).withTrimmedBottom (14.0f);
    if (box.getWidth() < 40.0f || box.getHeight() < 40.0f) return;

    // fit the whole scene (its 8 corners) into the box
    float x0 = 1.0e9f, x1 = -1.0e9f, y0 = 1.0e9f, y1 = -1.0e9f;
    for (float cx : { -kXW, kXW })
        for (float cyy : { 0.0f, kH })
            for (float cz : { 0.0f, kZD })
            {
                const auto p = project (cx, cyy, cz, box);
                x0 = juce::jmin (x0, p.x); x1 = juce::jmax (x1, p.x); y0 = juce::jmin (y0, p.y); y1 = juce::jmax (y1, p.y);
            }
    const float scale = juce::jmin (box.getWidth() / (x1 - x0), box.getHeight() / (y1 - y0));
    const float ox = box.getCentreX() - 0.5f * (x0 + x1) * scale, oy = box.getCentreY() - 0.5f * (y0 + y1) * scale;
    auto P = [&] (float x, float y, float z) { const auto p = project (x, y, z, box); return juce::Point<float> (ox + p.x * scale, oy + p.y * scale); };

    // floor grid and frequency labels
    g.setColour (juce::Colour (0xff2a3140));
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        g.drawLine (juce::Line<float> (P (xOf (hz), 0.0f, 0.0f), P (xOf (hz), 0.0f, kZD)), 1.0f);
    for (int k = 0; k <= 4; ++k)
    {
        const float z = kZD * (float) k / 4.0f;
        g.drawLine (juce::Line<float> (P (-kXW, 0.0f, z), P (kXW, 0.0f, z)), 1.0f);
    }
    // back wall: -24 / -48 / -72 dB lines
    g.setColour (juce::Colour (0xff222836));
    for (float db : { -24.0f, -48.0f, -72.0f })
        g.drawLine (juce::Line<float> (P (-kXW, height (db), kZD), P (kXW, height (db), kZD)), 1.0f);

    // the landscape, back to front so near rows cover far ones
    constexpr int N = W::kPoints;
    for (int age = W::kRows - 1; age >= 0; --age)
    {
        const float z = kZD * (float) age / (float) (W::kRows - 1);
        const float* r = waterfall.row (age);
        juce::Path ridge, body;
        float peak = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            const float x = -kXW + 2.0f * kXW * (float) i / (float) (N - 1);
            const float h = height (r[i]);
            peak = juce::jmax (peak, h);
            const auto p = P (x, h, z);
            if (i == 0) ridge.startNewSubPath (p); else ridge.lineTo (p);
        }
        body = ridge;
        body.lineTo (P (kXW, 0.0f, z));
        body.lineTo (P (-kXW, 0.0f, z));
        body.closeSubPath();

        const float fog = (float) age / (float) (W::kRows - 1);
        const auto base = P (0.0f, 0.0f, z), top = P (0.0f, kH, z);
        juce::ColourGradient grad (heat (0.05f).withAlpha (0.92f), base, heat (1.0f).interpolatedWith (juce::Colour (0xff0b0d12), 0.6f * fog).withAlpha (0.92f), top, false);
        grad.addColour (0.5, heat (0.55f).interpolatedWith (juce::Colour (0xff0b0d12), 0.6f * fog).withAlpha (0.92f));
        g.setGradientFill (grad);
        g.fillPath (body);

        const auto line = heat (0.35f + 0.65f * peak / kH).interpolatedWith (juce::Colour (0xff0b0d12), 0.7f * fog);
        if (age == 0)
        {
            g.setColour (juce::Colour (0xffff8a2a).withAlpha (0.25f));
            g.strokePath (ridge, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (juce::Colour (0xffffe08a));
            g.strokePath (ridge, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else
        {
            g.setColour (line.withAlpha (0.55f * (1.0f - fog) + 0.1f));
            g.strokePath (ridge, juce::PathStrokeType (1.0f));
        }
    }

    // labels at the front edge
    g.setFont (PlaygroundLookAndFeel::font (11.0f, false));
    g.setColour (juce::Colour (0xff8a8f99));
    for (const auto& [hz, text] : { std::pair<float, const char*> { 100.0f, "100" }, { 1000.0f, "1k" }, { 10000.0f, "10k" } })
    {
        const auto p = P (xOf (hz), 0.0f, 0.0f);
        g.drawText (text, juce::Rectangle<float> (p.x - 20.0f, p.y + 2.0f, 40.0f, 14.0f), juce::Justification::centred);
    }
    const auto backLeft = P (-kXW, 0.0f, kZD);
    g.drawText (juce::String (W::kRows * waterfall.secondsPerRow(), 1) + " s ago", juce::Rectangle<float> (backLeft.x - 70.0f, backLeft.y - 16.0f, 66.0f, 14.0f), juce::Justification::centredRight);
}
} // namespace pg::ui
