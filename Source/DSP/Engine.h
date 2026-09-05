/*
    Engine.h
    ---------------------------------------------------------------------------
    The whole signal chain, owned in one place, with no JUCE anywhere in sight.

        in -> INPUT (gain + tape saturation)
                |
                +--> dry -----------------------------------------+
                |                                                 |
                +--> GRANULAR TAPE ECHO --> RESONATOR --> REVERB --+--> TONE
                                                                        |
                                                             MIX -------+--> out

    Each of the three wet stages has its own blend against what came into it,
    so the chain can be a clean tape echo, a bowed resonator, an endless
    cathedral, or all three at once. MIX at the end sets how much of any of it
    you hear against the dry.

    Everything is driven from one plain struct (EngineParams), pushed in once
    per audio block. Modulation and coefficient updates run at CONTROL RATE --
    once every 32 samples -- which is inaudibly fine for knobs and 32x cheaper
    than doing it per sample.
*/

#pragma once

#include "GranularTapeEcho.h"
#include "InputStage.h"
#include "ModalResonator.h"
#include "Modulation.h"
#include "Reverb.h"

namespace gush
{

struct EngineParams
{
    //== input / output ========================================================
    float inputGainDb  = 0.0f;
    float drive        = 0.15f;
    float mix          = 0.5f;      // global dry/wet
    float outputGainDb = 0.0f;

    //== granular tape echo ====================================================
    float delaySeconds  = 0.42f;
    float speed         = 1.0f;     // tape speed; the ramp target
    float glideSeconds  = 0.35f;    // how long the tape takes to get there
    int   headPattern   = 2;        // 0 single, 1 dual, 2 triplet, 3 quad
    float feedback      = 0.45f;
    float tapeTone      = 0.55f;
    float pitchSemis    = 0.0f;
    float pitchFeedback = 0.0f;
    bool  reverse       = false;
    bool  freeze        = false;
    float echoMix       = 0.7f;

    float grainMix     = 0.35f;
    float grainSizeMs  = 90.0f;
    float grainDensity = 14.0f;     // grains per second
    float grainSpray   = 0.30f;
    float grainSpread  = 0.60f;
    float grainJitter  = 0.15f;

    //== resonator =============================================================
    float resMix        = 0.0f;
    float resFreq       = 220.0f;
    float resStructure  = 0.25f;
    float resBrightness = 0.50f;
    float resDamping    = 0.40f;
    float resPosition   = 0.28f;

    //== reverb ================================================================
    float revMix        = 0.40f;
    float revSize       = 0.65f;
    float revDecay      = 8.0f;     // seconds
    float revDamping    = 0.40f;
    float revShimmer    = 0.0f;
    float revPredelayMs = 20.0f;
    float revMod        = 0.35f;
    bool  revFreeze     = false;

    //== post tone =============================================================
    float cutoff    = 18000.0f;
    float resonance = 0.2f;

    //== modulation ============================================================
    float lfo1Rate = 0.25f;  int lfo1Shape = 0;
    float lfo2Rate = 0.11f;  int lfo2Shape = 1;
    float randRate = 0.60f;  float randSlew = 0.60f;

    int   modSource[ModMatrix::kSlots] { 0, 0, 0, 0 };
    int   modDest  [ModMatrix::kSlots] { 0, 0, 0, 0 };
    float modAmount[ModMatrix::kSlots] { 0.0f, 0.0f, 0.0f, 0.0f };

    uint32_t seed = 0x5EEDCAFEu;
};

//==============================================================================
class Engine
{
public:
    static constexpr int kControlBlock = 32;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** Push once per audio block, before process(). Cheap: it only copies. */
    void setParameters (const EngineParams& p) noexcept { params = p; }

    /** In-place stereo processing. No allocation, no locks, no exceptions. */
    void process (float* left, float* right, int numSamples) noexcept;

    int    activeGrainCount() const noexcept { return echo.activeGrainCount(); }
    double getSampleRate()    const noexcept { return sr; }

private:
    void applyControlBlock (float inputPeak) noexcept;

    double sr = 48000.0;

    InputStage       input;
    GranularTapeEcho echo;
    ModalResonator   resonator;
    Reverb           reverb;
    ModMatrix        mod;
    StateVariableFilter tone[2];

    Smoother echoMixSm, resMixSm, revMixSm, mixSm, outSm;

    EngineParams params;
    uint32_t     lastSeed = 0;
};

} // namespace gush
