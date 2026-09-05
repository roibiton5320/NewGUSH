/*
    GranularTapeEcho.h
    ---------------------------------------------------------------------------
    Stage 2: the Magneto / Microcell half of the box.

    One piece of tape, read three different ways, blended:

      HEADS   Up to four playback heads at fractions of the delay time, each
              with its own level and stereo position. Straight multi-head tape
              echo (Magneto).

      GRAINS  A cloud of short windowed reads scattered around the same point
              on the tape, each with its own pitch, pan and start offset. This
              is the Clouds/Microcell side. Grain positions are ABSOLUTE tape
              positions, so a grain keeps reading at its own speed while the
              write head runs on ahead of it.

      REVERSE A pair of half-overlapped backwards readers (see ReverseReader).

    Pitch is handled in two independent ways, because tape and granular do it
    differently and we want both:

      1. TAPE SPEED. The delay length is smoothed. Any change in delay length
         over time IS a pitch change: reading at writePos - d(t) plays back at
         rate 1 - d'(t). So dragging the delay knob, or pulling the SPEED
         control down, glides the pitch exactly the way slowing a tape does,
         then settles. Nothing extra needed — the smoother is the varispeed.

      2. FIXED INTERVAL. +12 / -12 cannot come from a glide (a glide has to
         stop). Those come from the grain playback rate, and from a dual-tap
         pitch shifter in the FEEDBACK path, so each repeat is an octave above
         or below the one before. That is the cascading-shimmer trick.

    FREEZE mutes the input into the tape and forces feedback to unity with the
    damping bypassed, so the last `delay time` seconds loop forever while you
    keep playing over the top. Grains keep grabbing from the frozen loop.
*/

#pragma once

#include "DelayBuffer.h"
#include "PitchShifter.h"

namespace gush
{

class GranularTapeEcho
{
public:
    static constexpr int kMaxGrains = 48;
    static constexpr int kHeads     = 4;
    static constexpr float kMaxDelaySeconds = 3.0f;

    void prepare (double sampleRate);
    void reset();

    //== control-rate setters ==================================================
    void setDelaySeconds     (float seconds) noexcept;
    void setSpeed            (float speed) noexcept;        // 0.25 .. 2.0, 1 = normal
    void setGlideSeconds     (float seconds) noexcept;      // how long a speed/time change takes
    void setHeadPattern      (int pattern) noexcept;        // 0 single, 1 dual, 2 triplet, 3 quad
    void setFeedback         (float f) noexcept;            // 0 .. 1.05
    void setTapeTone         (float t) noexcept;            // 0 dark .. 1 bright (feedback damping)
    void setPitchSemitones   (float st) noexcept;
    void setPitchFeedback    (float amount) noexcept;       // 0 .. 1
    void setReverse          (bool on) noexcept             { reverse = on; }
    void setFreeze           (bool on) noexcept             { freeze = on; }
    void setGrainMix         (float m) noexcept             { grainMix = clampf (m, 0.0f, 1.0f); }
    void setGrainSizeSeconds (float s) noexcept;
    void setGrainDensityHz   (float d) noexcept;
    void setGrainSpray       (float s) noexcept             { spray = clampf (s, 0.0f, 1.0f); }
    void setGrainSpread      (float s) noexcept             { spread = clampf (s, 0.0f, 1.0f); }
    void setGrainPitchJitter (float j) noexcept             { pitchJitter = clampf (j, 0.0f, 1.0f); }
    void setSeed             (uint32_t s) noexcept          { rng.setSeed (s); }

    /** In: the (already saturated) dry signal. Out: the wet echo/grain bus. */
    void process (float& l, float& r) noexcept;

    /** How many grains are currently sounding — handy for a meter or a UI. */
    int activeGrainCount() const noexcept { return activeGrains; }

private:
    struct Grain
    {
        bool   active = false;
        double pos    = 0.0;    // absolute tape position
        double rate   = 1.0;    // samples advanced per sample; negative = reversed
        double age    = 0.0;
        double length = 1000.0;
        float  panL   = 0.707f, panR = 0.707f;
    };

    void  spawnGrain (double writePos, float delaySamples) noexcept;
    void  updateDelayTarget() noexcept;
    void  updateGrainNormalisation() noexcept;

    double sr = 48000.0;

    TapeBuffer    tape;
    ReverseReader revL, revR;
    PitchShifter  fbShift[2];
    OnePoleLP     fbLP[2];
    OnePoleHP     fbHP[2];
    Smoother      delaySmooth, feedbackSmooth, grainMixSmooth;
    Rng           rng { 0x5EEDCAFEu };

    Grain grains[kMaxGrains];
    int   activeGrains = 0;
    float grainClock   = 0.0f;

    // head layout
    float headRatio[kHeads] { 1.0f, 0.0f, 0.0f, 0.0f };
    float headLevel[kHeads] { 1.0f, 0.0f, 0.0f, 0.0f };
    float headPanL[kHeads]  { 0.707f, 0.707f, 0.707f, 0.707f };
    float headPanR[kHeads]  { 0.707f, 0.707f, 0.707f, 0.707f };
    float headNorm = 1.0f;

    // state
    float baseDelaySamples = 12000.0f;
    float speed            = 1.0f;
    float glideSeconds     = 0.35f;
    float feedback         = 0.35f;
    float pitchRatio       = 1.0f;
    float pitchFb          = 0.0f;
    float grainMix         = 0.0f;
    float grainSizeSamples = 4800.0f;
    float densityHz        = 12.0f;
    float grainNorm        = 1.0f;
    float spray            = 0.0f;
    float spread           = 0.0f;
    float pitchJitter      = 0.0f;
    float maxSpraySamples  = 48000.0f;
    bool  reverse          = false;
    bool  freeze           = false;

    // Locked at the instant FREEZE engages: an integer loop length reads back
    // bit-exactly, so a held loop neither decays nor dulls.
    int   frozenDelay = 24000;
    bool  wasFrozen   = false;
};

} // namespace gush
