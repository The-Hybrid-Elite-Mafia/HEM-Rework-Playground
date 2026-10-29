#pragma once
// Min / max overviews of the tracks' audio for drawing waveforms, built on a background thread.
// UI thread API; an overview never changes once it is published.
#include "Worker.hpp"
#include "media/SampleData.hpp"

#include <map>
#include <memory>
#include <vector>

namespace hem::app
{
struct WaveOverview
{
    static constexpr int kSamplesPerBucket = 256;
    std::vector<float> mins, maxs;   // mono (L+R)/2 per bucket
    double sourceRate = 48000.0;
    std::int64_t frames = 0;
};

class WaveCache
{
public:
    WaveCache() = default;
    ~WaveCache() { alive->store (false); worker.stop(); }

    /** The overview, or nullptr while it is still being built. */
    std::shared_ptr<const WaveOverview> get (const pg::SampleData::Ptr& s);
    /** Drops overviews of audio that is no longer in use. */
    void prune (const std::vector<const pg::SampleData*>& inUse);

private:
    struct Entry { pg::SampleData::Ptr audio; std::shared_ptr<const WaveOverview> overview; };
    std::map<const pg::SampleData*, Entry> ready;
    std::map<const pg::SampleData*, pg::SampleData::Ptr> pending;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    Worker worker { 2 };
};
} // namespace hem::app
