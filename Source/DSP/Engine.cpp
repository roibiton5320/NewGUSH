#include "Engine.h"

namespace gush
{

void Engine::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sr = sampleRate;

    input.prepare (sampleRate);
    echo.prepare (sampleRate);
    resonator.prepare (sampleRate);
    reverb.prepare (sampleRate);
    mod.prepare (sampleRate / (double) kControlBlock);

    for (auto& t : tone) { t.prepare (sampleRate); t.set (18000.0f, 0.707f); }

    for (auto* s : { &echoMixSm, &resMixSm, &revMixSm, &mixSm, &outSm })
        s->setTime (sampleRate, 30.0f);

    echoMixSm.reset (params.echoMix);
    resMixSm .reset (params.resMix);
    revMixSm .reset (params.revMix);
    mixSm    .reset (params.mix);
    outSm    .reset (dbToGain (params.outputGainDb));

    lastSeed = 0;
}

void Engine::reset()
{
    input.reset();
    echo.reset();
    resonator.reset();
    reverb.reset();
    mod.reset();
    for (auto& t : tone) t.reset();
}

//==============================================================================
void Engine::applyControlBlock (float inputPeak) noexcept
{
    if (params.seed != lastSeed)
    {
        lastSeed = params.seed;
        echo.setSeed (params.seed);
        mod.setSeed (params.seed);
    }

    //== modulation sources ====================================================
    mod.setLfo (0, params.lfo1Rate, params.lfo1Shape);
    mod.setLfo (1, params.lfo2Rate, params.lfo2Shape);
    mod.setRandom (params.randRate, params.randSlew);

    for (int i = 0; i < ModMatrix::kSlots; ++i)
        mod.setSlot (i, params.modSource[i], params.modDest[i], params.modAmount[i]);

    mod.update (inputPeak);

    // Depths chosen so a slot at full amount is dramatic but never absurd.
    const float mDelay   = std::exp2 (mod.value (ModDest::DelayTime)    * 1.0f);
    const float mSize    = std::exp2 (mod.value (ModDest::GrainSize)    * 2.0f);
    const float mDensity = std::exp2 (mod.value (ModDest::GrainDensity) * 2.0f);
    const float mResF    = std::exp2 (mod.value (ModDest::ResFrequency) * 2.0f);
    const float mCutoff  = std::exp2 (mod.value (ModDest::Cutoff)       * 3.0f);

    //== input =================================================================
    input.setGainDb (params.inputGainDb);
    input.setDrive  (params.drive);

    //== granular tape echo ====================================================
    echo.setDelaySeconds  (clampf (params.delaySeconds * mDelay, 0.002f, 3.0f));
    echo.setSpeed         (params.speed);
    echo.setGlideSeconds  (params.glideSeconds);
    echo.setHeadPattern   (params.headPattern);
    echo.setFeedback      (clampf (params.feedback + mod.value (ModDest::Feedback) * 0.35f, 0.0f, 1.05f));
    echo.setTapeTone      (params.tapeTone);
    echo.setPitchSemitones(clampf (params.pitchSemis + mod.value (ModDest::Pitch) * 12.0f, -24.0f, 24.0f));
    echo.setPitchFeedback (params.pitchFeedback);
    echo.setReverse       (params.reverse);
    echo.setFreeze        (params.freeze);

    echo.setGrainMix         (params.grainMix);
    echo.setGrainSizeSeconds (clampf (params.grainSizeMs * 0.001f * mSize, 0.004f, 0.6f));
    echo.setGrainDensityHz   (clampf (params.grainDensity * mDensity, 0.2f, 120.0f));
    echo.setGrainSpray       (clampf (params.grainSpray + mod.value (ModDest::Spray) * 0.5f, 0.0f, 1.0f));
    echo.setGrainSpread      (params.grainSpread);
    echo.setGrainPitchJitter (params.grainJitter);

    //== resonator =============================================================
    resonator.setParams (clampf (params.resFreq * mResF, 20.0f, 4000.0f),
                         params.resStructure,
                         params.resBrightness,
                         clampf (params.resDamping + mod.value (ModDest::ResDamping) * 0.5f, 0.0f, 1.0f),
                         params.resPosition);

    //== reverb ================================================================
    reverb.setParams (clampf (params.revSize + mod.value (ModDest::RevSize) * 0.4f, 0.0f, 1.0f),
                      params.revDecay,
                      params.revDamping,
                      clampf (params.revShimmer + mod.value (ModDest::RevShimmer) * 0.5f, 0.0f, 1.0f),
                      params.revPredelayMs,
                      params.revMod,
                      params.revFreeze);

    //== post tone =============================================================
    const float fc = clampf (params.cutoff * mCutoff, 30.0f, (float) (sr * 0.45));
    const float q  = 0.5f + clampf (params.resonance, 0.0f, 1.0f) * 5.0f;
    tone[0].set (fc, q);
    tone[1].set (fc, q);

    //== blends ================================================================
    echoMixSm.setTarget (clampf (params.echoMix, 0.0f, 1.0f));
    resMixSm .setTarget (clampf (params.resMix,  0.0f, 1.0f));
    revMixSm .setTarget (clampf (params.revMix,  0.0f, 1.0f));
    mixSm    .setTarget (clampf (params.mix + mod.value (ModDest::Mix) * 0.5f, 0.0f, 1.0f));
    outSm    .setTarget (dbToGain (clampf (params.outputGainDb, -60.0f, 12.0f)));
}

//==============================================================================
void Engine::process (float* left, float* right, int numSamples) noexcept
{
    int i = 0;

    while (i < numSamples)
    {
        const int chunk = std::min (kControlBlock, numSamples - i);

        // The envelope follower wants the peak of what is about to happen.
        float peak = 0.0f;
        for (int k = 0; k < chunk; ++k)
            peak = std::max (peak, std::max (std::fabs (left[i + k]), std::fabs (right[i + k])));

        applyControlBlock (peak);

        for (int k = 0; k < chunk; ++k)
        {
            const int n = i + k;

            float l = left[n], r = right[n];

            //-- stage 1: gain + tape saturation ---------------------------
            input.process (l, r);
            const float dryL = l, dryR = r;

            //-- stage 2: granular tape echo -------------------------------
            float el = l, er = r;
            echo.process (el, er);
            const float em = echoMixSm.next();
            float xl = lerp (dryL, el, em);
            float xr = lerp (dryR, er, em);

            //-- stage 3a: modal resonator ---------------------------------
            float rl = 0.0f, rr = 0.0f;
            resonator.processStereo (xl, xr, rl, rr);
            const float rm = resMixSm.next();
            xl = lerp (xl, rl, rm);
            xr = lerp (xr, rr, rm);

            //-- stage 3b: reverb ------------------------------------------
            float vl = 0.0f, vr = 0.0f;
            reverb.processStereo (xl, xr, vl, vr);
            const float vm = revMixSm.next();
            xl = lerp (xl, vl, vm);
            xr = lerp (xr, vr, vm);

            //-- post tone --------------------------------------------------
            float lp, bp, hp;
            tone[0].process (xl, lp, bp, hp); xl = lp;
            tone[1].process (xr, lp, bp, hp); xr = lp;

            //-- global mix + output ----------------------------------------
            const float m = mixSm.next();
            const float g = outSm.next();

            left[n]  = flush (lerp (dryL, xl, m) * g);
            right[n] = flush (lerp (dryR, xr, m) * g);
        }

        i += chunk;
    }
}

} // namespace gush
