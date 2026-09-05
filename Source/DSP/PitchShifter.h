/*
    PitchShifter.h
    ---------------------------------------------------------------------------
    Zero-latency, time-domain, dual-tap crossfading pitch shifter.

    A delay line whose read pointer drifts relative to the write pointer shifts
    pitch — that is just the Doppler effect. The catch is that the pointer must
    eventually wrap, and the wrap is a click. Fix: two taps half a window apart,
    each windowed with a Hann. The two Hanns sum to exactly 1.0, and each tap's
    window is at zero precisely when that tap wraps. No click, no FFT, no
    latency, no allocation.

    This is the "Magneto grabs the tape and slows it" flavour of pitch shifting,
    not the pristine phase-vocoder flavour. At an octave it burbles a little.
    That burble is the sound we are here for.

    Used in two places:
      * inside the tape echo's feedback path  -> repeats climb or fall
      * inside the reverb's feedback path     -> shimmer
*/

#pragma once

#include "DelayBuffer.h"

namespace gush
{

class PitchShifter
{
public:
    void prepare (double sampleRate, float windowMs = 70.0f)
    {
        sr     = sampleRate;
        window = (float) (0.001 * (double) windowMs * sampleRate);
        line.prepare (sampleRate, (float) (0.001 * (double) windowMs * 2.5) + 0.02f);
        phase  = 0.0f;
        setRatio (1.0f);
    }

    void reset() noexcept { line.reset(); phase = 0.0f; }

    /** ratio 2.0 = one octave up, 0.5 = one octave down. */
    void setRatio (float r) noexcept
    {
        ratio = clampf (r, 0.25f, 4.0f);
        // Pointer drift per sample, expressed as a fraction of the window.
        // ratio > 1 -> negative -> the delay shortens -> pitch rises.
        inc = (1.0f - ratio) / window;
    }

    float getRatio() const noexcept { return ratio; }

    float process (float x) noexcept
    {
        line.push (x);

        phase += inc;
        while (phase >= 1.0f) phase -= 1.0f;
        while (phase <  0.0f) phase += 1.0f;

        const float p2 = (phase >= 0.5f) ? phase - 0.5f : phase + 0.5f;

        const float d1 = 2.0f + phase * window;
        const float d2 = 2.0f + p2    * window;

        return line.read (d1) * hann (phase)
             + line.read (d2) * hann (p2);
    }

private:
    DelayLine line;
    double sr     = 48000.0;
    float  window = 3360.0f, phase = 0.0f, inc = 0.0f, ratio = 1.0f;
};

} // namespace gush
