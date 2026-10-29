#pragma once
// =============================================================================
//  The sound card side of the Playground, on The Hybrid Core's Windows audio layer:
//    WASAPI shared   — any output Windows has (HDMI, Bluetooth headphones, USB...); the engine runs
//                      at 48 kHz and Windows converts to the device's own rate if it must.
//    WASAPI exclusive— the device to ourselves at 48 kHz, lowest latency without ASIO.
//    ASIO            — Steinberg ASIO drivers (SDK fetched at build time under its GPLv3 option).
//    Offline clock   — no sound card (the engine and meters still run; for tests).
//  The engine's process() runs on the device's real-time thread; nothing else does.
//  open / close / poll: message thread only.
// =============================================================================
#include "PlaygroundEngine.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hem::app
{
struct DeviceChoice
{
    enum class Kind : std::uint8_t { wasapiShared, wasapiExclusive, asio, offline };
    Kind kind = Kind::wasapiShared;
    std::wstring id;          // endpoint id / ASIO driver name; empty = Windows' default output
    std::string label;        // "WASAPI shared - Speakers (Realtek)"
    std::string name;         // "Speakers (Realtek)"

    std::string kindKey() const;    // "wasapi-shared", "wasapi-exclusive", "asio", "offline"
    static Kind kindFromKey (const std::string&);
};

struct AudioStatus
{
    bool running = false, alert = false;
    std::string driver;       // "WASAPI SHARED"
    std::string device;
    std::string format;       // "48 kHz - 480 frames = 10.0 ms - out 21.3 ms"
    std::string problem;      // plain words when something went wrong
    std::uint32_t sampleRate = 0, blockFrames = 0, outputs = 0;
    double latencyMs = 0.0;
    float loadAvg = 0.0f, loadPeak = 0.0f;   // share of the time one block lasts
    std::uint32_t overruns = 0, driverDropouts = 0;
};

class AudioHost
{
public:
    explicit AudioHost (pg::PlaygroundEngine&);
    ~AudioHost();

    static std::vector<DeviceChoice> listDevices();
    static bool hasAsio() noexcept;

    /** Opens and starts a device (closes the current one first). False + status().problem on failure. */
    bool open (const DeviceChoice&);
    void close() noexcept;
    /** Message thread, every frame: device events (unplugged, driver reset), load numbers. */
    void poll();

    const AudioStatus& status() const noexcept { return st; }
    const DeviceChoice& current() const noexcept { return choice; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    pg::PlaygroundEngine& engine;
    DeviceChoice choice;
    AudioStatus st;
};
} // namespace hem::app
