#include "PlaygroundEngine.hpp"
#include "media/MediaLoader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace hem::pg
{
namespace
{
    constexpr double kGainRampSeconds = 0.02;

    inline float faderGain (float db) noexcept { return db <= PlaygroundEngine::kFaderOffDb + 0.05f ? 0.0f : params::dbToGain (db); }

    StringArray busChoices()
    {
        StringArray s;
        for (int b = 0; b < PlaygroundEngine::kNumBuses; ++b) s.push_back ("Bus " + std::to_string (b + 1));
        return s;
    }

    StringArray outChoices()
    {
        auto s = busChoices();
        s.insert (s.begin(), "Master");
        return s;
    }

    constexpr uint32 kPalette[] { 0xffe0a526, 0xff2ec8e6, 0xffe0533c, 0xff5bc94a, 0xffa45be0, 0xff1fb59a, 0xffe05ba8, 0xff3a7be0 };

    /** Colour as the 0.10.0 sessions wrote it: 8 lower-case hex digits, alpha first ("ffe0a526"). */
    std::string colourText (uint32 c)
    {
        char b[16];
        std::snprintf (b, sizeof b, "%08x", (unsigned) c);
        return b;
    }

    /** Reads a colour the way 0.10.0 did: the hex digits in the text, last 8 kept. */
    uint32 colourFrom (const std::string& s, uint32 fallback)
    {
        uint32 v = 0;
        int digits = 0;
        for (char ch : s)
        {
            int d = -1;
            if (ch >= '0' && ch <= '9') d = ch - '0';
            else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
            else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
            if (d < 0) continue;
            v = (v << 4) | (uint32) d;
            ++digits;
        }
        return digits > 0 ? v : fallback;
    }

    template <typename T> bool inRange (T v, T end) noexcept { return v >= 0 && v < end; }
}

uint32 PlaygroundEngine::paletteColour (int track) noexcept { return kPalette[(track < 0 ? 0 : track) % 8]; }

params::Layout PlaygroundEngine::createLayout()
{
    params::Layout l;
    for (int t = 0; t < kMaxTracks; ++t)
    {
        const auto n = "Track " + std::to_string (t + 1) + " ";
        params::addDb (l, Ids::track (t, "vol"), n + "Volume", kFaderOffDb, 12.0f, 0.0f);
        params::addFloat (l, Ids::track (t, "pan"), n + "Pan", -1.0f, 1.0f, 0.0f, "", 0.01f);
        params::addBool (l, Ids::track (t, "mute"), n + "Mute", false);
        params::addBool (l, Ids::track (t, "solo"), n + "Solo", false);
        params::addChoice (l, Ids::track (t, "out"), n + "Output", outChoices(), 0);
        params::addDb (l, Ids::track (t, "sa"), n + "Send A", kFaderOffDb, 6.0f, kFaderOffDb);
        params::addChoice (l, Ids::track (t, "sa_bus"), n + "Send A Bus", busChoices(), 3);   // Reverb
        params::addDb (l, Ids::track (t, "sb"), n + "Send B", kFaderOffDb, 6.0f, kFaderOffDb);
        params::addChoice (l, Ids::track (t, "sb_bus"), n + "Send B Bus", busChoices(), 4);   // Delay
    }
    for (int b = 0; b < kNumBuses; ++b)
    {
        const auto n = "Bus " + std::to_string (b + 1) + " ";
        params::addDb (l, Ids::bus (b, "vol"), n + "Volume", kFaderOffDb, 12.0f, 0.0f);
        params::addFloat (l, Ids::bus (b, "pan"), n + "Pan", -1.0f, 1.0f, 0.0f, "", 0.01f);
        params::addBool (l, Ids::bus (b, "mute"), n + "Mute", false);
    }
    params::addDb (l, Ids::masterVol, "Master Volume", kFaderOffDb, 6.0f, 0.0f);
    params::addFloat (l, Ids::bpm, "Tempo", 40.0f, 240.0f, 120.0f, "BPM", 0.1f);
    return l;
}

PlaygroundEngine::PlaygroundEngine()
{
    for (auto& spec : createLayout()) mixerParams.add (std::move (spec));
    auto rawOf = [this] (const std::string& id) { return mixerParams.find (id)->rawPointer(); };
    for (int t = 0; t < kMaxTracks; ++t)
    {
        auto& s = strips[(size_t) t];
        s.vol = rawOf (Ids::track (t, "vol"));
        s.pan = rawOf (Ids::track (t, "pan"));
        s.mute = rawOf (Ids::track (t, "mute"));
        s.solo = rawOf (Ids::track (t, "solo"));
        s.out = rawOf (Ids::track (t, "out"));
        s.sa = rawOf (Ids::track (t, "sa"));
        s.saBus = rawOf (Ids::track (t, "sa_bus"));
        s.sb = rawOf (Ids::track (t, "sb"));
        s.sbBus = rawOf (Ids::track (t, "sb_bus"));
        tracks[(size_t) t].colour = kPalette[t % 8];
    }
    for (int b = 0; b < kNumBuses; ++b)
    {
        auto& s = strips[(size_t) busStrip (b)];
        s.vol = rawOf (Ids::bus (b, "vol"));
        s.pan = rawOf (Ids::bus (b, "pan"));
        s.mute = rawOf (Ids::bus (b, "mute"));
    }
    strips[(size_t) kMaster].vol = rawOf (Ids::masterVol);
    for (auto& m : meters) for (auto& c : m) c.store (0.0f);
}

PlaygroundEngine::~PlaygroundEngine()
{
    for (auto& s : strips)
        for (auto& p : s.slot) p.store (nullptr);
}

bool PlaygroundEngine::setMixer (std::string_view id, float value)
{
    if (auto* p = mixerParams.find (id)) { p->setValue (value); return true; }
    return false;
}

float PlaygroundEngine::getMixer (std::string_view id) const
{
    const auto* p = mixerParams.find (id);
    return p != nullptr ? p->get() : 0.0f;
}

//==============================================================================
void PlaygroundEngine::prepare (double sampleRate, int maximumBlockSize)
{
    const std::scoped_lock sl (structureLock);
    const double newFs = sampleRate > 0.0 ? sampleRate : 48000.0;
    MediaLoader::setTargetRate (newFs);   // files loaded from now on are converted once, at full quality
    const double oldFs = fs.load();
    position.store ((int64) std::llround ((double) position.load() * newFs / oldFs));
    fs.store (newFs);
    maxBlock.store (jmax (16, maximumBlockSize));
    const int mb = maxBlock.load();

    bufStride = mb;
    trackBuf.assign ((size_t) (2 * mb), 0.0f);
    busBuf.assign ((size_t) (2 * kNumBuses * mb), 0.0f);
    masterBuf.assign ((size_t) (2 * mb), 0.0f);
    directBuf.assign ((size_t) (2 * mb), 0.0f);
    lineLength = kMaxCompensation + mb;
    trackLines.assign ((size_t) (2 * kMaxTracks) * (size_t) lineLength, 0.0f);
    busLines.assign ((size_t) (2 * kNumBuses) * (size_t) lineLength, 0.0f);
    directLines.assign ((size_t) 2 * (size_t) lineLength, 0.0f);
    linePos = 0;

    for (auto& s : strips)
    {
        for (auto& p : s.owned)
            if (p != nullptr) p->prepare (newFs, mb);
        for (auto* v : { &s.gainL, &s.gainR, &s.sendA, &s.sendB })
        {
            v->reset (newFs, kGainRampSeconds);
            v->setCurrentAndTargetValue (0.0f);
        }
    }
    updateLatencies();
    snapGains();   // start at the real fader positions: an export must not fade in its first 20 ms
    audioRunning.store (true);
}

void PlaygroundEngine::snapGains() noexcept
{
    auto panGains = [] (float pan, float& gl, float& gr) noexcept
    {
        gl = pan > 0.0f ? 1.0f - pan : 1.0f;
        gr = pan < 0.0f ? 1.0f + pan : 1.0f;
    };
    bool anySolo = false;
    for (int t = 0; t < kMaxTracks; ++t)
        if (tracks[(size_t) t].used.load() && strips[(size_t) t].solo->load() > 0.5f) anySolo = true;
    for (int i = 0; i < kNumStrips; ++i)
    {
        auto& st = strips[(size_t) i];
        float g = faderGain (st.vol->load());
        float gl = 1.0f, gr = 1.0f;
        if (st.mute != nullptr && st.mute->load() > 0.5f) g = 0.0f;
        if (isTrack (i) && anySolo && st.solo->load() <= 0.5f) g = 0.0f;
        if (st.pan != nullptr) panGains (st.pan->load(), gl, gr);
        st.gainL.setCurrentAndTargetValue (i == kMaster ? g : g * gl);
        st.gainR.setCurrentAndTargetValue (i == kMaster ? g : g * gr);
        if (isTrack (i))
        {
            st.sendA.setCurrentAndTargetValue (g * faderGain (st.sa->load()));
            st.sendB.setCurrentAndTargetValue (g * faderGain (st.sb->load()));
        }
    }
}

void PlaygroundEngine::release()
{
    audioRunning.store (false);
}

void PlaygroundEngine::process (float* const* outputs, int chans, int n) noexcept
{
    ScopedNoDenormals noDenormals;
    const int mb = jmin (maxBlock.load (std::memory_order_relaxed), bufStride);
    const double rate = fs.load (std::memory_order_relaxed);

    const double loc = locateRequest.exchange (-1.0);
    if (loc >= 0.0) position.store ((int64) std::llround (loc * rate));

    int64 pos = position.load();
    int done = 0;
    while (done < n)
    {
        int len = jmin (mb, n - done);
        const bool play = playing.load (std::memory_order_relaxed);

        if (play && loopOn.load (std::memory_order_relaxed))
        {
            const auto ls = (int64) std::llround (loopStart.load() * rate);
            const auto le = (int64) std::llround (loopEnd.load() * rate);
            if (le - ls >= 64)
            {
                if (pos >= le || pos < ls) pos = ls;
                len = (int) jmin ((int64) len, le - pos);
            }
        }

        processChunk (pos, play, len);

        const float* mL = buf (masterBuf, 0);
        const float* mR = buf (masterBuf, 1);
        if (chans >= 2)
        {
            std::copy (mL, mL + len, outputs[0] + done);
            std::copy (mR, mR + len, outputs[1] + done);
            for (int c = 2; c < chans; ++c) std::fill (outputs[c] + done, outputs[c] + done + len, 0.0f);
        }
        else if (chans == 1)
        {
            float* o = outputs[0] + done;
            for (int i = 0; i < len; ++i) o[i] = 0.5f * (mL[i] + mR[i]);
        }

        // LIBRARY preview, added after the master (trackBuf is free again after processChunk)
        if (previewOn.load (std::memory_order_relaxed))
        {
            const auto* pm = preview.load (std::memory_order_acquire);
            const double seek = previewSeek.exchange (-1.0);
            int64 pp = seek >= 0.0 ? (int64) std::llround (seek * rate) : previewPos.load (std::memory_order_relaxed);
            if (pm != nullptr && ! pm->isEmpty())
            {
                const auto endPos = (int64) std::ceil ((double) pm->getNumFrames() * rate / pm->getSourceRate());
                float* pl = buf (trackBuf, 0);
                float* pr = buf (trackBuf, 1);
                readMedia (*pm, pp, pl, pr, len);
                if (chans >= 2)
                {
                    float* o0 = outputs[0] + done;
                    float* o1 = outputs[1] + done;
                    for (int i = 0; i < len; ++i) { o0[i] += pl[i]; o1[i] += pr[i]; }
                }
                else if (chans == 1)
                {
                    float* o = outputs[0] + done;
                    for (int i = 0; i < len; ++i) o[i] += 0.5f * (pl[i] + pr[i]);
                }
                pp += len;
                if (pp >= endPos) previewOn.store (false, std::memory_order_relaxed);
            }
            else
                previewOn.store (false, std::memory_order_relaxed);
            previewPos.store (pp, std::memory_order_relaxed);
        }

        if (play)
        {
            pos += len;
            if (loopOn.load (std::memory_order_relaxed))
            {
                // wrap right away so the play-head shown in the UI never sits on the loop end
                const auto ls = (int64) std::llround (loopStart.load() * rate);
                const auto le = (int64) std::llround (loopEnd.load() * rate);
                if (le - ls >= 64 && pos >= le) pos = ls;
            }
            if (stopAtEnd.load (std::memory_order_relaxed) && ! loopOn.load (std::memory_order_relaxed)
                && pos >= (int64) std::ceil (songLength.load() * rate))
                playing.store (false);   // the inserts keep running, so reverb / delay tails ring out
        }
        done += len;
    }

    // a locate that arrived during this block wins over our advance
    if (locateRequest.load() < 0.0) position.store (pos);
    blocksDone.fetch_add (1, std::memory_order_release);
}

void PlaygroundEngine::readMedia (const SampleData& media, int64 pos, float* l, float* r, int n) const noexcept
{
    const int frames = media.getNumFrames();
    const float* src[2] { media.channel (0), media.channel (media.getNumChannels() > 1 ? 1 : 0) };
    float* dst[2] { l, r };
    const double ratio = media.getSourceRate() / fs.load (std::memory_order_relaxed);

    if (std::abs (ratio - 1.0) < 1.0e-9)
    {
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                const auto idx = pos + i;
                dst[c][i] = (idx >= 0 && idx < frames) ? src[c][idx] : 0.0f;
            }
        return;
    }

    // Different file rate: 4-point Hermite interpolation on the fly (only after a device-rate change).
    for (int i = 0; i < n; ++i)
    {
        const double sp = (double) (pos + i) * ratio;
        const auto i1 = (int64) std::floor (sp);
        const float t = (float) (sp - (double) i1);
        for (int c = 0; c < 2; ++c)
        {
            auto at = [&] (int64 k) { return (k >= 0 && k < frames) ? src[c][k] : 0.0f; };
            const float y0 = at (i1 - 1), y1 = at (i1), y2 = at (i1 + 1), y3 = at (i1 + 2);
            const float c1 = 0.5f * (y2 - y0);
            const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            dst[c][i] = ((c3 * t + c2) * t + c1) * t + y1;
        }
    }
}

void PlaygroundEngine::processChunk (int64 pos, bool play, int n) noexcept
{
    for (int c = 0; c < 2 * kNumBuses; ++c) std::fill (buf (busBuf, c), buf (busBuf, c) + n, 0.0f);
    for (int c = 0; c < 2; ++c)
    {
        std::fill (buf (masterBuf, c), buf (masterBuf, c) + n, 0.0f);
        std::fill (buf (directBuf, c), buf (directBuf, c) + n, 0.0f);
    }
    const int len = lineLength;

    bool anySolo = false;
    for (int t = 0; t < kMaxTracks; ++t)
        if (tracks[(size_t) t].used.load (std::memory_order_relaxed) && strips[(size_t) t].solo->load (std::memory_order_relaxed) > 0.5f)
            anySolo = true;

    auto runInserts = [] (Strip& s, float* l, float* r, int count) noexcept
    {
        for (auto& slot : s.slot)
            if (auto* p = slot.load (std::memory_order_acquire))
                p->processStereo (l, r, count);
    };
    auto panGains = [] (float pan, float& gl, float& gr) noexcept
    {
        gl = pan > 0.0f ? 1.0f - pan : 1.0f;    // balance: a stereo stem keeps its image
        gr = pan < 0.0f ? 1.0f + pan : 1.0f;
    };

    //------------------------------------------------------------------ tracks
    float* tl = buf (trackBuf, 0);
    float* tr = buf (trackBuf, 1);
    for (int t = 0; t < kMaxTracks; ++t)
    {
        auto& tk = tracks[(size_t) t];
        if (! tk.used.load (std::memory_order_acquire))
        {
            // keep the delay line silent so a track that comes back starts clean
            for (int c = 0; c < 2; ++c)
            {
                float* ln = line (trackLines, 2 * t + c);
                for (int i = 0; i < n; ++i) ln[(linePos + i) % len] = 0.0f;
            }
            meters[(size_t) t][0].store (0.0f, std::memory_order_relaxed);
            meters[(size_t) t][1].store (0.0f, std::memory_order_relaxed);
            continue;
        }
        auto& s = strips[(size_t) t];
        const auto* media = tk.media.load (std::memory_order_acquire);
        if (play && media != nullptr && ! media->isEmpty())
            readMedia (*media, pos, tl, tr, n);
        else
        {
            std::fill (tl, tl + n, 0.0f);
            std::fill (tr, tr + n, 0.0f);
        }
        runInserts (s, tl, tr, n);
        {
            const int d = trackComp[(size_t) t].load (std::memory_order_relaxed);
            delayInPlace (line (trackLines, 2 * t), len, linePos, tl, n, d);
            delayInPlace (line (trackLines, 2 * t + 1), len, linePos, tr, n, d);
        }

        const bool silent = s.mute->load (std::memory_order_relaxed) > 0.5f || (anySolo && s.solo->load (std::memory_order_relaxed) <= 0.5f);
        const float g = silent ? 0.0f : faderGain (s.vol->load (std::memory_order_relaxed));
        float gl, gr;
        panGains (s.pan->load (std::memory_order_relaxed), gl, gr);
        s.gainL.setTargetValue (g * gl);
        s.gainR.setTargetValue (g * gr);
        s.sendA.setTargetValue (g * faderGain (s.sa->load (std::memory_order_relaxed)));   // post-fader
        s.sendB.setTargetValue (g * faderGain (s.sb->load (std::memory_order_relaxed)));

        const int out = jlimit (0, kNumBuses, (int) s.out->load (std::memory_order_relaxed));
        float* dl = out == 0 ? buf (directBuf, 0) : buf (busBuf, 2 * (out - 1));
        float* dr = out == 0 ? buf (directBuf, 1) : buf (busBuf, 2 * (out - 1) + 1);
        const int ba = jlimit (0, kNumBuses - 1, (int) s.saBus->load (std::memory_order_relaxed));
        const int bb = jlimit (0, kNumBuses - 1, (int) s.sbBus->load (std::memory_order_relaxed));
        float* al = buf (busBuf, 2 * ba); float* ar = buf (busBuf, 2 * ba + 1);
        float* bl = buf (busBuf, 2 * bb); float* br = buf (busBuf, 2 * bb + 1);

        float pl = 0.0f, pr = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float x = tl[i], y = tr[i];
            const float a = s.gainL.getNextValue(), b = s.gainR.getNextValue();
            const float sa = s.sendA.getNextValue(), sb = s.sendB.getNextValue();
            const float ol = x * a, orr = y * b;
            dl[i] += ol; dr[i] += orr;
            al[i] += x * sa; ar[i] += y * sa;
            bl[i] += x * sb; br[i] += y * sb;
            pl = jmax (pl, std::abs (ol)); pr = jmax (pr, std::abs (orr));
        }
        meters[(size_t) t][0].store (pl, std::memory_order_relaxed);
        meters[(size_t) t][1].store (pr, std::memory_order_relaxed);
    }

    //------------------------------------------------------------------ tracks straight to the master (wait for the bus path)
    float* ml = buf (masterBuf, 0);
    float* mr = buf (masterBuf, 1);
    {
        const int d = directComp.load (std::memory_order_relaxed);
        float* xl = buf (directBuf, 0);
        float* xr = buf (directBuf, 1);
        delayInPlace (line (directLines, 0), len, linePos, xl, n, d);
        delayInPlace (line (directLines, 1), len, linePos, xr, n, d);
        for (int i = 0; i < n; ++i) { ml[i] += xl[i]; mr[i] += xr[i]; }
    }

    //------------------------------------------------------------------ buses
    for (int b = 0; b < kNumBuses; ++b)
    {
        auto& s = strips[(size_t) busStrip (b)];
        float* l = buf (busBuf, 2 * b);
        float* r = buf (busBuf, 2 * b + 1);
        runInserts (s, l, r, n);
        {
            const int d = busComp[(size_t) b].load (std::memory_order_relaxed);
            delayInPlace (line (busLines, 2 * b), len, linePos, l, n, d);
            delayInPlace (line (busLines, 2 * b + 1), len, linePos, r, n, d);
        }
        const float g = s.mute->load (std::memory_order_relaxed) > 0.5f ? 0.0f : faderGain (s.vol->load (std::memory_order_relaxed));
        float gl, gr;
        panGains (s.pan->load (std::memory_order_relaxed), gl, gr);
        s.gainL.setTargetValue (g * gl);
        s.gainR.setTargetValue (g * gr);
        float pl = 0.0f, pr = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float ol = l[i] * s.gainL.getNextValue(), orr = r[i] * s.gainR.getNextValue();
            ml[i] += ol; mr[i] += orr;
            pl = jmax (pl, std::abs (ol)); pr = jmax (pr, std::abs (orr));
        }
        meters[(size_t) busStrip (b)][0].store (pl, std::memory_order_relaxed);
        meters[(size_t) busStrip (b)][1].store (pr, std::memory_order_relaxed);
    }

    //------------------------------------------------------------------ master
    // The master fader sits BEFORE the master inserts, so Elite Master's limiter is always the last
    // thing the signal sees: pushing the fader up drives the limiter, it can never push past the ceiling.
    auto& m = strips[(size_t) kMaster];
    const float g = faderGain (m.vol->load (std::memory_order_relaxed));
    m.gainL.setTargetValue (g);
    for (int i = 0; i < n; ++i)
    {
        const float gg = m.gainL.getNextValue();
        ml[i] *= gg; mr[i] *= gg;
    }
    runInserts (m, ml, mr, n);
    float pl = 0.0f, pr = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        pl = jmax (pl, std::abs (ml[i])); pr = jmax (pr, std::abs (mr[i]));
    }
    meters[(size_t) kMaster][0].store (pl, std::memory_order_relaxed);
    meters[(size_t) kMaster][1].store (pr, std::memory_order_relaxed);
    if (tapActive.load (std::memory_order_relaxed))
    {
        int s1, n1, s2, n2;
        tapFifo.prepareToWrite (n, s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) tapData[(size_t) (s1 + i)] = 0.5f * (ml[i] + mr[i]);
        for (int i = 0; i < n2; ++i) tapData[(size_t) (s2 + i)] = 0.5f * (ml[n1 + i] + mr[n1 + i]);
        tapFifo.finishedWrite (n1 + n2);
    }
    linePos = (linePos + n) % len;
}

int PlaygroundEngine::readMasterTap (float* dest, int maxSamples) noexcept
{
    int s1, n1, s2, n2;
    tapFifo.prepareToRead (maxSamples, s1, n1, s2, n2);
    std::copy (tapData.begin() + s1, tapData.begin() + s1 + n1, dest);
    std::copy (tapData.begin() + s2, tapData.begin() + s2 + n2, dest + n1);
    tapFifo.finishedRead (n1 + n2);
    return n1 + n2;
}

void PlaygroundEngine::delayInPlace (float* ln, int len, int writePos, float* io, int n, int d) noexcept
{
    // write the whole chunk first, then read it back d samples later (d + n <= len)
    for (int i = 0; i < n; ++i) ln[(writePos + i) % len] = io[i];
    if (d <= 0) return;
    for (int i = 0; i < n; ++i) io[i] = ln[(writePos + i - d + len) % len];
}

int PlaygroundEngine::stripLatency (int strip) const
{
    int total = 0;
    for (const auto& p : strips[(size_t) strip].owned)
        if (p != nullptr) total += p->getLatencySamples();
    return total;
}

void PlaygroundEngine::updateLatencies()
{
    int tMax = 0, bMax = 0;
    for (int t = 0; t < kMaxTracks; ++t)
        if (tracks[(size_t) t].used.load()) tMax = jmax (tMax, stripLatency (t));
    for (int b = 0; b < kNumBuses; ++b) bMax = jmax (bMax, stripLatency (busStrip (b)));
    tMax = jmin (tMax, kMaxCompensation);
    bMax = jmin (bMax, kMaxCompensation);
    for (int t = 0; t < kMaxTracks; ++t)
        trackComp[(size_t) t].store (jlimit (0, kMaxCompensation, tMax - stripLatency (t)));
    for (int b = 0; b < kNumBuses; ++b)
        busComp[(size_t) b].store (jlimit (0, kMaxCompensation, bMax - stripLatency (busStrip (b))));
    directComp.store (bMax);
    totalLatency.store (tMax + bMax + stripLatency (kMaster));
}

//==============================================================================
void PlaygroundEngine::setPositionSeconds (double seconds) noexcept
{
    locateRequest.store (jmax (0.0, seconds));
    if (! audioRunning.load()) position.store ((int64) std::llround (jmax (0.0, seconds) * fs.load()));
}

double PlaygroundEngine::getPositionSeconds() const noexcept
{
    const double pending = locateRequest.load();
    return pending >= 0.0 ? pending : (double) position.load() / fs.load();
}

void PlaygroundEngine::setLoop (bool on, double startSeconds, double endSeconds) noexcept
{
    loopStart.store (jmax (0.0, jmin (startSeconds, endSeconds)));
    loopEnd.store (jmax (0.0, jmax (startSeconds, endSeconds)));
    loopOn.store (on);
}

//==============================================================================
void PlaygroundEngine::startPreview (SampleData::Ptr media)
{
    const std::scoped_lock sl (structureLock);
    previewOn.store (false);
    auto old = previewOwned;
    previewOwned = media;
    previewPos.store (0);
    previewSeek.store (0.0);   // the audio thread starts from 0 even if it was mid-block
    preview.store (media.get(), std::memory_order_release);
    retire (nullptr, std::move (old));
    previewOn.store (media != nullptr && ! media->isEmpty());
}

void PlaygroundEngine::stopPreview() { previewOn.store (false); }

void PlaygroundEngine::resumePreview()
{
    if (previewOwned == nullptr || previewOwned->isEmpty()) return;
    const double len = previewOwned->getNumFrames() / previewOwned->getSourceRate();
    if (getPreviewPosition() >= len - 0.01) previewSeek.store (0.0);   // at the end: from the top
    previewOn.store (true);
}

void PlaygroundEngine::retire (std::unique_ptr<Plugin> plugin, SampleData::Ptr media)
{
    if (plugin == nullptr && media == nullptr) return;
    retired.push_back ({ std::move (plugin), std::move (media), blocksDone.load (std::memory_order_acquire) });
}

void PlaygroundEngine::collectGarbage()
{
    const std::scoped_lock sl (structureLock);
    const bool running = audioRunning.load();
    const auto done = blocksDone.load (std::memory_order_acquire);
    // Two finished blocks after the swap: the block that might still have held the old pointer is over.
    std::erase_if (retired, [&] (const Retired& r) { return ! running || done >= r.epoch + 2; });
}

void PlaygroundEngine::wirePlugin (Plugin& p, int strip)
{
    p.setGlobalBypass (&beforeMode);
    p.setGroupBypass (strip == kMaster ? &masterInsertsOff : nullptr);
    p.setTempoSource (mixerParams.find (Ids::bpm)->rawPointer());
}

bool PlaygroundEngine::canHost (int strip, std::string_view pluginId) const
{
    const auto* info = PluginRegistry::find (pluginId);
    return info != nullptr && inRange (strip, kNumStrips) && (! info->masterOnly || strip == kMaster);
}

Plugin* PlaygroundEngine::insertPlugin (int strip, int slot, std::string_view pluginId)
{
    if (! canHost (strip, pluginId) || ! inRange (slot, kSlots)) return nullptr;
    auto plugin = PluginRegistry::create (pluginId);
    if (plugin == nullptr) return nullptr;
    wirePlugin (*plugin, strip);

    const std::scoped_lock sl (structureLock);
    plugin->prepare (fs.load(), maxBlock.load());
    auto& s = strips[(size_t) strip];
    auto* rawPtr = plugin.get();
    auto old = std::move (s.owned[(size_t) slot]);
    s.owned[(size_t) slot] = std::move (plugin);
    s.slot[(size_t) slot].store (rawPtr, std::memory_order_release);
    retire (std::move (old), nullptr);
    updateLatencies();
    return rawPtr;
}

void PlaygroundEngine::removePlugin (int strip, int slot)
{
    if (! inRange (strip, kNumStrips) || ! inRange (slot, kSlots)) return;
    const std::scoped_lock sl (structureLock);
    auto& s = strips[(size_t) strip];
    s.slot[(size_t) slot].store (nullptr, std::memory_order_release);
    retire (std::move (s.owned[(size_t) slot]), nullptr);
    updateLatencies();
}

void PlaygroundEngine::swapSlots (int strip, int a, int b)
{
    if (! inRange (strip, kNumStrips) || ! inRange (a, kSlots) || ! inRange (b, kSlots) || a == b) return;
    const std::scoped_lock sl (structureLock);
    auto& s = strips[(size_t) strip];
    std::swap (s.owned[(size_t) a], s.owned[(size_t) b]);
    // Two stores: for at most one block the audio thread may see one plug-in in both slots or in
    // neither. Both are harmless (each plug-in is only ever called from the audio thread).
    s.slot[(size_t) a].store (s.owned[(size_t) a].get(), std::memory_order_release);
    s.slot[(size_t) b].store (s.owned[(size_t) b].get(), std::memory_order_release);
}

int PlaygroundEngine::getMasterLatencySamples() const { return stripLatency (kMaster); }

Plugin* PlaygroundEngine::getPlugin (int strip, int slot) const noexcept
{
    if (! inRange (strip, kNumStrips) || ! inRange (slot, kSlots)) return nullptr;
    return strips[(size_t) strip].owned[(size_t) slot].get();
}

//==============================================================================
void PlaygroundEngine::resetTrackControls (int t)
{
    for (const char* what : { "vol", "pan", "mute", "solo", "out", "sa", "sa_bus", "sb", "sb_bus" })
        if (auto* p = mixerParams.find (Ids::track (t, what)))
            p->resetToDefault();
}

int PlaygroundEngine::addTrack (SampleData::Ptr media, const std::string& name)
{
    for (int t = 0; t < kMaxTracks; ++t)
    {
        auto& tk = tracks[(size_t) t];
        if (tk.used.load()) continue;
        resetTrackControls (t);
        tk.name = ! name.empty() ? name : "Track " + std::to_string (t + 1);
        tk.colour = kPalette[t % 8];
        setTrackMedia (t, std::move (media));
        tk.used.store (true, std::memory_order_release);
        updateSongLength();
        updateLatencies();
        return t;
    }
    return -1;
}

void PlaygroundEngine::removeTrack (int t)
{
    if (! isTrack (t)) return;
    tracks[(size_t) t].used.store (false, std::memory_order_release);
    for (int s = 0; s < kSlots; ++s) removePlugin (t, s);
    setTrackMedia (t, nullptr);
    tracks[(size_t) t].name.clear();
    updateLatencies();
}

void PlaygroundEngine::setTrackMedia (int t, SampleData::Ptr media)
{
    if (! isTrack (t)) return;
    const std::scoped_lock sl (structureLock);
    auto& tk = tracks[(size_t) t];
    auto old = tk.mediaOwned;
    tk.mediaOwned = media;
    tk.missingFile.clear();
    tk.media.store (media.get(), std::memory_order_release);
    retire (nullptr, std::move (old));
    updateSongLength();
}

int PlaygroundEngine::getNumUsedTracks() const noexcept
{
    int n = 0;
    for (auto& t : tracks) n += t.used.load() ? 1 : 0;
    return n;
}

SampleData::Ptr PlaygroundEngine::getTrackMedia (int t) const { return isTrack (t) ? tracks[(size_t) t].mediaOwned : nullptr; }
std::string PlaygroundEngine::getTrackName (int t) const { return isTrack (t) ? tracks[(size_t) t].name : std::string(); }
void PlaygroundEngine::setTrackName (int t, const std::string& n) { if (isTrack (t)) tracks[(size_t) t].name = n; }
uint32 PlaygroundEngine::getTrackColour (int t) const { return isTrack (t) ? tracks[(size_t) t].colour : 0xff808080; }
void PlaygroundEngine::setTrackColour (int t, uint32 c) { if (isTrack (t)) tracks[(size_t) t].colour = c; }
std::string PlaygroundEngine::getBusName (int b) const { return inRange (b, kNumBuses) ? busNames[(size_t) b] : std::string(); }
void PlaygroundEngine::setBusName (int b, const std::string& n) { if (inRange (b, kNumBuses)) busNames[(size_t) b] = n; }

std::string PlaygroundEngine::getStripName (int strip) const
{
    if (isTrack (strip)) return getTrackName (strip);
    if (isBus (strip)) return getBusName (strip - kFirstBus);
    return strip == kMaster ? "Master" : std::string();
}

void PlaygroundEngine::updateSongLength()
{
    double longest = 0.0;
    for (auto& t : tracks)
        if (t.used.load() && t.mediaOwned != nullptr && t.mediaOwned->getSourceRate() > 0.0)
            longest = jmax (longest, (double) t.mediaOwned->getNumFrames() / t.mediaOwned->getSourceRate());
    songLength.store (longest);
}

//==============================================================================
Tree PlaygroundEngine::createSessionTree() const
{
    Tree s (kSessionType);
    s.set ("version", 1);
    s.set ("loopOn", loopOn.load());
    s.set ("loopStart", loopStart.load());
    s.set ("loopEnd", loopEnd.load());
    s.set ("position", getPositionSeconds());

    Tree mixerTree (kMixerType);
    for (const auto& p : mixerParams.all())
    {
        Tree param ("PARAM");
        param.set ("id", p->id());
        param.set ("value", Tree::numberText ((double) p->get()));
        mixerTree.add (std::move (param));
    }
    Tree m ("MIXER");
    m.add (std::move (mixerTree));
    s.add (std::move (m));

    for (int t = 0; t < kMaxTracks; ++t)
    {
        const auto& tk = tracks[(size_t) t];
        if (! tk.used.load()) continue;
        Tree tt ("TRACK");
        tt.set ("index", t);
        tt.set ("name", tk.name);
        tt.set ("colour", colourText (tk.colour));
        std::string file = tk.missingFile;   // a file that was not found stays in the session for next time
        if (tk.mediaOwned != nullptr)
        {
            const auto u8 = tk.mediaOwned->getFile().u8string();
            file.assign (u8.begin(), u8.end());
        }
        tt.set ("file", file);
        s.add (std::move (tt));
    }
    for (int b = 0; b < kNumBuses; ++b)
    {
        Tree bt ("BUS");
        bt.set ("index", b);
        bt.set ("name", busNames[(size_t) b]);
        s.add (std::move (bt));
    }
    for (int st = 0; st < kNumStrips; ++st)
        for (int sl = 0; sl < kSlots; ++sl)
            if (auto* p = strips[(size_t) st].owned[(size_t) sl].get())
            {
                Tree it ("INSERT");
                it.set ("strip", st);
                it.set ("slot", sl);
                it.set ("plugin", p->getInfo().id);
                if (auto state = Tree::fromXml (stateXml::write (p->saveState())))
                    it.add (std::move (*state));
                s.add (std::move (it));
            }
    return s;
}

StringArray PlaygroundEngine::mediaFilesIn (const Tree& session)
{
    StringArray files;
    for (const auto& c : session.children())
        if (c.hasType ("TRACK"))
        {
            const auto f = c.get ("file");
            if (! f.empty() && std::find (files.begin(), files.end(), f) == files.end()) files.push_back (f);
        }
    return files;
}

bool PlaygroundEngine::applySessionTree (const Tree& s, const std::map<std::string, SampleData::Ptr>& media, StringArray* missing)
{
    if (! s.hasType (kSessionType)) return false;

    for (int t = 0; t < kMaxTracks; ++t) removeTrack (t);
    for (int st = kFirstBus; st < kNumStrips; ++st)
        for (int sl = 0; sl < kSlots; ++sl) removePlugin (st, sl);

    const auto& mixerState = s.child ("MIXER").child (kMixerType);
    if (mixerState.isValid())
    {
        // only controls we know (forward compatible), everything else back to default
        for (const auto& p : mixerParams.all())
        {
            const auto& c = mixerState.childWith ("id", p->id());
            if (c.isValid()) p->setValue ((float) c.getDouble ("value"));
            else p->resetToDefault();
        }
    }

    busNames = defaultBusNames();
    for (const auto& c : s.children())
    {
        if (c.hasType ("TRACK"))
        {
            const int t = c.getInt ("index");
            if (! isTrack (t)) continue;
            const auto file = c.get ("file");
            auto it = media.find (file);
            SampleData::Ptr m = it != media.end() ? it->second : nullptr;
            if (m == nullptr && ! file.empty() && missing != nullptr) missing->push_back (file);
            auto& tk = tracks[(size_t) t];
            tk.name = c.get ("name");
            tk.colour = colourFrom (c.get ("colour", colourText (kPalette[t % 8])), kPalette[t % 8]);
            setTrackMedia (t, m);
            if (m == nullptr) tk.missingFile = file;
            tk.used.store (true, std::memory_order_release);
        }
        else if (c.hasType ("BUS"))
            setBusName (c.getInt ("index"), c.get ("name"));
    }
    for (const auto& c : s.children())
        if (c.hasType ("INSERT"))
        {
            const auto pluginId = c.get ("plugin");
            if (auto* p = insertPlugin (c.getInt ("strip"), c.getInt ("slot"), pluginId))
            {
                PluginState state;
                if (c.childAt (0).isValid() && stateXml::read (c.childAt (0).toXml(), pluginId, state))
                    p->loadState (state);
            }
        }

    updateSongLength();
    updateLatencies();
    setLoop (s.getBool ("loopOn"), s.getDouble ("loopStart"), s.getDouble ("loopEnd"));
    setPositionSeconds (s.getDouble ("position", 0.0));
    return true;
}

void PlaygroundEngine::newSession()
{
    const Tree empty (kSessionType);
    applySessionTree (empty, {});
    for (const auto& p : mixerParams.all()) p->resetToDefault();

    auto presetNamed = [] (const char* id, const char* name) -> const FactoryPreset*
    {
        for (const auto& p : Presets::factory (id))
            if (p.name == name) return &p;
        return nullptr;
    };
    if (auto* v = insertPlugin (busStrip (3), 0, "elite-verb"))
        if (auto* pr = presetNamed ("elite-verb", "Vocal Plate (send)")) Presets::apply (*v, *pr);
    if (auto* d = insertPlugin (busStrip (4), 0, "elite-delay"))
        if (auto* pr = presetNamed ("elite-delay", "1/4 Vocal Throw (send)")) Presets::apply (*d, *pr);
    insertPlugin (kMaster, kSlots - 1, "elite-master");
    stop();
    setPositionSeconds (0.0);
}
} // namespace hem::pg
