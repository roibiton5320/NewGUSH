/*
    dsp_smoke_test.cpp
    ---------------------------------------------------------------------------
    Runs the whole engine with no JUCE and no DAW.

    This exists because the expensive bugs in a feedback-heavy plugin are not
    compile errors. They are the NaN that appears after ninety seconds of
    shimmer, the runaway when feedback sits at 1.05 with an octave-up in the
    loop, the freeze that quietly decays to nothing. Those only show up if
    something actually pushes audio through the thing, hard, for a while.

    Build and run:
        cd Tools
        g++ -O2 -std=c++17 dsp_smoke_test.cpp ../Source/DSP/Engine.cpp \
            ../Source/DSP/GranularTapeEcho.cpp ../Source/DSP/ModalResonator.cpp \
            ../Source/DSP/Reverb.cpp -o smoke && ./smoke
*/

#include "../Source/DSP/Engine.h"
#include "../Source/DSP/PitchShifter.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace gush;

namespace
{
int failures = 0;

void check (bool ok, const std::string& what)
{
    std::printf ("  [%s] %s\n", ok ? " ok " : "FAIL", what.c_str());
    if (! ok) ++failures;
}

struct Stats { float peak = 0.0f; double rms = 0.0; bool finite = true; };

Stats analyse (const std::vector<float>& a, const std::vector<float>& b)
{
    Stats s;
    double sum = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        for (float x : { a[i], b[i] })
        {
            if (! std::isfinite (x)) { s.finite = false; continue; }
            s.peak = std::max (s.peak, std::fabs (x));
            sum += (double) x * (double) x;
        }

    s.rms = std::sqrt (sum / (double) std::max<size_t> (1, a.size() * 2));
    return s;
}

double rmsOf (const std::vector<float>& x, size_t from, size_t len)
{
    double sum = 0.0;
    const size_t end = std::min (x.size(), from + len);
    for (size_t i = from; i < end; ++i) sum += (double) x[i] * (double) x[i];
    return std::sqrt (sum / (double) std::max<size_t> (1, end - from));
}

/** Autocorrelation pitch estimate. Robust against the amplitude warble a
    dual-tap shifter produces, which zero-crossing counting is not. */
double estimateFrequency (const std::vector<float>& x, double sr, size_t start,
                          size_t len, double loHz, double hiHz)
{
    const int minLag = std::max (2, (int) (sr / hiHz));
    const int maxLag = (int) (sr / loHz);

    double bestScore = -2.0;
    int    bestLag   = minLag;

    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double num = 0.0, e0 = 0.0, e1 = 0.0;
        for (size_t i = 0; i < len; ++i)
        {
            const double a = x[start + i];
            const double b = x[start + i + (size_t) lag];
            num += a * b;  e0 += a * a;  e1 += b * b;
        }

        const double score = num / (std::sqrt (e0 * e1) + 1.0e-12);
        if (score > bestScore + 1.0e-4) { bestScore = score; bestLag = lag; }
    }

    return sr / (double) bestLag;
}

/** A plucked-ish source: decaying tone bursts plus a little noise. A fair
    stand-in for a guitar going into the real thing. */
float testSample (double t, Rng& rng)
{
    const double env = std::exp (-4.0 * std::fmod (t, 1.0));
    return (float) (0.45 * env * std::sin (2.0 * kPi * 196.0 * t))
         + 0.02f * rng.nextBipolar();
}

Stats run (Engine& e, const EngineParams& p, double sr, double seconds,
           bool feedInput, uint32_t noiseSeed = 12345u)
{
    const int total = (int) (sr * seconds);
    const int block = 128;

    std::vector<float> outL, outR;
    outL.reserve ((size_t) total);
    outR.reserve ((size_t) total);

    Rng rng (noiseSeed);
    std::vector<float> bl ((size_t) block), br ((size_t) block);

    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);

        for (int i = 0; i < n; ++i)
        {
            const float x = feedInput ? testSample ((double) (pos + i) / sr, rng) : 0.0f;
            bl[(size_t) i] = x;
            br[(size_t) i] = x * 0.9f;
        }

        e.setParameters (p);
        e.process (bl.data(), br.data(), n);

        outL.insert (outL.end(), bl.begin(), bl.begin() + n);
        outR.insert (outR.end(), br.begin(), br.begin() + n);
    }

    return analyse (outL, outR);
}

void report (const char* name, const Stats& s, float peakCeiling)
{
    std::printf ("       %-26s peak %7.3f   rms %7.4f\n", name, s.peak, s.rms);
    check (s.finite, std::string (name) + ": no NaN/Inf");
    check (s.peak < peakCeiling, std::string (name) + ": output stays bounded");
}
} // namespace

//==============================================================================
int main()
{
    std::printf ("\nGUSH -- DSP smoke test\n======================\n\n");

    //== 1. the pitch shifter actually transposes ==============================
    std::printf ("1. Pitch shifter\n");
    {
        const double sr = 48000.0;

        for (auto want : { std::pair<float, double> { 2.0f,  880.0 },
                           std::pair<float, double> { 0.5f,  220.0 },
                           std::pair<float, double> { 1.5f,  660.0 } })
        {
            PitchShifter ps;
            ps.prepare (sr, 70.0f);
            ps.setRatio (want.first);

            const int n = (int) sr * 2;
            std::vector<float> out ((size_t) n);

            for (int i = 0; i < n; ++i)
                out[(size_t) i] = ps.process ((float) std::sin (2.0 * kPi * 440.0 * (double) i / sr));

            const double measured = estimateFrequency (out, sr, (size_t) sr, 8000, 120.0, 1500.0);
            const double err = std::fabs (measured - want.second) / want.second;

            std::printf ("       ratio %.2f -> %6.1f Hz  (want %.0f, off by %.2f%%)\n",
                         want.first, measured, want.second, err * 100.0);
            check (err < 0.02, "transposition within 2%");
        }
    }

    //== 2. every stage, at sane settings, at every sample rate ================
    std::printf ("\n2. Full chain, musical settings\n");
    {
        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            Engine e;
            e.prepare (sr, 512);
            e.reset();

            EngineParams p;
            p.mix = 0.6f; p.grainMix = 0.4f; p.resMix = 0.35f; p.revMix = 0.5f;
            p.revShimmer = 0.3f; p.pitchSemis = 12.0f; p.pitchFeedback = 0.4f;

            report ((std::to_string ((int) sr) + " Hz").c_str(), run (e, p, sr, 4.0, true), 8.0f);
        }
    }

    //== 3. the settings that break things =====================================
    std::printf ("\n3. Hostile settings\n");
    {
        const double sr = 48000.0;
        struct Case { const char* name; void (*setup) (EngineParams&); };

        const Case cases[] =
        {
            { "feedback at max",     [] (EngineParams& p) { p.feedback = 1.05f; p.tapeTone = 1.0f; p.echoMix = 1.0f; } },
            { "freeze + shimmer",    [] (EngineParams& p) { p.freeze = true; p.revShimmer = 1.0f; p.revDecay = 60.0f; p.revMix = 1.0f; } },
            { "reverse + octave fb", [] (EngineParams& p) { p.reverse = true; p.pitchSemis = -12.0f; p.pitchFeedback = 1.0f; p.feedback = 0.95f; } },
            { "grain storm",         [] (EngineParams& p) { p.grainMix = 1.0f; p.grainDensity = 120.0f; p.grainSizeMs = 500.0f; p.grainSpray = 1.0f; p.feedback = 0.9f; } },
            { "tiny grains",         [] (EngineParams& p) { p.grainMix = 1.0f; p.grainDensity = 120.0f; p.grainSizeMs = 4.0f; } },
            { "resonator wide open", [] (EngineParams& p) { p.resMix = 1.0f; p.resDamping = 0.0f; p.resBrightness = 1.0f; p.resStructure = 1.0f; p.resFreq = 40.0f; } },
            { "everything at once",  [] (EngineParams& p)
                {
                    p.drive = 1.0f; p.inputGainDb = 12.0f; p.feedback = 1.0f;
                    p.grainMix = 0.8f; p.grainDensity = 90.0f; p.grainSpray = 1.0f;
                    p.resMix = 0.8f; p.resDamping = 0.05f;
                    p.revMix = 1.0f; p.revDecay = 60.0f; p.revShimmer = 1.0f;
                    p.pitchFeedback = 1.0f; p.pitchSemis = 12.0f;
                    p.mix = 1.0f; p.echoMix = 1.0f;
                    for (int i = 0; i < ModMatrix::kSlots; ++i)
                    { p.modSource[i] = i + 1; p.modDest[i] = i + 1; p.modAmount[i] = 1.0f; }
                } }
        };

        for (const auto& c : cases)
        {
            Engine e;
            e.prepare (sr, 512);
            e.reset();

            EngineParams p;
            c.setup (p);
            report (c.name, run (e, p, sr, 8.0, true), 12.0f);
        }
    }

    //== 4. the reverb has a tail, and the tail ends ===========================
    std::printf ("\n4. Reverb tail\n");
    {
        const double sr = 48000.0;
        Engine e;
        e.prepare (sr, 512);
        e.reset();

        EngineParams p;
        p.mix = 1.0f; p.echoMix = 0.0f; p.resMix = 0.0f; p.revMix = 1.0f;
        p.revDecay = 6.0f; p.revShimmer = 0.0f; p.revSize = 0.7f; p.drive = 0.0f;

        const int total = (int) (sr * 10.0);
        const int burst = (int) (sr * 0.25);
        std::vector<float> out ((size_t) total);

        Rng rng (7u);
        std::vector<float> bl (128), br (128);

        for (int pos = 0; pos < total; pos += 128)
        {
            const int n = std::min (128, total - pos);
            for (int i = 0; i < n; ++i)
            {
                const float x = (pos + i < burst) ? 0.4f * rng.nextBipolar() : 0.0f;
                bl[(size_t) i] = x;
                br[(size_t) i] = x;
            }

            e.setParameters (p);
            e.process (bl.data(), br.data(), n);
            for (int i = 0; i < n; ++i) out[(size_t) (pos + i)] = bl[(size_t) i];
        }

        const double early = rmsOf (out, (size_t) (sr * 1.0), (size_t) (sr * 0.5));
        const double late  = rmsOf (out, (size_t) (sr * 7.0), (size_t) (sr * 0.5));

        std::printf ("       rms at 1 s %.5f   at 7 s %.5f   (%.1f dB down)\n",
                     early, late, 20.0 * std::log10 ((late + 1e-12) / (early + 1e-12)));

        check (early > 1.0e-3, "a tail exists 1 s after the burst");
        check (late < early * 0.25, "the tail is decaying, not sustaining");
        check (late > 1.0e-9, "the tail has not collapsed to denormal silence");
    }

    //== 5. FREEZE actually holds ==============================================
    std::printf ("\n5. Tape freeze\n");
    {
        const double sr = 48000.0;
        Engine e;
        e.prepare (sr, 512);
        e.reset();

        EngineParams p;
        p.mix = 1.0f; p.echoMix = 1.0f; p.resMix = 0.0f; p.revMix = 0.0f;
        p.feedback = 0.4f; p.delaySeconds = 0.5f; p.grainMix = 0.0f; p.drive = 0.0f;

        const int total   = (int) (sr * 12.0);
        const int holdAt  = (int) (sr * 3.0);
        std::vector<float> out ((size_t) total);

        Rng rng (11u);
        std::vector<float> bl (128), br (128);

        for (int pos = 0; pos < total; pos += 128)
        {
            const int n = std::min (128, total - pos);
            const bool freezing = pos >= holdAt;

            for (int i = 0; i < n; ++i)
            {
                const float x = freezing ? 0.0f : testSample ((double) (pos + i) / sr, rng);
                bl[(size_t) i] = x;
                br[(size_t) i] = x;
            }

            p.freeze = freezing;
            e.setParameters (p);
            e.process (bl.data(), br.data(), n);
            for (int i = 0; i < n; ++i) out[(size_t) (pos + i)] = bl[(size_t) i];
        }

        const double justAfter = rmsOf (out, (size_t) (sr * 4.0),  (size_t) sr);
        const double muchLater = rmsOf (out, (size_t) (sr * 11.0), (size_t) sr);

        std::printf ("       rms 1 s into hold %.4f   8 s into hold %.4f   (%.1f dB drift)\n",
                     justAfter, muchLater,
                     20.0 * std::log10 ((muchLater + 1e-12) / (justAfter + 1e-12)));

        check (justAfter > 1.0e-3, "the loop is sounding after the input stops");
        check (muchLater > justAfter * 0.4, "it is still there eight seconds later");
        check (muchLater < justAfter * 3.0, "and it has not run away");
    }

    //== 6. silence in must mean silence out ===================================
    std::printf ("\n6. Silence discipline\n");
    {
        const double sr = 48000.0;
        Engine e;
        e.prepare (sr, 512);
        e.reset();

        EngineParams p;
        p.mix = 1.0f; p.echoMix = 1.0f; p.revMix = 1.0f; p.feedback = 0.6f;

        const auto s = run (e, p, sr, 3.0, false);
        std::printf ("       %-26s peak %.3e\n", "no input", s.peak);
        check (s.finite && s.peak < 1.0e-4f, "a silent input produces silence");
    }

    //== 7. the seed really does reproduce =====================================
    std::printf ("\n7. Reproducible randomness\n");
    {
        const double sr = 48000.0;
        const int total = (int) sr * 2;

        auto render = [&] (uint32_t seed)
        {
            Engine e;
            e.prepare (sr, 512);
            e.reset();

            EngineParams p;
            p.seed = seed;
            p.grainMix = 1.0f; p.grainSpray = 1.0f; p.grainDensity = 40.0f;
            p.mix = 1.0f; p.echoMix = 1.0f;
            p.modSource[0] = (int) ModSource::Random;
            p.modDest[0]   = (int) ModDest::GrainSize;
            p.modAmount[0] = 1.0f;

            std::vector<float> l ((size_t) total), r ((size_t) total);
            Rng rng (999u);
            for (int i = 0; i < total; ++i)
            {
                l[(size_t) i] = 0.3f * rng.nextBipolar();
                r[(size_t) i] = l[(size_t) i];
            }

            e.setParameters (p);
            for (int pos = 0; pos < total; pos += 128)
                e.process (l.data() + pos, r.data() + pos, std::min (128, total - pos));

            return l;
        };

        const auto a = render (0xC0FFEEu);
        const auto b = render (0xC0FFEEu);
        const auto c = render (0xBADBEEFu);

        bool identical = true, different = false;
        for (int i = 0; i < total; ++i)
        {
            if (a[(size_t) i] != b[(size_t) i]) identical = false;
            if (std::fabs (a[(size_t) i] - c[(size_t) i]) > 1.0e-6f) different = true;
        }

        check (identical, "same seed renders bit-identical audio");
        check (different, "a different seed renders something else");
    }

    //== 8. is it cheap enough to be worth playing =============================
    std::printf ("\n8. Cost\n");
    {
        const double sr = 48000.0;
        Engine e;
        e.prepare (sr, 512);
        e.reset();

        EngineParams p;
        p.grainMix = 0.7f; p.grainDensity = 60.0f; p.resMix = 0.6f;
        p.revMix = 0.7f; p.revShimmer = 0.5f; p.pitchFeedback = 0.5f;
        p.modSource[0] = 1; p.modDest[0] = 4; p.modAmount[0] = 0.4f;
        p.modSource[1] = 3; p.modDest[1] = 1; p.modAmount[1] = 0.6f;

        const double seconds = 20.0;
        const auto t0 = std::chrono::steady_clock::now();
        const auto s  = run (e, p, sr, seconds, true);
        const auto t1 = std::chrono::steady_clock::now();

        const double wall = std::chrono::duration<double> (t1 - t0).count();
        const double factor = seconds / wall;

        std::printf ("       %.0f s of audio in %.2f s wall  ->  %.0fx realtime"
                     "  (~%.2f%% of one core)\n", seconds, wall, factor, 100.0 / factor);
        check (s.finite, "still finite after 20 s");
        check (factor > 10.0, "comfortably faster than realtime");
    }

    //== 9. FEEDBACK must mean the same thing on every head pattern ============
    std::printf ("\n9. Feedback consistency across head patterns\n");
    {
        const double sr = 48000.0;
        double decayDb[4] = { 0.0, 0.0, 0.0, 0.0 };

        for (int pattern = 0; pattern < 4; ++pattern)
        {
            Engine e;
            e.prepare (sr, 512);
            e.reset();

            EngineParams p;
            p.mix = 1.0f; p.echoMix = 1.0f; p.resMix = 0.0f; p.revMix = 0.0f;
            p.grainMix = 0.0f; p.drive = 0.0f;
            p.delaySeconds = 0.25f; p.feedback = 0.85f; p.tapeTone = 1.0f;
            p.headPattern = pattern;

            const int total = (int) (sr * 12.0);
            const int burst = (int) (sr * 0.30);
            std::vector<float> out ((size_t) total);

            Rng rng (23u);
            std::vector<float> bl (128), br (128);

            for (int pos = 0; pos < total; pos += 128)
            {
                const int n = std::min (128, total - pos);
                for (int i = 0; i < n; ++i)
                {
                    const float x = (pos + i < burst) ? 0.4f * rng.nextBipolar() : 0.0f;
                    bl[(size_t) i] = x;
                    br[(size_t) i] = x;
                }

                e.setParameters (p);
                e.process (bl.data(), br.data(), n);
                for (int i = 0; i < n; ++i) out[(size_t) (pos + i)] = bl[(size_t) i];
            }

            const double a = rmsOf (out, (size_t) (sr * 2.0),  (size_t) sr);
            const double b = rmsOf (out, (size_t) (sr * 10.0), (size_t) sr);
            decayDb[pattern] = 20.0 * std::log10 ((b + 1e-12) / (a + 1e-12));

            std::printf ("       %-8s decays %6.1f dB between 2 s and 10 s\n",
                         (const char*[]) { "single", "dual", "triplet", "quad" }[pattern],
                         decayDb[pattern]);
        }

        double lo = decayDb[0], hi = decayDb[0];
        for (double d : decayDb) { lo = std::min (lo, d); hi = std::max (hi, d); }

        // A 0.25 s delay repeats 32 times in the 8 s measured, so the honest
        // answer is 32 * 20*log10(0.85) = -45 dB, plus a little filter loss.
        // The knob has to be quantitatively true, not just consistent.
        const double theoretical = 32.0 * 20.0 * std::log10 (0.85);
        std::printf ("       theory says %6.1f dB\n", theoretical);

        check (hi - lo < 6.0, "the same FEEDBACK decays alike on every pattern");
        check (std::fabs (decayDb[3] - theoretical) < 8.0,
               "the measured decay matches the feedback knob");
    }

    //== 10. the DECAY knob has to be telling the truth ========================
    std::printf ("\n10. Decay accuracy\n");
    {
        const double sr = 48000.0;

        for (float want : { 2.0f, 8.0f, 20.0f })
        {
            Engine e;
            e.prepare (sr, 512);
            e.reset();

            EngineParams p;
            p.mix = 1.0f; p.echoMix = 0.0f; p.resMix = 0.0f; p.drive = 0.0f;
            p.revMix = 1.0f; p.revDecay = want; p.revDamping = 0.0f;
            p.revShimmer = 0.0f; p.revSize = 0.7f; p.revPredelayMs = 0.0f;

            const int total = (int) (sr * (want * 1.1 + 2.0));
            const int burst = (int) (sr * 0.25);
            std::vector<float> out ((size_t) total);
            std::vector<float> bl (128), br (128);

            Rng rng (5u);
            OnePoleLP band;
            band.prepare (sr);
            band.setCutoff (700.0f);    // measure the band the tank should hold

            for (int pos = 0; pos < total; pos += 128)
            {
                const int n = std::min (128, total - pos);
                for (int i = 0; i < n; ++i)
                {
                    const float x = (pos + i < burst) ? band.process (0.9f * rng.nextBipolar()) : 0.0f;
                    bl[(size_t) i] = x;
                    br[(size_t) i] = x;
                }

                e.setParameters (p);
                e.process (bl.data(), br.data(), n);
                for (int i = 0; i < n; ++i) out[(size_t) (pos + i)] = bl[(size_t) i];
            }

            // Two late windows, past the build-up, inside the pure exponential.
            const double t1 = want * 0.30, t2 = want * 0.80;
            const double w  = std::min (0.5, (double) want * 0.1);
            const double a  = rmsOf (out, (size_t) (t1 * sr), (size_t) (w * sr));
            const double b  = rmsOf (out, (size_t) (t2 * sr), (size_t) (w * sr));
            const double rt60 = (t2 - t1) * 60.0 / -(20.0 * std::log10 ((b + 1e-15) / (a + 1e-15)));

            std::printf ("       knob %5.1f s  ->  measured %5.1f s  (%.0f%%)\n",
                         want, rt60, 100.0 * rt60 / (double) want);

            // A gentle one-pole highpass applied on every circulation used to
            // eat a third of this. It must not come back.
            check (rt60 > (double) want * 0.80, "the tail lasts about as long as DECAY says");
            check (rt60 < (double) want * 1.20, "and not longer");
        }
    }

    std::printf ("\n======================\n%s (%d failure%s)\n\n",
                 failures == 0 ? "ALL PASSED" : "FAILURES", failures,
                 failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
