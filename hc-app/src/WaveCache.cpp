#include "WaveCache.hpp"

#include <algorithm>

namespace hem::app
{
std::shared_ptr<const WaveOverview> WaveCache::get (const pg::SampleData::Ptr& s)
{
    if (s == nullptr) return nullptr;
    if (auto it = ready.find (s.get()); it != ready.end()) return it->second.overview;
    if (pending.count (s.get()) > 0) return nullptr;
    pending[s.get()] = s;
    auto flag = alive;
    worker.add ([this, s, flag]
    {
        auto ov = std::make_shared<WaveOverview>();
        const int n = s->getNumFrames();
        const int buckets = (n + WaveOverview::kSamplesPerBucket - 1) / WaveOverview::kSamplesPerBucket;
        ov->mins.resize ((std::size_t) buckets);
        ov->maxs.resize ((std::size_t) buckets);
        ov->sourceRate = s->getSourceRate();
        ov->frames = n;
        const float* l = s->channel (0);
        const float* r = s->channel (1);
        for (int k = 0; k < buckets && flag->load(); ++k)
        {
            float lo = 0.0f, hi = 0.0f;
            const int end = std::min (n, (k + 1) * WaveOverview::kSamplesPerBucket);
            for (int i = k * WaveOverview::kSamplesPerBucket; i < end; ++i)
            {
                const float v = 0.5f * (l[i] + r[i]);
                lo = std::min (lo, v);
                hi = std::max (hi, v);
            }
            ov->mins[(std::size_t) k] = lo;
            ov->maxs[(std::size_t) k] = hi;
        }
        Worker::toUi ([this, s, ov, flag]
        {
            if (! flag->load()) return;
            pending.erase (s.get());
            ready[s.get()] = Entry { s, ov };
        });
    });
    return nullptr;
}

void WaveCache::prune (const std::vector<const pg::SampleData*>& inUse)
{
    for (auto it = ready.begin(); it != ready.end();)
        it = std::find (inUse.begin(), inUse.end(), it->first) != inUse.end() ? std::next (it) : ready.erase (it);
}
} // namespace hem::app
