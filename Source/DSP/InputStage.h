/*
    InputStage.h
    ---------------------------------------------------------------------------
    Stage 1 of the chain: gain, then tape-style saturation.

    The saturation is not a distortion pedal. It is the thing that makes a
    guitar or a Digitakt sound like it has already been through a machine
    before the echoes get hold of it:

      pre-emphasis  -> push the highs harder into the curve
      tanh (ADAA)   -> soft, alias-suppressed compression of the peaks
      de-emphasis   -> pull the highs back down

    That pre/de-emphasis pair is what real tape does, and it is why tape
    "compresses the top end" rather than just clipping. Doing it around the
    nonlinearity, not before or after it, is the whole trick.

    Zero latency: no oversampling, because the saturator is antiderivative
    anti-aliased instead (see SaturatorADAA).
*/

#pragma once

#include "DspCommon.h"

namespace gush
{

class InputStage
{
public:
    void prepare (double sampleRate)
    {
        for (int c = 0; c < 2; ++c)
        {
            sat[c].reset();
            dc[c].prepare (sampleRate);
            preSplit[c].prepare (sampleRate);
            postSplit[c].prepare (sampleRate);
            preSplit[c].setCutoff (1600.0f);
            postSplit[c].setCutoff (4500.0f);
        }

        gain.setTime (sampleRate, 25.0f);
        gain.reset (1.0f);
        drive.setTime (sampleRate, 40.0f);
        drive.reset (0.0f);
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c) { sat[c].reset(); dc[c].reset(); preSplit[c].reset(); postSplit[c].reset(); }
    }

    void setGainDb (float db) noexcept { gain.setTarget (dbToGain (db)); }
    void setDrive  (float d)  noexcept { drive.setTarget (clampf (d, 0.0f, 1.0f)); }

    void process (float& l, float& r) noexcept
    {
        const float g = gain.next();
        const float d = drive.next();

        const float into   = 1.0f + d * 11.0f;          // how hard we hit the curve
        const float makeup = 1.0f / (1.0f + d * 1.55f); // ... and back out again
        const float boost  = 1.0f + d * 1.5f;           // pre-emphasis amount
        const float loss   = 1.0f - d * 0.45f;          // de-emphasis / HF loss

        float ch[2] = { l * g, r * g };

        for (int c = 0; c < 2; ++c)
        {
            float x = ch[c];

            const float lows = preSplit[c].process (x);
            x = lows + (x - lows) * boost;

            x = sat[c].process (x * into) * makeup;

            const float lows2 = postSplit[c].process (x);
            x = lows2 + (x - lows2) * loss;

            ch[c] = dc[c].process (x);
        }

        l = ch[0];
        r = ch[1];
    }

private:
    SaturatorADAA sat[2];
    OnePoleLP     preSplit[2], postSplit[2];
    DCBlocker     dc[2];
    Smoother      gain, drive;
};

} // namespace gush
