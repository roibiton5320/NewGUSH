/*
    Modulation.h
    ---------------------------------------------------------------------------
    Stage 4: the Maths half of the box.

    Four sources -- two LFOs, a random walk, and an envelope follower on the
    input -- routed through four assignable slots to any parameter in the
    engine. This is what stops an ambient patch being a static wash: the delay
    time creeps, the grains breathe, the resonator drifts, and none of it
    repeats on a bar line.

    Everything here runs at CONTROL RATE (one tick per 32 audio samples,
    ~1.5 kHz), which is far above anything a knob needs and 32x cheaper than
    running it per sample.

    The random walk is seeded. Same seed, same "random" performance, every
    time the preset loads -- so a happy accident can be kept.
*/

#pragma once

#include "DspCommon.h"

namespace gush
{

enum class ModSource { None = 0, Lfo1, Lfo2, Random, Envelope, Count };

enum class ModDest
{
    None = 0,
    GrainSize, GrainDensity, Spray, DelayTime, Pitch, Feedback,
    ResFrequency, ResDamping, RevSize, RevShimmer, Cutoff, Mix,
    Count
};

//==============================================================================
class Lfo
{
public:
    void prepare (double controlRate) noexcept { cr = controlRate; setRate (0.3f); phase = 0.0f; }
    void reset (float p = 0.0f) noexcept       { phase = p; }

    void setRate (float hz) noexcept  { inc = clampf (hz, 0.005f, 40.0f) / (float) cr; }
    void setShape (int s) noexcept    { shape = s; }

    float tick() noexcept
    {
        phase += inc;
        if (phase >= 1.0f) phase -= 1.0f;

        switch (shape)
        {
            case 1:  return 4.0f * std::fabs (phase - 0.5f) - 1.0f;          // triangle
            case 2:  return 1.0f - 2.0f * phase;                             // ramp down
            case 3:  return phase < 0.5f ? 1.0f : -1.0f;                     // square
            default: return std::sin (kTwoPi * phase);                       // sine
        }
    }

private:
    double cr = 1500.0;
    float  inc = 0.0002f, phase = 0.0f;
    int    shape = 0;
};

//==============================================================================
/** Stepped random that can be slewed into a continuous drift.
    slew = 0 -> classic sample & hold. slew = 1 -> a slow wander. */
class RandomWalk
{
public:
    void prepare (double controlRate) noexcept
    {
        cr = controlRate;
        setRate (0.8f);
        setSlew (0.5f);
        value = target = 0.0f;
        countdown = 0.0f;
    }

    void reset() noexcept { value = target = 0.0f; countdown = 0.0f; }
    void setSeed (uint32_t s) noexcept { rng.setSeed (s); }

    void setRate (float hz) noexcept { period = (float) cr / clampf (hz, 0.01f, 40.0f); }

    void setSlew (float s) noexcept
    {
        s = clampf (s, 0.0f, 1.0f);
        // 0 -> instant step, 1 -> ~1.5 s to arrive
        const float tau = 0.0005f + s * s * 1.5f;
        coeff = 1.0f - std::exp (-1.0f / std::max (1.0f, tau * (float) cr));
    }

    float tick() noexcept
    {
        countdown -= 1.0f;
        if (countdown <= 0.0f)
        {
            countdown += std::max (1.0f, period);
            // A walk, not white noise: each step departs from where we are.
            target = clampf (target + rng.nextBipolar() * 0.75f, -1.0f, 1.0f);
        }

        value += coeff * (target - value);
        return value;
    }

private:
    Rng    rng { 0xB16B00B5u };
    double cr = 1500.0;
    float  period = 1500.0f, countdown = 0.0f, target = 0.0f, value = 0.0f, coeff = 0.5f;
};

//==============================================================================
class EnvelopeFollower
{
public:
    void prepare (double controlRate) noexcept
    {
        atk = 1.0f - std::exp (-1.0f / std::max (1.0f, 0.010f * (float) controlRate));
        rel = 1.0f - std::exp (-1.0f / std::max (1.0f, 0.220f * (float) controlRate));
        value = 0.0f;
    }

    void reset() noexcept { value = 0.0f; }

    /** Feed the peak of one control block. Returns 0..1-ish. */
    float tick (float peak) noexcept
    {
        const float c = (peak > value) ? atk : rel;
        value += c * (peak - value);
        return clampf (value, 0.0f, 1.0f);
    }

private:
    float atk = 0.1f, rel = 0.01f, value = 0.0f;
};

//==============================================================================
class ModMatrix
{
public:
    static constexpr int kSlots     = 4;
    static constexpr int kNumSources = (int) ModSource::Count;
    static constexpr int kNumDests   = (int) ModDest::Count;

    void prepare (double controlRate)
    {
        lfo[0].prepare (controlRate);
        lfo[1].prepare (controlRate);
        lfo[1].reset (0.37f);
        walk.prepare (controlRate);
        env.prepare (controlRate);
        reset();
    }

    void reset()
    {
        lfo[0].reset (0.0f);
        lfo[1].reset (0.37f);
        walk.reset();
        env.reset();
        for (auto& d : dest) d = 0.0f;
    }

    void setSeed (uint32_t s) noexcept { walk.setSeed (s); }

    void setLfo (int index, float rateHz, int shape) noexcept
    {
        lfo[index].setRate (rateHz);
        lfo[index].setShape (shape);
    }

    void setRandom (float rateHz, float slew) noexcept
    {
        walk.setRate (rateHz);
        walk.setSlew (slew);
    }

    void setSlot (int i, int source, int destination, float amount) noexcept
    {
        slots[i].src    = (ModSource) clampf ((float) source, 0.0f, (float) kNumSources - 1.0f);
        slots[i].dst    = (ModDest)   clampf ((float) destination, 0.0f, (float) kNumDests - 1.0f);
        slots[i].amount = clampf (amount, -1.0f, 1.0f);
    }

    /** One control block. `inputPeak` drives the envelope follower source. */
    void update (float inputPeak) noexcept
    {
        src[(int) ModSource::None]     = 0.0f;
        src[(int) ModSource::Lfo1]     = lfo[0].tick();
        src[(int) ModSource::Lfo2]     = lfo[1].tick();
        src[(int) ModSource::Random]   = walk.tick();
        src[(int) ModSource::Envelope] = env.tick (inputPeak) * 2.0f - 1.0f;

        for (auto& d : dest) d = 0.0f;

        for (const auto& s : slots)
        {
            if (s.src == ModSource::None || s.dst == ModDest::None || s.amount == 0.0f)
                continue;

            dest[(int) s.dst] += src[(int) s.src] * s.amount;
        }

        for (auto& d : dest) d = clampf (d, -2.0f, 2.0f);
    }

    /** Signed modulation applied to one destination, roughly -1 .. 1. */
    float value (ModDest d) const noexcept { return dest[(int) d]; }

private:
    struct Slot { ModSource src = ModSource::None; ModDest dst = ModDest::None; float amount = 0.0f; };

    Lfo              lfo[2];
    RandomWalk       walk;
    EnvelopeFollower env;
    Slot             slots[kSlots];
    float            src[kNumSources] {};
    float            dest[kNumDests] {};
};

} // namespace gush
