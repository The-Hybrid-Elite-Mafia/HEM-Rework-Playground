#pragma once
// =============================================================================
//  MessageQueue — hands work from worker threads to the app's message (UI) thread.
//  Workers call post(); the app's UI loop calls drain() a few times a second (or more), which runs
//  everything posted so far, in order, on the UI thread. Never used by the audio thread.
// =============================================================================
#include <functional>
#include <mutex>
#include <vector>

namespace hem::pg
{
class MessageQueue
{
public:
    static MessageQueue& instance() { static MessageQueue q; return q; }

    void post (std::function<void()> fn)
    {
        const std::scoped_lock sl (lock);
        pending.push_back (std::move (fn));
    }

    /** UI thread. Returns how many were run. */
    int drain()
    {
        std::vector<std::function<void()>> now;
        {
            const std::scoped_lock sl (lock);
            now.swap (pending);
        }
        for (auto& fn : now) fn();
        return (int) now.size();
    }

private:
    std::mutex lock;
    std::vector<std::function<void()>> pending;
};
} // namespace hem::pg
