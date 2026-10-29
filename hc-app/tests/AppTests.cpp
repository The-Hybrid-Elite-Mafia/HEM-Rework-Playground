// Tests of the Playground app's own code: settings (and the 0.10.0 import), the shared Song Forge
// settings file, the plug-in faceplates (every control on its plate exactly once, every meter wired),
// and the layout helpers the pages are built from. Prints "HEM APP TEST SUMMARY: n passed, m failed".
#include "PluginPanels.hpp"
#include "Registry.hpp"
#include "Settings.hpp"
#include "gui/Paint.hpp"
#include "hem/pg/Files.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>

namespace
{
    int passed = 0, failed = 0;

    void expect (bool ok, const std::string& what)
    {
        if (ok) ++passed;
        else { ++failed; std::printf ("[FAIL] %s\n", what.c_str()); }
    }

    std::filesystem::path tempFolder()
    {
        auto d = std::filesystem::temp_directory_path() / ("hem-app-tests-" + hem::pg::files::newId());
        std::filesystem::create_directories (d);
        return d;
    }

    void settingsTests()
    {
        using namespace hem::app;
        const auto dir = tempFolder();
        // a 0.10.0 settings file as JUCE wrote it
        const auto legacy = dir / "Rework Playground.settings";
        hem::pg::files::writeTextAtomic (legacy,
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\n<PROPERTIES>\n"
            "  <VALUE name=\"exportName\" val=\"My Song - master\"/>\n"
            "  <VALUE name=\"exportBits\" val=\"0\"/>\n"
            "  <VALUE name=\"exportTail\" val=\"4.5\"/>\n"
            "  <VALUE name=\"forgeSmart\" val=\"1\"/>\n"
            "  <VALUE name=\"recentSessions\" val=\"C:\\Songs\\a.hemplay&#10;C:\\Songs\\b.hemplay\"/>\n"
            "  <VALUE name=\"audioDevice\">\n    <DEVICESETUP deviceType=\"Windows Audio\"/>\n  </VALUE>\n"
            "</PROPERTIES>\n");
        const auto file = dir / "Rework Playground" / "settings.json";
        {
            Settings s (file, legacy);
            s.load();
            expect (s.importedCount() == 5, "0.10.0 import: 5 text values (the XML audio device is skipped), got " + std::to_string (s.importedCount()));
            expect (s.get ("exportName") == "My Song - master", "import: export name");
            expect (s.getInt ("exportBits", 9) == 0, "import: bits");
            expect (std::abs (s.getDouble ("exportTail") - 4.5) < 1.0e-9, "import: tail");
            expect (s.getBool ("forgeSmart"), "import: bool");
            expect (s.get ("recentSessions").find ('\n') != std::string::npos, "import: recent list keeps its lines");
            s.set ("exportRate", 2);
            s.set ("bandBpm", 97.5);
            s.set ("bandAutoAdd", false);
            expect (s.saveIfNeeded(), "settings save");
        }
        {
            Settings s (file, legacy);
            s.load();
            expect (s.importedCount() == 0, "second start: nothing imported again");
            expect (s.getInt ("exportRate") == 2 && std::abs (s.getDouble ("bandBpm") - 97.5) < 1.0e-9 && ! s.getBool ("bandAutoAdd", true),
                    "settings read back");
            expect (s.get ("exportName") == "My Song - master", "imported value kept in the new file");
            expect (s.getInt ("missing", 7) == 7 && s.get ("missing", "x") == "x", "fallbacks");
        }
        {
            // SongForge.settings is shared with Hybrid Studio: values we do not own survive our save
            const auto sf = dir / "SongForge.settings";
            hem::pg::files::writeTextAtomic (sf, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\n<PROPERTIES>\n"
                                                 "  <VALUE name=\"server\" val=\"http://127.0.0.1:8001\"/>\n"
                                                 "  <VALUE name=\"studioOnly\" val=\"keep me\"/>\n"
                                                 "  <VALUE name=\"window\">\n    <BOUNDS x=\"1\"/>\n  </VALUE>\n</PROPERTIES>\n");
            PropertiesFile pf (sf);
            expect (pf.load(), "SongForge.settings loads");
            expect (pf.get ("server") == "http://127.0.0.1:8001", "shared server address");
            pf.set ("aceFolder", "C:\\ACE-Step-1.5");
            expect (pf.save(), "SongForge.settings saves");
            PropertiesFile again (sf);
            again.load();
            expect (again.get ("aceFolder") == "C:\\ACE-Step-1.5" && again.get ("studioOnly") == "keep me", "shared file keeps the other app's values");
            const auto text = hem::pg::files::readText (sf).value_or ("");
            expect (text.find ("<BOUNDS") != std::string::npos, "shared file keeps the other app's XML values");
            expect (text.rfind ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>", 0) == 0, "JUCE's header line");
        }
        std::error_code ec;
        std::filesystem::remove_all (dir, ec);
    }

    void panelTests()
    {
        using namespace hem;
        for (const auto* info : pg::PluginRegistry::all())
        {
            auto plugin = pg::PluginRegistry::create (info->id);
            expect (plugin != nullptr, std::string ("create ") + info->id);
            if (plugin == nullptr) continue;
            const auto map = app::panelFor (*plugin);
            const std::string name = info->id;
            expect (map.spec.params.size() == map.paramIds.size(), name + ": one faceplate control per id");
            std::set<std::string> seen;
            for (auto& id : map.paramIds)
            {
                expect (plugin->parameters().find (id) != nullptr, name + ": control '" + id + "' exists");
                expect (seen.insert (id).second, name + ": control '" + id + "' only once");
            }
            // no control is missing from the plate (every knob does something and can be reached)
            for (const auto& prm : plugin->parameters().all())
                if (prm->id() != "bypass") expect (seen.count (prm->id()) == 1, name + ": control '" + prm->id() + "' is on the faceplate");
            expect (map.spec.meters.size() == map.meters.size() && ! map.meters.empty(), name + ": meters wired");
            int grouped = 0;
            for (const auto& g : map.spec.groups)
            {
                expect (g.first == grouped && g.count > 0, name + ": sections cover the controls in order");
                grouped += g.count;
            }
            expect (map.spec.groups.empty() || grouped == (int) map.paramIds.size(), name + ": sections cover every control");
            const auto layout = hc::ui::layoutFxPanel (map.spec);
            expect (layout.width > 0 && layout.height > 0, name + ": faceplate lays out");
            for (std::size_t m = 0; m < map.meters.size(); ++m)
            {
                const float v = app::meterValue (*plugin, map.meters[m]);
                expect (std::isfinite (v), name + ": meter " + map.spec.meters[m].name + " reads a number");
            }
        }
    }

    void layoutTests()
    {
        using namespace hem::app;
        Rect r { 0, 0, 100, 50 };
        const Rect t = takeTop (r, 10);
        expect (t.h == 10 && r.y == 10 && r.h == 40, "takeTop");
        const Rect b = takeBottom (r, 15);
        expect (b.y == 35 && r.h == 25, "takeBottom");
        const Rect l = takeLeft (r, 30);
        expect (l.w == 30 && r.x == 30 && r.w == 70, "takeLeft");
        const Rect rr = takeRight (r, 20);
        expect (rr.x == 80 && r.w == 50, "takeRight");
        const Rect big = takeTop (r, 1000);
        expect (big.h == 25 && r.h == 0, "take more than there is");
        const Rect c = centred ({ 0, 0, 100, 100 }, 20, 10);
        expect (c.x == 40 && c.y == 45, "centred");
        const auto d = toDesign ({ 1, 2, 3, 4 });
        expect (std::abs (d.x - 2.4f) < 1.0e-5f && std::abs (d.w - 7.2f) < 1.0e-5f && std::abs (fromDesign (d).h - 4.0f) < 1.0e-5f, "L <-> design units");
        expect (styleFor (9.5f, false) == hc::ui::TextStyle::Micro && styleFor (13.0f, true) == hc::ui::TextStyle::SmallBold
                    && styleFor (38.0f, true) == hc::ui::TextStyle::Heading, "0.10.0 font heights land on the baked styles");
        const auto c1 = brighter (argb (0xff2f8bff), 0.5f);
        expect (c1.r > argb (0xff2f8bff).r && c1.a == 1.0f, "brighter");
        const auto c2 = darker (argb (0xff2f8bff), 1.0f);
        expect (std::abs (c2.b - argb (0xff2f8bff).b * 0.5f) < 1.0e-6f, "darker");
    }
}

int main()
{
    settingsTests();
    panelTests();
    layoutTests();
    std::printf ("HEM APP TEST SUMMARY: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
