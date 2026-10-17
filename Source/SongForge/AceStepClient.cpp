#include "AceStepClient.h"
#include "../Engine/MediaLoader.h"
#include "../Library/AudioFacts.h"

namespace pg::ace
{
double measuredOrReported (const juce::File& audioFile, double reportedBpm)
{
    juce::String err;
    const auto data = MediaLoader::decode (audioFile, err);
    if (data == nullptr || data->isEmpty()) return reportedBpm;
    const auto facts = library::measure (data->getBuffer(), data->getSourceRate());
    if (facts.bpm <= 0.0) return reportedBpm;
    // if ACE-Step's number is the same tempo (or exactly double / half of it), keep its round number
    for (const double k : { 1.0, 2.0, 0.5 })
        if (reportedBpm > 0.0 && std::abs (reportedBpm * k - facts.bpm) <= facts.bpm * 0.03)
            return reportedBpm * k;
    return facts.bpm;
}

namespace
{
    juce::var dataOf (const juce::var& root)
    {
        if (auto* o = root.getDynamicObject(); o != nullptr && o->hasProperty ("data"))
            return o->getProperty ("data");
        return root;          // tolerate an unwrapped reply
    }

    juce::String joinUrl (const juce::String& server, const juce::String& path)
    {
        auto s = server.trim();
        while (s.endsWithChar ('/')) s = s.dropLastCharacters (1);
        if (path.startsWith ("http://") || path.startsWith ("https://")) return path;
        return s + (path.startsWithChar ('/') ? path : "/" + path);
    }

    juce::String headersFor (const juce::String& apiKey, bool json)
    {
        juce::String h;
        if (json) h << "Content-Type: application/json\r\n";
        if (apiKey.isNotEmpty()) h << "Authorization: Bearer " << apiKey << "\r\n";
        return h;
    }
}

//==============================================================================
juce::String buildReleaseBody (const Request& r)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("prompt", r.style.trim());
    const auto lyrics = r.lyrics.trim();
    o->setProperty ("lyrics", (r.instrumental || lyrics.isEmpty()) ? juce::String ("[Instrumental]") : lyrics);
    o->setProperty ("vocal_language", r.language.isNotEmpty() ? r.language : juce::String ("en"));
    o->setProperty ("audio_format", "wav");
    o->setProperty ("audio_duration", juce::jlimit (10.0, 600.0, r.seconds));
    if (r.bpm >= 30 && r.bpm <= 300) o->setProperty ("bpm", r.bpm);
    if (r.key.isNotEmpty()) o->setProperty ("key_scale", r.key);
    if (r.timeSignature.isNotEmpty()) o->setProperty ("time_signature", r.timeSignature);
    o->setProperty ("batch_size", juce::jlimit (1, 4, r.takes));
    o->setProperty ("thinking", r.smart);
    if (r.seed >= 0)
    {
        o->setProperty ("use_random_seed", false);
        o->setProperty ("seed", r.seed);
    }
    else
        o->setProperty ("use_random_seed", true);
    return juce::JSON::toString (juce::var (o), true);
}

bool parseRelease (const juce::String& body, juce::String& taskId, juce::String& error)
{
    const auto root = juce::JSON::parse (body);
    if (! root.isObject()) { error = "ACE-Step sent a reply that is not JSON."; return false; }
    if (const auto e = root.getProperty ("error", {}); ! e.isVoid() && e.toString().isNotEmpty())
    {
        error = e.toString();
        return false;
    }
    const auto id = dataOf (root).getProperty ("task_id", {}).toString();
    if (id.isEmpty()) { error = "ACE-Step did not return a task id."; return false; }
    taskId = id;
    return true;
}

Poll parseQuery (const juce::String& body, const juce::String& taskId)
{
    Poll p;
    const auto root = juce::JSON::parse (body);
    const auto list = dataOf (root);
    if (! list.isArray()) { p.status = -1; p.error = "ACE-Step sent a reply that is not understood."; return p; }
    const juce::var* item = nullptr;
    for (auto& v : *list.getArray())
        if (v.getProperty ("task_id", {}).toString() == taskId) { item = &v; break; }
    if (item == nullptr) { p.status = -1; p.error = "ACE-Step does not know this task."; return p; }

    p.status = (int) item->getProperty ("status", 0);
    p.progress = item->getProperty ("progress_text", {}).toString();

    // "result" is a JSON string holding a list (older builds may send the list itself)
    auto res = item->getProperty ("result", {});
    if (res.isString()) res = juce::JSON::parse (res.toString());
    if (res.isArray())
        for (auto& r : *res.getArray())
        {
            if (const auto err = r.getProperty ("error", {}); ! err.isVoid() && err.toString().isNotEmpty())
                p.error = err.toString();
            const auto file = r.getProperty ("file", {}).toString();
            if (file.isEmpty()) continue;
            Take t;
            t.url = file;
            t.caption = r.getProperty ("prompt", {}).toString();
            t.lyrics = r.getProperty ("lyrics", {}).toString();
            const auto metas = r.getProperty ("metas", {});
            t.bpm = (double) metas.getProperty ("bpm", 0.0);
            t.seconds = (double) metas.getProperty ("duration", 0.0);
            t.key = metas.getProperty ("keyscale", {}).toString();
            t.timeSignature = metas.getProperty ("timesignature", {}).toString();
            p.takes.add (t);
        }
    if (p.status == 2 && p.error.isEmpty())
        p.error = p.progress.isNotEmpty() ? p.progress : juce::String ("ACE-Step says the song failed.");
    return p;
}

juce::String errorFrom (const juce::String& body, int status)
{
    const auto root = juce::JSON::parse (body);
    for (auto* field : { "error", "detail", "message" })
        if (const auto e = root.getProperty (field, {}); ! e.isVoid() && e.toString().isNotEmpty())
            return e.toString();
    if (status == 0) return "ACE-Step is not running (nothing answers at the server address).";
    if (status == 401 || status == 403) return "ACE-Step refused the API key (HTTP " + juce::String (status) + ").";
    return "ACE-Step answered HTTP " + juce::String (status) + ".";
}

//==============================================================================
juce::String httpPost (const juce::String& url, const juce::String& json, const juce::String& apiKey, int timeoutMs, int& status)
{
    status = 0;
    juce::URL u = juce::URL (url).withPOSTData (json);
    auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                    .withExtraHeaders (headersFor (apiKey, true))
                    .withConnectionTimeoutMs (timeoutMs)
                    .withStatusCode (&status);
    auto in = u.createInputStream (opts);
    return in != nullptr ? in->readEntireStreamAsString() : juce::String();
}

juce::String httpGet (const juce::String& url, const juce::String& apiKey, int timeoutMs, int& status)
{
    status = 0;
    auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                    .withExtraHeaders (headersFor (apiKey, false))
                    .withConnectionTimeoutMs (timeoutMs)
                    .withStatusCode (&status);
    auto in = juce::URL (url).createInputStream (opts);
    return in != nullptr ? in->readEntireStreamAsString() : juce::String();
}

bool httpDownload (const juce::String& url, const juce::String& apiKey, const juce::File& dest, int timeoutMs, int& status,
                   const std::function<bool()>& shouldStop)
{
    status = 0;
    auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                    .withExtraHeaders (headersFor (apiKey, false))
                    .withConnectionTimeoutMs (timeoutMs)
                    .withStatusCode (&status);
    auto in = juce::URL (url).createInputStream (opts);
    if (in == nullptr || status < 200 || status >= 300) return false;
    const auto tmp = dest.getSiblingFile (dest.getFileName() + ".part");
    tmp.deleteFile();
    {
        juce::FileOutputStream os (tmp);
        if (! os.openedOk()) return false;
        juce::HeapBlock<char> buf (65536);
        for (;;)
        {
            if (shouldStop && shouldStop()) { os.flush(); tmp.deleteFile(); return false; }
            const int n = in->read (buf.get(), 65536);
            if (n <= 0) break;
            if (! os.write (buf.get(), (size_t) n)) return false;
        }
        os.flush();
        if (os.getStatus().failed()) return false;
    }
    if (tmp.getSize() < 44) { tmp.deleteFile(); return false; }     // not even a WAV header
    dest.deleteFile();
    return tmp.moveFileTo (dest);
}

//==============================================================================
Job::Job (Request r, juce::String s, juce::String k, juce::File f, std::function<void (const juce::String&)> prog,
          std::function<void (Result)> done, bool onMessageThread)
    : juce::Thread ("SONG FORGE"), request (std::move (r)), server (std::move (s)), key (std::move (k)), folder (std::move (f)),
      progress (std::move (prog)), finished (std::move (done)), deliverOnMessageThread (onMessageThread)
{
}

Job::~Job()
{
    alive->store (false);
    signalThreadShouldExit();
    stopThread (15000);
}

void Job::report (const juce::String& text)
{
    if (! progress) return;
    if (! deliverOnMessageThread) { progress (text); return; }
    juce::MessageManager::callAsync ([a = alive, cb = progress, text] { if (a->load()) cb (text); });
}

void Job::finish (Result r)
{
    if (! finished) return;
    if (! deliverOnMessageThread) { finished (std::move (r)); return; }
    juce::MessageManager::callAsync ([a = alive, cb = finished, r = std::move (r)] { if (a->load()) cb (r); });
}

void Job::run()
{
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    Result result;
    auto elapsed = [t0] { return (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0; };

    report ("Sending the song to ACE-Step...");
    int status = 0;
    const auto reply = httpPost (joinUrl (server, "/release_task"), buildReleaseBody (request), key, 15000, status);
    juce::String taskId, error;
    if (status < 200 || status >= 300 || ! parseRelease (reply, taskId, error))
    {
        result.error = error.isNotEmpty() && status >= 200 && status < 300 ? error : errorFrom (reply, status);
        finish (result);
        return;
    }

    const auto queryBody = [&taskId]
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("task_id_list", juce::Array<juce::var> { taskId });
        return juce::JSON::toString (juce::var (o), true);
    }();

    Poll last;
    int misses = 0;
    for (;;)
    {
        if (threadShouldExit()) { result.cancelled = true; result.error = "Cancelled."; finish (result); return; }
        if (elapsed() > kMaxWaitSeconds) { result.error = "ACE-Step took longer than 45 minutes - stopped waiting."; finish (result); return; }
        wait (1000);
        if (threadShouldExit()) continue;
        const auto q = httpPost (joinUrl (server, "/query_result"), queryBody, key, 10000, status);
        if (status < 200 || status >= 300)
        {
            if (++misses >= 20) { result.error = errorFrom (q, status) + " (lost contact while making the song)"; finish (result); return; }
            continue;
        }
        misses = 0;
        last = parseQuery (q, taskId);
        if (last.status == 2 || last.status < 0) { result.error = last.error; finish (result); return; }
        if (last.status == 1) break;
        const int secs = (int) elapsed();
        report ("Making the song... " + juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2)
                + (last.progress.isNotEmpty() ? "   " + last.progress.substring (0, 90) : juce::String()));
    }

    if (last.takes.isEmpty()) { result.error = "ACE-Step finished but sent no audio."; finish (result); return; }
    if (! folder.createDirectory()) { result.error = "Cannot create " + folder.getFullPathName(); finish (result); return; }

    for (int i = 0; i < last.takes.size(); ++i)
    {
        auto t = last.takes[i];
        report ("Downloading take " + juce::String (i + 1) + " of " + juce::String (last.takes.size()) + "...");
        const auto ext = t.url.containsIgnoreCase (".wav") ? juce::String (".wav")
                       : t.url.containsIgnoreCase (".flac") ? juce::String (".flac")
                       : t.url.containsIgnoreCase (".mp3") ? juce::String (".mp3") : juce::String (".wav");
        const auto dest = folder.getChildFile ("SF Take " + juce::String (i + 1) + ext);
        int st = 0;
        if (! httpDownload (joinUrl (server, t.url), key, dest, 30000, st, [this] { return threadShouldExit(); }))
        {
            if (threadShouldExit()) { result.cancelled = true; result.error = "Cancelled."; finish (result); return; }
            result.error = "Could not download take " + juce::String (i + 1) + " (HTTP " + juce::String (st) + ").";
            finish (result);
            return;
        }
        t.file = dest;
        // ACE-Step's "bpm" is what its language model planned, not what the audio does (a take it called
        // 36 BPM measured about 89). Measure the downloaded audio and trust the measurement.
        report ("Measuring take " + juce::String (i + 1) + "...");
        t.bpm = measuredOrReported (dest, t.bpm);
        result.takes.add (t);
    }
    // keep what was asked for next to the audio
    {
        juce::String info;
        info << "STYLE: " << request.style << "\nLYRICS:\n" << (request.instrumental ? juce::String ("[Instrumental]") : request.lyrics)
             << "\n\nLength " << request.seconds << " s, BPM " << (request.bpm > 0 ? juce::String (request.bpm) : juce::String ("auto"))
             << ", key " << (request.key.isNotEmpty() ? request.key : juce::String ("auto")) << ", smart " << (request.smart ? "on" : "off") << "\n";
        for (int i = 0; i < result.takes.size(); ++i)
        {
            const auto& t = result.takes.getReference (i);
            info << "Take " << (i + 1) << ": " << t.bpm << " BPM, " << t.key << ", " << t.seconds << " s\n";
        }
        folder.getChildFile ("Song Forge request.txt").replaceWithText (info);
    }
    result.ok = true;
    result.secondsTaken = elapsed();
    finish (result);
}

//==============================================================================
HealthWatch::HealthWatch() : juce::Thread ("ACE-Step health")
{
    startThread (juce::Thread::Priority::low);
}

HealthWatch::~HealthWatch() { stopThread (6000); }

void HealthWatch::setServer (const juce::String& u, const juce::String& k)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    url = u;
    key = k;
}

void HealthWatch::run()
{
    while (! threadShouldExit())
    {
        juce::String u, k;
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            u = url; k = key;
        }
        bool ok = false;
        if (u.isNotEmpty())
        {
            int status = 0;
            const auto body = httpGet (joinUrl (u, "/health"), k, 1500, status);
            const auto root = juce::JSON::parse (body);
            ok = status == 200 && dataOf (root).getProperty ("status", {}).toString() == "ok";
        }
        up.store (ok);
        ++count;
        wait (2000);
    }
}
} // namespace pg::ace
