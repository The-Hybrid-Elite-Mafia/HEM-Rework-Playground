// =============================================================================
//  The 0.10.0 factory presets, carried over unchanged ONLY so the app is not empty until the new
//  presets (built from the engineer research, Phase D/E) replace them. Not part of the finished work.
//  Two names were changed to drop borrowed wording: "Mix Bus: British Glue" -> "Mix Bus: Iron Glue",
//  "Drums: American Punch" -> "Drums: Hard Punch".
//  A legacy preset sets every unlisted control to its 0.10.0 default (kept below), so it still sounds
//  exactly as before even though the plug-ins now start at zero.
// =============================================================================
#include "Presets.hpp"

namespace hem::pg::legacy
{
namespace
{
    using P = FactoryPreset;

    std::vector<FactoryPreset> voxPresets()
    {
        return {
            P { "Suno Kick Punch",          { { "mode", 1 }, { "squeeze", 9 }, { "gate", -45 } } },
            P { "Suno Snare Crack",         { { "mode", 1 }, { "squeeze", 8 }, { "gate", -42 } } },
            P { "Suno Lead Vocal Even",     { { "mode", 0 }, { "squeeze", 12 }, { "gate", -60 } } },
            P { "Suno Backing Vocals Tuck", { { "mode", 0 }, { "squeeze", 15 }, { "output", -3 } } },
            P { "808 Steady",               { { "mode", 2 }, { "squeeze", 10 } } },
            P { "Drum Bus Squeeze",         { { "mode", 3 }, { "squeeze", 6 } } } };
    }

    std::vector<FactoryPreset> eqPresets()
    {
        return {
            P { "Suno Kick: Weight + Click", { { "b1_type", 0 }, { "b1_freq", 30 }, { "b2_freq", 60 }, { "b2_gain", 3 }, { "b2_q", 1.0f },
                                               { "b3_freq", 350 }, { "b3_gain", -4 }, { "b3_q", 1.2f }, { "b5_freq", 3500 }, { "b5_gain", 3 },
                                               { "b6_freq", 10000 }, { "b6_gain", -2 } } },
            P { "Suno Snare: Body + Crack",  { { "b1_type", 0 }, { "b1_freq", 80 }, { "b2_freq", 200 }, { "b2_gain", 2.5f }, { "b2_q", 1.2f },
                                               { "b3_freq", 500 }, { "b3_gain", -3 }, { "b5_freq", 5000 }, { "b5_gain", 3 }, { "b6_freq", 12000 }, { "b6_gain", 1.5f } } },
            P { "Suno Hats: Tame Harsh",     { { "b1_type", 0 }, { "b1_freq", 300 }, { "b5_freq", 7000 }, { "b5_gain", -3 }, { "b5_q", 2.0f },
                                               { "b6_freq", 12000 }, { "b6_gain", 1 } } },
            P { "808: Clean Sub",            { { "b1_type", 0 }, { "b1_freq", 25 }, { "b2_freq", 55 }, { "b2_gain", 2 }, { "b3_freq", 250 }, { "b3_gain", -3 },
                                               { "b6_type", 1 }, { "b6_freq", 6000 } } },
            P { "Vocal: Clarity",            { { "b1_type", 0 }, { "b1_freq", 90 }, { "b3_freq", 300 }, { "b3_gain", -2.5f }, { "b4_freq", 3000 }, { "b4_gain", 2 },
                                               { "b4_q", 0.9f }, { "b6_freq", 12000 }, { "b6_gain", 2.5f } } },
            P { "Vocal: De-Mud Robot",       { { "b1_type", 0 }, { "b1_freq", 80 }, { "b2_freq", 180 }, { "b2_gain", -2 }, { "b3_freq", 450 }, { "b3_gain", -3 },
                                               { "b3_q", 1.4f }, { "b4_freq", 2500 }, { "b4_gain", 1.5f }, { "b5_freq", 6000 }, { "b5_gain", -2 }, { "b5_q", 2.0f } } },
            P { "Mix Bus: Smile",            { { "b1_freq", 80 }, { "b1_gain", 1.5f }, { "b3_freq", 400 }, { "b3_gain", -1 }, { "b6_freq", 10000 }, { "b6_gain", 1.5f } } } };
    }

    std::vector<FactoryPreset> compPresets()
    {
        return {
            P { "Drum Bus Glue",              { { "threshold", -18 }, { "ratio", 4 }, { "attack", 10 }, { "release", 100 }, { "mode", 1 }, { "gain", 2 } } },
            P { "Drum Bus Smash (Parallel)",  { { "threshold", -35 }, { "ratio", 10 }, { "attack", 1 }, { "release", 80 }, { "mode", 1 }, { "gain", 8 }, { "mix", 35 } } },
            P { "Vocal Smooth",               { { "threshold", -24 }, { "ratio", 3 }, { "attack", 5 }, { "release", 120 }, { "mode", 2 }, { "gain", 4 } } },
            P { "808 Control",                { { "threshold", -18 }, { "ratio", 4 }, { "attack", 20 }, { "release", 150 }, { "mode", 2 }, { "gain", 3 } } },
            P { "Mix Bus 2 dB",               { { "threshold", -12 }, { "ratio", 2 }, { "attack", 30 }, { "release", 200 }, { "mode", 0 }, { "gain", 1 } } } };
    }

    std::vector<FactoryPreset> bassPresets()
    {
        return {
            P { "808 Big",             { { "freq", 60 }, { "intensity", 60 }, { "output", -1 } } },
            P { "Small Speaker Boost", { { "freq", 70 }, { "intensity", 80 }, { "original", -6 } } },
            P { "Sub Tighten",         { { "freq", 45 }, { "intensity", 35 }, { "original", -3 } } },
            P { "Kick Weight",         { { "freq", 55 }, { "intensity", 40 } } } };
    }

    std::vector<FactoryPreset> channelPresets()
    {
        return {
            P { "Snare Cleanup", { { "gate_on", 1 }, { "gate_thr", -40 }, { "gate_floor", 25 }, { "gate_rel", 120 }, { "eq2_freq", 200 }, { "eq2_gain", 2 },
                                   { "eq3_freq", 5000 }, { "eq3_gain", 3 }, { "comp_thr", -18 }, { "comp_ratio", 4 }, { "comp_atk", 8 }, { "comp_rel", 120 } } },
            P { "Hats Tidy",     { { "gate_on", 1 }, { "gate_thr", -45 }, { "gate_floor", 15 }, { "eq1_freq", 300 }, { "eq1_gain", -12 },
                                   { "eq3_freq", 7000 }, { "eq3_gain", -2 }, { "comp_on", 0 } } },
            P { "Vocal Channel", { { "eq1_freq", 100 }, { "eq1_gain", -3 }, { "eq2_freq", 300 }, { "eq2_gain", -2 }, { "eq3_freq", 3000 }, { "eq3_gain", 2 },
                                   { "eq4_freq", 12000 }, { "eq4_gain", 2 }, { "comp_thr", -22 }, { "comp_ratio", 3 }, { "comp_mode", 1 } } },
            P { "Kick Channel",  { { "gate_on", 1 }, { "gate_thr", -35 }, { "gate_floor", 30 }, { "eq1_freq", 60 }, { "eq1_gain", 3 }, { "eq2_freq", 350 },
                                   { "eq2_gain", -4 }, { "eq3_freq", 4000 }, { "eq3_gain", 3 }, { "comp_thr", -16 }, { "comp_ratio", 4 }, { "comp_atk", 15 } } } };
    }

    std::vector<FactoryPreset> verbPresets()
    {
        return {
            P { "Drum Room (send)",     { { "type", 0 }, { "predelay", 5 }, { "size", 40 }, { "decay", 0.7f }, { "damping", 50 }, { "lowcut", 200 }, { "mix", 100 } } },
            P { "Vocal Plate (send)",   { { "type", 1 }, { "predelay", 30 }, { "size", 55 }, { "decay", 1.8f }, { "damping", 45 }, { "lowcut", 180 }, { "mix", 100 } } },
            P { "Big Hall (send)",      { { "type", 2 }, { "predelay", 40 }, { "size", 75 }, { "decay", 3.5f }, { "damping", 40 }, { "lowcut", 150 }, { "mix", 100 } } },
            P { "Snare Plate (insert)", { { "type", 1 }, { "predelay", 10 }, { "size", 45 }, { "decay", 1.2f }, { "mix", 18 } } },
            P { "Vocal Room (insert)",  { { "type", 0 }, { "predelay", 15 }, { "size", 50 }, { "decay", 0.9f }, { "mix", 15 } } } };
    }

    std::vector<FactoryPreset> delayPresets()
    {
        return {
            P { "1/4 Vocal Throw (send)", { { "sync", 1 }, { "division", 8 }, { "feedback", 30 }, { "lowcut", 200 }, { "highcut", 6000 }, { "mix", 100 }, { "duck", 50 } } },
            P { "1/8 Ping-Pong (send)",   { { "sync", 1 }, { "division", 5 }, { "feedback", 35 }, { "pingpong", 100 }, { "mix", 100 } } },
            P { "Slap Back (insert)",     { { "sync", 0 }, { "time", 110 }, { "feedback", 5 }, { "lowcut", 150 }, { "highcut", 5000 }, { "mix", 20 } } },
            P { "1/8 Dotted (insert)",    { { "sync", 1 }, { "division", 6 }, { "feedback", 40 }, { "mix", 20 }, { "duck", 30 } } },
            P { "Trap Echo Out",          { { "sync", 1 }, { "division", 8 }, { "feedback", 60 }, { "highcut", 4000 }, { "mix", 30 } } } };
    }

    std::vector<FactoryPreset> masterPresets()
    {
        return {
            P { "Streaming Safe (-14)", { { "input", 4 }, { "ceiling", -1 }, { "release", 80 }, { "character", 0 }, { "target", 0 } } },
            P { "Loud Trap (-9)",       { { "input", 10 }, { "ceiling", -1 }, { "release", 60 }, { "character", 1 }, { "target", 2 } } },
            P { "Warm Country",         { { "input", 6 }, { "ceiling", -1 }, { "release", 120 }, { "character", 2 }, { "target", 0 } } },
            P { "Club Banger (-7)",     { { "input", 13 }, { "ceiling", -0.5f }, { "release", 40 }, { "character", 1 }, { "target", 3 } } } };
    }

    std::vector<FactoryPreset> swirlPresets()
    {
        return {
            P { "Suno Swirl - Gentle",       { { "amount", 40 }, { "from", 4000 }, { "protect", 50 }, { "mode", 1 } } },
            P { "Suno Swirl - Strong",       { { "amount", 85 }, { "from", 3000 }, { "protect", 30 }, { "mode", 1 } } },
            P { "Cymbal Shimmer Wash",       { { "amount", 70 }, { "from", 6000 }, { "protect", 35 }, { "mode", 0 } } },
            P { "Vocal: Underwater Fix",     { { "amount", 60 }, { "from", 2500 }, { "protect", 45 }, { "mode", 1 } } },
            P { "Master Polish (subtle)",    { { "amount", 30 }, { "from", 5000 }, { "protect", 60 }, { "mode", 1 } } },
            P { "Hear What It Removes",      { { "amount", 70 }, { "from", 4000 }, { "protect", 40 }, { "mode", 1 }, { "listen", 1 } } },
            // the five "restoration recipes"
            P { "Recipe: Vocal Air Safe",    { { "amount", 45 }, { "from", 7500 }, { "protect", 55 }, { "mode", 1 }, { "shield", 85 } } },
            P { "Recipe: Heavy De-Shimmer",  { { "amount", 85 }, { "from", 5000 }, { "protect", 30 }, { "mode", 0 }, { "shield", 60 } } },
            P { "Recipe: Whistle Killer",    { { "amount", 35 }, { "from", 8000 }, { "protect", 50 }, { "mode", 1 }, { "whistle", 100 }, { "shield", 70 } } },
            P { "Recipe: Gentle Polish",     { { "amount", 25 }, { "from", 6000 }, { "protect", 65 }, { "mode", 1 }, { "whistle", 40 }, { "shield", 80 } } },
            P { "Recipe: Extreme Purge",     { { "amount", 100 }, { "from", 2500 }, { "protect", 15 }, { "mode", 0 }, { "whistle", 100 }, { "shield", 30 } } } };
    }

    std::vector<FactoryPreset> transientPresets()
    {
        return {
            P { "Kick: More Punch",          { { "attack", 60 }, { "sustain", -10 }, { "speed", 0 } } },
            P { "Snare: Crack + Short",      { { "attack", 50 }, { "sustain", -40 }, { "speed", 0 } } },
            P { "Drum Bus: Tighten Room",    { { "attack", 15 }, { "sustain", -50 }, { "speed", 1 } } },
            P { "Suno Drums: Bring Back Hit",{ { "attack", 70 }, { "sustain", -15 }, { "speed", 1 }, { "mix", 80 } } },
            P { "808: Longer Tail",          { { "attack", 0 }, { "sustain", 45 }, { "speed", 2 } } },
            P { "Soften Harsh Hits",         { { "attack", -50 }, { "sustain", 0 }, { "speed", 0 } } } };
    }

    std::vector<FactoryPreset> multibandPresets()
    {
        return {
            P { "Mix Bus: Gentle Glue",      { { "low_thr", -20 }, { "low_ratio", 2 }, { "mid_thr", -22 }, { "mid_ratio", 1.8f },
                                               { "high_thr", -24 }, { "high_ratio", 2 }, { "attack", 20 }, { "release", 200 } } },
            P { "Tame Boomy Low End",        { { "low_x", 150 }, { "low_thr", -26 }, { "low_ratio", 4 }, { "attack", 25 }, { "release", 180 } } },
            P { "Tame Harsh Highs",          { { "high_x", 4000 }, { "high_thr", -30 }, { "high_ratio", 4 }, { "attack", 3 }, { "release", 80 } } },
            P { "Suno Master: Even Out",     { { "low_x", 160 }, { "high_x", 5000 }, { "low_thr", -22 }, { "low_ratio", 2.5f },
                                               { "mid_thr", -24 }, { "mid_ratio", 2 }, { "high_thr", -28 }, { "high_ratio", 3 },
                                               { "attack", 15 }, { "release", 150 } } },
            P { "Vocal: Control Mids",       { { "low_x", 250 }, { "high_x", 3000 }, { "mid_thr", -28 }, { "mid_ratio", 3 }, { "attack", 8 }, { "release", 120 } } } };
    }

    std::vector<FactoryPreset> widthPresets()
    {
        return {
            P { "Master: Wide + Mono Bass",  { { "width", 120 }, { "mono_below", 120 } } },
            P { "Mono Bass Only",            { { "width", 100 }, { "mono_below", 150 } } },
            P { "Pads: Extra Wide",          { { "width", 170 }, { "mono_below", 200 } } },
            P { "Narrow (Focus Centre)",     { { "width", 60 }, { "mono_below", 20 } } },
            P { "Mono Check",                { { "width", 0 }, { "mono_below", 20 } } } };
    }

    std::vector<FactoryPreset> hushPresets()
    {
        return {
            P { "Light Hiss Clean-up",       { { "amount", 35 }, { "reduction", 8 }, { "from", 2000 } } },
            P { "Suno Noise Floor",          { { "amount", 50 }, { "reduction", 12 } } },
            P { "Heavy Room Noise",          { { "amount", 80 }, { "reduction", 20 } } },
            P { "Vocal Take: Air Hiss",      { { "amount", 45 }, { "reduction", 10 }, { "from", 4000 } } },
            P { "Hear What It Removes",      { { "amount", 50 }, { "reduction", 12 }, { "listen", 1 } } } };
    }

    std::vector<FactoryPreset> deEssPresets()
    {
        return {
            P { "Male Vocal",                { { "freq", 5500 }, { "threshold", -30 }, { "range", 8 } } },
            P { "Female Vocal",              { { "freq", 7500 }, { "threshold", -30 }, { "range", 8 } } },
            P { "Suno Vocal: Harsh Esses",   { { "freq", 6500 }, { "threshold", -36 }, { "range", 12 } } },
            P { "Hi-Hats: Soften",           { { "freq", 9000 }, { "threshold", -28 }, { "range", 6 } } },
            P { "Hear The Esses",            { { "freq", 6500 }, { "threshold", -36 }, { "range", 12 }, { "listen", 1 } } } };
    }

    std::vector<FactoryPreset> doublerPresets()
    {
        return {
            P { "Lead Vocal: Subtle Double", { { "amount", 35 }, { "width", 70 }, { "delay", 14 }, { "drift", 30 } } },
            P { "Hook: Big Wide Double",     { { "amount", 60 }, { "width", 100 }, { "delay", 20 }, { "drift", 50 } } },
            P { "Ad-Libs: Spread",           { { "amount", 50 }, { "width", 100 }, { "delay", 28 }, { "drift", 60 }, { "tone", 300 } } },
            P { "Guitar: Fake Double-Track", { { "amount", 70 }, { "width", 100 }, { "delay", 24 }, { "drift", 40 }, { "tone", 120 } } } };
    }

    std::vector<FactoryPreset> consolePresets()
    {
        return {
            P { "Mix Bus: Iron Glue",     { { "drive", 6 }, { "character", 1 }, { "crosstalk", 30 } } },
            P { "Drums: Hard Punch",     { { "drive", 9 }, { "character", 2 }, { "crosstalk", 20 } } },
            P { "Vocal: Tube Warmth",        { { "drive", 7 }, { "character", 3 }, { "crosstalk", 0 } } },
            P { "Suno Master: Analog Feel",  { { "drive", 4 }, { "character", 1 }, { "crosstalk", 40 } } },
            P { "Clean Desk (Drive Only)",   { { "drive", 8 }, { "character", 0 }, { "crosstalk", 0 } } } };
    }

    std::vector<FactoryPreset> lofiPresets()
    {
        return {
            P { "Dusty Sample",              { { "wow", 25 }, { "flutter", 20 }, { "bits", 12 }, { "rate", 22050 }, { "vinyl", 35 }, { "tone", 8000 } } },
            P { "Old Cassette",              { { "wow", 55 }, { "flutter", 40 }, { "vinyl", 15 }, { "tone", 7000 } } },
            P { "SP Sampler Grit",           { { "wow", 0 }, { "flutter", 0 }, { "bits", 12 }, { "rate", 26040 }, { "vinyl", 0 }, { "tone", 12000 } } },
            P { "Vinyl Only",                { { "wow", 10 }, { "flutter", 5 }, { "vinyl", 50 }, { "tone", 14000 } } },
            P { "Parallel Lo-Fi (40 %)",     { { "wow", 40 }, { "flutter", 30 }, { "bits", 10 }, { "rate", 16000 }, { "vinyl", 30 }, { "tone", 6000 }, { "mix", 40 } } } };
    }

    std::vector<FactoryPreset> tunePresets()
    {
        return {
            P { "Natural Vocal",             { { "speed", 60 }, { "amount", 80 } } },
            P { "Tight Pop",                 { { "speed", 20 }, { "amount", 100 } } },
            P { "Hard Snap (Robot)",         { { "speed", 0 }, { "amount", 100 } } },
            P { "Low Voice (Rap / Male)",    { { "speed", 30 }, { "low", 60 }, { "high", 500 } } },
            P { "High Voice (Female)",       { { "speed", 30 }, { "low", 150 }, { "high", 1200 } } } };
    }

    std::vector<FactoryPreset> balancePresets()
    {
        return {
            P { "Master: Gentle Balance",    { { "amount", 35 }, { "tilt", -1.5f }, { "range", 4 }, { "speed", 0 } } },
            P { "Suno Mix: Even It Out",     { { "amount", 60 }, { "tilt", -1.5f }, { "range", 6 }, { "speed", 1 } } },
            P { "Darker, Warmer Target",     { { "amount", 50 }, { "tilt", -3 }, { "range", 6 }, { "speed", 1 } } },
            P { "Brighter Target",           { { "amount", 50 }, { "tilt", 0 }, { "range", 6 }, { "speed", 1 } } },
            P { "Vocal Bus: Fast Fix",       { { "amount", 45 }, { "tilt", -1 }, { "range", 5 }, { "speed", 2 } } } };
    }
}

const std::vector<FactoryPreset>& factory (std::string_view id)
{
    static const std::vector<FactoryPreset> vox = voxPresets(), eq = eqPresets(), comp = compPresets(), bass = bassPresets(),
                                            channel = channelPresets(), verb = verbPresets(), delay = delayPresets(), master = masterPresets(),
                                            swirl = swirlPresets(), transient = transientPresets(), multiband = multibandPresets(),
                                            width = widthPresets(), hush = hushPresets(), deEss = deEssPresets(), doubler = doublerPresets(),
                                            console = consolePresets(), lofi = lofiPresets(), tune = tunePresets(), balance = balancePresets(), none;
    if (id == "elite-vox") return vox;
    if (id == "elite-eq") return eq;
    if (id == "elite-comp") return comp;
    if (id == "elite-bass") return bass;
    if (id == "elite-channel") return channel;
    if (id == "elite-verb") return verb;
    if (id == "elite-delay") return delay;
    if (id == "elite-master") return master;
    if (id == "elite-swirl") return swirl;
    if (id == "elite-transient") return transient;
    if (id == "elite-multiband") return multiband;
    if (id == "elite-width") return width;
    if (id == "elite-hush") return hush;
    if (id == "elite-de-ess") return deEss;
    if (id == "elite-doubler") return doubler;
    if (id == "elite-console") return console;
    if (id == "elite-lofi") return lofi;
    if (id == "elite-tune") return tune;
    if (id == "elite-balance") return balance;
    return none;
}

const std::vector<std::pair<std::string, float>>& defaults0100 (std::string_view id)
{
    // every control's exact 0.10.0 starting value (from the old engine), by plug-in
    static const std::vector<std::pair<std::string, std::vector<std::pair<std::string, float>>>> table {
        { "elite-vox", { { "gate", -0x1.4p+6f }, { "squeeze", 0x0p+0f }, { "output", 0x0p+0f }, { "mode", 0x0p+0f }, { "bypass", 0x0p+0f } } },
        { "elite-eq", { { "b1_on", 0x1p+0f }, { "b1_type", 0x1p+0f }, { "b1_freq", 0x1.ep+4f }, { "b1_gain", 0x0p+0f }, { "b1_q", 0x1.999998p-1f }, { "b2_on", 0x1p+0f }, { "b2_type", 0x0p+0f }, { "b2_freq", 0x1.dffffep+6f }, { "b2_gain", 0x0p+0f }, { "b2_q", 0x1.999998p-1f }, { "b3_on", 0x1p+0f }, { "b3_type", 0x0p+0f }, { "b3_freq", 0x1.9p+8f }, { "b3_gain", 0x0p+0f }, { "b3_q", 0x1.999998p-1f }, { "b4_on", 0x1p+0f }, { "b4_type", 0x0p+0f }, { "b4_freq", 0x1.770004p+10f }, { "b4_gain", 0x0p+0f }, { "b4_q", 0x1.999998p-1f }, { "b5_on", 0x1p+0f }, { "b5_type", 0x0p+0f }, { "b5_freq", 0x1.f3fffcp+11f }, { "b5_gain", 0x0p+0f }, { "b5_q", 0x1.999998p-1f }, { "b6_on", 0x1p+0f }, { "b6_type", 0x0p+0f }, { "b6_freq", 0x1.387ffep+13f }, { "b6_gain", 0x0p+0f }, { "b6_q", 0x1.999998p-1f }, { "output", 0x0p+0f }, { "bypass", 0x0p+0f } } },
        { "elite-comp", { { "threshold", -0x1.2p+4f }, { "ratio", 0x1p+2f }, { "attack", 0x1.400008p+3f }, { "release", 0x1.2bfffep+7f }, { "arc", 0x1p+0f }, { "mode", 0x0p+0f }, { "gain", 0x0p+0f }, { "mix", 0x1.9p+6f }, { "bypass", 0x0p+0f } } },
        { "elite-bass", { { "freq", 0x1.ep+5f }, { "intensity", 0x1.9p+5f }, { "original", 0x0p+0f }, { "output", 0x0p+0f }, { "solo", 0x0p+0f }, { "bypass", 0x0p+0f } } },
        { "elite-channel", { { "gate_on", 0x0p+0f }, { "gate_thr", -0x1.9p+5f }, { "gate_floor", 0x1.4p+5f }, { "gate_rel", 0x1.9p+6f }, { "eq_on", 0x1p+0f }, { "eq1_freq", 0x1.4p+6f }, { "eq1_gain", 0x0p+0f }, { "eq1_q", 0x1.666664p-1f }, { "eq2_freq", 0x1.2c0002p+8f }, { "eq2_gain", 0x0p+0f }, { "eq2_q", 0x1.fffffep-1f }, { "eq3_freq", 0x1.388002p+11f }, { "eq3_gain", 0x0p+0f }, { "eq3_q", 0x1.fffffep-1f }, { "eq4_freq", 0x1.388p+13f }, { "eq4_gain", 0x0p+0f }, { "eq4_q", 0x1.666664p-1f }, { "comp_on", 0x1p+0f }, { "comp_thr", -0x1.4p+4f }, { "comp_ratio", 0x1.800002p+1f }, { "comp_atk", 0x1.400004p+3f }, { "comp_rel", 0x1.2cp+7f }, { "comp_mode", 0x0p+0f }, { "output", 0x0p+0f }, { "phase", 0x0p+0f }, { "order", 0x0p+0f }, { "bypass", 0x0p+0f } } },
        { "elite-verb", { { "type", 0x1p+0f }, { "predelay", 0x1.3ffffep+4f }, { "size", 0x1.9p+5f }, { "decay", 0x1.ccccccp+0f }, { "damping", 0x1.68p+5f }, { "lowcut", 0x1.2cp+7f }, { "width", 0x1.9p+6f }, { "mix", 0x1.9p+4f }, { "bypass", 0x0p+0f } } },
        { "elite-delay", { { "time", 0x1.76fffep+8f }, { "sync", 0x1p+0f }, { "division", 0x1p+3f }, { "feedback", 0x1.e00002p+4f }, { "lowcut", 0x1.2cp+7f }, { "highcut", 0x1.f4p+12f }, { "pingpong", 0x0p+0f }, { "mix", 0x1.9p+4f }, { "duck", 0x0p+0f }, { "bypass", 0x0p+0f } } },
        { "elite-swirl", { { "amount", 0x1.9p+5f }, { "from", 0x1.f4p+11f }, { "protect", 0x1.4p+5f }, { "mode", 0x1p+0f }, { "listen", 0x0p+0f }, { "output", 0x0p+0f }, { "whistle", 0x0p+0f }, { "shield", 0x1.e00002p+5f }, { "bypass", 0x0p+0f } } },
        { "elite-master", { { "input", 0x0p+0f }, { "ceiling", -0x1p+0f }, { "release", 0x1.4p+6f }, { "character", 0x0p+0f }, { "target", 0x0p+0f }, { "bypass", 0x0p+0f } } },
        { "elite-transient", { { "attack", 0x0p+0f }, { "sustain", 0x0p+0f }, { "speed", 0x1p+0f }, { "output", 0x0p+0f }, { "mix", 0x1.9p+6f }, { "bypass", 0x0p+0f } } },
        { "elite-multiband", { { "low_x", 0x1.68p+7f }, { "high_x", 0x1.b58p+11f }, { "low_thr", 0x0p+0f }, { "low_ratio", 0x1p+1f }, { "low_gain", 0x0p+0f }, { "mid_thr", 0x0p+0f }, { "mid_ratio", 0x1p+1f }, { "mid_gain", 0x0p+0f }, { "high_thr", 0x0p+0f }, { "high_ratio", 0x1p+1f }, { "high_gain", 0x0p+0f }, { "attack", 0x1.e00002p+3f }, { "release", 0x1.2cp+7f }, { "mix", 0x1.9p+6f }, { "output", 0x0p+0f }, { "bypass", 0x0p+0f } } },
        { "elite-width", { { "width", 0x1.9p+6f }, { "mono_below", 0x1.e00002p+6f }, { "output", 0x0p+0f }, { "bypass", 0x0p+0f } } },
        { "elite-hush", { { "amount", 0x1.9p+5f }, { "reduction", 0x1.8p+3f }, { "from", 0x1.4p+4f }, { "listen", 0x0p+0f }, { "output", 0x0p+0f }, { "bypass", 0x0p+0f } } },
        { "elite-de-ess", { { "freq", 0x1.964p+12f }, { "threshold", -0x1.ep+4f }, { "range", 0x1.4p+3f }, { "listen", 0x0p+0f }, { "output", 0x0p+0f }, { "bypass", 0x0p+0f } } },
        { "elite-doubler", { { "amount", 0x1.9p+5f }, { "width", 0x1.9p+6f }, { "delay", 0x1.2p+4f }, { "drift", 0x1.4p+5f }, { "tone", 0x1.2cp+7f }, { "output", 0x0p+0f }, { "bypass", 0x0p+0f } } },
        { "elite-console", { { "drive", 0x1.8p+2f }, { "character", 0x1p+0f }, { "crosstalk", 0x1.e00002p+4f }, { "output", 0x0p+0f }, { "bypass", 0x0p+0f } } },
        { "elite-lofi", { { "wow", 0x1.4p+4f }, { "flutter", 0x1.e00002p+3f }, { "bits", 0x1p+4f }, { "rate", 0x1.77p+15f }, { "vinyl", 0x1.4p+4f }, { "tone", 0x1.76fffep+13f }, { "mix", 0x1.9p+6f }, { "output", 0x0p+0f }, { "bypass", 0x0p+0f } } },
        { "elite-tune", { { "key", 0x0p+0f }, { "scale", 0x0p+0f }, { "speed", 0x1.9p+4f }, { "amount", 0x1.9p+6f }, { "low", 0x1.4p+6f }, { "high", 0x1.f3fffcp+9f }, { "output", 0x0p+0f }, { "bypass", 0x0p+0f } } },
        { "elite-balance", { { "amount", 0x1.9p+5f }, { "tilt", -0x1.8p+0f }, { "range", 0x1.8p+2f }, { "speed", 0x1p+0f }, { "freeze", 0x0p+0f }, { "output", 0x0p+0f }, { "bypass", 0x0p+0f } } },
    };
    static const std::vector<std::pair<std::string, float>> none;
    for (auto& [pid, values] : table) if (pid == id) return values;
    return none;
}
} // namespace hem::pg::legacy
