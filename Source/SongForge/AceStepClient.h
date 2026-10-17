#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <functional>
#include <memory>

namespace pg::ace
{
/** SONG FORGE talks to ACE-Step 1.5's own REST server (start_api_server.bat, default
    http://127.0.0.1:8001) running on this computer. Nothing here touches the audio thread: every
    request runs on a worker thread and results come back on the message thread.

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
    juce::String style;          // ACE-Step "caption": genre, instruments, mood, vocal type
    juce::String lyrics;         // with [Verse] / [Chorus] tags; empty or instrumental -> "[Instrumental]"
    bool instrumental = false;
    double seconds = 180.0;      // 10 .. 600
    int bpm = 0;                 // 0 = let the model decide
    juce::String key;            // "" = model decides, else e.g. "A minor"
    juce::String timeSignature;  // "" or "2", "3", "4", "6"
    juce::String language = "en";
    int takes = 2;               // batch_size 1 .. 4
    bool smart = true;           // ACE-Step "thinking": the 5 Hz LM plans the song first (needs > 6 GB VRAM)
    int seed = -1;               // -1 = random
};

/** JSON body for POST /release_task. WAV out, so nothing is lost before SUNO FIX / mixing. */
juce::String buildReleaseBody (const Request&);

/** data.task_id from a /release_task reply. False + message when the server refused. */
bool parseRelease (const juce::String& replyBody, juce::String& taskId, juce::String& error);

/** Tempo of a downloaded take: measured from the audio; ACE-Step's own number only when it agrees
    (same tempo, or exactly double / half). Falls back to the reported number if the file can't be read. */
double measuredOrReported (const juce::File& audioFile, double reportedBpm);

struct Take
{
    juce::String url;            // "/v1/audio?path=..." (relative to the server)
    juce::String caption, lyrics, key, timeSignature;
    double bpm = 0.0, seconds = 0.0;
    juce::File file;             // after download
};

struct Poll
{
    int status = 0;              // 0 running, 1 done, 2 failed, -1 reply not understood
    juce::String progress;       // ACE-Step's progress line, when it gives one
    juce::String error;
    juce::Array<Take> takes;
};

/** One entry of a /query_result reply. */
Poll parseQuery (const juce::String& replyBody, const juce::String& taskId);

/** "error" from a wrapped reply, or the HTTP status text. */
juce::String errorFrom (const juce::String& replyBody, int httpStatus);

//==============================================================================
struct Result
{
    bool ok = false;
    bool cancelled = false;
    juce::String error;
    juce::Array<Take> takes;     // downloaded, file set
    double secondsTaken = 0.0;
};

/** release -> poll every second -> download every take into `folder`. */
class Job final : private juce::Thread
{
public:
    /** onMessageThread = false calls back on the worker thread (tests). */
    Job (Request, juce::String server, juce::String apiKey, juce::File folder,
         std::function<void (const juce::String&)> progress,
         std::function<void (Result)> finished, bool onMessageThread = true);
    ~Job() override;                        // cancels and waits (bounded)

    void start() { startThread(); }
    void cancel() { signalThreadShouldExit(); }
    bool isRunning() const { return isThreadRunning(); }

    static constexpr int kMaxWaitSeconds = 45 * 60;

private:
    void run() override;
    void report (const juce::String&);
    void finish (Result);

    Request request;
    juce::String server, key;
    juce::File folder;
    std::function<void (const juce::String&)> progress;
    std::function<void (Result)> finished;
    bool deliverOnMessageThread;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};

/** HTTP helpers (worker threads only). status = HTTP status, 0 = no connection. */
juce::String httpPost (const juce::String& url, const juce::String& json, const juce::String& apiKey, int timeoutMs, int& status);
juce::String httpGet (const juce::String& url, const juce::String& apiKey, int timeoutMs, int& status);
bool httpDownload (const juce::String& url, const juce::String& apiKey, const juce::File& dest, int timeoutMs, int& status,
                   const std::function<bool()>& shouldStop);

/** Background check of GET /health every 2 s while it exists. isUp() is safe from any thread. */
class HealthWatch final : private juce::Thread
{
public:
    HealthWatch();
    ~HealthWatch() override;
    void setServer (const juce::String& url, const juce::String& apiKey);   // message thread
    bool isUp() const noexcept { return up.load(); }
    int checks() const noexcept { return count.load(); }

private:
    void run() override;
    juce::SpinLock lock;            // guards url / key (message thread writes, worker reads; never the audio thread)
    juce::String url, key;
    std::atomic<bool> up { false };
    std::atomic<int> count { 0 };
};
} // namespace pg::ace
