#pragma once

#include "hem/pg/AudioBuffer.hpp"
#include <string>
#include <atomic>

namespace hem::pg::library
{
/** What the LIBRARY measures about a song or beat when it comes in (offline, background thread). */
struct AudioFacts
{
    double seconds = 0.0;
    double bpm = 0.0;           // 0 = no steady beat found
    float tempoConfidence = 0.0f;
    int keyRoot = -1;           // pitch class, -1 = no clear key
    bool minor = false;
    float keyConfidence = 0.0f; // correlation with the key profile
    float lufs = -100.0f;       // integrated loudness (BS.1770)
    float peakDb = -100.0f;     // sample peak

    /** "A minor" / "" */
    std::string keyName() const;
};

/** Tempo: onset envelope (spectral flux) -> autocorrelation with a tempo prior around 110 BPM,
    refined by a beat comb. Key: chroma against Krumhansl major / minor key profiles.
    Loudness: BS.1770 integrated. Returns early (partial facts) if cancel is set. */
AudioFacts measure (const AudioBuffer& audio, double sampleRate, const std::atomic<bool>* cancel = nullptr);
} // namespace hem::pg::library
