#include "PlaygroundEngine.h"
#include "Engine/MediaLoader.h"

namespace pg
{
namespace
{
    constexpr double kGainRampSeconds = 0.02;

    inline float faderGain (float db) noexcept { return db <= PlaygroundEngine::kFaderOffDb + 0.05f ? 0.0f : params::dbToGain (db); }

    juce::StringArray busChoices()
    {
        juce::StringArray s;
        for (int b = 0; b < PlaygroundEngine::kNumBuses; ++b) s.add ("Bus " + juce::String (b + 1));
        return s;
    }

    juce::StringArray outChoices()
    {
        auto s = busChoices();
        s.insert (0, "Master");
        return s;
    }

    const juce::Colour kPalette[] { juce::Colour (0xffe0a526), juce::Colour (0xff2ec8e6), juce::Colour (0xffe0533c), juce::Colour (0xff5bc94a),
                                    juce::Colour (0xffa45be0), juce::Colour (0xff1fb59a), juce::Colour (0xffe05ba8), juce::Colour (0xff3a7be0) };
}

juce::AudioProcessorValueTreeState::ParameterLayout PlaygroundEngine::createLayout()
{
    params::Layout l;
    for (int t = 0; t < kMaxTracks; ++t)
    {
        const auto n = "Track " + juce::String (t + 1) + " ";
        params::addDb (l, Ids::track (t, "vol").toRawUTF8(), (n + "Volume").toRawUTF8(), kFaderOffDb, 12.0f, 0.0f);
        params::addFloat (l, Ids::track (t, "pan").toRawUTF8(), (n + "Pan").toRawUTF8(), -1.0f, 1.0f, 0.0f, "", 0.01f);
        params::addBool (l, Ids::track (t, "mute").toRawUTF8(), (n + "Mute").toRawUTF8(), false);
        params::addBool (l, Ids::track (t, "solo").toRawUTF8(), (n + "Solo").toRawUTF8(), false);
        params::addChoice (l, Ids::track (t, "out").toRawUTF8(), (n + "Output").toRawUTF8(), outChoices(), 0);
        params::addDb (l, Ids::track (t, "sa").toRawUTF8(), (n + "Send A").toRawUTF8(), kFaderOffDb, 6.0f, kFaderOffDb);
        params::addChoice (l, Ids::track (t, "sa_bus").toRawUTF8(), (n + "Send A Bus").toRawUTF8(), busChoices(), 3);   // Reverb
        params::addDb (l, Ids::track (t, "sb").toRawUTF8(), (n + "Send B").toRawUTF8(), kFaderOffDb, 6.0f, kFaderOffDb);
        params::addChoice (l, Ids::track (t, "sb_bus").toRawUTF8(), (n + "Send B Bus").toRawUTF8(), busChoices(), 4);   // Delay
    }
    for (int b = 0; b < kNumBuses; ++b)
    {
        const auto n = "Bus " + juce::String (b + 1) + " ";
        params::addDb (l, Ids::bus (b, "vol").toRawUTF8(), (n + "Volume").toRawUTF8(), kFaderOffDb, 12.0f, 0.0f);
        params::addFloat (l, Ids::bus (b, "pan").toRawUTF8(), (n + "Pan").toRawUTF8(), -1.0f, 1.0f, 0.0f, "", 0.01f);
        params::addBool (l, Ids::bus (b, "mute").toRawUTF8(), (n + "Mute").toRawUTF8(), false);
    }
    params::addDb (l, Ids::masterVol, "Master Volume", kFaderOffDb, 6.0f, 0.0f);
    params::addFloat (l, Ids::bpm, "Tempo", 40.0f, 240.0f, 120.0f, "BPM", 0.1f);
    return l;
}

PlaygroundEngine::PlaygroundEngine()
    : AudioProcessor (BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PLAYGROUND_MIXER", createLayout())
{
    for (int t = 0; t < kMaxTracks; ++t)
    {
        auto& s = strips[(size_t) t];
        s.vol = apvts.getRawParameterValue (Ids::track (t, "vol"));
        s.pan = apvts.getRawParameterValue (Ids::track (t, "pan"));
        s.mute = apvts.getRawParameterValue (Ids::track (t, "mute"));
        s.solo = apvts.getRawParameterValue (Ids::track (t, "solo"));
        s.out = apvts.getRawParameterValue (Ids::track (t, "out"));
        s.sa = apvts.getRawParameterValue (Ids::track (t, "sa"));
        s.saBus = apvts.getRawParameterValue (Ids::track (t, "sa_bus"));
        s.sb = apvts.getRawParameterValue (Ids::track (t, "sb"));
        s.sbBus = apvts.getRawParameterValue (Ids::track (t, "sb_bus"));
        tracks[(size_t) t].colour = kPalette[t % 8];
    }
    for (int b = 0; b < kNumBuses; ++b)
    {
        auto& s = strips[(size_t) busStrip (b)];
        s.vol = apvts.getRawParameterValue (Ids::bus (b, "vol"));
        s.pan = apvts.getRawParameterValue (Ids::bus (b, "pan"));
        s.mute = apvts.getRawParameterValue (Ids::bus (b, "mute"));
    }
    strips[(size_t) kMaster].vol = apvts.getRawParameterValue (Ids::masterVol);
    for (auto& m : meters) for (auto& c : m) c.store (0.0f);
}

PlaygroundEngine::~PlaygroundEngine()
{
    for (auto& s : strips)
        for (auto& p : s.slot) p.store (nullptr);
}

bool PlaygroundEngine::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

//==============================================================================
void PlaygroundEngine::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    const juce::ScopedLock sl (structureLock);
    const double newFs = sampleRate > 0.0 ? sampleRate : 48000.0;
    MediaLoader::setTargetRate (newFs);   // files loaded from now on are converted once, at full quality
    const double oldFs = fs.load();
    position.store ((juce::int64) std::llround ((double) position.load() * newFs / oldFs));
    fs.store (newFs);
    maxBlock.store (juce::jmax (16, maximumExpectedSamplesPerBlock));
    const int mb = maxBlock.load();

    trackBuf.setSize (2, mb, false, true, false);
    busBuf.setSize (2 * kNumBuses, mb, false, true, false);
    masterBuf.setSize (2, mb, false, true, false);
    directBuf.setSize (2, mb, false, true, false);
    lineLength = kMaxCompensation + mb;
    trackLines.setSize (2 * kMaxTracks, lineLength, false, true, false);
    busLines.setSize (2 * kNumBuses, lineLength, false, true, false);
    directLines.setSize (2, lineLength, false, true, false);
    trackLines.clear(); busLines.clear(); directLines.clear();
    linePos = 0;

    for (auto& s : strips)
    {
        for (auto& p : s.owned)
            if (p != nullptr) p->prepareToPlay (newFs, mb);
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

void PlaygroundEngine::releaseResources()
{
    audioRunning.store (false);
}

void PlaygroundEngine::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int chans = buffer.getNumChannels();
    const int mb = juce::jmin (maxBlock.load (std::memory_order_relaxed), masterBuf.getNumSamples());
    const double rate = fs.load (std::memory_order_relaxed);

    const double loc = locateRequest.exchange (-1.0);
    if (loc >= 0.0) position.store ((juce::int64) std::llround (loc * rate));

    juce::int64 pos = position.load();
    int done = 0;
    while (done < n)
    {
        int len = juce::jmin (mb, n - done);
        const bool play = playing.load (std::memory_order_relaxed);

        if (play && loopOn.load (std::memory_order_relaxed))
        {
            const auto ls = (juce::int64) std::llround (loopStart.load() * rate);
            const auto le = (juce::int64) std::llround (loopEnd.load() * rate);
            if (le - ls >= 64)
            {
                if (pos >= le || pos < ls) pos = ls;
                len = (int) juce::jmin ((juce::int64) len, le - pos);
            }
        }

        processChunk (pos, play, len);

        auto* mL = masterBuf.getReadPointer (0);
        auto* mR = masterBuf.getReadPointer (1);
        if (chans >= 2)
        {
            std::copy (mL, mL + len, buffer.getWritePointer (0, done));
            std::copy (mR, mR + len, buffer.getWritePointer (1, done));
            for (int c = 2; c < chans; ++c) buffer.clear (c, done, len);
        }
        else if (chans == 1)
        {
            auto* o = buffer.getWritePointer (0, done);
            for (int i = 0; i < len; ++i) o[i] = 0.5f * (mL[i] + mR[i]);
        }

        // LIBRARY preview, added after the master (trackBuf is free again after processChunk)
        if (previewOn.load (std::memory_order_relaxed))
        {
            auto* pm = preview.load (std::memory_order_acquire);
            const double seek = previewSeek.exchange (-1.0);
            juce::int64 pp = seek >= 0.0 ? (juce::int64) std::llround (seek * rate) : previewPos.load (std::memory_order_relaxed);
            if (pm != nullptr && ! pm->isEmpty())
            {
                const auto endPos = (juce::int64) std::ceil ((double) pm->getNumFrames() * rate / pm->getSourceRate());
                float* pl = trackBuf.getWritePointer (0);
                float* pr = trackBuf.getWritePointer (1);
                readMedia (*pm, pp, pl, pr, len);
                if (chans >= 2)
                {
                    juce::FloatVectorOperations::add (buffer.getWritePointer (0, done), pl, len);
                    juce::FloatVectorOperations::add (buffer.getWritePointer (1, done), pr, len);
                }
                else if (chans == 1)
                {
                    auto* o = buffer.getWritePointer (0, done);
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
                const auto ls = (juce::int64) std::llround (loopStart.load() * rate);
                const auto le = (juce::int64) std::llround (loopEnd.load() * rate);
                if (le - ls >= 64 && pos >= le) pos = ls;
            }
            if (stopAtEnd.load (std::memory_order_relaxed) && ! loopOn.load (std::memory_order_relaxed)
                && pos >= (juce::int64) std::ceil (songLength.load() * rate))
                playing.store (false);   // the inserts keep running, so reverb / delay tails ring out
        }
        done += len;
    }

    // a locate that arrived during this block wins over our advance
    if (locateRequest.load() < 0.0) position.store (pos);
    blocksDone.fetch_add (1, std::memory_order_release);
}

void PlaygroundEngine::readMedia (const SampleData& media, juce::int64 pos, float* l, float* r, int n) const noexcept
{
    const auto& b = media.getBuffer();
    const int frames = b.getNumSamples();
    const float* src[2] { b.getReadPointer (0), b.getReadPointer (b.getNumChannels() > 1 ? 1 : 0) };
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

    // Different file rate: 4-point Hermite interpolation on the fly (stems keep their native rate).
    for (int i = 0; i < n; ++i)
    {
        const double sp = (double) (pos + i) * ratio;
        const auto i1 = (juce::int64) std::floor (sp);
        const float t = (float) (sp - (double) i1);
        for (int c = 0; c < 2; ++c)
        {
            auto at = [&] (juce::int64 k) { return (k >= 0 && k < frames) ? src[c][k] : 0.0f; };
            const float y0 = at (i1 - 1), y1 = at (i1), y2 = at (i1 + 1), y3 = at (i1 + 2);
            const float c1 = 0.5f * (y2 - y0);
            const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            dst[c][i] = ((c3 * t + c2) * t + c1) * t + y1;
        }
    }
}

void PlaygroundEngine::processChunk (juce::int64 pos, bool play, int n) noexcept
{
    for (int c = 0; c < busBuf.getNumChannels(); ++c) busBuf.clear (c, 0, n);
    masterBuf.clear (0, n);
    masterBuf.clear (1, n);
    directBuf.clear (0, n);
    directBuf.clear (1, n);
    const int len = lineLength;

    bool anySolo = false;
    for (int t = 0; t < kMaxTracks; ++t)
        if (tracks[(size_t) t].used.load (std::memory_order_relaxed) && strips[(size_t) t].solo->load (std::memory_order_relaxed) > 0.5f)
            anySolo = true;

    auto runInserts = [] (Strip& s, float* l, float* r, int len) noexcept
    {
        for (auto& slot : s.slot)
            if (auto* p = slot.load (std::memory_order_acquire))
                p->processStereo (l, r, len);
    };
    auto panGains = [] (float pan, float& gl, float& gr) noexcept
    {
        gl = pan > 0.0f ? 1.0f - pan : 1.0f;    // balance: a stereo stem keeps its image
        gr = pan < 0.0f ? 1.0f + pan : 1.0f;
    };

    //------------------------------------------------------------------ tracks
    float* tl = trackBuf.getWritePointer (0);
    float* tr = trackBuf.getWritePointer (1);
    for (int t = 0; t < kMaxTracks; ++t)
    {
        auto& tk = tracks[(size_t) t];
        if (! tk.used.load (std::memory_order_acquire))
        {
            // keep the delay line silent so a track that comes back starts clean
            for (int c = 0; c < 2; ++c)
            {
                float* line = trackLines.getWritePointer (2 * t + c);
                for (int i = 0; i < n; ++i) line[(linePos + i) % len] = 0.0f;
            }
            meters[(size_t) t][0].store (0.0f, std::memory_order_relaxed);
            meters[(size_t) t][1].store (0.0f, std::memory_order_relaxed);
            continue;
        }
        auto& s = strips[(size_t) t];
        auto* media = tk.media.load (std::memory_order_acquire);
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
            delayInPlace (trackLines.getWritePointer (2 * t), len, linePos, tl, n, d);
            delayInPlace (trackLines.getWritePointer (2 * t + 1), len, linePos, tr, n, d);
        }

        const bool silent = s.mute->load (std::memory_order_relaxed) > 0.5f || (anySolo && s.solo->load (std::memory_order_relaxed) <= 0.5f);
        const float g = silent ? 0.0f : faderGain (s.vol->load (std::memory_order_relaxed));
        float gl, gr;
        panGains (s.pan->load (std::memory_order_relaxed), gl, gr);
        s.gainL.setTargetValue (g * gl);
        s.gainR.setTargetValue (g * gr);
        s.sendA.setTargetValue (g * faderGain (s.sa->load (std::memory_order_relaxed)));   // post-fader
        s.sendB.setTargetValue (g * faderGain (s.sb->load (std::memory_order_relaxed)));

        const int out = juce::jlimit (0, kNumBuses, (int) s.out->load (std::memory_order_relaxed));
        float* dl = out == 0 ? directBuf.getWritePointer (0) : busBuf.getWritePointer (2 * (out - 1));
        float* dr = out == 0 ? directBuf.getWritePointer (1) : busBuf.getWritePointer (2 * (out - 1) + 1);
        const int ba = juce::jlimit (0, kNumBuses - 1, (int) s.saBus->load (std::memory_order_relaxed));
        const int bb = juce::jlimit (0, kNumBuses - 1, (int) s.sbBus->load (std::memory_order_relaxed));
        float* al = busBuf.getWritePointer (2 * ba); float* ar = busBuf.getWritePointer (2 * ba + 1);
        float* bl = busBuf.getWritePointer (2 * bb); float* br = busBuf.getWritePointer (2 * bb + 1);

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
            pl = juce::jmax (pl, std::abs (ol)); pr = juce::jmax (pr, std::abs (orr));
        }
        meters[(size_t) t][0].store (pl, std::memory_order_relaxed);
        meters[(size_t) t][1].store (pr, std::memory_order_relaxed);
    }

    //------------------------------------------------------------------ tracks straight to the master (wait for the bus path)
    float* ml = masterBuf.getWritePointer (0);
    float* mr = masterBuf.getWritePointer (1);
    {
        const int d = directComp.load (std::memory_order_relaxed);
        float* xl = directBuf.getWritePointer (0);
        float* xr = directBuf.getWritePointer (1);
        delayInPlace (directLines.getWritePointer (0), len, linePos, xl, n, d);
        delayInPlace (directLines.getWritePointer (1), len, linePos, xr, n, d);
        for (int i = 0; i < n; ++i) { ml[i] += xl[i]; mr[i] += xr[i]; }
    }

    //------------------------------------------------------------------ buses
    for (int b = 0; b < kNumBuses; ++b)
    {
        auto& s = strips[(size_t) busStrip (b)];
        float* l = busBuf.getWritePointer (2 * b);
        float* r = busBuf.getWritePointer (2 * b + 1);
        runInserts (s, l, r, n);
        {
            const int d = busComp[(size_t) b].load (std::memory_order_relaxed);
            delayInPlace (busLines.getWritePointer (2 * b), len, linePos, l, n, d);
            delayInPlace (busLines.getWritePointer (2 * b + 1), len, linePos, r, n, d);
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
            pl = juce::jmax (pl, std::abs (ol)); pr = juce::jmax (pr, std::abs (orr));
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
        pl = juce::jmax (pl, std::abs (ml[i])); pr = juce::jmax (pr, std::abs (mr[i]));
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

void PlaygroundEngine::delayInPlace (float* line, int len, int writePos, float* io, int n, int d) noexcept
{
    // write the whole chunk first, then read it back d samples later (d + n <= len)
    for (int i = 0; i < n; ++i) line[(writePos + i) % len] = io[i];
    if (d <= 0) return;
    for (int i = 0; i < n; ++i) io[i] = line[(writePos + i - d + len) % len];
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
        if (tracks[(size_t) t].used.load()) tMax = juce::jmax (tMax, stripLatency (t));
    for (int b = 0; b < kNumBuses; ++b) bMax = juce::jmax (bMax, stripLatency (busStrip (b)));
    tMax = juce::jmin (tMax, kMaxCompensation);
    bMax = juce::jmin (bMax, kMaxCompensation);
    for (int t = 0; t < kMaxTracks; ++t)
        trackComp[(size_t) t].store (juce::jlimit (0, kMaxCompensation, tMax - stripLatency (t)));
    for (int b = 0; b < kNumBuses; ++b)
        busComp[(size_t) b].store (juce::jlimit (0, kMaxCompensation, bMax - stripLatency (busStrip (b))));
    directComp.store (bMax);
    totalLatency.store (tMax + bMax + stripLatency (kMaster));
}

//==============================================================================
void PlaygroundEngine::setPositionSeconds (double seconds) noexcept
{
    locateRequest.store (juce::jmax (0.0, seconds));
    if (! audioRunning.load()) position.store ((juce::int64) std::llround (juce::jmax (0.0, seconds) * fs.load()));
}

double PlaygroundEngine::getPositionSeconds() const noexcept
{
    const double pending = locateRequest.load();
    return pending >= 0.0 ? pending : (double) position.load() / fs.load();
}

void PlaygroundEngine::setLoop (bool on, double startSeconds, double endSeconds) noexcept
{
    loopStart.store (juce::jmax (0.0, juce::jmin (startSeconds, endSeconds)));
    loopEnd.store (juce::jmax (0.0, juce::jmax (startSeconds, endSeconds)));
    loopOn.store (on);
}

//==============================================================================
void PlaygroundEngine::startPreview (SampleData::Ptr media)
{
    JUCE_ASSERT_MESSAGE_THREAD
    const juce::ScopedLock sl (structureLock);
    previewOn.store (false);
    auto old = previewOwned;
    previewOwned = media;
    previewPos.store (0);
    previewSeek.store (0.0);   // the audio thread starts from 0 even if it was mid-block
    preview.store (media.get(), std::memory_order_release);
    retire (nullptr, std::move (old));
    previewOn.store (media != nullptr && ! media->isEmpty());
}

void PlaygroundEngine::stopPreview()
{
    JUCE_ASSERT_MESSAGE_THREAD
    previewOn.store (false);
}

void PlaygroundEngine::resumePreview()
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (previewOwned == nullptr || previewOwned->isEmpty()) return;
    const double len = previewOwned->getNumFrames() / previewOwned->getSourceRate();
    if (getPreviewPosition() >= len - 0.01) previewSeek.store (0.0);   // at the end: from the top
    previewOn.store (true);
}

void PlaygroundEngine::retire (std::unique_ptr<ElitePlugin> plugin, SampleData::Ptr media)
{
    if (plugin == nullptr && media == nullptr) return;
    retired.push_back ({ std::move (plugin), std::move (media), blocksDone.load (std::memory_order_acquire) });
}

void PlaygroundEngine::collectGarbage()
{
    JUCE_ASSERT_MESSAGE_THREAD
    const juce::ScopedLock sl (structureLock);
    const bool running = audioRunning.load();
    const auto done = blocksDone.load (std::memory_order_acquire);
    // Two finished blocks after the swap: the block that might still have held the old pointer is over.
    std::erase_if (retired, [&] (const Retired& r) { return ! running || done >= r.epoch + 2; });
}

void PlaygroundEngine::wirePlugin (ElitePlugin& p, int strip)
{
    p.setGlobalBypass (&beforeMode);
    p.setGroupBypass (strip == kMaster ? &masterInsertsOff : nullptr);
    p.setTempoSource (apvts.getRawParameterValue (Ids::bpm));
}

bool PlaygroundEngine::canHost (int strip, const juce::String& pluginId) const
{
    const auto* info = PluginRegistry::find (pluginId);
    return info != nullptr && juce::isPositiveAndBelow (strip, kNumStrips) && (! info->masterOnly || strip == kMaster);
}

ElitePlugin* PlaygroundEngine::insertPlugin (int strip, int slot, const juce::String& pluginId)
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (! canHost (strip, pluginId) || ! juce::isPositiveAndBelow (slot, kSlots)) return nullptr;
    auto plugin = PluginRegistry::create (pluginId);
    if (plugin == nullptr) return nullptr;
    wirePlugin (*plugin, strip);

    const juce::ScopedLock sl (structureLock);
    plugin->prepareToPlay (fs.load(), maxBlock.load());
    auto& s = strips[(size_t) strip];
    auto* raw = plugin.get();
    auto old = std::move (s.owned[(size_t) slot]);
    s.owned[(size_t) slot] = std::move (plugin);
    s.slot[(size_t) slot].store (raw, std::memory_order_release);
    retire (std::move (old), nullptr);
    updateLatencies();
    return raw;
}

void PlaygroundEngine::removePlugin (int strip, int slot)
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (! juce::isPositiveAndBelow (strip, kNumStrips) || ! juce::isPositiveAndBelow (slot, kSlots)) return;
    const juce::ScopedLock sl (structureLock);
    auto& s = strips[(size_t) strip];
    s.slot[(size_t) slot].store (nullptr, std::memory_order_release);
    retire (std::move (s.owned[(size_t) slot]), nullptr);
    updateLatencies();
}

void PlaygroundEngine::swapSlots (int strip, int a, int b)
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (! juce::isPositiveAndBelow (strip, kNumStrips) || ! juce::isPositiveAndBelow (a, kSlots) || ! juce::isPositiveAndBelow (b, kSlots) || a == b) return;
    const juce::ScopedLock sl (structureLock);
    auto& s = strips[(size_t) strip];
    std::swap (s.owned[(size_t) a], s.owned[(size_t) b]);
    // Two stores: for at most one block the audio thread may see one plugin in both slots or in
    // neither. Both are harmless (each plugin is only ever called from the audio thread).
    s.slot[(size_t) a].store (s.owned[(size_t) a].get(), std::memory_order_release);
    s.slot[(size_t) b].store (s.owned[(size_t) b].get(), std::memory_order_release);
}

int PlaygroundEngine::getMasterLatencySamples() const
{
    int total = 0;
    for (const auto& p : strips[(size_t) kMaster].owned)
        if (p != nullptr) total += p->getLatencySamples();
    return total;
}

ElitePlugin* PlaygroundEngine::getPlugin (int strip, int slot) const noexcept
{
    if (! juce::isPositiveAndBelow (strip, kNumStrips) || ! juce::isPositiveAndBelow (slot, kSlots)) return nullptr;
    return strips[(size_t) strip].owned[(size_t) slot].get();
}

//==============================================================================
void PlaygroundEngine::resetTrackControls (int t)
{
    for (const char* what : { "vol", "pan", "mute", "solo", "out", "sa", "sa_bus", "sb", "sb_bus" })
        if (auto* p = apvts.getParameter (Ids::track (t, what)))
            p->setValueNotifyingHost (p->getDefaultValue());
}

int PlaygroundEngine::addTrack (SampleData::Ptr media, const juce::String& name)
{
    JUCE_ASSERT_MESSAGE_THREAD
    for (int t = 0; t < kMaxTracks; ++t)
    {
        auto& tk = tracks[(size_t) t];
        if (tk.used.load()) continue;
        resetTrackControls (t);
        tk.name = name.isNotEmpty() ? name : "Track " + juce::String (t + 1);
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
    JUCE_ASSERT_MESSAGE_THREAD
    if (! isTrack (t)) return;
    tracks[(size_t) t].used.store (false, std::memory_order_release);
    for (int s = 0; s < kSlots; ++s) removePlugin (t, s);
    setTrackMedia (t, nullptr);
    tracks[(size_t) t].name = {};
    updateLatencies();
}

void PlaygroundEngine::setTrackMedia (int t, SampleData::Ptr media)
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (! isTrack (t)) return;
    const juce::ScopedLock sl (structureLock);
    auto& tk = tracks[(size_t) t];
    auto old = tk.mediaOwned;
    tk.mediaOwned = media;
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
juce::String PlaygroundEngine::getTrackName (int t) const    { return isTrack (t) ? tracks[(size_t) t].name : juce::String(); }
void PlaygroundEngine::setTrackName (int t, const juce::String& n) { if (isTrack (t)) tracks[(size_t) t].name = n; }
juce::Colour PlaygroundEngine::getTrackColour (int t) const  { return isTrack (t) ? tracks[(size_t) t].colour : juce::Colours::grey; }
void PlaygroundEngine::setTrackColour (int t, juce::Colour c) { if (isTrack (t)) tracks[(size_t) t].colour = c; }
juce::String PlaygroundEngine::getBusName (int b) const       { return juce::isPositiveAndBelow (b, kNumBuses) ? busNames[b] : juce::String(); }
void PlaygroundEngine::setBusName (int b, const juce::String& n) { if (juce::isPositiveAndBelow (b, kNumBuses)) busNames.set (b, n); }

juce::String PlaygroundEngine::getStripName (int strip) const
{
    if (isTrack (strip)) return getTrackName (strip);
    if (isBus (strip)) return getBusName (strip - kFirstBus);
    return strip == kMaster ? "Master" : juce::String();
}

void PlaygroundEngine::updateSongLength()
{
    double longest = 0.0;
    for (auto& t : tracks)
        if (t.used.load() && t.mediaOwned != nullptr && t.mediaOwned->getSourceRate() > 0.0)
            longest = juce::jmax (longest, (double) t.mediaOwned->getNumFrames() / t.mediaOwned->getSourceRate());
    songLength.store (longest);
}

//==============================================================================
juce::ValueTree PlaygroundEngine::createSessionTree() const
{
    juce::ValueTree s (kSessionType);
    s.setProperty ("version", 1, nullptr);
    s.setProperty ("loopOn", loopOn.load(), nullptr);
    s.setProperty ("loopStart", loopStart.load(), nullptr);
    s.setProperty ("loopEnd", loopEnd.load(), nullptr);
    s.setProperty ("position", getPositionSeconds(), nullptr);

    juce::ValueTree mixer ("MIXER");
    mixer.appendChild (const_cast<juce::AudioProcessorValueTreeState&> (apvts).copyState(), nullptr);
    s.appendChild (mixer, nullptr);

    for (int t = 0; t < kMaxTracks; ++t)
    {
        const auto& tk = tracks[(size_t) t];
        if (! tk.used.load()) continue;
        juce::ValueTree tt ("TRACK");
        tt.setProperty ("index", t, nullptr);
        tt.setProperty ("name", tk.name, nullptr);
        tt.setProperty ("colour", tk.colour.toString(), nullptr);
        tt.setProperty ("file", tk.mediaOwned != nullptr ? tk.mediaOwned->getFile().getFullPathName() : juce::String(), nullptr);
        s.appendChild (tt, nullptr);
    }
    for (int b = 0; b < kNumBuses; ++b)
    {
        juce::ValueTree bt ("BUS");
        bt.setProperty ("index", b, nullptr);
        bt.setProperty ("name", busNames[b], nullptr);
        s.appendChild (bt, nullptr);
    }
    for (int st = 0; st < kNumStrips; ++st)
        for (int sl = 0; sl < kSlots; ++sl)
            if (auto* p = strips[(size_t) st].owned[(size_t) sl].get())
            {
                juce::ValueTree it ("INSERT");
                it.setProperty ("strip", st, nullptr);
                it.setProperty ("slot", sl, nullptr);
                it.setProperty ("plugin", p->getInfo().id, nullptr);
                it.appendChild (p->saveState(), nullptr);
                s.appendChild (it, nullptr);
            }
    return s;
}

juce::StringArray PlaygroundEngine::mediaFilesIn (const juce::ValueTree& session)
{
    juce::StringArray files;
    for (const auto& c : session)
        if (c.hasType ("TRACK") && c["file"].toString().isNotEmpty())
            files.addIfNotAlreadyThere (c["file"].toString());
    return files;
}

bool PlaygroundEngine::applySessionTree (const juce::ValueTree& s, const std::map<juce::String, SampleData::Ptr>& media, juce::StringArray* missing)
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (! s.hasType (kSessionType)) return false;

    for (int t = 0; t < kMaxTracks; ++t) removeTrack (t);
    for (int st = kFirstBus; st < kNumStrips; ++st)
        for (int sl = 0; sl < kSlots; ++sl) removePlugin (st, sl);

    const auto mixerState = s.getChildWithName ("MIXER").getChildWithName (apvts.state.getType());
    if (mixerState.isValid())
    {
        // only parameters we know (forward compatible), everything else back to default
        for (auto* p : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                const auto child = mixerState.getChildWithProperty ("id", rp->getParameterID());
                rp->setValueNotifyingHost (child.isValid() ? rp->convertTo0to1 ((float) child["value"]) : rp->getDefaultValue());
            }
    }

    busNames = defaultBusNames();
    for (const auto& c : s)
    {
        if (c.hasType ("TRACK"))
        {
            const int t = c["index"];
            if (! isTrack (t)) continue;
            const auto file = c["file"].toString();
            auto it = media.find (file);
            SampleData::Ptr m = it != media.end() ? it->second : nullptr;
            if (m == nullptr && file.isNotEmpty() && missing != nullptr) missing->add (file);
            auto& tk = tracks[(size_t) t];
            tk.name = c["name"].toString();
            tk.colour = juce::Colour::fromString (c.getProperty ("colour", kPalette[t % 8].toString()).toString());
            setTrackMedia (t, m);
            tk.used.store (true, std::memory_order_release);
        }
        else if (c.hasType ("BUS"))
            setBusName (c["index"], c["name"].toString());
    }
    for (const auto& c : s)
        if (c.hasType ("INSERT"))
            if (auto* p = insertPlugin (c["strip"], c["slot"], c["plugin"].toString()))
                p->loadState (c.getChild (0));

    updateSongLength();
    updateLatencies();
    setLoop ((bool) s["loopOn"], (double) s["loopStart"], (double) s["loopEnd"]);
    setPositionSeconds ((double) s.getProperty ("position", 0.0));
    return true;
}

void PlaygroundEngine::newSession()
{
    JUCE_ASSERT_MESSAGE_THREAD
    juce::ValueTree empty (kSessionType);
    applySessionTree (empty, {});
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->getDefaultValue());

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

void PlaygroundEngine::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = createSessionTree().createXml())
        copyXmlToBinary (*xml, dest);
}

void PlaygroundEngine::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        applySessionTree (juce::ValueTree::fromXml (*xml), {});
}
} // namespace pg
