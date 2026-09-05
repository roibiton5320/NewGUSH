/*
    DspCommon.h
    ---------------------------------------------------------------------------
    Small, dependency-free building blocks shared by every stage of the engine.

    Nothing in Source/DSP includes JUCE. The whole engine is plain C++17, so it
    can be compiled, tested and profiled without a plugin host. Only the thin
    wrapper in Source/ (PluginProcessor, Parameters) knows JUCE exists.

    Real-time rules obeyed everywhere in this folder:
      * no allocation, no locks, no I/O outside prepare()
      * every recursive state is denormal-guarded
      * coefficients are recomputed at control rate, never per sample
*/

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gush
{

constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

inline float clampf (float v, float lo, float hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

inline float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }

/** Kills denormals and any NaN/Inf that escaped a feedback path.
    Cheap enough to sit inside per-sample recursions. */
inline float flush (float x) noexcept
{
    if (! std::isfinite (x)) return 0.0f;
    return std::fabs (x) < 1.0e-25f ? 0.0f : x;
}

inline float semitonesToRatio (float semis) noexcept { return std::exp2 (semis * (1.0f / 12.0f)); }
inline float dbToGain (float db) noexcept            { return std::pow (10.0f, db * 0.05f); }

/** Hann window, phase in [0, 1). Two of these half a period apart sum to
    exactly 1.0 — which is why every crossfade in this engine uses it. */
inline float hann (float phase01) noexcept { return 0.5f - 0.5f * std::cos (kTwoPi * phase01); }

/** Equal-power pan. pan in [-1, 1]. */
inline void equalPowerPan (float pan, float& l, float& r) noexcept
{
    const float angle = (clampf (pan, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
    l = std::cos (angle);
    r = std::sin (angle);
}

/** Cheap tanh-shaped limiter, for feedback ceilings where the accuracy of a
    real tanh would be wasted. */
inline float softClip (float x) noexcept
{
    x = clampf (x, -3.0f, 3.0f);
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

/** Transparent below `threshold`, smoothly saturating above it, hard ceiling
    at +/-1.

    A plain soft clipper inside a feedback loop is not free. At 0.3 amplitude
    the usual tanh-shaped curve costs about 0.2 dB, and a reverb line that
    recirculates twenty-four times a second turns that into 5 dB per second --
    which quietly ate nearly half of whatever the DECAY knob promised. This
    one is exactly linear until the signal genuinely needs limiting, so a
    quiet tail decays at the rate it was asked to.

    Continuous in value and slope at the threshold: tanh(0) = 0 and its
    derivative there is 1, so the two halves meet without a kink. */
inline float softLimit (float x, float threshold = 0.7f) noexcept
{
    const float a = std::fabs (x);
    if (a <= threshold) return x;

    const float headroom = 1.0f - threshold;
    const float limited  = threshold + headroom * std::tanh ((a - threshold) / headroom);
    return x < 0.0f ? -limited : limited;
}

/** 4-point, 3rd-order Hermite interpolation (de Soras). Samples in time order:
    ym1 precedes y0; t in [0,1) walks from y0 towards y1. */
inline float hermite (float ym1, float y0, float y1, float y2, float t) noexcept
{
    const float c = (y1 - ym1) * 0.5f;
    const float v = y0 - y1;
    const float w = c + v;
    const float a = w + v + (y2 - y0) * 0.5f;
    const float b = w + a;
    return ((a * t - b) * t + c) * t + y0;
}

//==============================================================================
/** One-pole parameter smoother. Every continuous parameter that reaches the
    audio path goes through one of these, so knob moves never zipper. */
class Smoother
{
public:
    void setTime (double sampleRate, float milliseconds) noexcept
    {
        const double tau = std::max (0.01, 0.001 * (double) milliseconds) * sampleRate;
        coeff = (float) (1.0 - std::exp (-1.0 / tau));
    }

    void  reset (float v) noexcept     { value = target = v; }
    void  setTarget (float v) noexcept { target = v; }
    float getTarget() const noexcept   { return target; }
    float current() const noexcept     { return value; }

    float next() noexcept
    {
        value += coeff * (target - value);
        return value;
    }

private:
    float coeff = 0.01f, value = 0.0f, target = 0.0f;
};

//==============================================================================
class OnePoleLP
{
public:
    void prepare (double sr) noexcept { sampleRate = sr; setCutoff (18000.0f); reset(); }
    void reset() noexcept             { z = 0.0f; }

    void setCutoff (float hz) noexcept
    {
        hz = clampf (hz, 5.0f, (float) (sampleRate * 0.49));
        a  = 1.0f - std::exp (-kTwoPi * hz / (float) sampleRate);
    }

    float process (float x) noexcept { z = flush (z + a * (x - z)); return z; }

private:
    double sampleRate = 48000.0;
    float a = 0.5f, z = 0.0f;
};

class OnePoleHP
{
public:
    void  prepare (double sr) noexcept  { lp.prepare (sr); setCutoff (20.0f); }
    void  reset() noexcept              { lp.reset(); }
    void  setCutoff (float hz) noexcept { lp.setCutoff (hz); }
    float process (float x) noexcept    { return x - lp.process (x); }

private:
    OnePoleLP lp;
};

/** Removes the DC that asymmetric saturation and long feedback loops build up. */
class DCBlocker
{
public:
    void prepare (double sr) noexcept { R = 1.0f - (kTwoPi * 12.0f / (float) sr); reset(); }
    void reset() noexcept             { x1 = y1 = 0.0f; }

    float process (float x) noexcept
    {
        const float y = x - x1 + R * y1;
        x1 = x;
        y1 = flush (y);
        return y1;
    }

private:
    float R = 0.999f, x1 = 0.0f, y1 = 0.0f;
};

//==============================================================================
/** Topology-preserving-transform state variable filter (Zavalishin).
    Stable at any cutoff, and cutoff may be modulated every control block. */
class StateVariableFilter
{
public:
    void prepare (double sr) noexcept { sampleRate = sr; set (1000.0f, 0.707f); reset(); }
    void reset() noexcept             { ic1 = ic2 = 0.0f; }

    void set (float cutoffHz, float q) noexcept
    {
        cutoffHz = clampf (cutoffHz, 15.0f, (float) (sampleRate * 0.49));
        g  = std::tan (kPi * cutoffHz / (float) sampleRate);
        k  = 1.0f / std::max (0.05f, q);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void process (float v0, float& lp, float& bp, float& hp) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = flush (2.0f * v1 - ic1);
        ic2 = flush (2.0f * v2 - ic2);
        lp = v2;
        bp = v1;
        hp = v0 - k * v1 - v2;
    }

private:
    double sampleRate = 48000.0;
    float g = 0.1f, k = 1.0f, a1 = 0.5f, a2 = 0.5f, a3 = 0.5f, ic1 = 0.0f, ic2 = 0.0f;
};

//==============================================================================
/** xorshift32. Deterministic, allocation-free and seedable — which is what
    lets a whole "random" performance be recalled from one saved number. */
class Rng
{
public:
    explicit Rng (uint32_t seed = 0x9E3779B9u) noexcept : state (seed ? seed : 1u) {}

    void setSeed (uint32_t seed) noexcept { state = seed ? seed : 1u; }

    uint32_t nextUInt() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    float next01() noexcept      { return (float) (nextUInt() >> 8) * (1.0f / 16777216.0f); }
    float nextBipolar() noexcept { return next01() * 2.0f - 1.0f; }

private:
    uint32_t state;
};

//==============================================================================
/** tanh with first-order antiderivative anti-aliasing (Parker / Zavalishin).

    Distortion generates harmonics above Nyquist that fold back as aliasing.
    The usual cure is oversampling, which costs CPU and adds latency. ADAA
    instead integrates the nonlinearity across each sample interval, killing
    most of the folding for the price of one extra log. Latency: zero. That
    matters for a plugin people play a guitar through, live.
*/
class SaturatorADAA
{
public:
    void reset() noexcept { x1 = 0.0f; F1 = 0.0f; }

    float process (float x) noexcept
    {
        const float F = antiderivative (x);
        const float d = x - x1;
        const float y = (std::fabs (d) < 1.0e-5f) ? std::tanh ((x + x1) * 0.5f)
                                                  : (F - F1) / d;
        x1 = x;
        F1 = F;
        return flush (y);
    }

private:
    /** log(cosh(x)), evaluated so it cannot overflow for large |x|. */
    static float antiderivative (float x) noexcept
    {
        const float a = std::fabs (x);
        return a + std::log1p (std::exp (-2.0f * a)) - 0.69314718f;
    }

    float x1 = 0.0f, F1 = 0.0f;
};

} // namespace gush
