// Loudness meter accuracy: K-weighting against the ITU-R BS.1770 published table, and the
// EBU Tech 3341 reference case (1 kHz stereo sine at -23 dBFS must read -23.0 +/- 0.1 LUFS).

#include "Loudness.hpp"
#include "UnitTest.hpp"

#include <cmath>
#include <numbers>
#include <vector>

namespace hem::pg::test
{
namespace
{
    using hemtest::Str;
    struct Ref { double b0, b1, b2, a1, a2; };

    // ITU-R BS.1770-4, tables 1 and 2 (48 kHz).
    constexpr Ref kStage1At48k { 1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585 };
    constexpr Ref kStage2At48k { 1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621 };

    // Independent reference: the analog-prototype design used by libebur128 / pyloudnorm.
    Ref refStage1 (double fs)
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (std::numbers::pi * f0 / fs);
        const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        return { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
    }

    Ref refStage2 (double fs)
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan (std::numbers::pi * f0 / fs);
        const double a0 = 1.0 + K / Q + K * K;
        return { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
    }

    double gainDbAt (const BiquadCoeffs& c, double hz, double fs)
    {
        const double w = 2.0 * std::numbers::pi * hz / fs;
        const double cr = std::cos (w), ci = -std::sin (w), c2r = std::cos (2 * w), c2i = -std::sin (2 * w);
        const double nr = c.b0 + c.b1 * cr + c.b2 * c2r, ni = c.b1 * ci + c.b2 * c2i;
        const double dr = 1.0 + c.a1 * cr + c.a2 * c2r, di = c.a1 * ci + c.a2 * c2i;
        return 10.0 * std::log10 ((nr * nr + ni * ni) / (dr * dr + di * di));
    }
}

class LoudnessTests final : public hemtest::UnitTest
{
public:
    using Str = hemtest::Str;
    LoudnessTests() : hemtest::UnitTest ("Loudness Meter", "Playground") {}

    void checkCoeffs (const BiquadCoeffs& got, const Ref& want, double tol, const char* what)
    {
        expectWithinAbsoluteError (got.b0, want.b0, tol, Str (what) + " b0");
        expectWithinAbsoluteError (got.b1, want.b1, tol, Str (what) + " b1");
        expectWithinAbsoluteError (got.b2, want.b2, tol, Str (what) + " b2");
        expectWithinAbsoluteError (got.a1, want.a1, tol, Str (what) + " a1");
        expectWithinAbsoluteError (got.a2, want.a2, tol, Str (what) + " a2");
    }

    void runTest() override
    {
        using dsp::LoudnessMeter;

        beginTest ("K-weighting at 48 kHz matches the BS.1770 table (1e-8)");
        {
            checkCoeffs (LoudnessMeter::kWeightingStage1 (48000.0), kStage1At48k, 1.0e-8, "stage 1");
            checkCoeffs (LoudnessMeter::kWeightingStage2 (48000.0), kStage2At48k, 1.0e-8, "stage 2");
        }

        beginTest ("K-weighting at 44.1 / 96 kHz matches the analog-prototype design (1e-8)");
        for (double fs : { 44100.0, 88200.0, 96000.0, 192000.0 })
        {
            checkCoeffs (LoudnessMeter::kWeightingStage1 (fs), refStage1 (fs), 1.0e-8, (Str ("stage 1 @ ") + Str (fs)).c_str());
            checkCoeffs (LoudnessMeter::kWeightingStage2 (fs), refStage2 (fs), 1.0e-8, (Str ("stage 2 @ ") + Str (fs)).c_str());
        }

        beginTest ("K-weighting gain at 1 kHz: +0.69 dB at 48 kHz (BS.1770 table), same curve as the reference design at other rates");
        {
            // Table coefficients themselves give +0.6977 dB at 1 kHz / 48 kHz.
            const double g48 = gainDbAt (LoudnessMeter::kWeightingStage1 (48000.0), 1000.0, 48000.0)
                             + gainDbAt (LoudnessMeter::kWeightingStage2 (48000.0), 1000.0, 48000.0);
            expectWithinAbsoluteError (g48, 0.6977, 0.001, "gain at 48000");
        }
        for (double fs : { 44100.0, 48000.0, 96000.0 })
        {
            // The bilinear transform warps the shelf a little with the rate (0.68..0.70 dB at 1 kHz);
            // the meter must follow the reference design exactly at every rate.
            auto toC = [] (const Ref& r) { BiquadCoeffs c; c.b0 = r.b0; c.b1 = r.b1; c.b2 = r.b2; c.a1 = r.a1; c.a2 = r.a2; return c; };
            const double g = gainDbAt (LoudnessMeter::kWeightingStage1 (fs), 1000.0, fs)
                           + gainDbAt (LoudnessMeter::kWeightingStage2 (fs), 1000.0, fs);
            const double want = gainDbAt (toC (refStage1 (fs)), 1000.0, fs) + gainDbAt (toC (refStage2 (fs)), 1000.0, fs);
            logMessage (Str ("K gain @1 kHz, ") + Str (fs) + " Hz: " + Str (g, 4) + " dB (reference " + Str (want, 4) + " dB)");
            expectWithinAbsoluteError (g, want, 1.0e-6, Str ("gain at ") + Str (fs));
        }

        beginTest ("EBU Tech 3341 case 1: 1 kHz stereo sine at -23 dBFS reads -23.0 +/- 0.1 LUFS (M, S, I)");
        for (double fs : { 44100.0, 48000.0, 96000.0 })
        {
            LoudnessMeter lm;
            lm.prepare (fs);
            const int n = (int) (fs * 10.0);
            const float amp = (float) std::pow (10.0, -23.0 / 20.0);
            std::vector<float> l ((size_t) n), r ((size_t) n);
            for (int i = 0; i < n; ++i) l[(size_t) i] = r[(size_t) i] = amp * (float) std::sin (2.0 * std::numbers::pi * 1000.0 * i / fs);

            hemtest::beginTracking();
            for (int start = 0; start < n; start += 512)
                lm.process (l.data() + start, r.data() + start, std::min (512, n - start));
            const int allocs = hemtest::endTracking();
            expectEquals (allocs, 0, "allocations in process()");

            const Str at = Str (" @ ") + Str (fs) + " Hz";
            logMessage (Str ("M ") + Str (lm.getMomentary(), 3) + "  S " + Str (lm.getShortTerm(), 3) + "  I " + Str (lm.getIntegrated(), 3) + " LUFS" + at);
            expectWithinAbsoluteError (lm.getMomentary(), -23.0f, 0.1f, Str ("momentary") + at);
            expectWithinAbsoluteError (lm.getShortTerm(), -23.0f, 0.1f, Str ("short-term") + at);
            expectWithinAbsoluteError (lm.getIntegrated(), -23.0f, 0.1f, Str ("integrated") + at);
        }
    }
};

static LoudnessTests loudnessTests;
} // namespace hem::pg::test
