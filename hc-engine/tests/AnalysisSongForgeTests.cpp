// The 3D spectrum numbers (Waterfall) and the Song Forge (ACE-Step) messages.

#include "UnitTest.hpp"
#include "AceStepClient.hpp"
#include "Waterfall.hpp"
#include "hem/pg/Core.hpp"
#include "hem/pg/Json.hpp"

#include <chrono>
#include <thread>

#include <cmath>

namespace hem::pg::test
{
class WaterfallTests final : public hemtest::UnitTest
{
public:
    WaterfallTests() : UnitTest ("3D Spectrum", "Playground") {}

    void runTest() override
    {
        beginTest ("3D spectrum: a full-scale 1 kHz sine peaks at about 0 dB at 1 kHz, far below it elsewhere");
        analysis::Waterfall fresh;   // drawn before any audio arrives (this crashed 0.7.0 on the MASTER page)
        for (int age = 0; age < analysis::Waterfall::kRows; ++age)
            expectEquals (fresh.row (age)[analysis::Waterfall::kPoints - 1], analysis::Waterfall::kFloorDb);
        analysis::Waterfall w;
        w.prepare (48000.0);
        std::vector<float> x (48000);
        for (size_t i = 0; i < x.size(); ++i) x[i] = (float) std::sin (MathConstants<double>::twoPi * 1000.0 * (double) i / 48000.0);
        w.add (x.data(), (int) x.size());
        expectGreaterThan ((int) w.rowsMade(), 20);
        const float* r = w.row (0);
        int best = 0;
        for (int i = 1; i < analysis::Waterfall::kPoints; ++i) if (r[i] > r[best]) best = i;
        expectWithinAbsoluteError (analysis::Waterfall::pointHz (best), 1000.0f, 70.0f);
        expectWithinAbsoluteError (r[best], 0.0f, 1.5f);
        int far = 0;
        for (int i = 0; i < analysis::Waterfall::kPoints; ++i) if (analysis::Waterfall::pointHz (i) < 300.0f && r[i] > -60.0f) ++far;
        expectEquals (far, 0, "energy below 300 Hz from a 1 kHz tone");
    }
};
static WaterfallTests waterfallTests;

class JsonTests final : public hemtest::UnitTest
{
public:
    JsonTests() : UnitTest ("JSON", "Playground") {}

    void runTest() override
    {
        beginTest ("reads objects, lists, text with escapes, whole and decimal numbers; writes them back");
        const auto j = Json::parse (R"({"a": 1, "b": [true, null, 2.5, "x\"yé"], "c": {"d": -1700000000000}})");
        expect (j.has_value());
        if (j)
        {
            expectEquals ((*j)["a"].toInt(), 1);
            expect ((*j)["b"].isArray() && (*j)["b"].size() == 4);
            expect ((*j)["b"][0].toBool() && (*j)["b"][1].isNull());
            expectEquals ((*j)["b"][2].toDouble(), 2.5);
            expectEquals ((*j)["b"][3].toString(), std::string ("x\"y\xc3\xa9"));
            expect ((*j)["c"]["d"].toInt64() == -1700000000000LL);
            const auto again = Json::parse (j->dump (true));
            expect (again.has_value() && again->dump (false) == j->dump (false), "write -> read -> write is stable");
        }
        expect (! Json::parse ("{ not json").has_value());
        expect (! Json::parse ("[1, 2").has_value());
        expect (Json::parse ("  []  ").has_value());
    }
};
static JsonTests jsonTests;

class SongForgeTests final : public hemtest::UnitTest
{
public:
    SongForgeTests() : UnitTest ("Song Forge (ACE-Step)", "Playground") {}

    void runTest() override
    {
        beginTest ("the request body has the fields ACE-Step 1.5 reads, with limits applied");
        {
            ace::Request r;
            r.style = "  outlaw country, steel guitar  ";
            r.lyrics = "";
            r.seconds = 900.0;
            r.bpm = 96;
            r.key = "G major";
            r.takes = 9;
            r.seed = 42;
            const auto body = Json::parse (ace::buildReleaseBody (r));
            expect (body.has_value());
            if (body)
            {
                expectEquals ((*body)["prompt"].toString(), std::string ("outlaw country, steel guitar"));
                expectEquals ((*body)["lyrics"].toString(), std::string ("[Instrumental]"));
                expectEquals ((*body)["audio_format"].toString(), std::string ("wav"));
                expectEquals ((*body)["audio_duration"].toDouble(), 600.0);
                expectEquals ((*body)["bpm"].toInt(), 96);
                expectEquals ((*body)["key_scale"].toString(), std::string ("G major"));
                expectEquals ((*body)["batch_size"].toInt(), 4);
                expect (! (*body)["use_random_seed"].toBool() && (*body)["seed"].toInt() == 42);
                expect (! body->has ("time_signature"));
            }
        }

        beginTest ("task id, progress, takes and errors are read from ACE-Step's replies");
        {
            std::string id, err;
            expect (ace::parseRelease (R"({"data": {"task_id": "abc-1", "status": "queued"}, "code": 200, "error": null})", id, err));
            expectEquals (id, std::string ("abc-1"));
            expect (! ace::parseRelease (R"({"data": null, "error": "queue full"})", id, err));
            expectEquals (err, std::string ("queue full"));
            expect (! ace::parseRelease ("<html>", id, err));

            const std::string running = R"({"data": [{"task_id": "abc-1", "status": 0, "progress_text": "step 3/8"}]})";
            auto p = ace::parseQuery (running, "abc-1");
            expectEquals (p.status, 0);
            expectEquals (p.progress, std::string ("step 3/8"));
            const std::string done = R"({"data": [{"task_id": "abc-1", "status": 1, "result": "[{\"file\": \"/v1/audio?path=a.wav\", \"metas\": {\"bpm\": 96, \"duration\": 181.5, \"keyscale\": \"G major\"}}]"}]})";
            p = ace::parseQuery (done, "abc-1");
            expectEquals (p.status, 1);
            expectEquals ((int) p.takes.size(), 1);
            if (! p.takes.empty())
            {
                expectEquals (p.takes[0].url, std::string ("/v1/audio?path=a.wav"));
                expectEquals (p.takes[0].bpm, 96.0);
                expectEquals (p.takes[0].seconds, 181.5);
                expectEquals (p.takes[0].key, std::string ("G major"));
            }
            expectEquals (ace::parseQuery (done, "other").status, -1);
            p = ace::parseQuery (R"({"data": [{"task_id": "abc-1", "status": 2}]})", "abc-1");
            expectEquals (p.error, std::string ("ACE-Step says the song failed."));
            expectEquals (ace::errorFrom ("", 0), std::string ("ACE-Step is not running (nothing answers at the server address)."));
            expectEquals (ace::errorFrom (R"({"detail": "bad key"})", 401), std::string ("bad key"));
            expectEquals (ace::joinUrl ("http://127.0.0.1:8001/ ", "health"), std::string ("http://127.0.0.1:8001/health"));
        }

        beginTest ("nothing is running at a closed port: the health check says so and a job reports it");
        {
            int status = -1;
            const auto body = ace::httpGet ("http://127.0.0.1:9/health", {}, 800, status);
            expectEquals (status, 0);
            std::string error;
            ace::Job job ({}, "http://127.0.0.1:9", {}, std::filesystem::temp_directory_path() / "hem-sf-test", {},
                          [&] (ace::Result r) { error = r.error; }, false);
            job.start();
            for (int i = 0; i < 100 && job.isRunning(); ++i) std::this_thread::sleep_for (std::chrono::milliseconds (50));
            expect (! job.isRunning());
            expectEquals (error, std::string ("ACE-Step is not running (nothing answers at the server address)."));
        }
    }
};
static SongForgeTests songForgeTests;
} // namespace hem::pg::test
