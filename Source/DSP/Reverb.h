/*
    Reverb.h
    ---------------------------------------------------------------------------
    Stage 3b: the StarLab half of the box.

    Feedback delay network. Eight delay lines, damped, cross-mixed every sample
    by an 8x8 Hadamard matrix. Hadamard is orthogonal, so the mixing itself is
    lossless: the decay time is set purely by the per-line gains and nothing
    else, which is what makes an FDN controllable where a pile of comb filters
    is not.

      predelay  -> 4 allpass diffusers per channel -> FDN tank -> stereo taps

    SHIMMER takes the tank output, transposes it an octave up and feeds it back
    in. Energy climbs an octave per circulation, which is where the endless
    rising cathedral comes from. The pitch shifter is the same zero-latency
    dual-tap design used by the tape echo.

    FREEZE sets every line gain to unity, bypasses the damping and mutes the
    input: infinite tail. Shimmer still works while frozen, which is one of the
    best sounds this thing makes.

    Every line input is soft-clipped. With shimmer feeding energy back into a
    tank already at 0.999 feedback, that clipper is the only thing between a
    lush pad and a blown speaker.
*/

#pragma once

#include "DelayBuffer.h"
#include "PitchShifter.h"

namespace gush
{

class Reverb
{
public:
    static constexpr int kLines = 8;

    void prepare (double sampleRate);
    void reset();

    /** Control rate only. */
    void setParams (float size, float decaySeconds, float damping, float shimmer,
                    float predelayMs, float modDepth, bool freeze);

    void processStereo (float inL, float inR, float& outL, float& outR) noexcept;

private:
    static void hadamard8 (float* v) noexcept;
    static float allpass (DelayLine& line, float len, float coef, float x) noexcept
    {
        const float z = line.read (len);
        const float v = x - coef * z;
        line.push (flush (v));
        return z + coef * v;
    }

    double sr = 48000.0;

    DelayLine preDelay[2];
    DelayLine ap[2][4];
    float     apLen[2][4] {};
    float     apCoef = 0.62f;

    DelayLine line[kLines];
    float     lineLen[kLines] {}, lineGain[kLines] {}, lpZ[kLines] {}, hpZ[kLines] {};
    float     lpCoef = 0.5f, hpCoef = 0.01f;
    float     modPhase[kLines] {}, modInc[kLines] {}, modDepthSamples = 0.0f;

    PitchShifter shim;
    OnePoleLP    shimLP;
    float        shimAmt = 0.0f;

    Smoother preDelaySmooth;
    bool     frozen = false;
};

} // namespace gush
