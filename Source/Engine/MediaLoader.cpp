#include "MediaLoader.h"
#include "SincResampler.h"

namespace pg
{
namespace
{
    void registerFormats (juce::AudioFormatManager& fm) { fm.registerBasicFormats(); }
    std::atomic<double> targetRate { 0.0 };
}

void MediaLoader::setTargetRate (double rate) noexcept { targetRate.store (rate); }
double MediaLoader::getTargetRate() noexcept { return targetRate.load(); }

SampleData::Ptr MediaLoader::decode (const juce::File& file, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "File not found: " + file.getFullPathName();
        return nullptr;
    }
    juce::AudioFormatManager fm;
    registerFormats (fm);
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Not an audio file this app can read: " + file.getFileName();
        return nullptr;
    }
    if (reader->sampleRate < 8000.0 || reader->sampleRate > 384000.0)
    {
        error = "Unsupported sample rate in " + file.getFileName();
        return nullptr;
    }
    const auto maxFrames = (juce::int64) (kMaxMinutes * 60.0 * reader->sampleRate);
    if (reader->lengthInSamples <= 0 || reader->lengthInSamples > maxFrames)
    {
        error = file.getFileName() + " is empty or longer than " + juce::String ((int) kMaxMinutes) + " minutes.";
        return nullptr;
    }
    const int channels = (int) juce::jlimit (1u, 2u, reader->numChannels);
    const int frames = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> audio;
    try { audio.setSize (channels, frames); }
    catch (const std::bad_alloc&) { error = "Not enough memory for " + file.getFileName(); return nullptr; }
    if (! reader->read (&audio, 0, frames, 0, true, channels > 1))
    {
        error = "Could not read " + file.getFileName();
        return nullptr;
    }
    const double want = targetRate.load();
    const auto inRate = (std::uint32_t) std::llround (reader->sampleRate);
    const auto outRate = (std::uint32_t) std::llround (want);
    if (want >= 8000.0 && inRate != outRate && std::abs (reader->sampleRate - (double) inRate) < 1.0e-6)
    {
        const SincResampler rs (inRate, outRate);
        const auto outFrames = rs.outputLength ((std::uint64_t) frames);
        if (outFrames > 0 && outFrames < (std::uint64_t) std::numeric_limits<int>::max())
        {
            juce::AudioBuffer<float> converted;
            try { converted.setSize (channels, (int) outFrames); }
            catch (const std::bad_alloc&) { error = "Not enough memory for " + file.getFileName(); return nullptr; }
            for (int ch = 0; ch < channels; ++ch)
                rs.process (audio.getReadPointer (ch), (std::uint64_t) frames, converted.getWritePointer (ch));
            return new SampleData (std::move (converted), (double) outRate, file);
        }
    }
    return new SampleData (std::move (audio), reader->sampleRate, file);
}

juce::String MediaLoader::wildcard()
{
    juce::AudioFormatManager fm;
    registerFormats (fm);
    return fm.getWildcardForAllFormats();
}

bool MediaLoader::isAudioFile (const juce::File& f)
{
    juce::AudioFormatManager fm;
    registerFormats (fm);
    return fm.findFormatForFileExtension (f.getFileExtension()) != nullptr;
}

std::map<juce::String, SampleData::Ptr> MediaLoader::decodeAll (const juce::StringArray& paths, juce::StringArray& failed,
                                                                const std::function<bool()>& shouldCancel)
{
    std::map<juce::String, SampleData::Ptr> out;
    for (const auto& p : paths)
    {
        if (shouldCancel && shouldCancel()) break;
        if (p.isEmpty() || out.count (p) > 0) continue;
        juce::String err;
        if (auto s = decode (juce::File (p), err)) out[p] = s;
        else failed.add (err);
    }
    return out;
}
} // namespace pg
