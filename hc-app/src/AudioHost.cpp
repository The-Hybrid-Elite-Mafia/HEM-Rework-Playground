#include "AudioHost.hpp"
#include "platform/Platform.hpp"

#include "hybrid/io/AudioCallback.hpp"
#include "hybrid/io/DeviceProbe.hpp"
#include "hybrid/io/OfflineStream.hpp"
#include "hybrid/io/WasapiExclusiveOutput.hpp"
#include "hybrid/io/WasapiSharedOutput.hpp"
#if defined(HC_HAS_ASIO_SDK)
 #include "hybrid/io/AsioDuplex.hpp"
#endif

#include <windows.h>
#include <immintrin.h>

#include <algorithm>
#include <cstdio>

namespace hem::app
{
using platform::utf8;

std::string DeviceChoice::kindKey() const
{
    switch (kind)
    {
        case Kind::wasapiShared:    return "wasapi-shared";
        case Kind::wasapiExclusive: return "wasapi-exclusive";
        case Kind::asio:            return "asio";
        case Kind::offline:         return "offline";
    }
    return "wasapi-shared";
}

DeviceChoice::Kind DeviceChoice::kindFromKey (const std::string& k)
{
    if (k == "wasapi-exclusive") return Kind::wasapiExclusive;
    if (k == "asio") return Kind::asio;
    if (k == "offline") return Kind::offline;
    return Kind::wasapiShared;
}

namespace
{
    /** The engine on the device thread, with its own load meter. Never allocates or waits. */
    class EngineCallback final : public hc::io::AudioCallback
    {
    public:
        explicit EngineCallback (pg::PlaygroundEngine& e) : engine (e)
        {
            LARGE_INTEGER f;
            ::QueryPerformanceFrequency (&f);
            secondsPerTick = 1.0 / (double) f.QuadPart;
        }

        void prepare (const hc::io::StreamConfig& c) override
        {
            rate = c.sampleRate > 0 ? (double) c.sampleRate : 48000.0;
            engine.prepare (rate, (int) std::max<std::uint32_t> (c.blockFrames, 64));
            loadAvg.store (0.0f);
            loadPeak.store (0.0f);
            overruns.store (0);
        }

        void process (const float* const*, std::uint32_t, float* const* outputs, std::uint32_t numOutputs, std::uint32_t frames) noexcept override
        {
            _mm_setcsr (_mm_getcsr() | 0x8040);   // flush denormals to zero
            LARGE_INTEGER t0, t1;
            ::QueryPerformanceCounter (&t0);
            engine.process (outputs, (int) numOutputs, (int) frames);
            ::QueryPerformanceCounter (&t1);
            const double budget = (double) frames / rate;
            const float load = budget > 0.0 ? (float) ((double) (t1.QuadPart - t0.QuadPart) * secondsPerTick / budget) : 0.0f;
            loadAvg.store (loadAvg.load (std::memory_order_relaxed) * 0.97f + load * 0.03f, std::memory_order_relaxed);
            if (load > loadPeak.load (std::memory_order_relaxed)) loadPeak.store (load, std::memory_order_relaxed);
            if (load >= 1.0f) overruns.fetch_add (1, std::memory_order_relaxed);
        }

        pg::PlaygroundEngine& engine;
        double rate = 48000.0, secondsPerTick = 0.0;
        std::atomic<float> loadAvg { 0.0f }, loadPeak { 0.0f };
        std::atomic<std::uint32_t> overruns { 0 };
    };

    std::string ms (double v) { char b[32]; std::snprintf (b, sizeof b, "%.1f ms", v); return b; }
}

struct AudioHost::Impl
{
    explicit Impl (pg::PlaygroundEngine& e) : callback (e) {}
    EngineCallback callback;
    std::unique_ptr<hc::io::WasapiSharedOutput> shared;
    std::unique_ptr<hc::io::WasapiExclusiveOutput> exclusive;
    std::unique_ptr<hc::io::OfflineStream> offline;
#if defined(HC_HAS_ASIO_SDK)
    std::unique_ptr<hc::io::AsioDuplex> asio;
#endif
    float shownPeak = 0.0f;
};

AudioHost::AudioHost (pg::PlaygroundEngine& e) : impl (std::make_unique<Impl> (e)), engine (e) {}
AudioHost::~AudioHost() { close(); }

bool AudioHost::hasAsio() noexcept
{
#if defined(HC_HAS_ASIO_SDK)
    return true;
#else
    return false;
#endif
}

std::vector<DeviceChoice> AudioHost::listDevices()
{
    using K = DeviceChoice::Kind;
    std::vector<DeviceChoice> out;
    out.push_back ({ K::wasapiShared, L"", "WASAPI shared - Windows default output", "Windows default output" });
    std::vector<DeviceChoice> exclusive;
    if (auto eps = hc::io::enumerateWasapi())
        for (const auto& ep : *eps)
        {
            if (ep.flow != hc::io::Flow::Render || ep.id.empty()) continue;
            const std::string name = utf8 (ep.name);
            out.push_back ({ K::wasapiShared, ep.id, "WASAPI shared - " + name, name });
            if (ep.excl48kFloat32 || ep.excl48kInt24in32 || ep.excl48kInt16)
                exclusive.push_back ({ K::wasapiExclusive, ep.id, "WASAPI exclusive 48 kHz - " + name, name });
        }
    out.insert (out.end(), exclusive.begin(), exclusive.end());
#if defined(HC_HAS_ASIO_SDK)
    for (const auto& d : hc::io::enumerateAsioDrivers())
        if (! d.dllPath.empty()) out.push_back ({ K::asio, d.name, "ASIO - " + utf8 (d.name), utf8 (d.name) });
#endif
    out.push_back ({ K::offline, L"offline", "No sound card (offline clock)", "Offline clock" });
    return out;
}

void AudioHost::close() noexcept
{
    if (impl->shared) { impl->shared->close(); impl->shared.reset(); }
    if (impl->exclusive) { impl->exclusive->close(); impl->exclusive.reset(); }
    if (impl->offline) { impl->offline->close(); impl->offline.reset(); }
#if defined(HC_HAS_ASIO_SDK)
    if (impl->asio) { impl->asio->close(); impl->asio.reset(); }
#endif
    if (st.running) engine.release();
    st.running = false;
}

bool AudioHost::open (const DeviceChoice& c)
{
    close();
    choice = c;
    st = {};
    st.device = c.name;
    char buf[160];
    auto fail = [this] (const std::string& why)
    {
        st.problem = why;
        st.alert = true;
        st.running = false;
        engine.prepare (48000.0, 512);   // loading still converts to 48 kHz, export still works
        return false;
    };
    auto hex = [&buf] (long code) { std::snprintf (buf, sizeof buf, "0x%08lX", (unsigned long) code); return std::string (buf); };

    hc::io::StreamConfig cfg {};
    using K = DeviceChoice::Kind;
    if (c.kind == K::wasapiShared)
    {
        impl->shared = std::make_unique<hc::io::WasapiSharedOutput>();
        hc::io::WasapiSharedOutput::Options o;
        o.deviceId = c.id;
        o.bufferMs = 20.0;
        auto r = impl->shared->open (o, impl->callback);
        if (! r) { impl->shared.reset(); return fail ("This output would not open (" + std::string (r.error().stage) + ", " + hex (r.error().code) + "). Pick another in Audio Settings."); }
        cfg = *r;
        if (auto s = impl->shared->start(); ! s) { impl->shared.reset(); return fail ("This output would not start (" + hex (s.error().code) + ")."); }
        st.driver = "WASAPI SHARED";
        if (impl->shared->deviceMixRate() != 0 && impl->shared->deviceMixRate() != 48000)
        {
            std::snprintf (buf, sizeof buf, " (Windows converts to the device's %.1f kHz)", impl->shared->deviceMixRate() / 1000.0);
            st.format = buf;
        }
    }
    else if (c.kind == K::wasapiExclusive)
    {
        impl->exclusive = std::make_unique<hc::io::WasapiExclusiveOutput>();
        hc::io::WasapiExclusiveOutput::Options o;
        o.deviceId = c.id;
        auto r = impl->exclusive->open (o, impl->callback);
        if (! r)
        {
            const auto code = (unsigned long) r.error().code;
            impl->exclusive.reset();
            if (code == 0x88890008ul) return fail ("This output cannot play 48 kHz on its own. Pick it as WASAPI shared instead.");
            if (code == 0x8889000Aul) return fail ("Another app is using this output on its own. Close it, or pick WASAPI shared.");
            return fail ("WASAPI exclusive would not open (" + std::string (r.error().stage) + ", " + hex (r.error().code) + ").");
        }
        cfg = *r;
        if (auto s = impl->exclusive->start(); ! s) { impl->exclusive.reset(); return fail ("WASAPI exclusive would not start (" + hex (s.error().code) + ")."); }
        st.driver = "WASAPI EXCLUSIVE";
    }
    else if (c.kind == K::asio)
    {
#if defined(HC_HAS_ASIO_SDK)
        impl->asio = std::make_unique<hc::io::AsioDuplex>();
        hc::io::AsioDuplex::Options o;
        o.driverName = c.id;
        auto r = impl->asio->open (o, impl->callback);
        if (! r) { impl->asio.reset(); return fail ("The ASIO driver would not open (" + std::string (r.error().stage) + ", error " + std::to_string (r.error().code) + ")."); }
        cfg = *r;
        if (auto s = impl->asio->start(); ! s) { impl->asio.reset(); return fail ("The ASIO driver would not start (error " + std::to_string (s.error().code) + ")."); }
        st.driver = "ASIO";
#else
        return fail ("This build has no ASIO.");
#endif
    }
    else
    {
        impl->offline = std::make_unique<hc::io::OfflineStream>();
        hc::io::OfflineStream::Options o;
        o.numOutputs = 2;
        cfg = impl->offline->open (o, impl->callback);
        impl->offline->start();
        st.driver = "OFFLINE CLOCK";
    }

    st.running = true;
    st.sampleRate = cfg.sampleRate;
    st.blockFrames = cfg.blockFrames;
    st.outputs = cfg.numOutputs;
    st.latencyMs = cfg.outputLatencyMs;
    std::snprintf (buf, sizeof buf, "%.1f kHz - %u frames = %.1f ms", cfg.sampleRate / 1000.0, cfg.blockFrames,
                   1000.0 * cfg.blockFrames / std::max (1u, cfg.sampleRate));
    st.format = buf + (cfg.outputLatencyMs > 0.0 ? " - out " + ms (cfg.outputLatencyMs) : std::string()) + st.format;
    return true;
}

void AudioHost::poll()
{
    st.loadAvg = impl->callback.loadAvg.load (std::memory_order_relaxed);
    const float peak = impl->callback.loadPeak.exchange (0.0f, std::memory_order_relaxed);
    impl->shownPeak = std::max (peak, impl->shownPeak * 0.97f);
    st.loadPeak = impl->shownPeak;
    st.overruns = impl->callback.overruns.load (std::memory_order_relaxed);

    std::uint32_t ev = 0;
    if (impl->shared) ev |= impl->shared->takeEvents();
    if (impl->exclusive) ev |= impl->exclusive->takeEvents();
#if defined(HC_HAS_ASIO_SDK)
    if (impl->asio) ev |= impl->asio->takeEvents();
#endif
    using namespace hc::io::StreamEvent;
    if (ev & (ResetRequest | SampleRateMoved | ResyncRequest))
    {
        const DeviceChoice again = choice;
        open (again);   // the driver asked for a restart (buffer size / rate changed in its own panel)
        return;
    }
    if (ev & (DeviceLost | ThreadFault))
    {
        close();
        st.alert = true;
        st.problem = (ev & DeviceLost) ? "The sound card was unplugged or switched off. Pick one in File > Audio Settings."
                                       : "The sound card stopped answering. Pick one in File > Audio Settings.";
        engine.prepare (48000.0, 512);
        return;
    }
    if (ev & Overload) ++st.driverDropouts;
}
} // namespace hem::app
