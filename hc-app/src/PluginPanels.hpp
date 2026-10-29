#pragma once
// What each Elite plug-in window shows, as The Hybrid Core faceplate (FxPanel) sees it: the controls in
// order and in sections, and the meters. Same controls, names and meters as the 0.10.0 windows.
#include "hem/pg/Plugin.hpp"
#include "hybrid/ui/FxPanel.hpp"

#include <string>
#include <vector>

namespace hem::app
{
struct PanelMap
{
    enum class MeterSource : std::uint8_t { inputPeak, outputPeak, reduction, level, value };
    struct Meter { MeterSource source; int readout = -1; };

    hc::ui::PanelSpec spec;
    std::vector<std::string> paramIds;   // panel parameter index -> plug-in control id
    std::vector<Meter> meters;           // panel meter index -> where its value comes from
};

/** The faceplate description of one plug-in (hand-made per Elite plug-in, automatic for any other). */
PanelMap panelFor (const hem::pg::Plugin&);

/** The meter value the panel shows (dB, LUFS, Hz... as its unit says). */
float meterValue (const hem::pg::Plugin&, const PanelMap::Meter&);
} // namespace hem::app
