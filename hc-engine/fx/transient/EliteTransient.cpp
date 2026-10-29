#include "EliteTransient.hpp"

namespace hem::pg
{
const PluginInfo EliteTransient::info { "elite-transient", "Elite Transient", "PNCH",
                                        "Punch and sustain shaper: more (or less) hit, more (or less) ring-out", 0xffff4f6d };

namespace
{
    params::Layout transientLayout()
    {
        params::Layout l;
        params::addFloat (l, "attack", "Attack", -100.0f, 100.0f, 0.0f, "%", 1.0f);
        params::addFloat (l, "sustain", "Sustain", -100.0f, 100.0f, 0.0f, "%", 1.0f);
        params::addChoice (l, "speed", "Speed", EliteTransient::speeds(), 1);
        params::addDb (l, "output", "Output", -12.0f, 12.0f, 0.0f);
        params::addPercent (l, "mix", "Mix", 100.0f);
        return l;
    }

    // envelope timing per SPEED: fast attack/release, slow attack/release (ms)
    struct Times { float fastAtk, fastRel, slowAtk, slowRel, holdRel; };
    Times timesFor (int speed)
    {
        switch (speed)
        {
            case 0:  return { 0.3f, 25.0f, 8.0f, 60.0f, 6.0f };      // fast: tight drums, hats
            case 2:  return { 1.5f, 80.0f, 30.0f, 250.0f, 30.0f };   // slow: bass, full mix
            default: return { 0.6f, 45.0f, 15.0f, 120.0f, 12.0f };   // medium
        }
    }

    // follower with separate attack / release coefficients
    inline float follow (float env, float x, float atk, float rel) noexcept
    {
        return x > env ? x + atk * (env - x) : x + rel * (env - x);
    }

    constexpr float kMaxDb = 15.0f;   // the shaper never moves the level more than this
}

EliteTransient::EliteTransient() : Plugin (info, transientLayout())
{
    pAttack = raw ("attack"); pSustain = raw ("sustain"); pSpeed = raw ("speed"); pOutput = raw ("output"); pMix = raw ("mix");
}

void EliteTransient::prepareDsp (double fs, int)
{
    lastSpeed = -1;
    output.reset (fs, 0.02); mix.reset (fs, 0.02);
    output.setCurrentAndTargetValue (params::dbToGain (param (pOutput)));
    mix.setCurrentAndTargetValue (param (pMix) * 0.01f);
    resetDsp();
}

void EliteTransient::resetDsp() noexcept { envFast = envSlow = envHold = 0.0f; }

void EliteTransient::processDsp (float* left, float* right, int n) noexcept
{
    const int speed = jlimit (0, 2, (int) param (pSpeed));
    if (speed != lastSpeed)
    {
        const auto t = timesFor (speed);
        aFast = dsp::timeCoeff (t.fastAtk, sampleRateHz); rFast = dsp::timeCoeff (t.fastRel, sampleRateHz);
        aSlow = dsp::timeCoeff (t.slowAtk, sampleRateHz); rSlow = dsp::timeCoeff (t.slowRel, sampleRateHz);
        rHold = dsp::timeCoeff (t.holdRel, sampleRateHz);
        lastSpeed = speed;
    }
    const float attack = param (pAttack) * 0.01f, sustain = param (pSustain) * 0.01f;
    output.setTargetValue (params::dbToGain (param (pOutput)));
    mix.setTargetValue (param (pMix) * 0.01f);

    float shown = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float l = left[i], r = right[i];
        const float x = std::max (std::abs (l), std::abs (r));
        envFast = follow (envFast, x, aFast, rSlow);    // jumps on a hit, lets go slowly
        envSlow = follow (envSlow, x, aSlow, rSlow);    // rises slowly: the gap to envFast = the hit
        envHold = follow (envHold, x, aFast, rHold);    // falls quickly: the gap to envFast = the ring-out
        const float fastDb = dsp::linToDb (envFast + 1.0e-9f);
        const float hitDb = jlimit (0.0f, 24.0f, fastDb - dsp::linToDb (envSlow + 1.0e-9f));
        const float ringDb = jlimit (0.0f, 24.0f, fastDb - dsp::linToDb (envHold + 1.0e-9f));
        const float gDb = jlimit (-kMaxDb, kMaxDb, attack * hitDb * 0.75f + sustain * ringDb * 0.75f);
        const float g = dsp::dbToLin (gDb);
        const float m = mix.getNextValue(), out = output.getNextValue();
        left[i] = (l + m * (l * g - l)) * out;
        right[i] = (r + m * (r * g - r)) * out;
        if (std::abs (gDb) > std::abs (shown)) shown = gDb;
    }
    setReadout (gainNow, shown);
}
} // namespace hem::pg
