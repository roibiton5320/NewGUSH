#include "ModalResonator.h"

namespace gush
{

void ModalResonator::prepare (double sampleRate)
{
    sr = sampleRate;

    for (int c = 0; c < 2; ++c)
    {
        inHp[c].prepare (sampleRate);
        inHp[c].setCutoff (28.0f);
        dc[c].prepare (sampleRate);
    }

    reset();
    lastF0 = -1.0f;                       // force the next setParams to build
    setParams (220.0f, 0.2f, 0.5f, 0.4f, 0.28f);
}

void ModalResonator::reset()
{
    for (auto& b : bank)
        for (auto& m : b.modes) { m.y1 = 0.0f; m.y2 = 0.0f; }

    for (int c = 0; c < 2; ++c) { inHp[c].reset(); dc[c].reset(); }
}

//==============================================================================
void ModalResonator::build (Bank& b, float f0, float structure, float brightness,
                            float damping, float position)
{
    // Stiffness. 0 = a perfectly flexible string (pure harmonics),
    // rising towards the stretched, clangorous ratios of a bar or a bell.
    const float B = structure * structure * 0.0016f;

    // Amplitude tilt: dark bodies lose their upper partials fast.
    const float tilt = 3.2f - 2.9f * clampf (brightness, 0.0f, 1.0f);

    // Fundamental decay, and how much faster each partial above it dies.
    const float d      = clampf (damping, 0.0f, 1.0f);
    const float t60Low = 0.05f + 14.0f * (1.0f - d) * (1.0f - d);
    const float perMode = 0.55f + 0.9f * d;

    const float pos = clampf (position, 0.02f, 0.98f);
    const float nyq = (float) (sr * 0.45);

    int active = 0;

    for (int k = 1; k <= kMaxModes; ++k)
    {
        const float kf    = (float) k;
        const float ratio = kf * std::sqrt (1.0f + B * kf * kf);
        const float f     = f0 * ratio;
        if (f >= nyq) break;

        float t60 = t60Low / std::pow (kf, perMode);
        t60 = std::max (0.008f, t60);

        // -60 dB in t60 seconds -> pole radius.
        float rr = std::exp (-6.90776f / (t60 * (float) sr));
        rr = std::min (rr, 0.99995f);

        const float theta = kTwoPi * f / (float) sr;

        // Struck-position comb, then the brightness tilt.
        float amp = std::fabs (std::sin (kPi * kf * pos));
        amp *= std::pow (kf, -tilt);

        Mode& m = b.modes[(size_t) active];
        m.a1 = 2.0f * rr * std::cos (theta);
        m.a2 = -rr * rr;
        // Normalised so each mode peaks at roughly `amp` regardless of Q.
        m.g  = (1.0f - rr * rr) * std::sin (theta) * amp;

        ++active;
    }

    // Silence the tail of the pool so a shrinking bank cannot leave stale
    // modes ringing.
    for (int i = active; i < kMaxModes; ++i)
    {
        b.modes[(size_t) i].g  = 0.0f;
        b.modes[(size_t) i].a1 = 0.0f;
        b.modes[(size_t) i].a2 = 0.0f;
    }

    b.active = active;
    b.norm   = 2.0f / std::sqrt ((float) std::max (1, active));
}

void ModalResonator::setParams (float freqHz, float structure, float brightness,
                                float damping, float position)
{
    freqHz = clampf (freqHz, 20.0f, 4000.0f);

    // Rebuilding 28 modes means 28 transcendentals per bank. Only pay for it
    // when a parameter has actually moved enough to hear.
    const float eps = 1.0e-4f;
    if (std::fabs (freqHz     - lastF0)         < freqHz * 0.0005f
     && std::fabs (structure  - lastStructure)  < eps
     && std::fabs (brightness - lastBrightness) < eps
     && std::fabs (damping    - lastDamping)    < eps
     && std::fabs (position   - lastPosition)   < eps)
        return;

    lastF0 = freqHz; lastStructure = structure; lastBrightness = brightness;
    lastDamping = damping; lastPosition = position;

    build (bank[0], freqHz,          structure, brightness, damping, position);
    // Second bank: a few cents sharp and struck a little differently.
    build (bank[1], freqHz * 1.0037f, structure, brightness, damping,
           clampf (position * 0.86f + 0.07f, 0.02f, 0.98f));
}

//==============================================================================
float ModalResonator::run (Bank& b, float x) noexcept
{
    float sum = 0.0f;

    for (int i = 0; i < b.active; ++i)
    {
        Mode& m = b.modes[(size_t) i];
        const float y = m.g * x + m.a1 * m.y1 + m.a2 * m.y2;
        m.y2 = m.y1;
        m.y1 = flush (y);
        sum += y;
    }

    return sum * b.norm;
}

void ModalResonator::processStereo (float inL, float inR, float& outL, float& outR) noexcept
{
    const float xL = inHp[0].process (inL);
    const float xR = inHp[1].process (inR);

    outL = dc[0].process (run (bank[0], xL));
    outR = dc[1].process (run (bank[1], xR));
}

} // namespace gush
