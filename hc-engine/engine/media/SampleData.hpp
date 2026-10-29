#pragma once
// =============================================================================
//  SampleData — one decoded audio file, never changed after it is made.
//  Made on a loader thread, handed to the audio thread through an atomic pointer, and only freed by
//  the engine's collectGarbage() once the audio thread can no longer see it.
// =============================================================================
#include <filesystem>
#include <memory>
#include <vector>

namespace hem::pg
{
class SampleData final
{
public:
    using Ptr = std::shared_ptr<const SampleData>;

    /** `channels` holds 1 or 2 vectors of the same length. */
    SampleData (std::vector<std::vector<float>> channels, double rate, std::filesystem::path source)
        : chans (std::move (channels)), sourceRate (rate), file (std::move (source)) {}

    int getNumFrames() const noexcept { return chans.empty() ? 0 : (int) chans[0].size(); }
    int getNumChannels() const noexcept { return (int) chans.size(); }
    double getSourceRate() const noexcept { return sourceRate; }
    const std::filesystem::path& getFile() const noexcept { return file; }
    /** Channel c (0 or 1). A mono file returns its one channel for both. */
    const float* channel (int c) const noexcept { return chans[(std::size_t) (c < (int) chans.size() ? c : 0)].data(); }
    bool isEmpty() const noexcept { return getNumFrames() == 0; }

private:
    std::vector<std::vector<float>> chans;
    double sourceRate = 44100.0;
    std::filesystem::path file;
};
} // namespace hem::pg
