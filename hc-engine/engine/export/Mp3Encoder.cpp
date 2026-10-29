#include "Mp3Encoder.hpp"
#include "WavFile.hpp"
#include "dr_wav.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <mfapi.h>
 #include <mfidl.h>
 #include <mfreadwrite.h>
 #include <mferror.h>
#endif

namespace hem::pg
{
#if defined(_WIN32)
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

    std::string hr (const char* what, HRESULT h)
    {
        char b[32];
        std::snprintf (b, sizeof b, "%x", (unsigned) h);
        return std::string (what) + " failed (0x" + b + ")";
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
    HRESULT findMp3Type (UINT32 rate, UINT32 channels, int kbps, IMFMediaType** out, std::string& error)
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
        if (! best) { error = "The Windows MP3 encoder has no setting for " + std::to_string (rate) + " Hz / " + std::to_string (channels) + " channel(s)."; return E_FAIL; }
        best.p->AddRef();
        *out = best.p;
        return S_OK;
    }
}

bool Mp3Encoder::encode (const std::filesystem::path& wavIn, const std::filesystem::path& mp3Out, int kbps, std::string& error,
                         const std::function<void (double)>& progress, const std::function<bool()>& shouldCancel)
{
    drwav wav;
    if (! drwav_init_file_w (&wav, wavIn.c_str(), nullptr)) { error = "Cannot read " + pathText (wavIn.filename()); return false; }
    struct WavCloser { drwav* w; ~WavCloser() { drwav_uninit (w); } } wavCloser { &wav };
    const auto rate = (UINT32) wav.sampleRate;
    const auto fileChannels = (UINT32) wav.channels;
    const auto channels = (UINT32) std::clamp (fileChannels, 1u, 2u);
    if (rate != 44100 && rate != 48000) { error = "MP3 needs 44.1 or 48 kHz (this file is " + std::to_string (rate) + " Hz)."; return false; }

    MfSession mf;
    if (! mf.mfOk) { error = "Windows Media Foundation is not available (" + hr ("MFStartup", mf.mfResult) + ")"; return false; }

    Com<IMFMediaType> outType;
    if (FAILED (findMp3Type (rate, channels, kbps, outType.put(), error))) return false;

    std::error_code ec;
    std::filesystem::create_directories (mp3Out.parent_path(), ec);
    if (! std::filesystem::is_directory (mp3Out.parent_path(), ec)) { error = "Cannot create " + pathText (mp3Out.parent_path()); return false; }
    TempFile temp (mp3Out);
    Com<IMFSinkWriter> writer;
    HRESULT h = MFCreateSinkWriterFromURL (temp.path().c_str(), nullptr, nullptr, writer.put());
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
    std::vector<std::int16_t> in ((size_t) kChunk * fileChannels);
    const auto total = (std::int64_t) wav.totalPCMFrameCount;
    for (std::int64_t pos = 0; pos < total; pos += kChunk)
    {
        if (shouldCancel && shouldCancel()) { error = "Export cancelled."; writer->Finalize(); return false; }
        const int n = (int) std::min<std::int64_t> (kChunk, total - pos);
        if (drwav_read_pcm_frames_s16 (&wav, (drwav_uint64) n, in.data()) != (drwav_uint64) n) { error = "Cannot read " + pathText (wavIn.filename()); return false; }

        const DWORD bytes = (DWORD) (n * (int) channels * 2);
        Com<IMFMediaBuffer> mb;
        if (FAILED (h = MFCreateMemoryBuffer (bytes, mb.put()))) { error = hr ("MP3 buffer", h); return false; }
        BYTE* data = nullptr;
        mb->Lock (&data, nullptr, nullptr);
        auto* out = reinterpret_cast<std::int16_t*> (data);
        // Same steps as 0.10.0: the 16-bit sample read back as a float (x / 32768), then x 32767 and rounded.
        for (int i = 0; i < n; ++i)
            for (int ch = 0; ch < (int) channels; ++ch)
            {
                const float f = (float) in[(size_t) i * fileChannels + (size_t) ch] * (float) (1.0 / 32768.0);
                out[i * (int) channels + ch] = (std::int16_t) (int) std::nearbyint ((double) (std::clamp (f, -1.0f, 1.0f) * 32767.0f));
            }
        mb->Unlock();
        mb->SetCurrentLength (bytes);

        Com<IMFSample> sample;
        MFCreateSample (sample.put());
        sample->AddBuffer (mb.p);
        sample->SetSampleTime ((LONGLONG) (pos * 10000000 / (std::int64_t) rate));
        sample->SetSampleDuration ((LONGLONG) ((std::int64_t) n * 10000000 / (std::int64_t) rate));
        if (FAILED (h = writer->WriteSample (stream, sample.p))) { error = hr ("Writing the MP3", h); return false; }
        if (progress) progress ((double) (pos + n) / (double) total);
    }
    if (FAILED (h = writer->Finalize())) { error = hr ("Finishing the MP3", h); return false; }
    writer.put();   // release the file before moving it into place
    if (! temp.replaceTarget()) { error = "Cannot replace " + pathText (mp3Out) + " (is it open in another program?)"; return false; }
    return true;
}
#else
bool Mp3Encoder::encode (const std::filesystem::path&, const std::filesystem::path&, int, std::string& error,
                         const std::function<void (double)>&, const std::function<bool()>&)
{
    error = "MP3 export uses the encoder built into Windows; it is not available on this system.";
    return false;
}
#endif
} // namespace hem::pg
