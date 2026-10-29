#pragma once

#include "hem/pg/Fft.hpp"
#include <complex>
#include <cstdint>
#include <vector>

namespace hem::pg::analysis
{
/** The numbers behind the 3D spectrum: a history of spectra of the master output.

    kRows rows (row 0 = newest), each kPoints log-spaced frequencies from 20 Hz to 20 kHz, in dB
    where a full-scale sine reads 0 dB. A 4096-point Hann FFT runs every 1024 samples and a row is
    the louder of two FFTs, so at 48 kHz one row is about 43 ms and the history is about 2.7 s.
    Fed on the UI thread from the engine's master tap. Allocates only in prepare(). */
class Waterfall
{
public:
    static constexpr int kRows = 64, kPoints = 112, kOrder = 12, kFftSize = 1 << kOrder, kHop = 1024;
    static constexpr float kFloorDb = -96.0f;

    /** Ready to use straight away (48 kHz); prepare() again when the real rate is known. */
    Waterfall() { prepare (48000.0); }

    void prepare (double sampleRate);
    void reset();
    /** Mono samples, any count. */
    void add (const float* mono, int n);

    /** age 0 = newest row; kPoints values in dB (kFloorDb .. about 0). */
    const float* row (int age) const noexcept { return rows.data() + (size_t) ((newest - age + kRows * 2) % kRows) * kPoints; }
    static float pointHz (int i) noexcept;
    std::uint64_t rowsMade() const noexcept { return made; }
    double secondsPerRow() const noexcept { return 2.0 * kHop / fs; }

private:
    void runFft();

    double fs = 48000.0;
    dsp::Fft fft;
    std::vector<std::complex<float>> spectrum;
    std::vector<float> window, ring, work, rows, pending;
    std::vector<int> binLo, binHi;
    std::vector<float> binPos;
    int ringPos = 0, sinceHop = 0, newest = 0, halfCount = 0;
    std::uint64_t made = 0;
};
} // namespace hem::pg::analysis
