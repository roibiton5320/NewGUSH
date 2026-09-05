#include "Reverb.h"

namespace gush
{

namespace
{
    // Mutually prime-ish, at 48 kHz. Scaled by SIZE and by the real sample rate.
    const float kBaseLen[Reverb::kLines] =
        { 1123.0f, 1361.0f, 1657.0f, 1913.0f, 2179.0f, 2417.0f, 2711.0f, 3037.0f };

    // Slow, irrational-ish rates so the tank never settles into a pattern.
    const float kModRate[Reverb::kLines] =
        { 0.31f, 0.43f, 0.57f, 0.69f, 0.83f, 0.97f, 1.13f, 1.29f };

    const float kApLen[2][4] =
        { { 113.0f, 241.0f, 379.0f, 557.0f },
          { 149.0f, 281.0f, 419.0f, 601.0f } };
}

//==============================================================================
void Reverb::hadamard8 (float* v) noexcept
{
    for (int stride = 1; stride < 8; stride <<= 1)
        for (int i = 0; i < 8; i += stride << 1)
            for (int j = i; j < i + stride; ++j)
            {
                const float a = v[j], b = v[j + stride];
                v[j]          = a + b;
                v[j + stride] = a - b;
            }

    constexpr float norm = 0.35355339f;   // 1 / sqrt(8): keeps the mix lossless
    for (int i = 0; i < 8; ++i) v[i] *= norm;
}

//==============================================================================
void Reverb::prepare (double sampleRate)
{
    sr = sampleRate;
    const float srScale = (float) (sampleRate / 48000.0);

    for (int c = 0; c < 2; ++c)
    {
        preDelay[c].prepare (sampleRate, 0.55f);
        for (int i = 0; i < 4; ++i)
        {
            apLen[c][i] = kApLen[c][i] * srScale;
            ap[c][i].prepare (sampleRate, (apLen[c][i] + 8.0f) / (float) sampleRate);
        }
    }

    for (int i = 0; i < kLines; ++i)
    {
        // Headroom for the largest SIZE plus the modulation swing.
        line[i].prepare (sampleRate, (kBaseLen[i] * 2.2f * srScale + 64.0f) / (float) sampleRate);
        modInc[i]   = kModRate[i] / (float) sampleRate;
        modPhase[i] = (float) i * 0.125f;
        lpZ[i] = hpZ[i] = 0.0f;
    }

    shim.prepare (sampleRate, 90.0f);
    shim.setRatio (2.0f);                 // one octave up
    shimLP.prepare (sampleRate);
    shimLP.setCutoff (5200.0f);           // stop the shimmer turning into hiss

    hpCoef = 1.0f - std::exp (-kTwoPi * 70.0f / (float) sampleRate);

    preDelaySmooth.setTime (sampleRate, 120.0f);
    preDelaySmooth.reset (1.0f);

    setParams (0.6f, 6.0f, 0.4f, 0.0f, 0.0f, 0.35f, false);
    reset();
}

void Reverb::reset()
{
    for (int c = 0; c < 2; ++c)
    {
        preDelay[c].reset();
        for (int i = 0; i < 4; ++i) ap[c][i].reset();
    }

    for (int i = 0; i < kLines; ++i)
    {
        line[i].reset();
        lpZ[i] = hpZ[i] = 0.0f;
    }

    shim.reset();
    shimLP.reset();
}

//==============================================================================
void Reverb::setParams (float size, float decaySeconds, float damping, float shimmer,
                        float predelayMs, float modDepth, bool freeze)
{
    frozen = freeze;

    const float srScale   = (float) (sr / 48000.0);
    const float sizeScale = 0.30f + clampf (size, 0.0f, 1.0f) * 1.90f;
    const float decay     = std::max (0.1f, decaySeconds);

    modDepthSamples = clampf (modDepth, 0.0f, 1.0f) * 22.0f * srScale;

    for (int i = 0; i < kLines; ++i)
    {
        const float maxLen = (float) line[i].capacity() - modDepthSamples - 8.0f;
        lineLen[i] = clampf (kBaseLen[i] * sizeScale * srScale, 16.0f, maxLen);

        if (frozen)
        {
            lineGain[i] = 1.0f;
        }
        else
        {
            // -60 dB after `decay` seconds, per line, accounting for how often
            // each line recirculates.
            const float g = std::pow (10.0f, -3.0f * lineLen[i] / (decay * (float) sr));
            lineGain[i] = clampf (g, 0.0f, 0.99995f);
        }
    }

    // Damping: how fast the top end dies inside the tank.
    if (frozen)
    {
        lpCoef = 1.0f;                    // bypass, or a frozen tail decays anyway
    }
    else
    {
        const float cutoff = 18000.0f * std::pow (0.06f, clampf (damping, 0.0f, 1.0f));
        lpCoef = 1.0f - std::exp (-kTwoPi * clampf (cutoff, 200.0f, (float) sr * 0.45f) / (float) sr);
    }

    shimAmt = clampf (shimmer, 0.0f, 1.0f);
    preDelaySmooth.setTarget (clampf (predelayMs, 0.0f, 500.0f) * 0.001f * (float) sr + 1.0f);
}

//==============================================================================
void Reverb::processStereo (float inL, float inR, float& outL, float& outR) noexcept
{
    //== pre-delay + input diffusion ==========================================
    const float pd = preDelaySmooth.next();

    preDelay[0].push (inL);
    preDelay[1].push (inR);

    float dL = preDelay[0].read (pd);
    float dR = preDelay[1].read (pd);

    for (int i = 0; i < 4; ++i)
    {
        dL = allpass (ap[0][i], apLen[0][i], apCoef, dL);
        dR = allpass (ap[1][i], apLen[1][i], apCoef, dR);
    }

    //== read the tank, damped ================================================
    float s[kLines];

    for (int i = 0; i < kLines; ++i)
    {
        const float mod = modDepthSamples * std::sin (kTwoPi * modPhase[i]);
        modPhase[i] += modInc[i];
        if (modPhase[i] >= 1.0f) modPhase[i] -= 1.0f;

        float v = line[i].read (lineLen[i] + mod);

        lpZ[i] = flush (lpZ[i] + lpCoef * (v - lpZ[i]));
        v = lpZ[i];

        hpZ[i] = flush (hpZ[i] + hpCoef * (v - hpZ[i]));
        v -= hpZ[i];

        s[i] = v;
    }

    outL = (s[0] + s[2] - s[4] + s[6]) * 0.5f;
    outR = (s[1] - s[3] + s[5] + s[7]) * 0.5f;

    //== shimmer ==============================================================
    float shimOut = 0.0f;
    const float shimIn = shim.process ((outL + outR) * 0.5f);
    if (shimAmt > 0.0005f)
        shimOut = shimLP.process (shimIn) * shimAmt * 0.45f;

    //== mix and write back ===================================================
    float v[kLines];
    for (int i = 0; i < kLines; ++i) v[i] = s[i] * lineGain[i];

    hadamard8 (v);

    const float inGate = frozen ? 0.0f : 1.0f;

    for (int i = 0; i < kLines; ++i)
    {
        const float inject = (i < 4 ? dL : dR) * 0.5f * inGate;
        line[i].push (softClip (v[i] + inject + shimOut));
    }
}

} // namespace gush
