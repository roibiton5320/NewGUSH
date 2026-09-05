/*
    ModalResonator.h
    ---------------------------------------------------------------------------
    Stage 3a: the Rings half of the box.

    A bank of two-pole resonators, one per partial. Hit a bank with any signal
    and it rings like a struck object, because that is literally what a struck
    object is: a sum of exponentially decaying sinusoids.

      FREQUENCY   the fundamental
      STRUCTURE   harmonic (a string) -> inharmonic (a bar, then a bell).
                  Implemented as stiffness: f_k = f0 * k * sqrt(1 + B k^2),
                  which is the real dispersion law for a stiff string, so the
                  whole sweep stays physical instead of just detuning.
      BRIGHTNESS  amplitude tilt across the partials
      DAMPING     decay time, shorter for high partials as in any real body
      POSITION    where the object is struck: sin(pi k position) notches out
                  the partials that have a node at that point

    Two banks run in parallel with slightly different position and tuning, so a
    mono input comes out wide without any fake stereo trickery.
*/

#pragma once

#include "DspCommon.h"

namespace gush
{

class ModalResonator
{
public:
    static constexpr int kMaxModes = 28;

    void prepare (double sampleRate);
    void reset();

    /** Control rate only. Rebuilds the bank, and only when something moved. */
    void setParams (float freqHz, float structure, float brightness,
                    float damping, float position);

    void processStereo (float inL, float inR, float& outL, float& outR) noexcept;

private:
    struct Mode { float a1 = 0.0f, a2 = 0.0f, g = 0.0f, y1 = 0.0f, y2 = 0.0f; };

    struct Bank
    {
        Mode  modes[kMaxModes];
        int   active = 0;
        float norm   = 1.0f;
    };

    void  build (Bank&, float f0, float structure, float brightness,
                 float damping, float position);
    float run (Bank&, float x) noexcept;

    Bank      bank[2];
    OnePoleHP inHp[2];
    DCBlocker dc[2];
    double    sr = 48000.0;

    float lastF0 = -1.0f, lastStructure = -1.0f, lastBrightness = -1.0f,
          lastDamping = -1.0f, lastPosition = -1.0f;
};

} // namespace gush
