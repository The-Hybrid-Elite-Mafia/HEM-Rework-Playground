#pragma once
// Mixer pieces shared by the MIX and MASTER pages: insert slots and channel strips.
#include "../App.hpp"

namespace hem::app
{
inline constexpr float kStripWidth = 104.0f;

/** One insert slot: "+" when empty, else the plug-in's name on its colour. Click opens / adds,
    right-click: bypass, replace, move, remove. */
void drawInsertSlot (App&, Ui&, Rect r, int strip, int slot);

/** A whole channel strip (inserts, sends, output, pan, fader + meter, mute / solo). */
void drawChannelStrip (App&, Ui&, Rect r, int strip);

/** "+3.5" / "-inf" for faders. */
std::string dbText (float db);

/** Colour of a strip (tracks: their colour; buses: HEM blue; master: gold). */
Rgba stripAccent (App&, int strip);
} // namespace hem::app
