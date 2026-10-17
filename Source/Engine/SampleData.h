#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace pg
{
/** Immutable decoded sample. Created on a background loader thread, published to
    the audio thread through HybridEngine's pending-slot handoff, and only ever
    destroyed on the message thread by HybridEngine::collectGarbage(). */
class SampleData final : public juce::ReferenceCountedObject
{
public:
    using Ptr = juce::ReferenceCountedObjectPtr<SampleData>;

    SampleData() = default;

    SampleData (juce::AudioBuffer<float>&& audio, double rate, juce::File source)
        : buffer (std::move (audio)), sourceRate (rate), file (std::move (source)) {}

    int getNumFrames() const noexcept   { return buffer.getNumSamples(); }
    int getNumChannels() const noexcept { return buffer.getNumChannels(); }
    double getSourceRate() const noexcept { return sourceRate; }
    const juce::File& getFile() const noexcept { return file; }   // message thread only
    const juce::AudioBuffer<float>& getBuffer() const noexcept { return buffer; }
    bool isEmpty() const noexcept { return buffer.getNumSamples() == 0; }

private:
    juce::AudioBuffer<float> buffer;
    double sourceRate = 44100.0;
    juce::File file;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleData)
};
} // namespace pg
