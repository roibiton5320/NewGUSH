#include "Parameters.h"

namespace
{
using APF   = juce::AudioParameterFloat;
using APC   = juce::AudioParameterChoice;
using APB   = juce::AudioParameterBool;
using APInt = juce::AudioParameterInt;
using FRange = juce::NormalisableRange<float>;

/** A range whose midpoint sits where the ear expects it, not where the
    arithmetic does. A 5 ms - 3 s delay knob is useless if half its travel is
    spent above one second. */
FRange skewed (float lo, float hi, float centre, float interval = 0.0f)
{
    FRange r (lo, hi, interval);
    r.setSkewForCentre (centre);
    return r;
}

FRange unit() { return FRange (0.0f, 1.0f, 0.0f); }

auto ms      = juce::AudioParameterFloatAttributes().withLabel ("ms");
auto hz      = juce::AudioParameterFloatAttributes().withLabel ("Hz");
auto sec     = juce::AudioParameterFloatAttributes().withLabel ("s");
auto db      = juce::AudioParameterFloatAttributes().withLabel ("dB");
auto st      = juce::AudioParameterFloatAttributes().withLabel ("st");
auto percent = juce::AudioParameterFloatAttributes().withLabel ("%");

juce::ParameterID pid (const juce::String& id) { return { id, 1 }; }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    //== input / output ========================================================
    layout.add (std::make_unique<APF> (pid (ids::inGain),  "Input",   FRange (-24.0f, 24.0f, 0.1f),  0.0f, db));
    layout.add (std::make_unique<APF> (pid (ids::drive),   "Drive",   unit(), 0.15f, percent));
    layout.add (std::make_unique<APF> (pid (ids::mix),     "Mix",     unit(), 0.5f,  percent));
    layout.add (std::make_unique<APF> (pid (ids::outGain), "Output",  FRange (-24.0f, 12.0f, 0.1f),  0.0f, db));

    //== granular tape echo ====================================================
    layout.add (std::make_unique<APF> (pid (ids::delayTime), "Delay Time", skewed (5.0f, 3000.0f, 400.0f, 0.1f), 420.0f, ms));
    layout.add (std::make_unique<APF> (pid (ids::speed),     "Tape Speed", skewed (0.25f, 2.0f, 1.0f, 0.001f),   1.0f));
    layout.add (std::make_unique<APF> (pid (ids::ramp),      "Ramp Time",  skewed (0.02f, 10.0f, 0.5f, 0.01f),   0.35f, sec));
    layout.add (std::make_unique<APC> (pid (ids::heads),     "Heads",
                                       StringArray { "Single", "Dual", "Triplet", "Quad" }, 2));
    layout.add (std::make_unique<APF> (pid (ids::feedback),  "Feedback",   FRange (0.0f, 1.05f, 0.001f), 0.45f, percent));
    layout.add (std::make_unique<APF> (pid (ids::tapeTone),  "Tape Tone",  unit(), 0.55f, percent));
    layout.add (std::make_unique<APF> (pid (ids::pitch),     "Pitch",      FRange (-24.0f, 24.0f, 1.0f), 0.0f, st));
    layout.add (std::make_unique<APF> (pid (ids::pitchFb),   "Pitch Regen", unit(), 0.0f, percent));
    layout.add (std::make_unique<APB> (pid (ids::reverse),   "Reverse", false));
    layout.add (std::make_unique<APB> (pid (ids::freeze),    "Freeze",  false));
    layout.add (std::make_unique<APF> (pid (ids::echoMix),   "Echo Mix", unit(), 0.7f, percent));

    //== grains ================================================================
    layout.add (std::make_unique<APF> (pid (ids::grainMix),     "Grain Blend",   unit(), 0.35f, percent));
    layout.add (std::make_unique<APF> (pid (ids::grainSize),    "Grain Size",    skewed (4.0f, 600.0f, 90.0f, 0.1f), 90.0f, ms));
    layout.add (std::make_unique<APF> (pid (ids::grainDensity), "Grain Density", skewed (0.2f, 120.0f, 15.0f, 0.1f), 14.0f, hz));
    layout.add (std::make_unique<APF> (pid (ids::grainSpray),   "Spray",         unit(), 0.30f, percent));
    layout.add (std::make_unique<APF> (pid (ids::grainSpread),  "Spread",        unit(), 0.60f, percent));
    layout.add (std::make_unique<APF> (pid (ids::grainJitter),  "Pitch Jitter",  unit(), 0.15f, percent));

    //== resonator =============================================================
    layout.add (std::make_unique<APF> (pid (ids::resMix),       "Resonate",   unit(), 0.0f,  percent));
    layout.add (std::make_unique<APF> (pid (ids::resFreq),      "Res Tune",   skewed (20.0f, 4000.0f, 300.0f, 0.1f), 220.0f, hz));
    layout.add (std::make_unique<APF> (pid (ids::resStructure), "Structure",  unit(), 0.25f, percent));
    layout.add (std::make_unique<APF> (pid (ids::resBright),    "Brightness", unit(), 0.50f, percent));
    layout.add (std::make_unique<APF> (pid (ids::resDamp),      "Damping",    unit(), 0.40f, percent));
    layout.add (std::make_unique<APF> (pid (ids::resPos),       "Position",   unit(), 0.28f, percent));

    //== reverb ================================================================
    layout.add (std::make_unique<APF> (pid (ids::revMix),      "Reverb",       unit(), 0.40f, percent));
    layout.add (std::make_unique<APF> (pid (ids::revSize),     "Size",         unit(), 0.65f, percent));
    layout.add (std::make_unique<APF> (pid (ids::revDecay),    "Decay",        skewed (0.2f, 60.0f, 6.0f, 0.01f), 8.0f, sec));
    layout.add (std::make_unique<APF> (pid (ids::revDamp),     "Rev Damping",  unit(), 0.40f, percent));
    layout.add (std::make_unique<APF> (pid (ids::revShimmer),  "Shimmer",      unit(), 0.0f,  percent));
    layout.add (std::make_unique<APF> (pid (ids::revPredelay), "Pre-delay",    skewed (0.0f, 500.0f, 60.0f, 0.1f), 20.0f, ms));
    layout.add (std::make_unique<APF> (pid (ids::revMod),      "Rev Movement", unit(), 0.35f, percent));
    layout.add (std::make_unique<APB> (pid (ids::revFreeze),   "Rev Freeze", false));

    //== post tone =============================================================
    layout.add (std::make_unique<APF> (pid (ids::cutoff),    "Cutoff",    skewed (30.0f, 20000.0f, 1200.0f, 1.0f), 20000.0f, hz));
    layout.add (std::make_unique<APF> (pid (ids::resonance), "Resonance", unit(), 0.2f, percent));

    //== modulation ============================================================
    const StringArray shapes { "Sine", "Triangle", "Ramp", "Square" };

    layout.add (std::make_unique<APF> (pid (ids::lfo1Rate),  "LFO 1 Rate", skewed (0.01f, 20.0f, 0.5f, 0.001f), 0.25f, hz));
    layout.add (std::make_unique<APC> (pid (ids::lfo1Shape), "LFO 1 Shape", shapes, 0));
    layout.add (std::make_unique<APF> (pid (ids::lfo2Rate),  "LFO 2 Rate", skewed (0.01f, 20.0f, 0.5f, 0.001f), 0.11f, hz));
    layout.add (std::make_unique<APC> (pid (ids::lfo2Shape), "LFO 2 Shape", shapes, 1));
    layout.add (std::make_unique<APF> (pid (ids::randRate),  "Random Rate", skewed (0.01f, 20.0f, 0.5f, 0.001f), 0.6f, hz));
    layout.add (std::make_unique<APF> (pid (ids::randSlew),  "Random Glide", unit(), 0.6f, percent));
    layout.add (std::make_unique<APInt> (pid (ids::seed),    "Seed", 0, 99999, 1337));

    // Must stay in the same order as gush::ModSource / gush::ModDest.
    const StringArray sources { "Off", "LFO 1", "LFO 2", "Random", "Envelope" };
    const StringArray dests   { "Off", "Grain Size", "Grain Density", "Spray",
                                "Delay Time", "Pitch", "Feedback", "Res Tune",
                                "Res Damping", "Reverb Size", "Shimmer",
                                "Cutoff", "Mix" };

    for (int slot = 0; slot < gush::ModMatrix::kSlots; ++slot)
    {
        const auto n = juce::String (slot + 1);
        layout.add (std::make_unique<APC> (pid (ids::modSrc (slot)), "Mod " + n + " Source", sources, 0));
        layout.add (std::make_unique<APC> (pid (ids::modDst (slot)), "Mod " + n + " Target", dests,   0));
        layout.add (std::make_unique<APF> (pid (ids::modAmt (slot)), "Mod " + n + " Amount",
                                           FRange (-1.0f, 1.0f, 0.001f), 0.0f, percent));
    }

    return layout;
}

//==============================================================================
std::atomic<float>* ParameterRefs::get (const juce::String& id) const
{
    auto* p = apvts.getRawParameterValue (id);
    jassert (p != nullptr);          // an id in Parameters.h with no parameter behind it
    return p;
}

ParameterRefs::ParameterRefs (juce::AudioProcessorValueTreeState& state)
    : apvts (state)
{
    pInGain    = get (ids::inGain);
    pDrive     = get (ids::drive);
    pMix       = get (ids::mix);
    pOutGain   = get (ids::outGain);

    pDelayTime = get (ids::delayTime);
    pSpeed     = get (ids::speed);
    pRamp      = get (ids::ramp);
    pHeads     = get (ids::heads);
    pFeedback  = get (ids::feedback);
    pTapeTone  = get (ids::tapeTone);
    pPitch     = get (ids::pitch);
    pPitchFb   = get (ids::pitchFb);
    pReverse   = get (ids::reverse);
    pFreeze    = get (ids::freeze);
    pEchoMix   = get (ids::echoMix);

    pGrainMix     = get (ids::grainMix);
    pGrainSize    = get (ids::grainSize);
    pGrainDensity = get (ids::grainDensity);
    pGrainSpray   = get (ids::grainSpray);
    pGrainSpread  = get (ids::grainSpread);
    pGrainJitter  = get (ids::grainJitter);

    pResMix       = get (ids::resMix);
    pResFreq      = get (ids::resFreq);
    pResStructure = get (ids::resStructure);
    pResBright    = get (ids::resBright);
    pResDamp      = get (ids::resDamp);
    pResPos       = get (ids::resPos);

    pRevMix      = get (ids::revMix);
    pRevSize     = get (ids::revSize);
    pRevDecay    = get (ids::revDecay);
    pRevDamp     = get (ids::revDamp);
    pRevShimmer  = get (ids::revShimmer);
    pRevPredelay = get (ids::revPredelay);
    pRevMod      = get (ids::revMod);
    pRevFreeze   = get (ids::revFreeze);

    pCutoff    = get (ids::cutoff);
    pResonance = get (ids::resonance);

    pLfo1Rate  = get (ids::lfo1Rate);
    pLfo1Shape = get (ids::lfo1Shape);
    pLfo2Rate  = get (ids::lfo2Rate);
    pLfo2Shape = get (ids::lfo2Shape);
    pRandRate  = get (ids::randRate);
    pRandSlew  = get (ids::randSlew);
    pSeed      = get (ids::seed);

    for (int i = 0; i < gush::ModMatrix::kSlots; ++i)
    {
        pModSrc[i] = get (ids::modSrc (i));
        pModDst[i] = get (ids::modDst (i));
        pModAmt[i] = get (ids::modAmt (i));
    }
}

//==============================================================================
gush::EngineParams ParameterRefs::read() const noexcept
{
    gush::EngineParams p;

    p.inputGainDb  = pInGain->load();
    p.drive        = pDrive->load();
    p.mix          = pMix->load();
    p.outputGainDb = pOutGain->load();

    p.delaySeconds  = pDelayTime->load() * 0.001f;
    p.speed         = pSpeed->load();
    p.glideSeconds  = pRamp->load();
    p.headPattern   = (int) pHeads->load();
    p.feedback      = pFeedback->load();
    p.tapeTone      = pTapeTone->load();
    p.pitchSemis    = pPitch->load();
    p.pitchFeedback = pPitchFb->load();
    p.reverse       = pReverse->load() > 0.5f;
    p.freeze        = pFreeze->load() > 0.5f;
    p.echoMix       = pEchoMix->load();

    p.grainMix     = pGrainMix->load();
    p.grainSizeMs  = pGrainSize->load();
    p.grainDensity = pGrainDensity->load();
    p.grainSpray   = pGrainSpray->load();
    p.grainSpread  = pGrainSpread->load();
    p.grainJitter  = pGrainJitter->load();

    p.resMix        = pResMix->load();
    p.resFreq       = pResFreq->load();
    p.resStructure  = pResStructure->load();
    p.resBrightness = pResBright->load();
    p.resDamping    = pResDamp->load();
    p.resPosition   = pResPos->load();

    p.revMix        = pRevMix->load();
    p.revSize       = pRevSize->load();
    p.revDecay      = pRevDecay->load();
    p.revDamping    = pRevDamp->load();
    p.revShimmer    = pRevShimmer->load();
    p.revPredelayMs = pRevPredelay->load();
    p.revMod        = pRevMod->load();
    p.revFreeze     = pRevFreeze->load() > 0.5f;

    p.cutoff    = pCutoff->load();
    p.resonance = pResonance->load();

    p.lfo1Rate  = pLfo1Rate->load();
    p.lfo1Shape = (int) pLfo1Shape->load();
    p.lfo2Rate  = pLfo2Rate->load();
    p.lfo2Shape = (int) pLfo2Shape->load();
    p.randRate  = pRandRate->load();
    p.randSlew  = pRandSlew->load();

    // Seed 0 is a legal knob position but an illegal xorshift state.
    p.seed = (uint32_t) juce::jmax (1, (int) pSeed->load());

    for (int i = 0; i < gush::ModMatrix::kSlots; ++i)
    {
        p.modSource[i] = (int) pModSrc[i]->load();
        p.modDest[i]   = (int) pModDst[i]->load();
        p.modAmount[i] = pModAmt[i]->load();
    }

    return p;
}
