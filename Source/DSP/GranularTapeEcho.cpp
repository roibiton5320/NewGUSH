#include "GranularTapeEcho.h"

namespace gush
{

//==============================================================================
namespace
{
    // Magneto-style head layouts. Ratios are fractions of the delay time; the
    // last head always sits on the beat, the earlier ones subdivide it.
    struct HeadPattern { float ratio[4]; float level[4]; };

    const HeadPattern kPatterns[4] =
    {
        { { 1.00f, 0.00f,      0.00f, 0.00f }, { 1.00f, 0.00f, 0.00f, 0.00f } }, // single
        { { 0.50f, 1.00f,      0.00f, 0.00f }, { 0.70f, 1.00f, 0.00f, 0.00f } }, // dual
        { { 0.3333f, 0.6667f,  1.00f, 0.00f }, { 0.55f, 0.75f, 1.00f, 0.00f } }, // triplet
        { { 0.25f, 0.50f,      0.75f, 1.00f }, { 0.45f, 0.60f, 0.80f, 1.00f } }  // quad
    };

    const float kHeadPan[4] = { -0.60f, 0.55f, -0.35f, 0.75f };
}

//==============================================================================
void GranularTapeEcho::prepare (double sampleRate)
{
    sr = sampleRate;

    // 10 s of tape: enough for a 3 s delay, a 3 s reverse window (which reads
    // twice as far back as it is long) and 2 s of grain spray behind that.
    tape.prepare (sampleRate, 10.0f);
    revL.prepare (sampleRate);
    revR.prepare (sampleRate);

    for (int c = 0; c < 2; ++c)
    {
        fbShift[c].prepare (sampleRate, 70.0f);
        fbLP[c].prepare (sampleRate);
        fbHP[c].prepare (sampleRate);
        fbHP[c].setCutoff (45.0f);
    }

    delaySmooth.setTime (sampleRate, glideSeconds * 1000.0f);
    delaySmooth.reset (baseDelaySamples);
    feedbackSmooth.setTime (sampleRate, 30.0f);
    feedbackSmooth.reset (feedback);
    grainMixSmooth.setTime (sampleRate, 40.0f);
    grainMixSmooth.reset (grainMix);

    maxSpraySamples = std::min ((float) (sampleRate * 2.0), (float) tape.length() * 0.30f);

    setHeadPattern (0);
    setTapeTone (0.6f);
    updateGrainNormalisation();
    updateDelayTarget();
}

void GranularTapeEcho::reset()
{
    tape.reset();
    revL.reset();
    revR.reset();

    for (int c = 0; c < 2; ++c)
    {
        fbShift[c].reset();
        fbLP[c].reset();
        fbHP[c].reset();
    }

    for (auto& g : grains) g.active = false;

    activeGrains = 0;
    grainClock   = 0.0f;
    wasFrozen    = false;
    delaySmooth.reset (delaySmooth.getTarget());
    feedbackSmooth.reset (feedback);
    grainMixSmooth.reset (grainMix);
}

//==============================================================================
void GranularTapeEcho::updateDelayTarget() noexcept
{
    const float maxSamples = kMaxDelaySeconds * (float) sr;
    const float t = clampf (baseDelaySamples / std::max (0.05f, speed), 64.0f, maxSamples);
    delaySmooth.setTarget (t);
}

void GranularTapeEcho::updateGrainNormalisation() noexcept
{
    // Expected number of grains sounding at once. Hann windows average 0.5,
    // and uncorrelated grains add in power, hence the sqrt.
    const float overlap = densityHz * (grainSizeSamples / (float) sr);
    grainNorm = 1.6f / std::sqrt (std::max (1.0f, overlap));
}

void GranularTapeEcho::setDelaySeconds (float seconds) noexcept
{
    baseDelaySamples = clampf (seconds, 0.002f, kMaxDelaySeconds) * (float) sr;
    updateDelayTarget();
}

void GranularTapeEcho::setSpeed (float s) noexcept
{
    speed = clampf (s, 0.25f, 2.0f);
    updateDelayTarget();
}

void GranularTapeEcho::setGlideSeconds (float seconds) noexcept
{
    glideSeconds = clampf (seconds, 0.01f, 20.0f);
    delaySmooth.setTime (sr, glideSeconds * 1000.0f);
}

void GranularTapeEcho::setHeadPattern (int pattern) noexcept
{
    const HeadPattern& p = kPatterns[(size_t) clampf ((float) pattern, 0.0f, 3.0f)];

    int active = 0;
    for (int h = 0; h < kHeads; ++h)
    {
        headRatio[h] = p.ratio[h];
        headLevel[h] = p.level[h];
        equalPowerPan (kHeadPan[h], headPanL[h], headPanR[h]);
        if (headLevel[h] > 0.0f) ++active;
    }

    headNorm = 1.0f / std::sqrt ((float) std::max (1, active));
}

void GranularTapeEcho::setFeedback (float f) noexcept
{
    feedback = clampf (f, 0.0f, 1.05f);
}

void GranularTapeEcho::setTapeTone (float t) noexcept
{
    // 0 = dark, murky repeats that dissolve; 1 = the tape is nearly new.
    t = clampf (t, 0.0f, 1.0f);
    const float hz = 700.0f * std::pow (24.0f, t);   // 700 Hz .. ~16.8 kHz
    fbLP[0].setCutoff (hz);
    fbLP[1].setCutoff (hz);
}

void GranularTapeEcho::setPitchSemitones (float st) noexcept
{
    pitchRatio = semitonesToRatio (clampf (st, -24.0f, 24.0f));
    fbShift[0].setRatio (pitchRatio);
    fbShift[1].setRatio (pitchRatio);
}

void GranularTapeEcho::setPitchFeedback (float amount) noexcept
{
    pitchFb = clampf (amount, 0.0f, 1.0f);
}

void GranularTapeEcho::setGrainSizeSeconds (float s) noexcept
{
    grainSizeSamples = clampf (s, 0.004f, 0.6f) * (float) sr;
    updateGrainNormalisation();
}

void GranularTapeEcho::setGrainDensityHz (float d) noexcept
{
    densityHz = clampf (d, 0.2f, 120.0f);
    updateGrainNormalisation();
}

//==============================================================================
void GranularTapeEcho::spawnGrain (double writePos, float delaySamples) noexcept
{
    int idx = -1;
    for (int i = 0; i < kMaxGrains; ++i)
        if (! grains[i].active) { idx = i; break; }

    // Pool exhausted. Drop the grain rather than steal one mid-window: a
    // stolen grain is a click, and clicks are the one thing granular
    // synthesis is never forgiven for.
    if (idx < 0) return;

    Grain& g = grains[idx];

    const float sizeJit = 1.0f + spray * rng.nextBipolar() * 0.35f;
    g.length = (double) std::max (48.0f, grainSizeSamples * sizeJit);

    float rate = pitchRatio * (1.0f + pitchJitter * rng.nextBipolar() * 0.04f);
    if (reverse) rate = -rate;
    g.rate = (double) rate;

    // Where on the tape this grain starts: the delay point, scattered back.
    const double sprayOffset = (double) (spray * rng.next01() * maxSpraySamples);
    double start = writePos - (double) delaySamples - sprayOffset;

    // Keep the whole window inside tape we have actually written. The write
    // head advances during the grain's life too, so this is conservative.
    const double travel = g.rate * g.length;
    const double newest = writePos - 8.0;
    const double oldest = writePos - (double) tape.length() + 16.0;

    if (start + travel > newest) start = newest - travel;
    if (start + travel < oldest) start = oldest - travel;
    start = std::min (std::max (start, oldest), newest);

    g.pos    = start;
    g.age    = 0.0;
    g.active = true;

    equalPowerPan (spread * rng.nextBipolar(), g.panL, g.panR);
}

//==============================================================================
void GranularTapeEcho::process (float& l, float& r) noexcept
{
    const float inL = l, inR = r;

    // The smoother IS the varispeed: any movement of this value bends pitch.
    const float delaySamples = delaySmooth.next();
    const double wp = tape.writePosition();

    //== heads, or reverse =====================================================
    float echoL = 0.0f, echoR = 0.0f;

    if (reverse)
    {
        revL.setLength (delaySamples);
        revR.setLength (delaySamples);
        echoL = revL.process (tape, 0, wp);
        echoR = revR.process (tape, 1, wp);
    }
    else
    {
        for (int h = 0; h < kHeads; ++h)
        {
            const float lev = headLevel[h];
            if (lev <= 0.0f) continue;

            const double p = wp - (double) (delaySamples * headRatio[h]);
            echoL += tape.read (0, p) * lev * headPanL[h];
            echoR += tape.read (1, p) * lev * headPanR[h];
        }

        echoL *= headNorm;
        echoR *= headNorm;
    }

    //== grain cloud ===========================================================
    grainMixSmooth.setTarget (grainMix);
    const float gm = grainMixSmooth.next();

    float grL = 0.0f, grR = 0.0f;

    if (gm > 0.0005f)
    {
        grainClock -= 1.0f;
        while (grainClock <= 0.0f)
        {
            spawnGrain (wp, delaySamples);
            const float jit = 1.0f + spray * rng.nextBipolar() * 0.6f;
            grainClock += std::max (16.0f, (float) sr / std::max (0.2f, densityHz) * jit);
        }
    }

    int live = 0;
    for (auto& g : grains)
    {
        if (! g.active) continue;
        ++live;

        const float env = hann ((float) (g.age / g.length));
        grL += tape.read (0, g.pos) * env * g.panL;
        grR += tape.read (1, g.pos) * env * g.panR;

        g.pos += g.rate;
        g.age += 1.0;
        if (g.age >= g.length) g.active = false;
    }

    activeGrains = live;
    grL *= grainNorm;
    grR *= grainNorm;

    float wetL = lerp (echoL, grL, gm);
    float wetR = lerp (echoR, grR, gm);

    //== what goes back onto the tape ==========================================
    if (freeze && ! wasFrozen)
    {
        // Round to whole samples. Reading at an integer position returns the
        // stored sample untouched, so the loop can circulate forever without
        // the interpolator quietly filtering it away pass after pass.
        frozenDelay = (int) std::max (64.0f, std::round (delaySamples));
    }
    wasFrozen = freeze;

    float fbL, fbR;

    if (freeze)
    {
        // HOLD. Copy the tape onto itself at exactly unity: no gain, no
        // damping, no clipper. Anything less than exact and an eight-second
        // hold audibly sags; anything more and it climbs.
        const double tap = wp - (double) frozenDelay;
        fbL = tape.read (0, tap);
        fbR = tape.read (1, tap);
        feedbackSmooth.next();          // keep tracking, for a clean release
    }
    else
    {
        feedbackSmooth.setTarget (feedback);
        const float fbAmt = feedbackSmooth.next();

        fbL = fbHP[0].process (fbLP[0].process (wetL * fbAmt));
        fbR = fbHP[1].process (fbLP[1].process (wetR * fbAmt));
    }

    // Pitch inside the loop: every repeat lands an interval away from the one
    // before it. Blend rather than switch, so it can be dialled in. The
    // shifter runs unconditionally to keep its line warm, so switching it in
    // does not click.
    const float ps0 = fbShift[0].process (fbL);
    const float ps1 = fbShift[1].process (fbR);
    if (pitchFb > 0.0005f)
    {
        fbL = lerp (fbL, ps0, pitchFb);
        fbR = lerp (fbR, ps1, pitchFb);
    }

    if (freeze)
    {
        // A soft clipper here would compress the held loop away over a minute,
        // so this is a plain safety rail instead. The loop is unity gain; it
        // cannot reach these values in normal use.
        fbL = clampf (fbL, -4.0f, 4.0f);
        fbR = clampf (fbR, -4.0f, 4.0f);
        tape.write (fbL, fbR);
    }
    else
    {
        tape.write (inL + softClip (fbL), inR + softClip (fbR));
    }

    l = wetL;
    r = wetR;
}

} // namespace gush
