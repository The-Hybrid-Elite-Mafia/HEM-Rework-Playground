#include "WaveCache.h"

namespace pg::ui
{
WaveCache::WaveCache() = default;

WaveCache::~WaveCache()
{
    alive->store (false);
    pool.removeAllJobs (true, 5000);
}

std::shared_ptr<const WaveOverview> WaveCache::get (const SampleData::Ptr& s)
{
    if (s == nullptr) return nullptr;
    if (auto it = ready.find (s.get()); it != ready.end()) return it->second.overview;
    if (pending.count (s.get()) > 0) return nullptr;
    pending[s.get()] = s;
    auto flag = alive;
    pool.addJob ([this, s, flag]
    {
        auto ov = std::make_shared<WaveOverview>();
        const auto& b = s->getBuffer();
        const int n = b.getNumSamples();
        const int buckets = (n + WaveOverview::kSamplesPerBucket - 1) / WaveOverview::kSamplesPerBucket;
        ov->mins.resize ((size_t) buckets);
        ov->maxs.resize ((size_t) buckets);
        ov->sourceRate = s->getSourceRate();
        ov->frames = n;
        const float* l = b.getReadPointer (0);
        const float* r = b.getReadPointer (b.getNumChannels() > 1 ? 1 : 0);
        for (int k = 0; k < buckets && flag->load(); ++k)
        {
            float lo = 0.0f, hi = 0.0f;
            const int end = juce::jmin (n, (k + 1) * WaveOverview::kSamplesPerBucket);
            for (int i = k * WaveOverview::kSamplesPerBucket; i < end; ++i)
            {
                const float v = 0.5f * (l[i] + r[i]);
                lo = juce::jmin (lo, v); hi = juce::jmax (hi, v);
            }
            ov->mins[(size_t) k] = lo; ov->maxs[(size_t) k] = hi;
        }
        juce::MessageManager::callAsync ([this, s, ov, flag]
        {
            if (! flag->load()) return;
            pending.erase (s.get());
            ready[s.get()] = Entry { s, ov };
            if (onReady) onReady();
        });
    });
    return nullptr;
}

void WaveCache::prune (const juce::Array<SampleData*>& inUse)
{
    for (auto it = ready.begin(); it != ready.end();)
        it = inUse.contains (const_cast<SampleData*> (it->first)) ? std::next (it) : ready.erase (it);
}
} // namespace pg::ui
