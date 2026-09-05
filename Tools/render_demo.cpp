/*
    render_demo.cpp
    ---------------------------------------------------------------------------
    Renders demo WAVs through the real engine, offline, with no DAW.

    There is no guitar in a build container, so the source material is a
    plucked-string model (Karplus-Strong) playing an arpeggio: real transients,
    real sustain, real overlap. That is what the granular and tape stages
    actually have to survive, and a sine wave would prove nothing.

    Each preset is rendered from the same performance so they can be compared,
    then normalised to -1.5 dBFS so listening does not mean riding the volume.

        g++ -O2 -std=c++17 render_demo.cpp ../Source/DSP/Engine.cpp \
            ../Source/DSP/GranularTapeEcho.cpp ../Source/DSP/ModalResonator.cpp \
            ../Source/DSP/Reverb.cpp -o render && ./render <output-directory>
*/

#include "../Source/DSP/Engine.h"

#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace gush;

namespace
{
constexpr double kSampleRate = 44100.0;

//==============================================================================
/** A plucked string. Noise into a short delay line with a one-pole loss in the
    loop: the cheapest model that still behaves like a real string, transient
    and all. */
class PluckedString
{
public:
    void prepare (double sampleRate) { sr = sampleRate; buffer.assign (4096, 0.0f); }

    void pluck (float freqHz, float amplitude, Rng& rng)
    {
        length = std::min ((int) buffer.size(), std::max (8, (int) (sr / freqHz)));

        // A filtered noise burst: less fizz than white, more like a fingertip.
        float z = 0.0f;
        for (int i = 0; i < length; ++i)
        {
            z += 0.45f * (rng.nextBipolar() - z);
            buffer[(size_t) i] = z;
        }

        index  = 0;
        amp    = amplitude;
        active = true;

        // Higher strings lose energy faster, as they do on an instrument.
        damping = 0.9975f - 0.0009f * (freqHz / 200.0f);
    }

    float next()
    {
        if (! active) return 0.0f;

        const float current = buffer[(size_t) index];
        const int   nextIdx = (index + 1) % length;
        buffer[(size_t) index] = (current + buffer[(size_t) nextIdx]) * 0.5f * damping;
        index = nextIdx;

        return current * amp;
    }

    bool isActive() const { return active; }

private:
    std::vector<float> buffer;
    double sr = 44100.0;
    int    length = 100, index = 0;
    float  amp = 0.0f, damping = 0.997f;
    bool   active = false;
};

//==============================================================================
/** The performance every preset is rendered from. */
void renderSource (std::vector<float>& l, std::vector<float>& r, double sr)
{
    struct Note { double time; float freq; float amp; float pan; };

    // A minor, walked up and let ring.
    const Note notes[] =
    {
        { 0.00, 110.00f, 0.95f, -0.35f },   // A2
        { 0.62, 164.81f, 0.80f,  0.30f },   // E3
        { 1.24, 220.00f, 0.85f, -0.20f },   // A3
        { 1.86, 261.63f, 0.75f,  0.35f },   // C4
        { 2.48, 329.63f, 0.80f, -0.30f },   // E4
        { 3.30, 246.94f, 0.70f,  0.25f },   // B3
        { 3.92, 329.63f, 0.75f, -0.15f },   // E4
        { 4.54, 392.00f, 0.85f,  0.40f },   // G4
        { 5.36, 293.66f, 0.70f, -0.25f },   // D4
        { 5.98, 220.00f, 0.90f,  0.10f }    // A3
    };

    constexpr int kVoices = 10;
    PluckedString voices[kVoices];
    for (auto& v : voices) v.prepare (sr);

    Rng rng (0x5A17u);
    OnePoleLP bodyL, bodyR;
    OnePoleHP rumbleL, rumbleR;
    bodyL.prepare (sr);   bodyR.prepare (sr);
    bodyL.setCutoff (6500.0f); bodyR.setCutoff (6500.0f);
    rumbleL.prepare (sr); rumbleR.prepare (sr);
    rumbleL.setCutoff (85.0f); rumbleR.setCutoff (85.0f);

    float panL[kVoices], panR[kVoices];
    for (int i = 0; i < kVoices; ++i) equalPowerPan (notes[i].pan, panL[i], panR[i]);

    const size_t total = l.size();
    size_t nextNote = 0;

    for (size_t n = 0; n < total; ++n)
    {
        const double t = (double) n / sr;

        while (nextNote < kVoices && t >= notes[nextNote].time)
        {
            voices[nextNote].pluck (notes[nextNote].freq, notes[nextNote].amp, rng);
            ++nextNote;
        }

        float sl = 0.0f, sr_ = 0.0f;
        for (int v = 0; v < kVoices; ++v)
        {
            if (! voices[v].isActive()) continue;
            const float s = voices[v].next();
            sl += s * panL[v];
            sr_ += s * panR[v];
        }

        sl *= 0.35f;
        sr_ *= 0.35f;

        l[n] = rumbleL.process (bodyL.process (sl));
        r[n] = rumbleR.process (bodyR.process (sr_));
    }
}

//==============================================================================
void writeWav (const std::string& path, const std::vector<float>& l,
               const std::vector<float>& r, int sampleRate)
{
    const uint32_t frames    = (uint32_t) l.size();
    const uint32_t dataBytes = frames * 2u * 2u;          // stereo, 16 bit

    FILE* f = std::fopen (path.c_str(), "wb");
    if (f == nullptr) { std::printf ("  could not open %s\n", path.c_str()); return; }

    auto u32 = [f] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto u16 = [f] (uint16_t v) { std::fwrite (&v, 2, 1, f); };

    std::fwrite ("RIFF", 1, 4, f);  u32 (36u + dataBytes);
    std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f);  u32 (16u);
    u16 (1);                                   // PCM
    u16 (2);                                   // channels
    u32 ((uint32_t) sampleRate);
    u32 ((uint32_t) sampleRate * 4u);          // byte rate
    u16 (4);                                   // block align
    u16 (16);                                  // bits
    std::fwrite ("data", 1, 4, f);  u32 (dataBytes);

    Rng dither (0xD177u);
    std::vector<int16_t> interleaved ((size_t) frames * 2);

    for (uint32_t i = 0; i < frames; ++i)
        for (int ch = 0; ch < 2; ++ch)
        {
            // TPDF dither, one LSB: the honest way down to 16 bit.
            const float noise = (dither.next01() - dither.next01()) * (1.0f / 32768.0f);
            const float v = clampf ((ch == 0 ? l[i] : r[i]) + noise, -1.0f, 1.0f);
            interleaved[(size_t) i * 2 + (size_t) ch] = (int16_t) std::lrint (v * 32767.0f);
        }

    std::fwrite (interleaved.data(), 2, interleaved.size(), f);
    std::fclose (f);
}

void normaliseAndFade (std::vector<float>& l, std::vector<float>& r, double sr)
{
    float peak = 0.0f;
    for (size_t i = 0; i < l.size(); ++i)
        peak = std::max (peak, std::max (std::fabs (l[i]), std::fabs (r[i])));

    const float gain = (peak > 1.0e-6f) ? (0.84f / peak) : 1.0f;   // -1.5 dBFS

    const size_t fadeIn  = (size_t) (sr * 0.01);
    const size_t fadeOut = (size_t) (sr * 0.15);

    for (size_t i = 0; i < l.size(); ++i)
    {
        float g = gain;
        if (i < fadeIn)                  g *= (float) i / (float) fadeIn;
        if (i + fadeOut > l.size())      g *= (float) (l.size() - i) / (float) fadeOut;
        l[i] *= g;
        r[i] *= g;
    }
}

//==============================================================================
struct Preset
{
    const char* file;
    const char* what;
    double      seconds;
    std::function<void (EngineParams&)>          setup;
    std::function<void (EngineParams&, double)>  automate;   // optional
    std::function<float (double)>                inputGate;  // optional
};
} // namespace

//==============================================================================
int main (int argc, char** argv)
{
    const std::string outDir = (argc > 1) ? argv[1] : ".";

    std::printf ("\nGUSH -- rendering demos into %s\n\n", outDir.c_str());

    //== the shared performance ================================================
    const size_t sourceLen = (size_t) (kSampleRate * 9.0);
    std::vector<float> srcL (sourceLen, 0.0f), srcR (sourceLen, 0.0f);
    renderSource (srcL, srcR, kSampleRate);

    //== the presets ===========================================================
    const std::vector<Preset> presets =
    {
        { "00-dry", "the source alone, untouched", 9.0,
          [] (EngineParams& p) { p.drive = 0.0f; p.mix = 0.0f; }, nullptr, nullptr },

        { "01-tape-echo", "Magneto: three heads, warm regen, barely any reverb", 20.0,
          [] (EngineParams& p)
          {
              p.drive = 0.40f; p.mix = 0.58f; p.echoMix = 1.0f;
              p.delaySeconds = 0.36f; p.headPattern = 2; p.feedback = 0.76f;
              p.tapeTone = 0.42f; p.grainMix = 0.0f; p.resMix = 0.0f;
              p.revMix = 0.16f; p.revDecay = 3.0f; p.revSize = 0.45f;
          }, nullptr, nullptr },

        { "02-grain-cloud", "Microcell: the same notes broken into grains", 17.0,
          [] (EngineParams& p)
          {
              p.drive = 0.2f; p.mix = 0.78f; p.echoMix = 1.0f;
              p.delaySeconds = 0.45f; p.feedback = 0.42f;
              p.grainMix = 0.95f; p.grainSizeMs = 70.0f; p.grainDensity = 26.0f;
              p.grainSpray = 0.55f; p.grainSpread = 0.90f; p.grainJitter = 0.18f;
              p.revMix = 0.30f; p.revDecay = 5.0f; p.revSize = 0.6f;
          }, nullptr, nullptr },

        { "03-resonator", "Rings: the echoes strike a bell instead of a speaker", 17.0,
          [] (EngineParams& p)
          {
              p.mix = 0.82f; p.echoMix = 0.55f; p.delaySeconds = 0.28f;
              p.feedback = 0.38f; p.grainMix = 0.30f; p.grainSizeMs = 110.0f;
              p.resMix = 0.85f; p.resFreq = 196.0f; p.resStructure = 0.70f;
              p.resBrightness = 0.62f; p.resDamping = 0.14f; p.resPosition = 0.22f;
              p.revMix = 0.45f; p.revDecay = 8.0f;
          }, nullptr, nullptr },

        { "04-cathedral", "StarLab: 22 second decay with an octave in the tail", 30.0,
          [] (EngineParams& p)
          {
              p.mix = 0.88f; p.echoMix = 0.55f; p.delaySeconds = 0.80f;
              p.feedback = 0.50f; p.pitchSemis = 12.0f; p.pitchFeedback = 0.25f;
              p.grainMix = 0.50f; p.grainSizeMs = 150.0f; p.grainDensity = 16.0f;
              p.grainSpread = 0.8f;
              p.revMix = 0.85f; p.revDecay = 22.0f; p.revSize = 0.92f;
              p.revShimmer = 0.75f; p.revPredelayMs = 45.0f; p.revMod = 0.5f;
              p.revDamping = 0.28f;
          }, nullptr, nullptr },

        { "05-reverse-octave-down", "backwards, each repeat an octave lower", 19.0,
          [] (EngineParams& p)
          {
              p.mix = 0.85f; p.echoMix = 1.0f; p.reverse = true;
              p.delaySeconds = 0.70f; p.feedback = 0.68f;
              p.pitchSemis = -12.0f; p.pitchFeedback = 0.80f;
              p.tapeTone = 0.5f; p.grainMix = 0.0f;
              p.revMix = 0.50f; p.revDecay = 9.0f; p.revSize = 0.7f;
          }, nullptr, nullptr },

        { "06-freeze", "playing stops at 5 s, FREEZE holds the loop for the rest", 24.0,
          [] (EngineParams& p)
          {
              p.mix = 0.92f; p.echoMix = 1.0f; p.delaySeconds = 1.1f;
              p.feedback = 0.55f; p.grainMix = 0.45f; p.grainSizeMs = 120.0f;
              p.grainDensity = 20.0f; p.grainSpray = 0.35f; p.grainSpread = 0.85f;
              p.revMix = 0.55f; p.revDecay = 12.0f; p.revSize = 0.8f; p.revShimmer = 0.45f;
          },
          [] (EngineParams& p, double t) { p.freeze = (t >= 5.0); },
          [] (double t) { return t < 5.0 ? 1.0f : 0.0f; } },

        { "07-maths-modulation", "nothing touched: random walk and two LFOs moving it", 22.0,
          [] (EngineParams& p)
          {
              p.mix = 0.85f; p.echoMix = 0.9f; p.delaySeconds = 0.55f;
              p.feedback = 0.55f; p.grainMix = 0.7f; p.grainSizeMs = 90.0f;
              p.grainDensity = 22.0f; p.grainSpread = 0.85f;
              p.revMix = 0.6f; p.revDecay = 12.0f; p.revSize = 0.75f;

              p.lfo1Rate = 0.09f; p.lfo1Shape = 0;      // slow sine
              p.lfo2Rate = 0.06f; p.lfo2Shape = 1;      // slower triangle
              p.randRate = 0.7f;  p.randSlew = 0.75f;   // a wander, not a jump

              p.modSource[0] = (int) ModSource::Random;   p.modDest[0] = (int) ModDest::GrainSize;  p.modAmount[0] =  0.85f;
              p.modSource[1] = (int) ModSource::Lfo1;     p.modDest[1] = (int) ModDest::DelayTime;  p.modAmount[1] =  0.30f;
              p.modSource[2] = (int) ModSource::Lfo2;     p.modDest[2] = (int) ModDest::Cutoff;     p.modAmount[2] = -0.45f;
              p.modSource[3] = (int) ModSource::Envelope; p.modDest[3] = (int) ModDest::RevShimmer; p.modAmount[3] =  0.70f;
          }, nullptr, nullptr }
    };

    //== render ================================================================
    for (const auto& preset : presets)
    {
        const size_t total = (size_t) (kSampleRate * preset.seconds);
        std::vector<float> l (total, 0.0f), r (total, 0.0f);

        for (size_t i = 0; i < total; ++i)
        {
            const double t = (double) i / kSampleRate;
            const float gate = preset.inputGate ? preset.inputGate (t) : 1.0f;

            if (i < sourceLen)
            {
                l[i] = srcL[i] * gate;
                r[i] = srcR[i] * gate;
            }
        }

        Engine engine;
        engine.prepare (kSampleRate, 256);
        engine.reset();

        EngineParams params;
        preset.setup (params);

        for (size_t pos = 0; pos < total; pos += 256)
        {
            const int n = (int) std::min<size_t> (256, total - pos);

            if (preset.automate)
                preset.automate (params, (double) pos / kSampleRate);

            engine.setParameters (params);
            engine.process (l.data() + pos, r.data() + pos, n);
        }

        normaliseAndFade (l, r, kSampleRate);

        const std::string path = outDir + "/" + preset.file + ".wav";
        writeWav (path, l, r, (int) kSampleRate);

        std::printf ("  %-24s %5.1f s   %s\n", preset.file, preset.seconds, preset.what);
    }

    std::printf ("\ndone\n\n");
    return 0;
}
