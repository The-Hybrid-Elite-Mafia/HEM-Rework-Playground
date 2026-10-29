#include "PluginPanels.hpp"

#include "Registry.hpp"
#include "EliteBalance.hpp"
#include "EliteBass.hpp"
#include "EliteChannel.hpp"
#include "EliteComp.hpp"
#include "EliteConsole.hpp"
#include "EliteDeEss.hpp"
#include "EliteDelay.hpp"
#include "EliteDoubler.hpp"
#include "EliteEq.hpp"
#include "EliteHush.hpp"
#include "EliteMaster.hpp"
#include "EliteMultiband.hpp"
#include "EliteSwirl.hpp"
#include "EliteTransient.hpp"
#include "EliteTune.hpp"
#include "EliteVox.hpp"
#include "EliteWidth.hpp"

#include <cctype>
#include <cmath>

namespace hem::app
{
namespace
{
    using MS = PanelMap::MeterSource;

    struct Builder
    {
        const pg::Plugin& plugin;
        PanelMap map;

        void group (const std::string& name, std::initializer_list<std::pair<const char*, const char*>> controls)
        {
            hc::ui::PanelGroup g;
            g.name = name;
            g.first = (int) map.paramIds.size();
            for (auto& [id, label] : controls) add (id, label);
            g.count = (int) map.paramIds.size() - g.first;
            if (g.count > 0) map.spec.groups.push_back (g);
        }

        void add (const std::string& id, const std::string& label)
        {
            const auto* prm = plugin.parameters().find (id);
            if (prm == nullptr) return;
            const auto& sp = prm->spec();
            hc::ui::PanelParam pp;
            pp.name = label;
            if (sp.type == pg::ParamSpec::Type::toggle) pp.kind = hc::ui::PanelParam::Kind::Toggle;
            else if (sp.type == pg::ParamSpec::Type::choice)
            {
                pp.kind = hc::ui::PanelParam::Kind::Choice;
                pp.choices.assign (sp.choices.begin(), sp.choices.end());
            }
            pp.defNormalized = prm->defaultNormalised();
            pp.bipolar = sp.type == pg::ParamSpec::Type::floating && sp.range.start < 0.0f && sp.range.end > 0.0f;
            map.spec.params.push_back (std::move (pp));
            map.paramIds.push_back (id);
        }

        void meter (const std::string& name, MS source, int readout = -1, const std::string& unit = "dB", float range = 24.0f)
        {
            hc::ui::PanelMeter m;
            m.name = name;
            switch (source)
            {
                case MS::inputPeak: case MS::outputPeak: case MS::level:
                    m.unit = "dBFS"; m.min = -60.0f; m.max = 0.0f; break;
                case MS::reduction:
                    m.unit = "dB"; m.min = -range; m.max = 0.0f; break;
                case MS::value:
                    m.unit = unit;
                    if (unit == "dB+-") { m.unit = "dB"; m.centred = true; m.min = -range; m.max = range; }
                    else if (unit == "cents") { m.centred = true; m.min = -range; m.max = range; }
                    else if (unit == "LUFS") { m.min = -36.0f; m.max = 0.0f; }
                    else if (unit == "dBTP") { m.min = -24.0f; m.max = 3.0f; }
                    else if (unit == "ms") { m.min = 0.0f; m.max = 2000.0f; }
                    else if (unit == "%") { m.min = 0.0f; m.max = 100.0f; }
                    else if (unit == "corr") { m.min = -1.0f; m.max = 1.0f; }
                    else if (unit == "Hz") { m.min = 0.0f; m.max = 20000.0f; }
                    else if (unit == "pitch") { m.unit = "Hz"; m.min = 50.0f; m.max = 1000.0f; }
                    else if (unit == "midi") { m.unit = "note"; m.min = 36.0f; m.max = 84.0f; }
                    else if (unit == "dBFS") { m.min = -96.0f; m.max = 0.0f; }
                    else { m.min = 0.0f; m.max = 18.0f; }
                    break;
            }
            map.spec.meters.push_back (m);
            map.meters.push_back ({ source, readout });
        }
    };

    std::string upper (std::string s) { for (auto& c : s) c = (char) std::toupper ((unsigned char) c); return s; }
}

PanelMap panelFor (const pg::Plugin& plugin)
{
    Builder b { plugin, {} };
    const std::string id = plugin.getInfo().id;
    b.map.spec.name = upper (plugin.getInfo().name);
    b.map.spec.blurb = plugin.getInfo().blurb;
    b.map.spec.hasBypass = true;

    if (id == "elite-vox")
    {
        b.group ("", { { "mode", "MODE" }, { "gate", "GATE" }, { "squeeze", "SQUEEZE" }, { "output", "OUTPUT" } });
        b.meter ("IN", MS::inputPeak);
        b.meter ("GATE", MS::reduction, pg::EliteVox::gateGr, "dB", 40.0f);
        b.meter ("SQUEEZE", MS::reduction, pg::EliteVox::compGr, "dB", 24.0f);
        b.meter ("LIMIT", MS::reduction, pg::EliteVox::limitGr, "dB", 12.0f);
        b.meter ("OUT", MS::outputPeak);
    }
    else if (id == "elite-comp")
    {
        b.group ("", { { "mode", "CHARACTER" }, { "threshold", "THRESHOLD" }, { "ratio", "RATIO" }, { "attack", "ATTACK" }, { "release", "RELEASE" },
                       { "arc", "AUTO RELEASE" }, { "gain", "GAIN" }, { "mix", "MIX" } });
        b.meter ("IN", MS::inputPeak);
        b.meter ("GAIN REDUCTION", MS::reduction, pg::EliteComp::gr, "dB", 24.0f);
        b.meter ("OUT", MS::outputPeak);
    }
    else if (id == "elite-bass")
    {
        b.group ("", { { "freq", "FREQUENCY" }, { "intensity", "INTENSITY" }, { "original", "ORIGINAL BASS" }, { "output", "OUTPUT" },
                       { "solo", "HARMONICS ONLY" } });
        b.meter ("IN", MS::inputPeak);
        b.meter ("ORIGINAL", MS::level, pg::EliteBass::originalLevel);
        b.meter ("HARMONICS", MS::level, pg::EliteBass::harmonicsLevel);
        b.meter ("OUT", MS::outputPeak);
    }
    else if (id == "elite-channel")
    {
        b.group ("GATE", { { "gate_on", "GATE" }, { "gate_thr", "GATE THR" }, { "gate_floor", "FLOOR" }, { "gate_rel", "GATE REL" } });
        // LOW Q and HIGH Q were controls 0.10.0's window never showed; every control is on the plate now
        b.group ("EQ", { { "eq_on", "EQ" }, { "eq1_freq", "LOW HZ" }, { "eq1_gain", "LOW" }, { "eq1_q", "LOW Q" }, { "eq2_freq", "LO-MID HZ" },
                         { "eq2_gain", "LO-MID" }, { "eq2_q", "LO-MID Q" }, { "eq3_freq", "HI-MID HZ" }, { "eq3_gain", "HI-MID" }, { "eq3_q", "HI-MID Q" },
                         { "eq4_freq", "HIGH HZ" }, { "eq4_gain", "HIGH" }, { "eq4_q", "HIGH Q" } });
        b.group ("COMP", { { "comp_on", "COMP" }, { "comp_mode", "COMP MODE" }, { "comp_thr", "COMP THR" }, { "comp_ratio", "RATIO" },
                           { "comp_atk", "ATTACK" }, { "comp_rel", "RELEASE" } });
        b.group ("OUTPUT", { { "order", "ORDER" }, { "phase", "PHASE INVERT" }, { "output", "OUTPUT" } });
        b.meter ("IN", MS::inputPeak);
        b.meter ("GATE", MS::reduction, pg::EliteChannel::gateGr, "dB", 40.0f);
        b.meter ("COMP", MS::reduction, pg::EliteChannel::compGr, "dB", 24.0f);
        b.meter ("OUT", MS::outputPeak);
    }
    else if (id == "elite-eq")
    {
        for (int band = 0; band < pg::EliteEq::kBands; ++band)
        {
            const std::string n = std::to_string (band + 1);
            hc::ui::PanelGroup g;
            g.name = "BAND " + n;
            g.first = (int) b.map.paramIds.size();
            b.add (pg::EliteEq::id (band, "on"), "BAND " + n);
            b.add (pg::EliteEq::id (band, "type"), "TYPE " + n);
            b.add (pg::EliteEq::id (band, "freq"), "FREQ " + n);
            b.add (pg::EliteEq::id (band, "gain"), "GAIN " + n);
            b.add (pg::EliteEq::id (band, "q"), "Q " + n);
            g.count = (int) b.map.paramIds.size() - g.first;
            b.map.spec.groups.push_back (g);
        }
        b.group ("OUTPUT", { { "output", "OUTPUT" } });
        b.meter ("IN", MS::inputPeak);
        b.meter ("OUT", MS::outputPeak);
    }
    else if (id == "elite-verb")
    {
        b.group ("", { { "type", "TYPE" }, { "predelay", "PRE-DELAY" }, { "size", "SIZE" }, { "decay", "DECAY" }, { "damping", "DAMPING" },
                       { "lowcut", "LOW CUT" }, { "width", "WIDTH" }, { "mix", "MIX" } });
        b.meter ("IN", MS::inputPeak);
        b.meter ("OUT", MS::outputPeak);
    }
    else if (id == "elite-delay")
    {
        b.group ("", { { "time", "TIME" }, { "feedback", "FEEDBACK" }, { "mix", "MIX" }, { "sync", "TEMPO SYNC" }, { "division", "NOTE" },
                       { "lowcut", "LOW CUT" }, { "highcut", "HIGH CUT" }, { "pingpong", "PING-PONG" }, { "duck", "DUCK" } });
        b.meter ("IN", MS::inputPeak);
        b.meter ("ECHO", MS::value, pg::EliteDelay::delayMs, "ms");
        b.meter ("OUT", MS::outputPeak);
    }
    else if (id == "elite-swirl")
    {
        b.group ("", { { "mode", "MODE" }, { "amount", "AMOUNT" }, { "from", "FROM" }, { "protect", "PROTECT WIDTH" }, { "whistle", "WHISTLE HUNTER" },
                       { "shield", "TRANSIENT SHIELD" }, { "output", "OUTPUT" }, { "listen", "LISTEN (REMOVED ONLY)" } });
        b.meter ("IN", MS::inputPeak);
        b.meter ("COHERENCE", MS::value, pg::EliteSwirl::coherence, "corr");
        b.meter ("SWIRL", MS::value, pg::EliteSwirl::swirlPercent, "%");
        b.meter ("SIDE CUT", MS::reduction, pg::EliteSwirl::removedDb, "dB", 24.0f);
        b.meter ("WHISTLE", MS::value, pg::EliteSwirl::whistleHz, "Hz");
        b.meter ("OUT", MS::outputPeak);
    }
    else if (id == "elite-master")
    {
        b.group ("", { { "character", "CHARACTER" }, { "input", "INPUT" }, { "ceiling", "CEILING" }, { "release", "RELEASE" }, { "target", "TARGET" } });
        b.meter ("LIMIT", MS::reduction, pg::EliteMaster::gr, "dB", 12.0f);
        b.meter ("SHORT-TERM", MS::value, pg::EliteMaster::shortTerm, "LUFS");
        b.meter ("INTEGRATED", MS::value, pg::EliteMaster::integrated, "LUFS");
        b.meter ("TRUE PEAK", MS::value, pg::EliteMaster::truePeak, "dBTP");
        b.meter ("SUGGESTED IN", MS::value, pg::EliteMaster::suggestedInput, "dB");
    }
    else if (id == "elite-transient")
    {
        b.group ("", { { "speed", "SPEED" }, { "attack", "ATTACK" }, { "sustain", "SUSTAIN" }, { "mix", "MIX" }, { "output", "OUTPUT" } });
        b.meter ("IN", MS::inputPeak);
        b.meter ("SHAPE", MS::value, pg::EliteTransient::gainNow, "dB+-", 15.0f);
        b.meter ("OUT", MS::outputPeak);
    }
    else if (id == "elite-multiband")
    {
        b.group ("LOW", { { "low_thr", "LOW THR" }, { "low_ratio", "LOW RATIO" }, { "low_gain", "LOW GAIN" } });
        b.group ("MID", { { "mid_thr", "MID THR" }, { "mid_ratio", "MID RATIO" }, { "mid_gain", "MID GAIN" } });
        b.group ("HIGH", { { "high_thr", "HIGH THR" }, { "high_ratio", "HIGH RATIO" }, { "high_gain", "HIGH GAIN" } });
        b.group ("SPLITS AND TIMING", { { "low_x", "LOW / MID" }, { "high_x", "MID / HIGH" }, { "attack", "ATTACK" }, { "release", "RELEASE" },
                                        { "mix", "MIX" }, { "output", "OUTPUT" } });
        b.meter ("IN", MS::inputPeak);
        b.meter ("LOW GR", MS::reduction, pg::EliteMultiband::lowGr, "dB", 18.0f);
        b.meter ("MID GR", MS::reduction, pg::EliteMultiband::midGr, "dB", 18.0f);
        b.meter ("HIGH GR", MS::reduction, pg::EliteMultiband::highGr, "dB", 18.0f);
        b.meter ("OUT", MS::outputPeak);
    }
    else if (id == "elite-width")
    {
        b.group ("", { { "width", "WIDTH" }, { "mono_below", "MONO BELOW" }, { "output", "OUTPUT" } });
        b.meter ("IN", MS::inputPeak);
        b.meter ("CORRELATION", MS::value, pg::EliteWidth::correlation, "corr");
        b.meter ("SIDE", MS::value, pg::EliteWidth::sideLevel, "%");
        b.meter ("OUT", MS::outputPeak);
    }
    else if (id == "elite-hush")
    {
        b.group ("", { { "amount", "AMOUNT" }, { "reduction", "REDUCTION" }, { "from", "FROM" }, { "output", "OUTPUT" },
                       { "listen", "LISTEN (REMOVED ONLY)" } });
        b.meter ("IN", MS::inputPeak);
        b.meter ("NOISE FLOOR", MS::value, pg::EliteHush::noiseDb, "dBFS");
        b.meter ("REMOVED", MS::reduction, pg::EliteHush::removedDb, "dB", 24.0f);
        b.meter ("OUT", MS::outputPeak);
    }
    else if (id == "elite-de-ess")
    {
        b.group ("", { { "freq", "FREQUENCY" }, { "threshold", "THRESHOLD" }, { "range", "RANGE" }, { "output", "OUTPUT" },
                       { "listen", "LISTEN (ESS ONLY)" } });
        b.meter ("IN", MS::inputPeak);
        b.meter ("ESS LEVEL", MS::value, pg::EliteDeEss::sibilanceDb, "dBFS");
        b.meter ("REDUCTION", MS::reduction, pg::EliteDeEss::gr, "dB", 20.0f);
        b.meter ("OUT", MS::outputPeak);
    }
    else if (id == "elite-doubler")
    {
        b.group ("", { { "amount", "AMOUNT" }, { "width", "WIDTH" }, { "delay", "DELAY" }, { "drift", "DRIFT" }, { "tone", "LOW CUT" },
                       { "output", "OUTPUT" } });
        b.meter ("IN", MS::inputPeak);
        b.meter ("SPREAD", MS::value, pg::EliteDoubler::spreadMs, "ms");
        b.meter ("OUT", MS::outputPeak);
    }
    else if (id == "elite-console")
    {
        b.group ("", { { "character", "CHARACTER" }, { "drive", "DRIVE" }, { "crosstalk", "CROSSTALK" }, { "output", "OUTPUT" } });
        b.meter ("IN", MS::inputPeak);
        b.meter ("SATURATION", MS::reduction, pg::EliteConsole::driveDb, "dB", 12.0f);
        b.meter ("OUT", MS::outputPeak);
    }
    else if (id == "elite-lofi")
    {
        b.group ("", { { "wow", "WOW" }, { "flutter", "FLUTTER" }, { "bits", "BITS" }, { "rate", "SAMPLE RATE" }, { "vinyl", "VINYL" },
                       { "tone", "TONE" }, { "mix", "MIX" }, { "output", "OUTPUT" } });
        b.meter ("IN", MS::inputPeak);
        b.meter ("OUT", MS::outputPeak);
    }
    else if (id == "elite-tune")
    {
        b.group ("", { { "scale", "SCALE" }, { "key", "KEY" }, { "speed", "RETUNE SPEED" }, { "amount", "AMOUNT" }, { "low", "LOWEST NOTE" },
                       { "high", "HIGHEST NOTE" }, { "output", "OUTPUT" } });
        b.meter ("IN", MS::inputPeak);
        b.meter ("PITCH", MS::value, pg::EliteTune::detectedHz, "pitch");
        b.meter ("NOTE", MS::value, pg::EliteTune::targetMidi, "midi");
        b.meter ("CORRECTION", MS::value, pg::EliteTune::correctionCents, "cents", 100.0f);
        b.meter ("OUT", MS::outputPeak);
    }
    else if (id == "elite-balance")
    {
        b.group ("", { { "speed", "SPEED" }, { "amount", "AMOUNT" }, { "tilt", "TILT" }, { "range", "RANGE" }, { "output", "OUTPUT" },
                       { "freeze", "FREEZE" } });
        b.meter ("IN", MS::inputPeak);
        for (int band = 0; band < pg::EliteBalance::kBands; ++band)
        {
            const int hz = (int) pg::EliteBalance::bandHz (band);
            b.meter (hz < 1000 ? std::to_string (hz) : std::to_string (hz / 1000) + "K", MS::value, band, "dB+-", 12.0f);
        }
        b.meter ("OUT", MS::outputPeak);
    }
    else
    {
        for (const auto& prm : plugin.parameters().all())
            if (prm->id() != "bypass") b.add (prm->id(), upper (prm->spec().name));
        b.meter ("IN", MS::inputPeak);
        b.meter ("OUT", MS::outputPeak);
    }
    if (b.map.spec.groups.size() == 1 && b.map.spec.groups[0].name.empty()) b.map.spec.groups.clear();   // one section holding everything
    return std::move (b.map);
}

float meterValue (const pg::Plugin& p, const PanelMap::Meter& m)
{
    switch (m.source)
    {
        case MS::inputPeak:  return pg::params::gainToDb (p.getInputPeak());
        case MS::outputPeak: return pg::params::gainToDb (p.getOutputPeak());
        case MS::reduction:  return -std::abs (p.getReadout (m.readout));
        case MS::level:      return p.getReadout (m.readout);
        case MS::value:      return p.getReadout (m.readout);
    }
    return 0.0f;
}
} // namespace hem::app
