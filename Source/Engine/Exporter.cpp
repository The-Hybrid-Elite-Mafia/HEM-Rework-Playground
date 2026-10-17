#include "Exporter.h"
#include "Mp3Encoder.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <limits>

namespace pg
{
namespace { constexpr int kRenderBlock = 1024; }

OfflineExport::OfflineExport (const juce::ValueTree& session, const std::map<juce::String, SampleData::Ptr>& media, ExportOptions o)
    : options (std::move (o))
{
    JUCE_ASSERT_MESSAGE_THREAD
    engine.setNonRealtime (true);
    engine.prepareToPlay (options.sampleRate, kRenderBlock);
    engine.applySessionTree (session, media);
    engine.releaseResources();          // not a live device: garbage is freed at once
    engine.prepareToPlay (options.sampleRate, kRenderBlock);
    engine.setLoop (false, 0.0, 0.0);
    engine.setStopAtEnd (false);
    engine.setBeforeMode (options.beforeMode);
    engine.setMasterInsertsBypassed (options.clean);
    engine.setPositionSeconds (0.0);
    lengthSamples = (juce::int64) std::ceil ((engine.getSongLengthSeconds() + juce::jmax (0.0, options.tailSeconds)) * options.sampleRate);
    latency = engine.getTotalLatencySamples();
}

bool OfflineExport::render (const std::function<void (double)>& progress, const std::function<bool()>& shouldCancel, juce::String& error)
{
    if (options.mp3Kbps <= 0)
        return renderWav (options.file.withFileExtension (".wav"), progress, shouldCancel, error);

    // MP3: 16-bit WAV next to the target first (90 % of the bar), then Windows encodes it (last 10 %)
    const double rate = options.sampleRate;
    if (std::abs (rate - 44100.0) > 0.5 && std::abs (rate - 48000.0) > 0.5) { error = "MP3 needs 44.1 or 48 kHz."; return false; }
    const auto mp3 = getTargetFile();
    const auto wav = mp3.getSiblingFile (mp3.getFileNameWithoutExtension() + " (making mp3).wav");
    const int bits = options.bitDepth;
    options.bitDepth = 16;
    const bool rendered = renderWav (wav, [&progress] (double p) { if (progress) progress (p * 0.9); }, shouldCancel, error);
    options.bitDepth = bits;
    if (! rendered) { wav.deleteFile(); return false; }
    const bool ok = Mp3Encoder::encode (wav, mp3, options.mp3Kbps, error,
                                        [&progress] (double p) { if (progress) progress (0.9 + 0.1 * p); }, shouldCancel);
    wav.deleteFile();
    return ok;
}

bool OfflineExport::renderWav (const juce::File& target, const std::function<void (double)>& progress, const std::function<bool()>& shouldCancel, juce::String& error)
{
    if (options.bitDepth != 16 && options.bitDepth != 24 && options.bitDepth != 32) { error = "Unsupported bit depth."; return false; }
    if (engine.getSongLengthSeconds() <= 0.0) { error = "Nothing to export: load some tracks first."; return false; }

    if (! target.getParentDirectory().createDirectory()) { error = "Cannot create " + target.getParentDirectory().getFullPathName(); return false; }
    juce::TemporaryFile temp (target);

    std::unique_ptr<juce::AudioFormatWriter> writer;
    {
        auto fos = std::make_unique<juce::FileOutputStream> (temp.getFile());
        if (! fos->openedOk()) { error = "Cannot write to " + target.getFullPathName(); return false; }
        std::unique_ptr<juce::OutputStream> stream (std::move (fos));
        using SF = juce::AudioFormatWriterOptions::SampleFormat;
        const auto opts = juce::AudioFormatWriterOptions{}
                              .withSampleRate (options.sampleRate)
                              .withNumChannels (2)
                              .withBitsPerSample (options.bitDepth)
                              .withSampleFormat (options.bitDepth == 32 ? SF::floatingPoint : SF::integral);
        juce::WavAudioFormat wav;
        writer = wav.createWriterFor (stream, opts);
        if (writer == nullptr) { error = "The WAV writer refused " + juce::String (options.bitDepth) + "-bit / " + juce::String (options.sampleRate) + " Hz."; return false; }
    }

    engine.play();
    juce::AudioBuffer<float> block (2, kRenderBlock);
    juce::MidiBuffer midi;
    juce::Random rng (0x48454D);
    const float lsb = 1.0f / 32768.0f;
    const juce::int64 total = lengthSamples + latency;
    juce::int64 done = 0;
    peak = 0.0f;
    cleanGain = 1.0f;

    auto fail = [&] (const juce::String& e) { writer.reset(); temp.deleteTemporaryFile(); error = e; return false; };
    auto write = [&] (juce::AudioBuffer<float>& b, int start, int count)
    {
        if (options.bitDepth == 16)
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = b.getWritePointer (ch, start);
                for (int i = 0; i < count; ++i) d[i] += (rng.nextFloat() - rng.nextFloat()) * lsb;
            }
        return writer->writeFromAudioSampleBuffer (b, start, count);
    };

    // CLEAN: the whole mix is held (float) until its peak is known, then turned down if needed
    juce::AudioBuffer<float> whole;
    juce::int64 held = 0;
    if (options.clean)
    {
        if (lengthSamples > (juce::int64) std::numeric_limits<int>::max()) return fail ("Song too long for a clean export.");
        whole.setSize (2, (int) lengthSamples, false, false, true);
    }

    while (done < total)
    {
        if (shouldCancel && shouldCancel()) return fail ("Export cancelled.");
        const int n = (int) juce::jmin ((juce::int64) kRenderBlock, total - done);
        juce::AudioBuffer<float> view (block.getArrayOfWritePointers(), 2, n);
        engine.processBlock (view, midi);

        const int skip = (int) juce::jlimit ((juce::int64) 0, (juce::int64) n, (juce::int64) latency - done);   // limiter look-ahead
        const int count = (int) juce::jmin ((juce::int64) (n - skip), lengthSamples - held);
        if (count > 0)
        {
            if (options.clean)
            {
                for (int ch = 0; ch < 2; ++ch) whole.copyFrom (ch, (int) held, view, ch, skip, count);
            }
            else
            {
                peak = juce::jmax (peak, view.getMagnitude (skip, count));
                if (! write (view, skip, count)) return fail ("Disk write failed (disk full?).");
            }
            held += count;
        }
        done += n;
        if (progress) progress ((double) done / (double) total * (options.clean ? 0.9 : 1.0));
    }

    if (options.clean)
    {
        const float rawPeak = whole.getMagnitude (0, (int) held);
        const float limit = juce::Decibels::decibelsToGain (ExportOptions::kCleanPeakDb);
        cleanGain = rawPeak > limit ? limit / rawPeak : 1.0f;
        whole.applyGain (0, (int) held, cleanGain);
        peak = rawPeak * cleanGain;
        for (int st = 0; st < (int) held; st += 65536)
        {
            if (shouldCancel && shouldCancel()) return fail ("Export cancelled.");
            if (! write (whole, st, juce::jmin (65536, (int) held - st))) return fail ("Disk write failed (disk full?).");
        }
        if (progress) progress (1.0);
    }

    writer.reset();
    if (! temp.overwriteTargetFileWithTemporary()) { error = "Cannot replace " + target.getFullPathName() + " (is it open in another program?)"; return false; }
    return true;
}
} // namespace pg
