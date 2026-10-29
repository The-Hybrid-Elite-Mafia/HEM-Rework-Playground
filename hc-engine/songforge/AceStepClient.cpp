#include "AceStepClient.hpp"
#include "AudioFacts.hpp"
#include "hem/pg/Files.hpp"
#include "hem/pg/MessageQueue.hpp"
#include "media/MediaLoader.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <winhttp.h>
#endif

namespace hem::pg::ace
{
namespace
{
    std::string trim (const std::string& s)
    {
        const auto a = s.find_first_not_of (" \t\r\n");
        if (a == std::string::npos) return {};
        const auto b = s.find_last_not_of (" \t\r\n");
        return s.substr (a, b - a + 1);
    }

    const Json& dataOf (const Json& root)
    {
        if (root.isObject() && root.has ("data")) return root["data"];
        return root;          // tolerate an unwrapped reply
    }

    std::string lower (std::string s)
    {
        for (auto& c : s) c = (char) std::tolower ((unsigned char) c);
        return s;
    }

    std::string numberText (double v)
    {
        char b[64];
        std::snprintf (b, sizeof b, "%.10g", v);
        return b;
    }

    Json parseOrNull (const std::string& body)
    {
        auto j = Json::parse (body);
        return j ? *j : Json();
    }
}

double measuredOrReported (const std::filesystem::path& audioFile, double reportedBpm)
{
    std::string err;
    const auto data = MediaLoader::decode (audioFile, err);
    if (data == nullptr || data->isEmpty()) return reportedBpm;
    AudioBuffer buf (data->getNumChannels(), data->getNumFrames());
    for (int c = 0; c < data->getNumChannels(); ++c) buf.copyFrom (c, 0, data->channel (c), data->getNumFrames());
    const auto facts = library::measure (buf, data->getSourceRate());
    if (facts.bpm <= 0.0) return reportedBpm;
    // if ACE-Step's number is the same tempo (or exactly double / half of it), keep its round number
    for (const double k : { 1.0, 2.0, 0.5 })
        if (reportedBpm > 0.0 && std::abs (reportedBpm * k - facts.bpm) <= facts.bpm * 0.03)
            return reportedBpm * k;
    return facts.bpm;
}

std::string joinUrl (const std::string& server, const std::string& path)
{
    auto s = trim (server);
    while (! s.empty() && s.back() == '/') s.pop_back();
    if (path.rfind ("http://", 0) == 0 || path.rfind ("https://", 0) == 0) return path;
    return s + (! path.empty() && path[0] == '/' ? path : "/" + path);
}

//==============================================================================
std::string buildReleaseBody (const Request& r)
{
    Json o = Json::object();
    o.set ("prompt", trim (r.style));
    const auto lyrics = trim (r.lyrics);
    o.set ("lyrics", (r.instrumental || lyrics.empty()) ? std::string ("[Instrumental]") : lyrics);
    o.set ("vocal_language", ! r.language.empty() ? r.language : std::string ("en"));
    o.set ("audio_format", "wav");
    o.set ("audio_duration", jlimit (10.0, 600.0, r.seconds));
    if (r.bpm >= 30 && r.bpm <= 300) o.set ("bpm", r.bpm);
    if (! r.key.empty()) o.set ("key_scale", r.key);
    if (! r.timeSignature.empty()) o.set ("time_signature", r.timeSignature);
    o.set ("batch_size", jlimit (1, 4, r.takes));
    o.set ("thinking", r.smart);
    if (r.seed >= 0)
    {
        o.set ("use_random_seed", false);
        o.set ("seed", r.seed);
    }
    else
        o.set ("use_random_seed", true);
    return o.dump (false);
}

bool parseRelease (const std::string& body, std::string& taskId, std::string& error)
{
    const auto root = parseOrNull (body);
    if (! root.isObject()) { error = "ACE-Step sent a reply that is not JSON."; return false; }
    if (const auto& e = root["error"]; ! e.isNull() && ! e.toString().empty())
    {
        error = e.toString();
        return false;
    }
    const auto id = dataOf (root)["task_id"].toString();
    if (id.empty()) { error = "ACE-Step did not return a task id."; return false; }
    taskId = id;
    return true;
}

Poll parseQuery (const std::string& body, const std::string& taskId)
{
    Poll p;
    const auto root = parseOrNull (body);
    const auto& list = dataOf (root);
    if (! list.isArray()) { p.status = -1; p.error = "ACE-Step sent a reply that is not understood."; return p; }
    const Json* item = nullptr;
    for (auto& v : list.items())
        if (v["task_id"].toString() == taskId) { item = &v; break; }
    if (item == nullptr) { p.status = -1; p.error = "ACE-Step does not know this task."; return p; }

    p.status = item->has ("status") ? (*item)["status"].toInt() : 0;
    p.progress = (*item)["progress_text"].toString();

    // "result" is a JSON string holding a list (older builds may send the list itself)
    Json res = (*item)["result"];
    if (res.isString()) res = parseOrNull (res.toString());
    if (res.isArray())
        for (auto& r : res.items())
        {
            if (const auto& err = r["error"]; ! err.isNull() && ! err.toString().empty())
                p.error = err.toString();
            const auto file = r["file"].toString();
            if (file.empty()) continue;
            Take t;
            t.url = file;
            t.caption = r["prompt"].toString();
            t.lyrics = r["lyrics"].toString();
            const auto& metas = r["metas"];
            t.bpm = metas.has ("bpm") ? metas["bpm"].toDouble() : 0.0;
            t.seconds = metas.has ("duration") ? metas["duration"].toDouble() : 0.0;
            t.key = metas["keyscale"].toString();
            t.timeSignature = metas["timesignature"].toString();
            p.takes.push_back (t);
        }
    if (p.status == 2 && p.error.empty())
        p.error = ! p.progress.empty() ? p.progress : std::string ("ACE-Step says the song failed.");
    return p;
}

std::string errorFrom (const std::string& body, int status)
{
    const auto root = parseOrNull (body);
    for (auto* field : { "error", "detail", "message" })
        if (const auto& e = root[field]; ! e.isNull() && ! e.toString().empty())
            return e.toString();
    if (status == 0) return "ACE-Step is not running (nothing answers at the server address).";
    if (status == 401 || status == 403) return "ACE-Step refused the API key (HTTP " + std::to_string (status) + ").";
    return "ACE-Step answered HTTP " + std::to_string (status) + ".";
}

//==============================================================================
#if defined(_WIN32)
namespace
{
    std::wstring widen (const std::string& s)
    {
        if (s.empty()) return {};
        const int n = MultiByteToWideChar (CP_UTF8, 0, s.data(), (int) s.size(), nullptr, 0);
        std::wstring w ((size_t) n, L'\0');
        MultiByteToWideChar (CP_UTF8, 0, s.data(), (int) s.size(), w.data(), n);
        return w;
    }

    struct Handle
    {
        HINTERNET h = nullptr;
        ~Handle() { if (h != nullptr) WinHttpCloseHandle (h); }
    };

    /** One HTTP request. `sink` gets the body in pieces (return false to stop). */
    bool request (const std::string& url, const char* method, const std::string& body, const std::string& apiKey, bool json,
                  int timeoutMs, int& status, const std::function<bool (const char*, size_t)>& sink)
    {
        status = 0;
        const auto wurl = widen (url);
        URL_COMPONENTS uc {};
        uc.dwStructSize = sizeof uc;
        wchar_t host[256] {}, path[4096] {};
        uc.lpszHostName = host; uc.dwHostNameLength = 255;
        uc.lpszUrlPath = path; uc.dwUrlPathLength = 4000;
        wchar_t extra[4096] {};
        uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 4000;
        if (! WinHttpCrackUrl (wurl.c_str(), 0, 0, &uc)) return false;
        const std::wstring target = std::wstring (path, uc.dwUrlPathLength) + std::wstring (extra, uc.dwExtraInfoLength);

        Handle session { WinHttpOpen (L"HEM Rework Playground", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0) };
        if (session.h == nullptr) return false;
        WinHttpSetTimeouts (session.h, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
        Handle connect { WinHttpConnect (session.h, std::wstring (host, uc.dwHostNameLength).c_str(), uc.nPort, 0) };
        if (connect.h == nullptr) return false;
        const DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
        Handle req { WinHttpOpenRequest (connect.h, widen (method).c_str(), target.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags) };
        if (req.h == nullptr) return false;
        std::string headers;
        if (json) headers += "Content-Type: application/json\r\n";
        if (! apiKey.empty()) headers += "Authorization: Bearer " + apiKey + "\r\n";
        const auto wh = widen (headers);
        if (! WinHttpSendRequest (req.h, wh.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wh.c_str(), wh.empty() ? 0 : (DWORD) -1L,
                                  body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID) body.data(), (DWORD) body.size(), (DWORD) body.size(), 0))
            return false;
        if (! WinHttpReceiveResponse (req.h, nullptr)) return false;
        DWORD code = 0, size = sizeof code;
        WinHttpQueryHeaders (req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
        status = (int) code;
        std::vector<char> buf (65536);
        for (;;)
        {
            DWORD got = 0;
            if (! WinHttpReadData (req.h, buf.data(), (DWORD) buf.size(), &got)) return false;
            if (got == 0) break;
            if (! sink (buf.data(), got)) return false;
        }
        return true;
    }
}

std::string httpPost (const std::string& url, const std::string& json, const std::string& apiKey, int timeoutMs, int& status)
{
    std::string out;
    if (! request (url, "POST", json, apiKey, true, timeoutMs, status, [&] (const char* d, size_t n) { out.append (d, n); return true; }))
        if (status == 0) return {};
    return out;
}

std::string httpGet (const std::string& url, const std::string& apiKey, int timeoutMs, int& status)
{
    std::string out;
    if (! request (url, "GET", {}, apiKey, false, timeoutMs, status, [&] (const char* d, size_t n) { out.append (d, n); return true; }))
        if (status == 0) return {};
    return out;
}

bool httpDownload (const std::string& url, const std::string& apiKey, const std::filesystem::path& dest, int timeoutMs, int& status,
                   const std::function<bool()>& shouldStop)
{
    auto tmp = dest;
    tmp += ".part";
    std::error_code ec;
    std::filesystem::remove (tmp, ec);
    bool ok = false;
    {
        std::ofstream os (tmp, std::ios::binary | std::ios::trunc);
        if (! os) return false;
        bool stopped = false;
        ok = request (url, "GET", {}, apiKey, false, timeoutMs, status, [&] (const char* d, size_t n)
        {
            if (status < 200 || status >= 300) return false;
            if (shouldStop && shouldStop()) { stopped = true; return false; }
            os.write (d, (std::streamsize) n);
            return (bool) os;
        });
        os.close();
        if (stopped || ! os) ok = false;
    }
    if (! ok || status < 200 || status >= 300) { std::filesystem::remove (tmp, ec); return false; }
    if (std::filesystem::file_size (tmp, ec) < 44) { std::filesystem::remove (tmp, ec); return false; }   // not even a WAV header
    std::filesystem::remove (dest, ec);
    std::filesystem::rename (tmp, dest, ec);
    return ! ec;
}
#else
std::string httpPost (const std::string&, const std::string&, const std::string&, int, int& status) { status = 0; return {}; }
std::string httpGet (const std::string&, const std::string&, int, int& status) { status = 0; return {}; }
bool httpDownload (const std::string&, const std::string&, const std::filesystem::path&, int, int& status, const std::function<bool()>&) { status = 0; return false; }
#endif

//==============================================================================
Job::Job (Request r, std::string s, std::string k, std::filesystem::path f, std::function<void (const std::string&)> prog,
          std::function<void (Result)> done, bool onMessageThread)
    : request (std::move (r)), server (std::move (s)), key (std::move (k)), folder (std::move (f)),
      progress (std::move (prog)), finished (std::move (done)), deliverOnMessageThread (onMessageThread)
{
}

Job::~Job()
{
    alive->store (false);
    cancel();
    if (worker.joinable()) worker.join();
}

void Job::start()
{
    if (running.exchange (true)) return;
    if (worker.joinable()) worker.join();   // a finished earlier run
    stopFlag.store (false);
    worker = std::thread ([this] { run(); running.store (false); });
}

void Job::cancel()
{
    stopFlag.store (true);
    const std::scoped_lock sl (waitLock);
    waitCv.notify_all();
}

void Job::waitFor (int ms)
{
    std::unique_lock ul (waitLock);
    waitCv.wait_for (ul, std::chrono::milliseconds (ms), [this] { return stopFlag.load(); });
}

void Job::report (const std::string& text)
{
    if (! progress) return;
    if (! deliverOnMessageThread) { progress (text); return; }
    MessageQueue::instance().post ([a = alive, cb = progress, text] { if (a->load()) cb (text); });
}

void Job::finish (Result r)
{
    if (! finished) return;
    if (! deliverOnMessageThread) { finished (std::move (r)); return; }
    MessageQueue::instance().post ([a = alive, cb = finished, r = std::move (r)] { if (a->load()) cb (r); });
}

void Job::run()
{
    const auto t0 = std::chrono::steady_clock::now();
    Result result;
    auto elapsed = [t0] { return std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count(); };

    report ("Sending the song to ACE-Step...");
    int status = 0;
    const auto reply = httpPost (joinUrl (server, "/release_task"), buildReleaseBody (request), key, 15000, status);
    std::string taskId, error;
    if (status < 200 || status >= 300 || ! parseRelease (reply, taskId, error))
    {
        result.error = ! error.empty() && status >= 200 && status < 300 ? error : errorFrom (reply, status);
        finish (result);
        return;
    }

    Json q = Json::object();
    Json ids = Json::array();
    ids.push (taskId);
    q.set ("task_id_list", std::move (ids));
    const auto queryBody = q.dump (false);

    Poll last;
    int misses = 0;
    for (;;)
    {
        if (shouldExit()) { result.cancelled = true; result.error = "Cancelled."; finish (result); return; }
        if (elapsed() > kMaxWaitSeconds) { result.error = "ACE-Step took longer than 45 minutes - stopped waiting."; finish (result); return; }
        waitFor (1000);
        if (shouldExit()) continue;
        const auto qr = httpPost (joinUrl (server, "/query_result"), queryBody, key, 10000, status);
        if (status < 200 || status >= 300)
        {
            if (++misses >= 20) { result.error = errorFrom (qr, status) + " (lost contact while making the song)"; finish (result); return; }
            continue;
        }
        misses = 0;
        last = parseQuery (qr, taskId);
        if (last.status == 2 || last.status < 0) { result.error = last.error; finish (result); return; }
        if (last.status == 1) break;
        const int secs = (int) elapsed();
        char t[32];
        std::snprintf (t, sizeof t, "%d:%02d", secs / 60, secs % 60);
        report (std::string ("Making the song... ") + t + (! last.progress.empty() ? "   " + last.progress.substr (0, 90) : std::string()));
    }

    if (last.takes.empty()) { result.error = "ACE-Step finished but sent no audio."; finish (result); return; }
    std::error_code ec;
    std::filesystem::create_directories (folder, ec);
    if (! std::filesystem::is_directory (folder, ec)) { result.error = "Cannot create " + files::toText (folder); finish (result); return; }

    for (size_t i = 0; i < last.takes.size(); ++i)
    {
        auto t = last.takes[i];
        report ("Downloading take " + std::to_string (i + 1) + " of " + std::to_string (last.takes.size()) + "...");
        const auto u = lower (t.url);
        const std::string ext = u.find (".wav") != std::string::npos ? ".wav"
                              : u.find (".flac") != std::string::npos ? ".flac"
                              : u.find (".mp3") != std::string::npos ? ".mp3" : ".wav";
        const auto dest = folder / ("SF Take " + std::to_string (i + 1) + ext);
        int st = 0;
        if (! httpDownload (joinUrl (server, t.url), key, dest, 30000, st, [this] { return shouldExit(); }))
        {
            if (shouldExit()) { result.cancelled = true; result.error = "Cancelled."; finish (result); return; }
            result.error = "Could not download take " + std::to_string (i + 1) + " (HTTP " + std::to_string (st) + ").";
            finish (result);
            return;
        }
        t.file = dest;
        // ACE-Step's "bpm" is what its language model planned, not what the audio does (a take it called
        // 36 BPM measured about 89). Measure the downloaded audio and trust the measurement.
        report ("Measuring take " + std::to_string (i + 1) + "...");
        t.bpm = measuredOrReported (dest, t.bpm);
        result.takes.push_back (t);
    }
    // keep what was asked for next to the audio
    {
        std::ostringstream info;
        info << "STYLE: " << request.style << "\nLYRICS:\n" << (request.instrumental ? std::string ("[Instrumental]") : request.lyrics)
             << "\n\nLength " << numberText (request.seconds) << " s, BPM " << (request.bpm > 0 ? std::to_string (request.bpm) : std::string ("auto"))
             << ", key " << (! request.key.empty() ? request.key : std::string ("auto")) << ", smart " << (request.smart ? "on" : "off") << "\n";
        for (size_t i = 0; i < result.takes.size(); ++i)
        {
            const auto& t = result.takes[i];
            info << "Take " << (i + 1) << ": " << numberText (t.bpm) << " BPM, " << t.key << ", " << numberText (t.seconds) << " s\n";
        }
        files::writeTextAtomic (folder / "Song Forge request.txt", info.str());
    }
    result.ok = true;
    result.secondsTaken = elapsed();
    finish (result);
}

//==============================================================================
HealthWatch::HealthWatch()
{
    worker = std::thread ([this] { run(); });
#if defined(_WIN32)
    SetThreadPriority ((HANDLE) worker.native_handle(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
}

HealthWatch::~HealthWatch()
{
    stopFlag.store (true);
    {
        const std::scoped_lock sl (waitLock);
        waitCv.notify_all();
    }
    if (worker.joinable()) worker.join();
}

void HealthWatch::setServer (const std::string& u, const std::string& k)
{
    const std::scoped_lock sl (lock);
    url = u;
    key = k;
}

void HealthWatch::run()
{
    while (! stopFlag.load())
    {
        std::string u, k;
        {
            const std::scoped_lock sl (lock);
            u = url; k = key;
        }
        bool ok = false;
        if (! u.empty())
        {
            int status = 0;
            const auto body = httpGet (joinUrl (u, "/health"), k, 1500, status);
            const auto root = parseOrNull (body);
            ok = status == 200 && dataOf (root)["status"].toString() == "ok";
        }
        up.store (ok);
        ++count;
        std::unique_lock ul (waitLock);
        waitCv.wait_for (ul, std::chrono::milliseconds (2000), [this] { return stopFlag.load(); });
    }
}
} // namespace hem::pg::ace
