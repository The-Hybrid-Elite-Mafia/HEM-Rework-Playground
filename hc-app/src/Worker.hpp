#pragma once
// A small pool of background threads for slow jobs (decoding files, drawing waveform overviews,
// measuring tempo). Results go back to the UI thread through hem::pg::MessageQueue.
// Never used by the audio thread.
#include "hem/pg/MessageQueue.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace hem::app
{
class Worker
{
public:
    explicit Worker (int threads = 1)
    {
        for (int i = 0; i < threads; ++i) pool.emplace_back ([this] { run(); });
    }

    ~Worker() { stop(); }

    /** Waits for the job that is running, drops the ones still queued. */
    void stop()
    {
        {
            const std::scoped_lock sl (lock);
            quitting = true;
            jobs.clear();
        }
        wake.notify_all();
        for (auto& t : pool) if (t.joinable()) t.join();
        pool.clear();
    }

    void add (std::function<void()> job)
    {
        {
            const std::scoped_lock sl (lock);
            if (quitting) return;
            jobs.push_back (std::move (job));
        }
        wake.notify_one();
    }

    /** Runs `fn` on the UI thread (next frame). */
    static void toUi (std::function<void()> fn) { pg::MessageQueue::instance().post (std::move (fn)); }

private:
    void run()
    {
        for (;;)
        {
            std::function<void()> job;
            {
                std::unique_lock ul (lock);
                wake.wait (ul, [this] { return quitting || ! jobs.empty(); });
                if (quitting) return;
                job = std::move (jobs.front());
                jobs.pop_front();
            }
            job();
        }
    }

    std::mutex lock;
    std::condition_variable wake;
    std::deque<std::function<void()>> jobs;
    bool quitting = false;
    std::vector<std::thread> pool;
};
} // namespace hem::app
