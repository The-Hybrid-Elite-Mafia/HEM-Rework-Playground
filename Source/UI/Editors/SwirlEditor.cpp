#include "SwirlEditor.h"

namespace pg::ui
{
namespace { constexpr int B = EliteSwirl::kBands; }

SwirlEditor::SwirlEditor (EliteSwirl& p) : EliteEditor (p), swirl (p)
{
    for (auto& f : history) { f.level.fill (-90.0f); f.swirl.fill (0.0f); f.spread.fill (0.0f); }
    quads.reserve ((size_t) (kHistory * B));
    projected.resize ((size_t) (kHistory * B));
    depthOf.resize ((size_t) (kHistory * B));
    resized();   // the base constructor laid out before displayHeight() was ours
}

juce::Colour SwirlEditor::heat (float v)
{
    // blue -> cyan -> green -> yellow -> red, like a thermal camera
    v = juce::jlimit (0.0f, 1.0f, v);
    const juce::Colour stops[] { juce::Colour (0xff1a3cff), juce::Colour (0xff19c8ff), juce::Colour (0xff35e06a), juce::Colour (0xffffe04a), juce::Colour (0xffff3b30) };
    const float x = v * 4.0f;
    const int i = juce::jmin (3, (int) x);
    return stops[i].interpolatedWith (stops[i + 1], x - (float) i);
}

void SwirlEditor::tick()
{
    const int got = swirl.readFrames (incoming.data(), (int) incoming.size());
    for (int i = 0; i < got; ++i)
    {
        newest = (newest + 1) % kHistory;
        history[(size_t) newest] = incoming[(size_t) i];
    }
}

juce::Rectangle<float> SwirlEditor::heightButton() const
{
    const auto a = getDisplayArea().reduced (8.0f);
    return { a.getRight() - 170.0f * (float) getWidth() / 1100.0f, a.getY() + 4.0f, 160.0f * (float) getWidth() / 1100.0f, 24.0f };
}

juce::Rectangle<float> SwirlEditor::cameraButton (int i) const
{
    const float U = (float) getWidth() / 1100.0f;
    const auto a = getDisplayArea().reduced (8.0f);
    const float w = 64.0f * U;
    return { a.getRight() - 180.0f * U - (4 - i) * (w + 6.0f * U), a.getY() + 4.0f, w, 24.0f };
}

void SwirlEditor::setCamera (int i)
{
    camera = i;
    switch (i)
    {
        case 1:  yaw = 0.0f;   pitch = 1.5f;  break;            // top: time across, frequency up the screen
        case 2:  yaw = 0.0f;   pitch = 0.12f; break;            // side: level / width against time
        case 3:  yaw = -1.5708f; pitch = 0.12f; heightMode = 2; break;   // front: the vocal (centre) axis, left / right
        default: yaw = -0.62f; pitch = 0.62f; break;
    }
    repaint (getDisplayArea().toNearestInt());
}

void SwirlEditor::mouseDown (const juce::MouseEvent& e)
{
    if (heightButton().contains (e.position)) { heightMode = (heightMode + 1) % 3; repaint (getDisplayArea().toNearestInt()); return; }
    for (int i = 0; i < 4; ++i)
        if (cameraButton (i).contains (e.position)) { setCamera (i); return; }
    dragging = getDisplayArea().contains (e.position);
    dragYaw = yaw; dragPitch = pitch;
}

void SwirlEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    yaw = dragYaw + (float) e.getDistanceFromDragStartX() * 0.008f;
    pitch = juce::jlimit (0.08f, 1.5f, dragPitch + (float) e.getDistanceFromDragStartY() * 0.006f);
    camera = -1;
    repaint (getDisplayArea().toNearestInt());
}

void SwirlEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! getDisplayArea().contains (e.position)) return;
    setCamera (0);
}

void SwirlEditor::paintDisplay (juce::Graphics& g, juce::Rectangle<float> area)
{
    const float U = (float) getWidth() / 1100.0f;
    const auto plot = area.reduced (40.0f * U, 22.0f * U).withTrimmedRight (70.0f * U);
    const float cy = std::cos (yaw), sy = std::sin (yaw), cp = std::cos (pitch), sp = std::sin (pitch);
    const float scale = juce::jmin (plot.getWidth() * 0.62f, plot.getHeight() * 0.95f);
    const auto centre = plot.getCentre().translated (0.0f, plot.getHeight() * 0.12f);

    // world: x = time (-0.5 oldest .. 0.5 newest), y = frequency (-0.5 low .. 0.5 high), z = height (0 .. 0.45)
    auto project = [&] (float x, float y, float z, float& depth)
    {
        const float rx = x * cy - y * sy, ry = x * sy + y * cy;    // turn around the vertical axis
        const float py = ry * cp - z * sp, pz = ry * sp + z * cp;   // tilt
        depth = py;
        const float persp = 1.0f / (1.0f + 0.35f * py);
        return juce::Point<float> (centre.x + rx * scale * 1.35f * persp, centre.y + (py * 0.55f - pz * 1.2f + 0.0f) * scale * persp);
    };
    auto heightOf = [this] (const EliteSwirl::Frame& f, int b)
    {
        if (heightMode == 1) return juce::jlimit (0.0f, 1.0f, f.spread[(size_t) b]) * 0.45f;
        if (heightMode == 2)   // left / right around the centre plane, louder bands stand out more
            return 0.225f + juce::jlimit (-1.0f, 1.0f, f.pan[(size_t) b]) * 0.225f
                              * juce::jlimit (0.0f, 1.0f, (f.level[(size_t) b] + 84.0f) / 60.0f);
        return juce::jlimit (0.0f, 1.0f, (f.level[(size_t) b] + 84.0f) / 84.0f) * 0.45f;
    };

    // floor grid + axes
    float d;
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    for (int i = 0; i <= 8; ++i)
    {
        const float v = -0.5f + (float) i / 8.0f;
        g.drawLine ({ project (v, -0.5f, 0.0f, d), project (v, 0.5f, 0.0f, d) }, 1.0f);
        g.drawLine ({ project (-0.5f, v, 0.0f, d), project (0.5f, v, 0.0f, d) }, 1.0f);
    }
    g.setFont (PlaygroundLookAndFeel::font (11.0f * U));
    g.setColour (colours::paperDim);
    for (float hz : { 100.0f, 1000.0f, 10000.0f })
    {
        const float y = std::log (hz / 40.0f) / std::log (20000.0f / 40.0f) - 0.5f;
        const auto p = project (0.56f, y, 0.0f, d);
        g.drawText (hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz), juce::Rectangle<float> (p.x - 4.0f, p.y - 8.0f, 40.0f, 16.0f),
                    juce::Justification::centredLeft, false);
    }
    {
        const auto p = project (0.0f, -0.62f, 0.0f, d);
        g.drawText ("TIME", juce::Rectangle<float> (p.x - 30.0f, p.y - 8.0f, 60.0f, 16.0f), juce::Justification::centred, false);
        const auto q = project (0.66f, 0.0f, 0.0f, d);
        g.drawText ("FREQ", juce::Rectangle<float> (q.x - 30.0f, q.y - 8.0f, 60.0f, 16.0f), juce::Justification::centred, false);
    }

    // surface: project every vertex once, then draw quads far to near
    for (int t = 0; t < kHistory; ++t)
    {
        const auto& f = history[(size_t) ((newest + 1 + t) % kHistory)];   // t = 0 oldest
        const float x = -0.5f + (float) t / (float) (kHistory - 1);
        for (int b = 0; b < B; ++b)
        {
            const float y = -0.5f + (float) b / (float) (B - 1);
            projected[(size_t) (t * B + b)] = project (x, y, heightOf (f, b), depthOf[(size_t) (t * B + b)]);
        }
    }
    quads.clear();
    for (int t = 0; t < kHistory - 1; ++t)
        for (int b = 0; b < B - 1; ++b)
            quads.push_back ({ depthOf[(size_t) (t * B + b)] + depthOf[(size_t) ((t + 1) * B + b + 1)], t, b });
    std::sort (quads.begin(), quads.end(), [] (const Quad& a, const Quad& b2) { return a.depth > b2.depth; });

    juce::Path quad;
    for (const auto& q : quads)
    {
        const auto& f0 = history[(size_t) ((newest + 1 + q.t) % kHistory)];
        const auto& f1 = history[(size_t) ((newest + 2 + q.t) % kHistory)];
        const float sw = 0.25f * (f0.swirl[(size_t) q.b] + f0.swirl[(size_t) q.b + 1] + f1.swirl[(size_t) q.b] + f1.swirl[(size_t) q.b + 1]);
        const float lv = juce::jlimit (0.0f, 1.0f, 0.5f * ((f0.level[(size_t) q.b] + 84.0f) + (f1.level[(size_t) q.b + 1] + 84.0f)) / 84.0f);
        // colour: swirl drives the heat; quiet clean areas sink to deep blue
        const auto c = heat (juce::jmax (sw * 1.6f, lv * 0.38f)).withMultipliedBrightness (0.55f + 0.45f * lv);
        quad.clear();
        quad.startNewSubPath (projected[(size_t) (q.t * B + q.b)]);
        quad.lineTo (projected[(size_t) ((q.t + 1) * B + q.b)]);
        quad.lineTo (projected[(size_t) ((q.t + 1) * B + q.b + 1)]);
        quad.lineTo (projected[(size_t) (q.t * B + q.b + 1)]);
        quad.closeSubPath();
        g.setColour (c.withAlpha (0.92f));
        g.fillPath (quad);
        g.setColour (c.brighter (0.4f).withAlpha (0.35f));
        g.strokePath (quad, juce::PathStrokeType (0.6f));
    }

    // FROM line on the floor
    const float fromHz = swirl.getAPVTS().getRawParameterValue ("from")->load();
    const float fy = std::log (fromHz / 40.0f) / std::log (20000.0f / 40.0f) - 0.5f;
    g.setColour (juce::Colour (swirl.getInfo().accent).withAlpha (0.9f));
    g.drawLine ({ project (-0.5f, fy, 0.0f, d), project (0.5f, fy, 0.0f, d) }, 2.0f);
    {
        const auto p = project (-0.56f, fy, 0.0f, d);
        g.drawText ("FROM", juce::Rectangle<float> (p.x - 46.0f, p.y - 8.0f, 44.0f, 16.0f), juce::Justification::centredRight, false);
    }

    // legend
    auto legend = juce::Rectangle<float> (area.getRight() - 52.0f * U, area.getY() + 40.0f * U, 14.0f * U, area.getHeight() - 90.0f * U);
    juce::ColourGradient grad (heat (1.0f), 0.0f, legend.getY(), heat (0.0f), 0.0f, legend.getBottom(), false);
    grad.addColour (0.25, heat (0.75f)); grad.addColour (0.5, heat (0.5f)); grad.addColour (0.75, heat (0.25f));
    g.setGradientFill (grad);
    g.fillRoundedRectangle (legend, 3.0f);
    g.setColour (colours::paperDim);
    g.setFont (PlaygroundLookAndFeel::font (10.0f * U));
    g.drawText ("SWIRL", legend.withY (legend.getY() - 18.0f * U).withHeight (16.0f * U).expanded (14.0f * U, 0.0f), juce::Justification::centred, false);
    g.drawText ("CLEAN", legend.withY (legend.getBottom() + 2.0f).withHeight (16.0f * U).expanded (14.0f * U, 0.0f), juce::Justification::centred, false);

    // title + height switch
    g.setFont (PlaygroundLookAndFeel::font (12.5f * U));
    g.setColour (colours::paper);
    g.drawText ("3D SWIRL MAP  -  drag to turn", area.reduced (12.0f, 8.0f).withHeight (24.0f), juce::Justification::centredLeft, false);
    const char* camNames[4] { "3D", "TOP", "SIDE", "FRONT" };
    for (int i = 0; i < 4; ++i)
    {
        const auto cb = cameraButton (i);
        g.setColour (camera == i ? juce::Colour (swirl.getInfo().accent).withAlpha (0.45f) : juce::Colour (0xff1b1f26));
        g.fillRoundedRectangle (cb, 4.0f);
        g.setColour (juce::Colour (swirl.getInfo().accent).withAlpha (0.7f));
        g.drawRoundedRectangle (cb, 4.0f, 1.0f);
        g.setColour (colours::paper);
        g.drawText (camNames[i], cb, juce::Justification::centred, false);
    }
    if (heightMode == 2)
    {
        // centre plane = the vocal axis
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawLine ({ project (-0.5f, -0.5f, 0.225f, d), project (0.5f, -0.5f, 0.225f, d) }, 1.0f);
        g.drawLine ({ project (-0.5f, 0.5f, 0.225f, d), project (0.5f, 0.5f, 0.225f, d) }, 1.0f);
    }
    const auto hb = heightButton();
    g.setColour (juce::Colour (0xff1b1f26));
    g.fillRoundedRectangle (hb, 4.0f);
    g.setColour (juce::Colour (swirl.getInfo().accent));
    g.drawRoundedRectangle (hb, 4.0f, 1.0f);
    g.setColour (colours::paper);
    const char* modes[3] { "HEIGHT: LEVEL", "HEIGHT: STEREO WIDTH", "HEIGHT: LEFT / RIGHT" };
    g.drawText (modes[heightMode], hb, juce::Justification::centred, false);
}
} // namespace pg::ui
