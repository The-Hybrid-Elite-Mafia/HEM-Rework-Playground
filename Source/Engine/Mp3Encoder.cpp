#include "Mp3Encoder.h"
#include <juce_audio_formats/juce_audio_formats.h>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <mfapi.h>
 #include <mfidl.h>
 #include <mfreadwrite.h>
 #include <mferror.h>
#endif

namespace pg
{
#if JUCE_WINDOWS
namespace
{
    /** Minimal COM holder (no ATL / WRL needed). */
    template <typename T>
    struct Com
    {
        T* p = nullptr;
        ~Com() { if (p != nullptr) p->Release(); }
        T** put() { if (p != nullptr) { p->Release(); p = nullptr; } return &p; }
        T* operator->() const { return p; }
        explicit operator bool() const { return p != nullptr; }
    };

    juce::String hr (const char* what, HRESULT h)
    {
        return juce::String (what) + " failed (0x" + juce::String::toHexString ((juce::uint32) h) + ")";
    }

    struct MfSession
    {
        bool comOwned = false, mfOk = false;
        HRESULT mfResult = S_OK;
        MfSession()
        {
            const HRESULT c = CoInitializeEx (nullptr, COINIT_MULTITHREADED);
            comOwned = SUCCEEDED (c);   // S_OK / S_FALSE: ours to release; RPC_E_CHANGED_MODE: already set up by someone else
            mfResult = MFStartup (MF_VERSION, MFSTARTUP_LITE);
            mfOk = SUCCEEDED (mfResult);
        }
        ~MfSession()
        {
            if (mfOk) MFShutdown();
            if (comOwned) CoUninitialize();
        }
    };

    /** Picks the encoder's MP3 output type for this rate / channel count with the bitrate closest to kbps (not above, unless nothing lower). */
    HRESULT findMp3Type (UINT32 rate, UINT32 channels, int kbps, IMFMediaType** out, juce::String& error)
    {
        Com<IMFCollection> types;
        HRESULT h = MFTranscodeGetAudioOutputAvailableTypes (MFAudioFormat_MP3, MFT_ENUM_FLAG_ALL, nullptr, types.put());
        if (FAILED (h)) { error = "Windows has no MP3 encoder here (" + hr ("MFTranscodeGetAudioOutputAvailableTypes", h) + ")"; return h; }
        DWORD n = 0;
        types->GetElementCount (&n);
        const UINT32 wantBytes = (UINT32) (kbps * 1000 / 8);
        Com<IMFMediaType> best;
        UINT32 bestBytes = 0;
        for (DWORD i = 0; i < n; ++i)
        {
            Com<IUnknown> unk;
            if (FAILED (types->GetElement (i, unk.put()))) continue;
            Com<IMFMediaType> t;
            if (FAILED (unk->QueryInterface (IID_PPV_ARGS (t.put())))) continue;
            UINT32 r = 0, c = 0, bytes = 0;
            t->GetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, &r);
            t->GetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, &c);
            t->GetUINT32 (MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &bytes);
            if (r != rate || c != channels) continue;
            const bool better = ! best
                             || (bytes <= wantBytes && (bestBytes > wantBytes || bytes > bestBytes))
                             || (bytes > wantBytes && bestBytes > wantBytes && bytes < bestBytes);
            if (better) { t.p->AddRef(); *best.put() = t.p; bestBytes = bytes; }
        }
        if (! best) { error = "The Windows MP3 encoder has no setting for " + juce::String (rate) + " Hz / " + juce::String (channels) + " channel(s)."; return E_FAIL; }
        best.p->AddRef();
        *out = best.p;
        return S_OK;
    }
}

bool Mp3Encoder::encode (const juce::File& wavIn, const juce::File& mp3Out, int kbps, juce::String& error,
                         const std::function<void (double)>& progress, const std::function<bool()>& shouldCancel)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (wavIn));
    if (reader == nullptr) { error = "Cannot read " + wavIn.getFileName(); return false; }
    const auto rate = (UINT32) std::llround (reader->sampleRate);
    const auto channels = (UINT32) juce::jlimit (1u, 2u, reader->numChannels);
    if (rate != 44100 && rate != 48000) { error = "MP3 needs 44.1 or 48 kHz (this file is " + juce::String (rate) + " Hz)."; return false; }

    MfSession mf;
    if (! mf.mfOk) { error = "Windows Media Foundation is not available (" + hr ("MFStartup", mf.mfResult) + ")"; return false; }

    Com<IMFMediaType> outType;
    if (FAILED (findMp3Type (rate, channels, kbps, outType.put(), error))) return false;

    if (! mp3Out.getParentDirectory().createDirectory()) { error = "Cannot create " + mp3Out.getParentDirectory().getFullPathName(); return false; }
    juce::TemporaryFile temp (mp3Out);
    Com<IMFSinkWriter> writer;
    HRESULT h = MFCreateSinkWriterFromURL (temp.getFile().getFullPathName().toWideCharPointer(), nullptr, nullptr, writer.put());
    if (FAILED (h)) { error = hr ("Creating the MP3 file", h); return false; }

    DWORD stream = 0;
    if (FAILED (h = writer->AddStream (outType.p, &stream))) { error = hr ("MP3 stream", h); return false; }

    Com<IMFMediaType> inType;
    MFCreateMediaType (inType.put());
    inType->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    inType->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_PCM);
    inType->SetUINT32 (MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    inType->SetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
    inType->SetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, channels);
    inType->SetUINT32 (MF_MT_AUDIO_BLOCK_ALIGNMENT, channels * 2);
    inType->SetUINT32 (MF_MT_AUDIO_AVG_BYTES_PER_SECOND, rate * channels * 2);
    inType->SetUINT32 (MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    if (FAILED (h = writer->SetInputMediaType (stream, inType.p, nullptr))) { error = hr ("MP3 input format", h); return false; }
    if (FAILED (h = writer->BeginWriting())) { error = hr ("Starting the MP3", h); return false; }

    constexpr int kChunk = 4096;
    juce::AudioBuffer<float> buf ((int) channels, kChunk);
    const juce::int64 total = reader->lengthInSamples;
    for (juce::int64 pos = 0; pos < total; pos += kChunk)
    {
        if (shouldCancel && shouldCancel()) { error = "Export cancelled."; writer->Finalize(); temp.deleteTemporaryFile(); return false; }
        const int n = (int) juce::jmin ((juce::int64) kChunk, total - pos);
        reader->read (&buf, 0, n, pos, true, channels > 1);

        const DWORD bytes = (DWORD) (n * (int) channels * 2);
        Com<IMFMediaBuffer> mb;
        if (FAILED (h = MFCreateMemoryBuffer (bytes, mb.put()))) { error = hr ("MP3 buffer", h); return false; }
        BYTE* data = nullptr;
        mb->Lock (&data, nullptr, nullptr);
        auto* out = reinterpret_cast<juce::int16*> (data);
        for (int i = 0; i < n; ++i)
            for (int ch = 0; ch < (int) channels; ++ch)
                out[i * (int) channels + ch] = (juce::int16) juce::roundToInt (juce::jlimit (-1.0f, 1.0f, buf.getSample (ch, i)) * 32767.0f);
        mb->Unlock();
        mb->SetCurrentLength (bytes);

        Com<IMFSample> sample;
        MFCreateSample (sample.put());
        sample->AddBuffer (mb.p);
        sample->SetSampleTime ((LONGLONG) (pos * 10000000 / (juce::int64) rate));
        sample->SetSampleDuration ((LONGLONG) ((juce::int64) n * 10000000 / (juce::int64) rate));
        if (FAILED (h = writer->WriteSample (stream, sample.p))) { error = hr ("Writing the MP3", h); return false; }
        if (progress) progress ((double) (pos + n) / (double) total);
    }
    if (FAILED (h = writer->Finalize())) { error = hr ("Finishing the MP3", h); return false; }
    writer.put();   // release the file before moving it into place
    if (! temp.overwriteTargetFileWithTemporary()) { error = "Cannot replace " + mp3Out.getFullPathName() + " (is it open in another program?)"; return false; }
    return true;
}
#else
bool Mp3Encoder::encode (const juce::File&, const juce::File&, int, juce::String& error,
                         const std::function<void (double)>&, const std::function<bool()>&)
{
    error = "MP3 export uses the encoder built into Windows; it is not available on this system.";
    return false;
}
#endif
} // namespace pg
