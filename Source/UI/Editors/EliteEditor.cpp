#include "EliteEditor.h"
#include "SwirlEditor.h"
#include "EqEditor.h"
#include "PlaygroundBinaryData.h"

namespace pg::ui
{
namespace
{
    using K = EditorSpec::Knob;
    using M = EditorSpec::Meter;

    constexpr int kSaveId = 9001, kDeleteId = 9002, kResetId = 9003, kUserBase = 501;
    const juce::Colour kAmber { 0xffffb02e };

    EditorSpec autoSpec (ElitePlugin& p)
    {
        EditorSpec s;
        std::vector<K> row;
        for (auto* prm : p.getParameters())
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm);
            if (rp == nullptr || rp->getParameterID() == "bypass") continue;
            if (dynamic_cast<juce::AudioParameterBool*> (rp) != nullptr)        s.toggles.push_back ({ rp->getParameterID(), rp->getName (24).toUpperCase() });
            else if (dynamic_cast<juce::AudioParameterChoice*> (rp) != nullptr) s.choices.push_back ({ rp->getParameterID(), rp->getName (24).toUpperCase() });
            else
            {
                row.push_back ({ rp->getParameterID(), rp->getName (24).toUpperCase() });
                if (row.size() == 8) { s.knobRows.push_back (row); row.clear(); }
            }
        }
        if (! row.empty()) s.knobRows.push_back (row);
        s.meters = { { "IN", M::inputPeak }, { "OUT", M::outputPeak } };
        s.height = 420 + 170 * (int) s.knobRows.size();
        return s;
    }
}

EditorSpec EditorSpec::forPlugin (ElitePlugin& p)
{
    const juce::String id (p.getInfo().id);
    EditorSpec s;
    if (id == "elite-vox")
    {
        s.tabsParam = "mode";
        s.knobRows = { { K { "gate", "GATE" }, K { "squeeze", "SQUEEZE", true }, K { "output", "OUTPUT" } } };
        s.meters = { { "IN", M::inputPeak }, { "GATE", M::reduction, EliteVox::gateGr, 40.0f },
                     { "SQUEEZE", M::reduction, EliteVox::compGr, 24.0f }, { "LIMIT", M::reduction, EliteVox::limitGr, 12.0f },
                     { "OUT", M::outputPeak } };
        s.width = 980; s.height = 640;
    }
    else if (id == "elite-comp")
    {
        s.tabsParam = "mode";
        s.knobRows = { { K { "threshold", "THRESHOLD", true }, K { "ratio", "RATIO" }, K { "attack", "ATTACK" }, K { "release", "RELEASE" } },
                       { K { "gain", "GAIN" }, K { "mix", "MIX" } } };
        s.toggles = { { "arc", "AUTO RELEASE" } };
        s.meters = { { "IN", M::inputPeak }, { "GAIN REDUCTION", M::reduction, EliteComp::gr, 24.0f }, { "OUT", M::outputPeak } };
        s.width = 980; s.height = 800;
    }
    else if (id == "elite-bass")
    {
        s.knobRows = { { K { "freq", "FREQUENCY", true }, K { "intensity", "INTENSITY", true }, K { "original", "ORIGINAL BASS" }, K { "output", "OUTPUT" } } };
        s.toggles = { { "solo", "HARMONICS ONLY" } };
        s.meters = { { "IN", M::inputPeak }, { "ORIGINAL", M::level, EliteBass::originalLevel }, { "HARMONICS", M::level, EliteBass::harmonicsLevel },
                     { "OUT", M::outputPeak } };
        s.width = 980; s.height = 600;
    }
    else if (id == "elite-channel")
    {
        s.knobRows = { { K { "gate_thr", "GATE THR" }, K { "gate_floor", "FLOOR" }, K { "gate_rel", "GATE REL" },
                         K { "comp_thr", "COMP THR" }, K { "comp_ratio", "RATIO" }, K { "comp_atk", "ATTACK" }, K { "comp_rel", "RELEASE" }, K { "output", "OUTPUT" } },
                       { K { "eq1_freq", "LOW HZ" }, K { "eq1_gain", "LOW" }, K { "eq2_freq", "LO-MID HZ" }, K { "eq2_gain", "LO-MID" }, K { "eq2_q", "LO-MID Q" },
                         K { "eq3_freq", "HI-MID HZ" }, K { "eq3_gain", "HI-MID" }, K { "eq3_q", "HI-MID Q" }, K { "eq4_freq", "HIGH HZ" }, K { "eq4_gain", "HIGH" } } };
        s.toggles = { { "gate_on", "GATE" }, { "eq_on", "EQ" }, { "comp_on", "COMP" }, { "phase", "PHASE INVERT" } };
        s.choices = { { "comp_mode", "COMP MODE" }, { "order", "ORDER" } };
        s.meters = { { "IN", M::inputPeak }, { "GATE", M::reduction, EliteChannel::gateGr, 40.0f },
                     { "COMP", M::reduction, EliteChannel::compGr, 24.0f }, { "OUT", M::outputPeak } };
        s.width = 1180; s.height = 820;
    }
    else if (id == "elite-eq")
    {
        std::vector<K> freq, gain, q;
        for (int b = 0; b < EliteEq::kBands; ++b)
        {
            const auto n = juce::String (b + 1);
            freq.push_back ({ EliteEq::id (b, "freq"), "FREQ " + n });
            gain.push_back ({ EliteEq::id (b, "gain"), "GAIN " + n });
            q.push_back ({ EliteEq::id (b, "q"), "Q " + n });
            s.toggles.push_back ({ EliteEq::id (b, "on"), "BAND " + n });
            s.choices.push_back ({ EliteEq::id (b, "type"), "TYPE " + n });
        }
        gain.push_back ({ "output", "OUTPUT" });
        s.knobRows = { freq, gain, q };
        s.meters = { { "IN", M::inputPeak }, { "OUT", M::outputPeak } };
        s.width = 1180; s.height = 1120;   // room for the analyzer + curve and the two switch rows
    }
    else if (id == "elite-verb")
    {
        s.tabsParam = "type";
        s.knobRows = { { K { "predelay", "PRE-DELAY" }, K { "size", "SIZE" }, K { "decay", "DECAY", true }, K { "damping", "DAMPING" } },
                       { K { "lowcut", "LOW CUT" }, K { "width", "WIDTH" }, K { "mix", "MIX" } } };
        s.meters = { { "IN", M::inputPeak }, { "OUT", M::outputPeak } };
        s.width = 980; s.height = 800;
    }
    else if (id == "elite-delay")
    {
        s.knobRows = { { K { "time", "TIME" }, K { "feedback", "FEEDBACK", true }, K { "mix", "MIX" } },
                       { K { "lowcut", "LOW CUT" }, K { "highcut", "HIGH CUT" }, K { "pingpong", "PING-PONG" }, K { "duck", "DUCK" } } };
        s.toggles = { { "sync", "TEMPO SYNC" } };
        s.choices = { { "division", "NOTE" } };
        s.meters = { { "IN", M::inputPeak }, { "ECHO", M::value, EliteDelay::delayMs, 0.0f, "ms" }, { "OUT", M::outputPeak } };
        s.width = 980; s.height = 860;
    }
    else if (id == "elite-swirl")
    {
        s.tabsParam = "mode";
        s.knobRows = { { K { "amount", "AMOUNT", true }, K { "from", "FROM" }, K { "protect", "PROTECT WIDTH" },
                         K { "whistle", "WHISTLE HUNTER" }, K { "shield", "TRANSIENT SHIELD" }, K { "output", "OUTPUT" } } };
        s.toggles = { { "listen", "LISTEN (REMOVED ONLY)" } };
        s.meters = { { "IN", M::inputPeak }, { "COHERENCE", M::value, EliteSwirl::coherence, 0.0f, "corr" },
                     { "SWIRL", M::value, EliteSwirl::swirlPercent, 0.0f, "%" },
                     { "SIDE CUT", M::reduction, EliteSwirl::removedDb, 24.0f },
                     { "WHISTLE", M::value, EliteSwirl::whistleHz, 0.0f, "Hz" }, { "OUT", M::outputPeak } };
        s.width = 1240; s.height = 1000;
    }
    else if (id == "elite-master")
    {
        s.tabsParam = "character";
        s.knobRows = { { K { "input", "INPUT", true }, K { "ceiling", "CEILING" }, K { "release", "RELEASE" } } };
        s.choices = { { "target", "TARGET" } };
        s.meters = { { "LIMIT", M::reduction, EliteMaster::gr, 12.0f }, { "SHORT-TERM", M::value, EliteMaster::shortTerm, 0.0f, "LUFS" },
                     { "INTEGRATED", M::value, EliteMaster::integrated, 0.0f, "LUFS" }, { "TRUE PEAK", M::value, EliteMaster::truePeak, 0.0f, "dBTP" },
                     { "SUGGESTED IN", M::value, EliteMaster::suggestedInput, 0.0f, "dB" } };
        s.width = 980; s.height = 700;
    }
    else if (id == "elite-transient")
    {
        s.tabsParam = "speed";
        s.knobRows = { { K { "attack", "ATTACK", true }, K { "sustain", "SUSTAIN", true }, K { "mix", "MIX" }, K { "output", "OUTPUT" } } };
        s.meters = { { "IN", M::inputPeak }, { "SHAPE", M::value, EliteTransient::gainNow, 15.0f, "dB+-" }, { "OUT", M::outputPeak } };
        s.width = 980; s.height = 640;
    }
    else if (id == "elite-multiband")
    {
        s.knobRows = { { K { "low_thr", "LOW THR", true }, K { "low_ratio", "LOW RATIO" }, K { "low_gain", "LOW GAIN" }, K { "low_x", "LOW / MID" } },
                       { K { "mid_thr", "MID THR", true }, K { "mid_ratio", "MID RATIO" }, K { "mid_gain", "MID GAIN" }, K { "high_x", "MID / HIGH" } },
                       { K { "high_thr", "HIGH THR", true }, K { "high_ratio", "HIGH RATIO" }, K { "high_gain", "HIGH GAIN" } },
                       { K { "attack", "ATTACK" }, K { "release", "RELEASE" }, K { "mix", "MIX" }, K { "output", "OUTPUT" } } };
        s.meters = { { "IN", M::inputPeak }, { "LOW GR", M::reduction, EliteMultiband::lowGr, 18.0f },
                     { "MID GR", M::reduction, EliteMultiband::midGr, 18.0f }, { "HIGH GR", M::reduction, EliteMultiband::highGr, 18.0f },
                     { "OUT", M::outputPeak } };
        s.width = 1080; s.height = 1120;
    }
    else if (id == "elite-width")
    {
        s.knobRows = { { K { "width", "WIDTH", true }, K { "mono_below", "MONO BELOW", true }, K { "output", "OUTPUT" } } };
        s.meters = { { "IN", M::inputPeak }, { "CORRELATION", M::value, EliteWidth::correlation, 0.0f, "corr" },
                     { "SIDE", M::value, EliteWidth::sideLevel, 0.0f, "%" }, { "OUT", M::outputPeak } };
        s.width = 980; s.height = 620;
    }
    else if (id == "elite-hush")
    {
        s.knobRows = { { K { "amount", "AMOUNT", true }, K { "reduction", "REDUCTION", true }, K { "from", "FROM" }, K { "output", "OUTPUT" } } };
        s.toggles = { { "listen", "LISTEN (REMOVED ONLY)" } };
        s.meters = { { "IN", M::inputPeak }, { "NOISE FLOOR", M::value, EliteHush::noiseDb, 0.0f, "dBFS" },
                     { "REMOVED", M::reduction, EliteHush::removedDb, 24.0f }, { "OUT", M::outputPeak } };
        s.width = 980; s.height = 680;
    }
    else if (id == "elite-de-ess")
    {
        s.knobRows = { { K { "freq", "FREQUENCY", true }, K { "threshold", "THRESHOLD", true }, K { "range", "RANGE" }, K { "output", "OUTPUT" } } };
        s.toggles = { { "listen", "LISTEN (ESS ONLY)" } };
        s.meters = { { "IN", M::inputPeak }, { "ESS LEVEL", M::value, EliteDeEss::sibilanceDb, 0.0f, "dBFS" },
                     { "REDUCTION", M::reduction, EliteDeEss::gr, 20.0f }, { "OUT", M::outputPeak } };
        s.width = 980; s.height = 680;
    }
    else if (id == "elite-doubler")
    {
        s.knobRows = { { K { "amount", "AMOUNT", true }, K { "width", "WIDTH", true }, K { "delay", "DELAY" } },
                       { K { "drift", "DRIFT" }, K { "tone", "LOW CUT" }, K { "output", "OUTPUT" } } };
        s.meters = { { "IN", M::inputPeak }, { "SPREAD", M::value, EliteDoubler::spreadMs, 0.0f, "ms" }, { "OUT", M::outputPeak } };
        s.width = 980; s.height = 800;
    }
    else if (id == "elite-console")
    {
        s.tabsParam = "character";
        s.knobRows = { { K { "drive", "DRIVE", true }, K { "crosstalk", "CROSSTALK" }, K { "output", "OUTPUT" } } };
        s.meters = { { "IN", M::inputPeak }, { "SATURATION", M::reduction, EliteConsole::driveDb, 12.0f }, { "OUT", M::outputPeak } };
        s.width = 980; s.height = 660;
    }
    else if (id == "elite-lofi")
    {
        s.knobRows = { { K { "wow", "WOW", true }, K { "flutter", "FLUTTER" }, K { "bits", "BITS" }, K { "rate", "SAMPLE RATE" } },
                       { K { "vinyl", "VINYL", true }, K { "tone", "TONE" }, K { "mix", "MIX" }, K { "output", "OUTPUT" } } };
        s.meters = { { "IN", M::inputPeak }, { "OUT", M::outputPeak } };
        s.width = 980; s.height = 820;
    }
    else if (id == "elite-tune")
    {
        s.tabsParam = "scale";
        s.knobRows = { { K { "speed", "RETUNE SPEED", true }, K { "amount", "AMOUNT", true }, K { "low", "LOWEST NOTE" },
                         K { "high", "HIGHEST NOTE" }, K { "output", "OUTPUT" } } };
        s.choices = { { "key", "KEY" } };
        s.meters = { { "IN", M::inputPeak }, { "PITCH", M::value, EliteTune::detectedHz, 0.0f, "pitch" },
                     { "NOTE", M::value, EliteTune::targetMidi, 0.0f, "midi" },
                     { "CORRECTION", M::value, EliteTune::correctionCents, 100.0f, "cents" }, { "OUT", M::outputPeak } };
        s.width = 1080; s.height = 700;
    }
    else if (id == "elite-balance")
    {
        s.tabsParam = "speed";
        s.knobRows = { { K { "amount", "AMOUNT", true }, K { "tilt", "TILT", true }, K { "range", "RANGE" }, K { "output", "OUTPUT" } } };
        s.toggles = { { "freeze", "FREEZE" } };
        s.meters.push_back ({ "IN", M::inputPeak });
        for (int b = 0; b < EliteBalance::kBands; ++b)
        {
            const int hz = (int) EliteBalance::bandHz (b);   // 62, 125, 250, 500, 1K .. 8K
            s.meters.push_back ({ hz < 1000 ? juce::String (hz) : juce::String (hz / 1000) + "K", M::value, b, 12.0f, "dB+-" });
        }
        s.meters.push_back ({ "OUT", M::outputPeak });
        s.width = 1320; s.height = 700;
    }
    else
        s = autoSpec (p);
    return s;
}

//==============================================================================
void EliteEditor::install()
{
    ElitePlugin::editorFactory = [] (ElitePlugin& p) -> juce::AudioProcessorEditor*
    {
        if (auto* sw = dynamic_cast<EliteSwirl*> (&p)) return new SwirlEditor (*sw);
        if (auto* eq = dynamic_cast<EliteEq*> (&p)) return new EqEditor (*eq);
        return new EliteEditor (p);
    };
}

EliteEditor::EliteEditor (ElitePlugin& p)
    : AudioProcessorEditor (p), plugin (p), spec (EditorSpec::forPlugin (p)), lnf (juce::Colour (p.getInfo().accent))
{
    setLookAndFeel (&lnf);
    logo = juce::ImageCache::getFromMemory (PlaygroundBinary::HEMIcon1024_png, PlaygroundBinary::HEMIcon1024_pngSize);
    auto& apvts = plugin.getAPVTS();

    for (const auto& row : spec.knobRows)
        for (const auto& k : row)
        {
            KnobUi ui;
            ui.param = apvts.getParameter (k.id);
            jassert (ui.param != nullptr);
            if (ui.param == nullptr) continue;
            ui.label = k.label;
            ui.slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
            ui.slider->setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
            ui.slider->getProperties().set ("big", k.big);
            ui.slider->setTooltip (ui.param->getName (64));
            addAndMakeVisible (*ui.slider);
            ui.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, k.id, *ui.slider);
            ui.slider->setDoubleClickReturnValue (true, ui.param->convertFrom0to1 (ui.param->getDefaultValue()));
            knobs.push_back (std::move (ui));
        }

    for (const auto& [id, label] : spec.toggles)
    {
        ToggleUi ui;
        ui.button = std::make_unique<juce::ToggleButton> (label);
        addAndMakeVisible (*ui.button);
        ui.att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, id, *ui.button);
        toggles.push_back (std::move (ui));
    }

    for (const auto& [id, label] : spec.choices)
    {
        ChoiceUi ui;
        ui.label = label;
        ui.box = std::make_unique<juce::ComboBox>();
        ui.box->getProperties().set (props::style, "plate");
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (id)))
            ui.box->addItemList (c->choices, 1);
        addAndMakeVisible (*ui.box);
        ui.att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, id, *ui.box);
        choices.push_back (std::move (ui));
    }

    if (spec.tabsParam.isNotEmpty())
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (spec.tabsParam)))
        {
            for (int i = 0; i < juce::jmin (4, c->choices.size()); ++i)
            {
                auto b = std::make_unique<juce::TextButton> (c->choices[i].toUpperCase());
                b->getProperties().set (props::style, "tab");
                b->setClickingTogglesState (false);
                b->onClick = [this, i] { if (tabAtt) tabAtt->setValueAsCompleteGesture ((float) i); };
                addAndMakeVisible (*b);
                tabs.push_back (std::move (b));
            }
            tabAtt = std::make_unique<juce::ParameterAttachment> (*c, [this] (float v)
            {
                for (size_t i = 0; i < tabs.size(); ++i) tabs[i]->setToggleState ((int) std::lround (v) == (int) i, juce::dontSendNotification);
            });
            tabAtt->sendInitialUpdate();
        }

    bypass.getProperties().set (props::accent, (juce::int64) colours::meterRed.getARGB());
    bypass.getProperties().set ("onChrome", true);
    bypass.setTooltip ("Bypass this plugin (BEFORE / AFTER for just this one)");
    addAndMakeVisible (bypass);
    bypassAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "bypass", bypass);

    presetBox.setTooltip ("Presets: factory (Suno fixes) and your own");
    presetBox.onChange = [this] { presetChosen(); };
    addAndMakeVisible (presetBox);
    for (auto* b : { &prevButton, &nextButton, &saveButton }) addAndMakeVisible (*b);
    prevButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (1); };
    saveButton.onClick = [this] { savePresetAs(); };
    saveButton.setTooltip ("Save these settings as your own preset");
    rebuildPresetMenu();
    armDoubleClickToCurrent();   // double-click goes back to how the knob was when this window opened

    meterShown.assign (spec.meters.size(), 0.0f);
    meterHold.assign (spec.meters.size(), 0.0f);

    setResizable (true, true);
    const float aspect = (float) spec.width / (float) spec.height;
    setResizeLimits (spec.width * 6 / 10, spec.height * 6 / 10, spec.width * 2, spec.height * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio (aspect);
    setSize (spec.width, spec.height);
    plugin.setAnalyzerActive (true);
    startTimerHz (30);
}

EliteEditor::~EliteEditor()
{
    plugin.setAnalyzerActive (false);
    setLookAndFeel (nullptr);
}

//==============================================================================
void EliteEditor::resized()
{
    const float U = u();
    auto r = getLocalBounds().toFloat();
    const float frameScale = 0.42f * U;
    auto inner = r.reduced (50.0f * frameScale - 2.0f * U);
    pieceScale = 0.34f * U;
    const float gapH = 8.0f * U;
    chromeRows.clear(); gaps.clear();

    titleArea = inner.removeFromTop (62.0f * U);
    gaps.push_back (inner.removeFromTop (gapH));
    auto row2 = inner.removeFromTop (84.0f * U);
    chromeRows.push_back (row2);
    gaps.push_back (inner.removeFromTop (gapH));
    juce::Rectangle<float> tabRow;
    if (! tabs.empty())
    {
        tabRow = inner.removeFromTop (64.0f * U);
        chromeRows.push_back (tabRow);
        gaps.push_back (inner.removeFromTop (gapH));
    }
    bottomArea = inner.removeFromBottom (40.0f * U);
    gaps.push_back (inner.removeFromBottom (gapH));
    panelArea = inner;

    // title strip: logo socket (in the art), name, bypass
    const float ts = titleArea.getHeight() / (float) FaceplateKit::titleStrip.src.getHeight();
    logoArea = juce::Rectangle<float> (FaceplateKit::kLogoR * 2.0f * ts, FaceplateKit::kLogoR * 2.0f * ts)
                   .withCentre ({ titleArea.getX() + FaceplateKit::kLogoCx * ts, titleArea.getY() + FaceplateKit::kLogoCy * ts });
    nameArea = titleArea.withTrimmedLeft (215.0f * ts).withTrimmedRight (200.0f * U);
    bypass.setBounds (juce::Rectangle<float> (150.0f * U, 34.0f * U)
                          .withCentre ({ titleArea.getRight() - 70.0f * ts - 75.0f * U, titleArea.getCentreY() }).toNearestInt());

    // row 2: preset plate + arrows + save, glass meters
    auto row = row2.reduced (34.0f * pieceScale, 12.0f * U);
    presetPlate = row.removeFromLeft (row.getWidth() * 0.62f);
    presetBox.setBounds (presetPlate.reduced (presetPlate.getHeight() * 0.22f, presetPlate.getHeight() * 0.2f).toNearestInt());
    row.removeFromLeft (8.0f * U);
    const float bw = row.getHeight() * 1.05f;
    prevButton.setBounds (row.removeFromLeft (bw).toNearestInt());
    row.removeFromLeft (4.0f * U);
    nextButton.setBounds (row.removeFromLeft (bw).toNearestInt());
    row.removeFromLeft (4.0f * U);
    saveButton.setBounds (row.removeFromLeft (bw * 1.7f).toNearestInt());
    row.removeFromLeft (12.0f * U);
    statusArea = row;   // engraved preset count / hint on the chrome

    // tabs
    if (! tabs.empty())
    {
        auto t = tabRow.reduced (34.0f * pieceScale, 8.0f * U);
        const int n = (int) tabs.size();
        const float w = (t.getWidth() - (float) (n - 1) * 10.0f * U) / (float) n;
        for (int i = 0; i < n; ++i)
            tabs[(size_t) i]->setBounds (juce::Rectangle<float> (t.getX() + (float) i * (w + 10.0f * U), t.getY(), w, t.getHeight()).toNearestInt());
    }

    // panel: display, knobs, switches; meters in a glass column on the right
    auto p = panelArea.reduced (30.0f * U, 22.0f * U);
    glassArea = {};
    if (! spec.meters.empty())
    {
        glassArea = p.removeFromRight (34.0f * U + 54.0f * U * (float) spec.meters.size());
        p.removeFromRight (20.0f * U);
    }
    displayArea = displayHeight() > 0 ? p.removeFromTop ((float) displayHeight() * U) : juce::Rectangle<float>();
    if (! displayArea.isEmpty()) p.removeFromTop (10.0f * U);

    if (! toggles.empty() || ! choices.empty())
    {
        // Many switches (Elite EQ: 6 band switches + 6 type boxes) get two rows, so every type box is
        // wide enough to show its whole name ("Low Shelf") instead of a cut-off "..".
        const bool twoRows = toggles.size() + choices.size() > 8 && ! toggles.empty() && ! choices.empty();
        auto layoutRow = [U] (juce::Rectangle<float> sw, std::vector<ToggleUi>* tg, std::vector<ChoiceUi>* ch)
        {
            const int n = (int) ((tg != nullptr ? tg->size() : 0) + (ch != nullptr ? ch->size() : 0));
            const float cw = sw.getWidth() / (float) juce::jmax (1, n);
            int i = 0;
            if (tg != nullptr)
                for (auto& t : *tg)
                {
                    auto cell = juce::Rectangle<float> (sw.getX() + cw * (float) i++, sw.getY(), cw, sw.getHeight());
                    t.button->setBounds (cell.withSizeKeepingCentre (juce::jmin (cw - 6.0f * U, 240.0f * U), 34.0f * U).withY (sw.getBottom() - 38.0f * U).toNearestInt());
                }
            if (ch != nullptr)
                for (auto& c : *ch)
                {
                    auto cell = juce::Rectangle<float> (sw.getX() + cw * (float) i++, sw.getY(), cw, sw.getHeight());
                    c.labelArea = cell.withHeight (20.0f * U);
                    c.box->setBounds (cell.withTrimmedTop (22.0f * U).withSizeKeepingCentre (juce::jmin (cw - 8.0f * U, 200.0f * U), 40.0f * U).toNearestInt());
                }
        };
        if (twoRows)
        {
            layoutRow (p.removeFromBottom (66.0f * U), nullptr, &choices);
            layoutRow (p.removeFromBottom (40.0f * U), &toggles, nullptr);
        }
        else
            layoutRow (p.removeFromBottom (choices.empty() ? 40.0f * U : 66.0f * U), &toggles, &choices);
        p.removeFromBottom (8.0f * U);
    }

    size_t k = 0;
    const int rows = (int) spec.knobRows.size();
    const float rowH = rows > 0 ? p.getHeight() / (float) rows : 0.0f;
    for (int ri = 0; ri < rows; ++ri)
    {
        auto rowArea = p.removeFromTop (rowH);
        const auto& specRow = spec.knobRows[(size_t) ri];
        const int n = (int) specRow.size();
        const float cw = rowArea.getWidth() / (float) n;
        const float labelH = 22.0f * U, valueH = 28.0f * U;
        const float maxD = rowH - labelH - valueH - 14.0f * U;
        for (int ci = 0; ci < n && k < knobs.size(); ++ci, ++k)
        {
            auto cell = juce::Rectangle<float> (rowArea.getX() + cw * (float) ci, rowArea.getY(), cw, rowH);
            const bool big = specRow[(size_t) ci].big;
            const float d = juce::jmin (cw * (big ? 0.92f : 0.74f), maxD * (big ? 1.0f : 0.78f));
            auto& ui = knobs[k];
            ui.labelArea = cell.withHeight (labelH).withY (cell.getCentreY() - (labelH + d + valueH + 8.0f * U) * 0.5f);
            auto knob = juce::Rectangle<float> (d, d).withCentre ({ cell.getCentreX(), ui.labelArea.getBottom() + 2.0f * U + d * 0.5f });
            ui.slider->setBounds (knob.toNearestInt());
            ui.valueArea = juce::Rectangle<float> (juce::jmin (cw - 8.0f * U, 124.0f * U), valueH)
                               .withCentre ({ cell.getCentreX(), knob.getBottom() + 6.0f * U + valueH * 0.5f });
        }
    }

    renderBackground();
}

void EliteEditor::renderBackground()
{
    const float ds = juce::jmax (1.0f, (float) juce::Desktop::getInstance().getGlobalScaleFactor()
                                         * (float) (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr
                                                        ? juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale : 1.0));
    const int w = juce::roundToInt ((float) getWidth() * ds), h = juce::roundToInt ((float) getHeight() * ds);
    if (w <= 0 || h <= 0) return;
    background = juce::Image (juce::Image::ARGB, w, h, true);
    juce::Graphics g (background);
    g.addTransform (juce::AffineTransform::scale (ds));
    auto& kit = FaceplateKit::get();
    const float U = u();
    const auto acc = juce::Colour (plugin.getInfo().accent);

    g.fillAll (colours::navy);
    kit.drawFrameBorder (g, getLocalBounds().toFloat(), 0.42f * U);
    for (auto gr : gaps)
        kit.draw (g, FaceplateKit::gap, gr.withSizeKeepingCentre (gr.getWidth(), juce::jmax (gr.getHeight(), 4.0f)), pieceScale);

    kit.draw (g, FaceplateKit::titleStrip, titleArea, titleArea.getHeight() / (float) FaceplateKit::titleStrip.src.getHeight());
    for (auto row : chromeRows) kit.draw (g, FaceplateKit::chromeStrip, row, pieceScale);
    kit.draw (g, FaceplateKit::plate, presetPlate, presetPlate.getHeight() / (float) FaceplateKit::plate.src.getHeight());
    const int nFactory = (int) Presets::factory (plugin.getInfo().id).size();
    PlaygroundLookAndFeel::drawEngraved (g, juce::String (nFactory + userPresetCount) + " PRESETS", statusArea, juce::Justification::centredRight, 13.0f * U);
    kit.draw (g, FaceplateKit::panel, panelArea, pieceScale);
    if (! glassArea.isEmpty()) kit.draw (g, FaceplateKit::glass, glassArea, pieceScale);
    kit.draw (g, FaceplateKit::chromeStrip, bottomArea, bottomArea.getHeight() / (float) FaceplateKit::chromeStrip.src.getHeight());

    // name + blurb, engraved in the chrome
    auto na = nameArea;
    const juce::String name = juce::String (plugin.getInfo().name).toUpperCase();
    const float nameH = 30.0f * U;
    const float nameW = juce::GlyphArrangement::getStringWidth (PlaygroundLookAndFeel::font (nameH), name) + 10.0f * U;
    PlaygroundLookAndFeel::drawEngraved (g, name, na.removeFromLeft (nameW), juce::Justification::centredLeft, nameH);
    // accent bar between name and blurb
    g.setColour (acc);
    g.fillRoundedRectangle (na.removeFromLeft (6.0f * U).withSizeKeepingCentre (4.0f * U, na.getHeight() * 0.45f), 2.0f * U);
    na.removeFromLeft (12.0f * U);
    PlaygroundLookAndFeel::drawEngraved (g, plugin.getInfo().blurb, na, juce::Justification::centredLeft, 14.0f * U);

    PlaygroundLookAndFeel::drawEngraved (g, "HYBRID ELITE MAFIA  -  REWORK PLAYGROUND", bottomArea.reduced (34.0f * pieceScale, 0.0f),
                                         juce::Justification::centredLeft, 13.0f * U);
    PlaygroundLookAndFeel::drawEngraved (g, "DOUBLE-CLICK A KNOB TO RESET", bottomArea.reduced (34.0f * pieceScale, 0.0f),
                                         juce::Justification::centredRight, 12.0f * U);

    // knob labels + value glass
    for (auto& k : knobs)
    {
        PlaygroundLookAndFeel::drawPanelText (g, k.label, k.labelArea, juce::Justification::centred, 14.0f * U);
        kit.draw (g, FaceplateKit::glass, k.valueArea, k.valueArea.getHeight() / (float) FaceplateKit::glass.src.getHeight());
    }
    for (auto& c : choices)
        PlaygroundLookAndFeel::drawPanelText (g, c.label, c.labelArea, juce::Justification::centred, 13.0f * U, colours::paperDim);

    if (! displayArea.isEmpty())
        kit.draw (g, FaceplateKit::glass, displayArea, pieceScale);
}

//==============================================================================
void EliteEditor::paint (juce::Graphics& g)
{
    if (background.isValid())
        g.drawImage (background, getLocalBounds().toFloat());
    else
        g.fillAll (colours::navy);

    if (logo.isValid() && ! logoArea.isEmpty())
    {
        juce::Graphics::ScopedSaveState s (g);
        juce::Path clip; clip.addEllipse (logoArea.reduced (logoArea.getWidth() * 0.06f));
        g.reduceClipRegion (clip);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (logo, logoArea.reduced (logoArea.getWidth() * 0.06f), juce::RectanglePlacement::fillDestination);
    }

    const float U = u();
    const auto acc = juce::Colour (plugin.getInfo().accent);
    for (auto& k : knobs)
    {
        g.setFont (PlaygroundLookAndFeel::font (14.0f * U));
        g.setColour (acc.brighter (0.5f));
        g.drawText (k.shown, k.valueArea.reduced (6.0f * U, 0.0f), juce::Justification::centred, false);
    }

    if (! displayArea.isEmpty())
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (displayArea.reduced (8.0f * U).toNearestInt());
        paintDisplay (g, displayArea.reduced (8.0f * U));
    }
    paintMeters (g);
}

void EliteEditor::paintMeters (juce::Graphics& g)
{
    if (spec.meters.empty() || glassArea.isEmpty()) return;
    const float U = u();
    const auto acc = juce::Colour (plugin.getInfo().accent);
    auto area = glassArea.reduced (17.0f * U, 18.0f * U);
    const int n = (int) spec.meters.size();
    const float colW = area.getWidth() / (float) n;
    const float textH = 11.5f * U;

    for (int i = 0; i < n; ++i)
    {
        const auto& m = spec.meters[(size_t) i];
        auto col = juce::Rectangle<float> (area.getX() + colW * (float) i, area.getY(), colW, area.getHeight());
        g.setFont (PlaygroundLookAndFeel::font (textH));
        g.setColour (colours::paperDim);
        g.drawFittedText (m.label, col.removeFromBottom (30.0f * U).toNearestInt(), juce::Justification::centred, 2, 0.7f);
        auto valueR = col.removeFromTop (20.0f * U);
        const float shown = meterShown[(size_t) i];

        juce::String text;
        const auto bar = col.reduced (juce::jmax (2.0f * U, (colW - 16.0f * U) * 0.5f), 4.0f * U);
        g.setColour (juce::Colour (0xff0b0d10));
        g.fillRoundedRectangle (bar, 3.0f * U);
        // scale ticks
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        for (int t = 1; t < 6; ++t)
        {
            const float y = bar.getY() + bar.getHeight() * (float) t / 6.0f;
            g.drawHorizontalLine (juce::roundToInt (y), bar.getX(), bar.getRight());
        }

        float frac = 0.0f, centreFrac = 0.0f;   // centreFrac: -1..1 bar drawn from the middle
        juce::Colour c = acc;
        bool fromTop = false;
        switch (m.kind)
        {
            case M::reduction:
                frac = juce::jlimit (0.0f, 1.0f, shown / m.range); c = kAmber; fromTop = true;
                text = shown > 0.05f ? "-" + juce::String (shown, 1) : "0.0";
                break;
            case M::value:
            {
                if (m.unit == "dB+-" || m.unit == "cents")
                {
                    // signed values (boost / cut, sharp / flat): the bar grows up or down from the middle
                    const float half = m.range > 0.0f ? m.range : 12.0f;
                    const float v = juce::jlimit (-half, half, shown);
                    centreFrac = v / half;
                    c = v < 0.0f ? kAmber : acc;
                    text = (v > 0.05f ? "+" : "") + (m.unit == "cents" ? juce::String (juce::roundToInt (shown)) : juce::String (shown, 1));
                    break;
                }
                if (m.unit == "midi")
                {
                    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
                    const int note = juce::roundToInt (shown);
                    frac = note <= 0 ? 0.0f : juce::jlimit (0.0f, 1.0f, (shown - 36.0f) / 48.0f);
                    text = note <= 0 ? juce::String ("--") : juce::String (names[note % 12]) + juce::String (note / 12 - 1);
                    break;
                }
                if (m.unit == "pitch")
                {
                    frac = shown < 1.0f ? 0.0f : juce::jlimit (0.0f, 1.0f, std::log2 (shown / 50.0f) / std::log2 (20.0f));
                    text = shown < 1.0f ? juce::String ("--") : juce::String (juce::roundToInt (shown));
                    break;
                }
                if (m.unit == "dBFS")
                {
                    frac = shown <= -95.0f ? 0.0f : juce::jlimit (0.0f, 1.0f, (shown + 96.0f) / 96.0f);
                    text = shown <= -95.0f ? juce::String ("--") : juce::String (shown, 1);
                    break;
                }
                // value meters (LUFS, dBTP, ms) show a number and a bar over a sensible range
                const bool lufs = m.unit == "LUFS";
                const bool corr = m.unit == "corr", hz = m.unit == "Hz";
                const float lo = lufs ? -36.0f : (m.unit == "dBTP" ? -24.0f : (corr ? -1.0f : 0.0f));
                const float hi = lufs ? 0.0f : (m.unit == "dBTP" ? 3.0f : (m.unit == "ms" ? 2000.0f : (m.unit == "%" ? 100.0f
                                 : (corr ? 1.0f : (hz ? 20000.0f : 18.0f)))));
                frac = shown <= -69.0f ? 0.0f : juce::jlimit (0.0f, 1.0f, (shown - lo) / (hi - lo));
                text = shown <= -69.0f ? "--" : (m.unit == "ms" ? juce::String (juce::roundToInt (shown)) : juce::String (shown, corr ? 2 : 1));
                if (corr) c = shown > 0.6f ? colours::hemGreen : (shown > 0.2f ? kAmber : colours::meterRed);
                if (hz) { text = shown < 1.0f ? juce::String ("--") : juce::String (shown / 1000.0f, 1) + "k"; c = kAmber; }
                if (m.unit == "dBTP" && shown > -1.0f) c = colours::meterRed;
                break;
            }
            case M::inputPeak: case M::outputPeak: case M::level:
                frac = juce::jlimit (0.0f, 1.0f, (shown + 60.0f) / 60.0f);
                text = shown <= -59.0f ? "-inf" : juce::String (shown, 1);
                if (shown > -0.5f) c = colours::meterRed;
                break;
        }
        if (centreFrac != 0.0f)
        {
            const float mid = bar.getCentreY(), h = bar.getHeight() * 0.5f * std::abs (centreFrac);
            auto fill = centreFrac > 0.0f ? bar.withTop (mid - h).withBottom (mid) : bar.withTop (mid).withBottom (mid + h);
            g.setColour (c);
            g.fillRoundedRectangle (fill, 2.0f * U);
            g.setColour (c.withAlpha (0.22f));
            g.fillRoundedRectangle (fill.expanded (3.0f * U, 0.0f), 4.0f * U);
        }
        if (m.kind == M::value && (m.unit == "dB+-" || m.unit == "cents"))
        {
            g.setColour (juce::Colours::white.withAlpha (0.35f));   // the zero line
            g.drawHorizontalLine (juce::roundToInt (bar.getCentreY()), bar.getX(), bar.getRight());
        }
        if (frac > 0.0f)
        {
            auto fill = fromTop ? bar.withHeight (bar.getHeight() * frac) : bar.withTop (bar.getBottom() - bar.getHeight() * frac);
            juce::ColourGradient grad (c.brighter (0.35f), 0.0f, fill.getY(), c.darker (0.45f), 0.0f, fill.getBottom(), false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (fill, 3.0f * U);
            g.setColour (c.withAlpha (0.22f));
            g.fillRoundedRectangle (fill.expanded (3.0f * U, 0.0f), 5.0f * U);
        }
        g.setColour (acc.brighter (0.5f));
        g.setFont (PlaygroundLookAndFeel::font (textH));
        g.drawText (text, valueR, juce::Justification::centred, false);
    }
}

void EliteEditor::timerCallback()
{
    tick();
    for (size_t i = 0; i < spec.meters.size(); ++i)
    {
        const auto& m = spec.meters[i];
        float target = 0.0f;
        switch (m.kind)
        {
            case M::inputPeak:  target = params::gainToDb (plugin.getInputPeak()); break;
            case M::outputPeak: target = params::gainToDb (plugin.getOutputPeak()); break;
            case M::reduction:  target = std::abs (plugin.getReadout (m.readout)); break;
            case M::level:      target = plugin.getReadout (m.readout); break;
            case M::value:      target = plugin.getReadout (m.readout); break;
        }
        auto& s = meterShown[i];
        if (m.kind == M::value) s = target;
        else if (m.kind == M::reduction) s = target > s ? target : juce::jmax (target, s - 0.8f);
        else s = target > s ? target : juce::jmax (target, s - 1.5f);   // ~45 dB/s fall
    }
    if (! glassArea.isEmpty()) repaint (glassArea.toNearestInt());

    for (auto& k : knobs)
    {
        const auto text = k.param->getText (k.param->getValue(), 12);
        if (text != k.shown) { k.shown = text; repaint (k.valueArea.toNearestInt()); }
    }
    if (! displayArea.isEmpty()) repaint (displayArea.toNearestInt());
}

//==============================================================================
void EliteEditor::rebuildPresetMenu()
{
    presetBox.clear (juce::dontSendNotification);
    menuNames.clear();
    const auto& factory = Presets::factory (plugin.getInfo().id);
    presetBox.addSectionHeading ("FACTORY");
    for (size_t i = 0; i < factory.size(); ++i)
        presetBox.addItem (factory[i].name, 1 + (int) i);
    const auto user = Presets::userPresetNames (plugin.getInfo().id);
    if (! user.isEmpty())
    {
        presetBox.addSectionHeading ("MY PRESETS");
        for (int i = 0; i < user.size(); ++i)
            presetBox.addItem (user[i], kUserBase + i);
    }
    presetBox.addSeparator();
    presetBox.addItem ("Save as new preset...", kSaveId);
    presetBox.addItem ("Delete my preset \"" + currentPreset + "\"", kDeleteId);
    presetBox.setItemEnabled (kDeleteId, user.contains (currentPreset));
    presetBox.addItem ("Reset to default", kResetId);
    // Nothing stays selected: the name is shown as "nothing selected" text, so choosing the preset that
    // is already showing still loads it (a ComboBox sends no change for re-picking the selected item).
    presetBox.setSelectedId (0, juce::dontSendNotification);
    presetBox.setTextWhenNothingSelected (currentPreset);
    userPresetCount = user.size();
    repaint();
}

void EliteEditor::presetChosen()
{
    const int id = presetBox.getSelectedId();
    if (id == 0) return;
    const auto& factory = Presets::factory (plugin.getInfo().id);
    const auto user = Presets::userPresetNames (plugin.getInfo().id);

    if (id >= 1 && id <= (int) factory.size())
    {
        Presets::apply (plugin, factory[(size_t) id - 1]);
        currentPreset = factory[(size_t) id - 1].name;
    }
    else if (id >= kUserBase && id < kUserBase + user.size())
    {
        currentPreset = user[id - kUserBase];
        Presets::loadUser (plugin, currentPreset);
    }
    else if (id == kSaveId)
    {
        presetBox.setSelectedId (0, juce::dontSendNotification);
        savePresetAs();
        return;
    }
    else if (id == kDeleteId)
    {
        Presets::deleteUser (plugin.getInfo().id, currentPreset);
        currentPreset = "Default";
    }
    else if (id == kResetId)
    {
        Presets::apply (plugin, FactoryPreset { "Default", {} });   // bypass is kept
        currentPreset = "Default";
    }
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<EliteEditor> (this)]
    {
        if (safe != nullptr) { safe->armDoubleClickToCurrent(); safe->rebuildPresetMenu(); }
    });
}

void EliteEditor::stepPreset (int delta)
{
    juce::StringArray all;
    for (const auto& f : Presets::factory (plugin.getInfo().id)) all.add (f.name);
    const auto user = Presets::userPresetNames (plugin.getInfo().id);
    all.addArray (user);
    if (all.isEmpty()) return;
    int idx = all.indexOf (currentPreset);
    idx = idx < 0 ? (delta > 0 ? 0 : all.size() - 1) : (idx + delta + all.size()) % all.size();
    const auto& factory = Presets::factory (plugin.getInfo().id);
    if (idx < (int) factory.size()) Presets::apply (plugin, factory[(size_t) idx]);
    else Presets::loadUser (plugin, all[idx]);
    currentPreset = all[idx];
    armDoubleClickToCurrent();
    rebuildPresetMenu();
}

void EliteEditor::armDoubleClickToCurrent()
{
    for (auto& k : knobs)
        if (k.slider != nullptr && k.param != nullptr)
            k.slider->setDoubleClickReturnValue (true, k.param->convertFrom0to1 (k.param->getValue()));
}

void EliteEditor::savePresetAs()
{
    auto* w = new juce::AlertWindow ("Save preset", "Name your " + juce::String (plugin.getInfo().name) + " preset:", juce::MessageBoxIconType::NoIcon, this);
    w->setLookAndFeel (&lnf);
    w->addTextEditor ("name", currentPreset == "Default" ? juce::String() : currentPreset);
    w->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<EliteEditor> (this), w] (int result)
    {
        if (result != 1 || safe == nullptr) return;
        const auto name = w->getTextEditorContents ("name").trim();
        if (name.isEmpty()) return;
        if (Presets::saveUser (safe->plugin, name))
        {
            safe->currentPreset = juce::File::createLegalFileName (name);
            safe->armDoubleClickToCurrent();
            safe->rebuildPresetMenu();
        }
    }), true);
}
} // namespace pg::ui
