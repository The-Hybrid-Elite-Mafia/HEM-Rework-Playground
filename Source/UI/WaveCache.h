#pragma once

#include "Engine/SampleData.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

namespace pg::ui
{
/** Min / max overview of a track's audio for drawing waveforms, built on a background thread.
    Message thread API; the overview is immutable once published. */
struct WaveOverview
{
    static constexpr int kSamplesPerBucket = 256;
    std::vector<float> mins, maxs;   // mono (L+R)/2 per bucket
    double sourceRate = 48000.0;
    juce::int64 frames = 0;
};

class WaveCache
{
public:
    WaveCache();
    ~WaveCache();

    /** The overview, or nullptr while it is still being built (then onReady is called). */
    std::shared_ptr<const WaveOverview> get (const SampleData::Ptr&);
    std::function<void()> onReady;
    /** Drops overviews of audio that is no longer in use. */
    void prune (const juce::Array<SampleData*>& inUse);

private:
    struct Entry { SampleData::Ptr audio; std::shared_ptr<const WaveOverview> overview; };   // the Ptr keeps the key's address unique
    std::map<const SampleData*, Entry> ready;
    std::map<const SampleData*, SampleData::Ptr> pending;   // keeps the audio alive while building
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withNumberOfThreads (2).withThreadName ("Waveforms") };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};
} // namespace pg::ui
