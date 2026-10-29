#include "hem/pg/Plugin.hpp"

#include <cstdlib>

namespace hem::pg
{
namespace
{
    constexpr double kCrossfadeMs = 5.0;

    params::Layout withBypass (params::Layout layout)
    {
        params::addBool (layout, "bypass", "Bypass", false);
        return layout;
    }
}

Plugin::Plugin (const PluginInfo& i, params::Layout layout) : info (i)
{
    for (auto& spec : withBypass (std::move (layout)))
        paramSet.add (std::move (spec));
    bypassParam = raw ("bypass");
    for (auto& r : readouts) r.store (0.0f);
}

std::atomic<float>* Plugin::raw (std::string_view id)
{
    auto* p = paramSet.find (id);
    // A plug-in asking for a control it never declared is a programming error: fail loudly.
    if (p == nullptr) std::abort();
    return p->rawPointer();
}

void Plugin::prepare (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRateHz = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxBlockSize = jmax (1, maximumExpectedSamplesPerBlock);
    alignedDryL.assign ((std::size_t) maxBlockSize, 0.0f);
    alignedDryR.assign ((std::size_t) maxBlockSize, 0.0f);
    prepareDsp (sampleRateHz, maxBlockSize);
    latency = jmax (0, dspLatency());
    dryDelayL.assign ((std::size_t) jmax (1, latency), 0.0f);
    dryDelayR.assign ((std::size_t) jmax (1, latency), 0.0f);
    dryDelayPos = 0;
    wetStep = (float) (1.0 / jmax (1.0, kCrossfadeMs * 0.001 * sampleRateHz));
    wetMix = isBypassedNow() ? 0.0f : 1.0f;
    wasFullyBypassed = wetMix == 0.0f;
    resetDsp();
}

void Plugin::reset() noexcept
{
    resetDsp();
    std::fill (dryDelayL.begin(), dryDelayL.end(), 0.0f);
    std::fill (dryDelayR.begin(), dryDelayR.end(), 0.0f);
    dryDelayPos = 0;
}

bool Plugin::isBypassedNow() const noexcept
{
    const bool own = bypassParam != nullptr && bypassParam->load (std::memory_order_relaxed) > 0.5f;
    const bool global = globalBypass != nullptr && globalBypass->load (std::memory_order_relaxed);
    const bool group = groupBypass != nullptr && groupBypass->load (std::memory_order_relaxed);
    return own || global || group;
}

void Plugin::processStereo (float* left, float* right, int numSamples) noexcept
{
    ScopedNoDenormals noDenormals;
    if (numSamples <= 0) return;

    if (numSamples > maxBlockSize)
    {
        for (int start = 0; start < numSamples; start += maxBlockSize)
            processStereo (left + start, right + start, jmin (maxBlockSize, numSamples - start));
        return;
    }

    float in = 0.0f;
    for (int i = 0; i < numSamples; ++i) in = jmax (in, std::abs (left[i]), std::abs (right[i]));
    inPeak.store (in, std::memory_order_relaxed);

    const float target = isBypassedNow() ? 0.0f : 1.0f;
    const bool steadyBypass = target == 0.0f && wetMix == 0.0f;

    float* dl = alignedDryL.data();
    float* dr = alignedDryR.data();
    if (latency == 0)
    {
        std::copy (left, left + numSamples, dl);
        std::copy (right, right + numSamples, dr);
    }
    else
    {
        for (int i = 0; i < numSamples; ++i)
        {
            dl[i] = dryDelayL[(std::size_t) dryDelayPos];
            dr[i] = dryDelayR[(std::size_t) dryDelayPos];
            dryDelayL[(std::size_t) dryDelayPos] = left[i];
            dryDelayR[(std::size_t) dryDelayPos] = right[i];
            if (++dryDelayPos >= latency) dryDelayPos = 0;
        }
    }

    if (steadyBypass)
    {
        // Fully bypassed: the DSP is skipped; a plug-in with latency still outputs its aligned dry.
        std::copy (dl, dl + numSamples, left);
        std::copy (dr, dr + numSamples, right);
        wasFullyBypassed = true;
    }
    else
    {
        if (wasFullyBypassed)
        {
            resetDsp();   // stale tails must not come back when the plug-in is switched on again
            wasFullyBypassed = false;
        }

        processDsp (left, right, numSamples);

        if (wetMix != target || target != 1.0f)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                wetMix = target > wetMix ? jmin (target, wetMix + wetStep) : jmax (target, wetMix - wetStep);
                left[i]  = dl[i] + wetMix * (left[i] - dl[i]);
                right[i] = dr[i] + wetMix * (right[i] - dr[i]);
            }
        }
    }

    float out = 0.0f;
    for (int i = 0; i < numSamples; ++i) out = jmax (out, std::abs (left[i]), std::abs (right[i]));
    outPeak.store (out, std::memory_order_relaxed);

    if (analyzerActive.load (std::memory_order_relaxed))
        pushAnalyzer (left, right, numSamples);
}

void Plugin::pushAnalyzer (const float* left, const float* right, int numSamples) noexcept
{
    int start1, size1, start2, size2;
    analyzerFifo.prepareToWrite (numSamples, start1, size1, start2, size2);
    for (int i = 0; i < size1; ++i) analyzerData[(std::size_t) (start1 + i)] = 0.5f * (left[i] + right[i]);
    for (int i = 0; i < size2; ++i) analyzerData[(std::size_t) (start2 + i)] = 0.5f * (left[size1 + i] + right[size1 + i]);
    analyzerFifo.finishedWrite (size1 + size2);   // when full, the newest samples are dropped
}

int Plugin::readAnalyzer (float* dest, int maxSamples) noexcept
{
    int start1, size1, start2, size2;
    analyzerFifo.prepareToRead (maxSamples, start1, size1, start2, size2);
    std::copy (analyzerData.begin() + start1, analyzerData.begin() + start1 + size1, dest);
    std::copy (analyzerData.begin() + start2, analyzerData.begin() + start2 + size2, dest + size1);
    analyzerFifo.finishedRead (size1 + size2);
    return size1 + size2;
}

//==============================================================================
PluginState Plugin::saveState() const
{
    PluginState s;
    s.pluginId = info.id;
    for (auto& p : paramSet.all())
        s.values.emplace_back (p->id(), p->get());
    return s;
}

void Plugin::loadState (const PluginState& state)
{
    // Only controls we know; a preset from a newer version may carry extra ones.
    for (auto& p : paramSet.all())
    {
        bool found = false;
        for (auto& [id, v] : state.values)
            if (id == p->id()) { p->setValue (v); found = true; break; }
        if (! found) p->resetToDefault();
    }
}

void Plugin::resetToDefaults()
{
    for (auto& p : paramSet.all()) p->resetToDefault();
}

bool Plugin::setParameter (std::string_view id, float value)
{
    if (auto* p = paramSet.find (id)) { p->setValue (value); return true; }
    return false;
}

bool Plugin::setRawParameter (std::string_view id, float value)
{
    if (auto* p = paramSet.find (id)) { p->rawPointer()->store (value); return true; }
    return false;
}

float Plugin::getParameter (std::string_view id) const
{
    if (auto* p = paramSet.find (id)) return p->get();
    return 0.0f;
}
} // namespace hem::pg
