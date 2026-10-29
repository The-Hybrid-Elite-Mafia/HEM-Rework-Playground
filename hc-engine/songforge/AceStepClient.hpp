#pragma once

#include "hem/pg/Json.hpp"

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace hem::pg::ace
{
/** SONG FORGE talks to ACE-Step 1.5's own REST server (start_api_server.bat, default
    http://127.0.0.1:8001) running on this computer. Nothing here touches the audio thread: every
    request runs on a worker thread and results come back on the message thread (MessageQueue).

    Protocol (ACE-Step 1.5 docs/en/API.md, checked against acestep/api/http/ (Python)):
      POST /release_task   JSON           -> { data: { task_id, status: "queued", queue_position } }
      POST /query_result   { task_id_list } -> { data: [ { task_id, status: 0|1|2, progress_text,
                                                     result: "<JSON string: [ { file, metas, ... } ]>" } ] }
      GET  /v1/audio?path=...              -> the audio file
      GET  /health                         -> { data: { status: "ok", service: "ACE-Step API" } }
    Every reply is wrapped as { data, code, error, timestamp, extra }. */
inline constexpr const char* kDefaultServer = "http://127.0.0.1:8001";

struct Request
{
    std::string style;           // ACE-Step "caption": genre, instruments, mood, vocal type
    std::string lyrics;          // with [Verse] / [Chorus] tags; empty or instrumental -> "[Instrumental]"
    bool instrumental = false;
    double seconds = 180.0;      // 10 .. 600
    int bpm = 0;                 // 0 = let the model decide
    std::string key;             // "" = model decides, else e.g. "A minor"
    std::string timeSignature;   // "" or "2", "3", "4", "6"
    std::string language = "en";
    int takes = 2;               // batch_size 1 .. 4
    bool smart = true;           // ACE-Step "thinking": the 5 Hz LM plans the song first (needs > 6 GB VRAM)
    int seed = -1;               // -1 = random
};

/** JSON body for POST /release_task. WAV out, so nothing is lost before SUNO FIX / mixing. */
std::string buildReleaseBody (const Request&);

/** data.task_id from a /release_task reply. False + message when the server refused. */
bool parseRelease (const std::string& replyBody, std::string& taskId, std::string& error);

/** Tempo of a downloaded take: measured from the audio; ACE-Step's own number only when it agrees
    (same tempo, or exactly double / half). Falls back to the reported number if the file can't be read. */
double measuredOrReported (const std::filesystem::path& audioFile, double reportedBpm);

struct Take
{
    std::string url;             // "/v1/audio?path=..." (relative to the server)
    std::string caption, lyrics, key, timeSignature;
    double bpm = 0.0, seconds = 0.0;
    std::filesystem::path file;  // after download
};

struct Poll
{
    int status = 0;              // 0 running, 1 done, 2 failed, -1 reply not understood
    std::string progress;        // ACE-Step's progress line, when it gives one
    std::string error;
    std::vector<Take> takes;
};

/** One entry of a /query_result reply. */
Poll parseQuery (const std::string& replyBody, const std::string& taskId);

/** "error" from a wrapped reply, or the HTTP status text. */
std::string errorFrom (const std::string& replyBody, int httpStatus);

/** Server address + path ("http://127.0.0.1:8001" + "/health"). */
std::string joinUrl (const std::string& server, const std::string& path);

//==============================================================================
struct Result
{
    bool ok = false;
    bool cancelled = false;
    std::string error;
    std::vector<Take> takes;     // downloaded, file set
    double secondsTaken = 0.0;
};

/** release -> poll every second -> download every take into `folder`. */
class Job final
{
public:
    /** onMessageThread = false calls back on the worker thread (tests). */
    Job (Request, std::string server, std::string apiKey, std::filesystem::path folder,
         std::function<void (const std::string&)> progress,
         std::function<void (Result)> finished, bool onMessageThread = true);
    ~Job();                                 // cancels and waits

    void start();
    void cancel();
    bool isRunning() const { return running.load(); }

    static constexpr int kMaxWaitSeconds = 45 * 60;

private:
    void run();
    bool shouldExit() const { return stopFlag.load(); }
    /** Waits up to ms, returns early when cancelled. */
    void waitFor (int ms);
    void report (const std::string&);
    void finish (Result);

    Request request;
    std::string server, key;
    std::filesystem::path folder;
    std::function<void (const std::string&)> progress;
    std::function<void (Result)> finished;
    bool deliverOnMessageThread;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    std::atomic<bool> stopFlag { false }, running { false };
    std::mutex waitLock;
    std::condition_variable waitCv;
    std::thread worker;
};

/** HTTP helpers (worker threads only). status = HTTP status, 0 = no connection. */
std::string httpPost (const std::string& url, const std::string& json, const std::string& apiKey, int timeoutMs, int& status);
std::string httpGet (const std::string& url, const std::string& apiKey, int timeoutMs, int& status);
bool httpDownload (const std::string& url, const std::string& apiKey, const std::filesystem::path& dest, int timeoutMs, int& status,
                   const std::function<bool()>& shouldStop);

/** Background check of GET /health every 2 s while it exists. isUp() is safe from any thread. */
class HealthWatch final
{
public:
    HealthWatch();
    ~HealthWatch();
    void setServer (const std::string& url, const std::string& apiKey);   // message thread
    bool isUp() const noexcept { return up.load(); }
    int checks() const noexcept { return count.load(); }

private:
    void run();
    std::mutex lock;                // guards url / key (message thread writes, worker reads; never the audio thread)
    std::string url, key;
    std::atomic<bool> up { false }, stopFlag { false };
    std::atomic<int> count { 0 };
    std::mutex waitLock;
    std::condition_variable waitCv;
    std::thread worker;
};
} // namespace hem::pg::ace
