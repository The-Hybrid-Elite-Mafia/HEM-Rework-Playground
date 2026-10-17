#pragma once

#include "UI/PlaygroundLookAndFeel.h"
#include "Engine/PlaygroundEngine.h"

namespace pg::ui
{
/** Round transport button that sits in one of the 6 chrome sockets of the main faceplate.
    The socket itself is part of the background art; this draws the cap, icon and LED. */
class TransportButton final : public juce::Button
{
public:
    enum class Icon { toStart, play, stop, loop, beforeAfter, exportWav };
    TransportButton (Icon i, const juce::String& tip) : juce::Button (tip), icon (i) { setTooltip (tip); }

    bool lit = false;            // LED / glow state (playing, loop on, BEFORE, exporting)
    juce::Colour ledColour { colours::hemGreen };

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto b = getLocalBounds().toFloat();
        const auto c = b.getCentre();
        const float R = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
        // cap fills the whole recess up to the chrome ring (no grey recess showing around it)
        const float capR = R * 0.975f;
        juce::ColourGradient cap (juce::Colour (down ? 0xff2a2f36 : 0xff4b525c), c.x - capR * 0.4f, c.y - capR * 0.7f,
                                  juce::Colour (0xff101215), c.x + capR * 0.5f, c.y + capR, true);
        g.setGradientFill (cap);
        g.fillEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f);
        g.setColour (juce::Colours::white.withAlpha (over ? 0.22f : 0.12f));
        g.drawEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f, juce::jmax (1.0f, R * 0.03f));
        if (lit)
        {
            g.setColour (ledColour.withAlpha (0.18f));
            g.fillEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f);
            g.setColour (ledColour.withAlpha (0.8f));
            g.drawEllipse (c.x - capR * 0.96f, c.y - capR * 0.96f, capR * 1.92f, capR * 1.92f, juce::jmax (1.5f, R * 0.05f));
        }

        const auto ink = lit ? ledColour.brighter (0.6f) : colours::paper.withAlpha (over ? 1.0f : 0.85f);
        g.setColour (ink);
        const float s = R * 0.336f;   // icon size unchanged from the old 0.80 cap
        juce::Path p;
        switch (icon)
        {
            case Icon::toStart:
                p.addRectangle (c.x - s, c.y - s * 0.8f, s * 0.22f, s * 1.6f);
                p.addTriangle (c.x - s * 0.7f, c.y, c.x + s * 0.15f, c.y - s * 0.8f, c.x + s * 0.15f, c.y + s * 0.8f);
                p.addTriangle (c.x + s * 0.05f, c.y, c.x + s * 0.9f, c.y - s * 0.8f, c.x + s * 0.9f, c.y + s * 0.8f);
                break;
            case Icon::play:
                if (lit) { p.addRectangle (c.x - s * 0.6f, c.y - s * 0.8f, s * 0.42f, s * 1.6f); p.addRectangle (c.x + s * 0.18f, c.y - s * 0.8f, s * 0.42f, s * 1.6f); }
                else p.addTriangle (c.x - s * 0.55f, c.y - s * 0.9f, c.x - s * 0.55f, c.y + s * 0.9f, c.x + s * 0.95f, c.y);
                break;
            case Icon::stop:
                p.addRoundedRectangle (c.x - s * 0.7f, c.y - s * 0.7f, s * 1.4f, s * 1.4f, s * 0.12f);
                break;
            case Icon::loop:
            {
                juce::Path arc;
                arc.addCentredArc (c.x, c.y, s * 0.85f, s * 0.85f, 0.0f, 0.5f, juce::MathConstants<float>::twoPi - 0.2f, true);
                g.strokePath (arc, juce::PathStrokeType (s * 0.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                const float ax = c.x + std::sin (-0.2f) * s * 0.85f, ay = c.y - std::cos (-0.2f) * s * 0.85f;
                p.addTriangle (ax - s * 0.38f, ay - s * 0.3f, ax - s * 0.38f, ay + s * 0.3f, ax + s * 0.22f, ay);
                break;
            }
            case Icon::beforeAfter:
            {
                g.setFont (PlaygroundLookAndFeel::font (capR * 0.34f));
                g.drawText (lit ? "BEFORE" : "AFTER", juce::Rectangle<float> (c.x - capR, c.y - s * 0.4f, capR * 2.0f, s * 0.8f), juce::Justification::centred, false);
                break;
            }
            case Icon::exportWav:
                p.addRectangle (c.x - s * 0.14f, c.y - s * 0.9f, s * 0.28f, s * 0.9f);
                p.addTriangle (c.x - s * 0.55f, c.y - s * 0.05f, c.x + s * 0.55f, c.y - s * 0.05f, c.x, c.y + s * 0.55f);
                p.addRectangle (c.x - s * 0.85f, c.y + s * 0.7f, s * 1.7f, s * 0.2f);
                break;
        }
        g.fillPath (p);
    }

private:
    Icon icon;
};

/** Stereo peak meter fed from engine strip peaks (call update() from a UI timer). */
class StripMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    void update (float l, float r)
    {
        auto fall = [] (float& shown, float target) { const float db = params::gainToDb (target); shown = db > shown ? db : juce::jmax (db, shown - 1.6f); };
        fall (levL, l); fall (levR, r);
        holdL = juce::jmax (holdL - 0.3f, levL); holdR = juce::jmax (holdR - 0.3f, levR);
        if (l >= 1.0f || r >= 1.0f) clipped = true;
        repaint();
    }
    void resetClip() { clipped = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override { resetClip(); }

    bool horizontal = false;

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0b0d10));
        g.fillRoundedRectangle (b, 2.0f);
        if (horizontal)
        {
            // two horizontal bars (L above R) with a clip LED on the right
            auto clipLed = b.removeFromRight (juce::jmin (10.0f, b.getWidth() * 0.04f));
            g.setColour (clipped ? colours::meterRed : juce::Colour (0xff2a1414));
            g.fillRect (clipLed.reduced (1.0f, 2.0f));
            b = b.reduced (2.0f);
            const float h = (b.getHeight() - 2.0f) * 0.5f;
            drawHBar (g, b.withHeight (h), levL, holdL);
            drawHBar (g, b.withTrimmedTop (h + 2.0f), levR, holdR);
            return;
        }
        auto clipLed = b.removeFromTop (juce::jmin (6.0f, b.getHeight() * 0.05f));
        g.setColour (clipped ? colours::meterRed : juce::Colour (0xff2a1414));
        g.fillRect (clipLed.reduced (1.0f, 0.0f));
        b.removeFromTop (1.0f);
        const float w = (b.getWidth() - 1.0f) * 0.5f;
        drawBar (g, b.withWidth (w), levL, holdL);
        drawBar (g, b.withTrimmedLeft (w + 1.0f), levR, holdR);
    }

private:
    static float frac (float db) { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f); }   // -60 .. +6 dB
    static void drawHBar (juce::Graphics& g, juce::Rectangle<float> r, float db, float hold)
    {
        const float f = frac (db);
        if (f > 0.0f)
        {
            juce::ColourGradient grad (colours::hemGreen.darker (0.3f), r.getX(), 0.0f, colours::meterRed, r.getRight(), 0.0f, false);
            grad.addColour (frac (-12.0f), colours::hemGreen);
            grad.addColour (frac (0.0f), juce::Colour (0xffffb02e));
            g.setGradientFill (grad);
            g.fillRect (r.withWidth (r.getWidth() * f));
        }
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.fillRect (r.getX() + r.getWidth() * frac (hold), r.getY(), 1.0f, r.getHeight());
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.fillRect (r.getX() + r.getWidth() * frac (0.0f), r.getY(), 1.0f, r.getHeight());
    }
    static void drawBar (juce::Graphics& g, juce::Rectangle<float> r, float db, float hold)
    {
        const float f = frac (db);
        if (f > 0.0f)
        {
            const auto fill = r.withTop (r.getBottom() - r.getHeight() * f);
            juce::ColourGradient grad (colours::meterRed, 0.0f, r.getY(), colours::hemGreen.darker (0.3f), 0.0f, r.getBottom(), false);
            grad.addColour (1.0 - frac (0.0f), juce::Colour (0xffffb02e));
            grad.addColour (1.0 - frac (-12.0f), colours::hemGreen);
            g.setGradientFill (grad);
            g.fillRect (fill);
        }
        const float hy = r.getBottom() - r.getHeight() * frac (hold);
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.fillRect (r.getX(), hy, r.getWidth(), 1.0f);
        // 0 dB tick
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.fillRect (r.getX(), r.getBottom() - r.getHeight() * frac (0.0f), r.getWidth(), 1.0f);
    }
    float levL = -100.0f, levR = -100.0f, holdL = -100.0f, holdR = -100.0f;
    bool clipped = false;
};

/** dB -> "−3.5" style text for faders. */
inline juce::String dbText (float db)
{
    if (db <= PlaygroundEngine::kFaderOffDb + 0.05f) return "-inf";
    return (db > 0.0f ? "+" : "") + juce::String (db, 1);
}
} // namespace pg::ui
