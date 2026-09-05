/*
    Parameters.h
    ---------------------------------------------------------------------------
    The single place every parameter is declared.

    Two jobs:
      1. Build the APVTS layout the host sees (createParameterLayout).
      2. Hand the audio thread a lock-free snapshot of it (ParameterRefs),
         as a plain gush::EngineParams struct the DSP understands.

    ParameterRefs caches the atomic pointers once, at construction. Looking a
    parameter up by string on the audio thread is a hash lookup per parameter
    per block; caching turns that into a pointer dereference.
*/

#pragma once

#include "DSP/Engine.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace ids
{
    // input / output
    inline constexpr auto inGain    = "inGain";
    inline constexpr auto drive     = "drive";
    inline constexpr auto mix       = "mix";
    inline constexpr auto outGain   = "outGain";

    // granular tape echo
    inline constexpr auto delayTime = "delayTime";
    inline constexpr auto speed     = "speed";
    inline constexpr auto ramp      = "ramp";
    inline constexpr auto heads     = "heads";
    inline constexpr auto feedback  = "feedback";
    inline constexpr auto tapeTone  = "tapeTone";
    inline constexpr auto pitch     = "pitch";
    inline constexpr auto pitchFb   = "pitchFb";
    inline constexpr auto reverse   = "reverse";
    inline constexpr auto freeze    = "freeze";
    inline constexpr auto echoMix   = "echoMix";

    // grains
    inline constexpr auto grainMix     = "grainMix";
    inline constexpr auto grainSize    = "grainSize";
    inline constexpr auto grainDensity = "grainDensity";
    inline constexpr auto grainSpray   = "grainSpray";
    inline constexpr auto grainSpread  = "grainSpread";
    inline constexpr auto grainJitter  = "grainJitter";

    // resonator
    inline constexpr auto resMix       = "resMix";
    inline constexpr auto resFreq      = "resFreq";
    inline constexpr auto resStructure = "resStructure";
    inline constexpr auto resBright    = "resBright";
    inline constexpr auto resDamp      = "resDamp";
    inline constexpr auto resPos       = "resPos";

    // reverb
    inline constexpr auto revMix      = "revMix";
    inline constexpr auto revSize     = "revSize";
    inline constexpr auto revDecay    = "revDecay";
    inline constexpr auto revDamp     = "revDamp";
    inline constexpr auto revShimmer  = "revShimmer";
    inline constexpr auto revPredelay = "revPredelay";
    inline constexpr auto revMod      = "revMod";
    inline constexpr auto revFreeze   = "revFreeze";

    // post tone
    inline constexpr auto cutoff    = "cutoff";
    inline constexpr auto resonance = "resonance";

    // modulation
    inline constexpr auto lfo1Rate  = "lfo1Rate";
    inline constexpr auto lfo1Shape = "lfo1Shape";
    inline constexpr auto lfo2Rate  = "lfo2Rate";
    inline constexpr auto lfo2Shape = "lfo2Shape";
    inline constexpr auto randRate  = "randRate";
    inline constexpr auto randSlew  = "randSlew";
    inline constexpr auto seed      = "seed";

    // four modulation slots: modSrc1..4, modDst1..4, modAmt1..4
    inline juce::String modSrc (int slot) { return "modSrc" + juce::String (slot + 1); }
    inline juce::String modDst (int slot) { return "modDst" + juce::String (slot + 1); }
    inline juce::String modAmt (int slot) { return "modAmt" + juce::String (slot + 1); }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

//==============================================================================
/** Cached, lock-free access to every parameter, and the conversion into the
    engine's own struct. Constructed once; read once per audio block. */
class ParameterRefs
{
public:
    explicit ParameterRefs (juce::AudioProcessorValueTreeState& state);

    /** Audio thread. No allocation, no locks, no string lookups. */
    gush::EngineParams read() const noexcept;

private:
    std::atomic<float>* get (const juce::String& id) const;

    juce::AudioProcessorValueTreeState& apvts;

    std::atomic<float>* pInGain    = nullptr;
    std::atomic<float>* pDrive     = nullptr;
    std::atomic<float>* pMix       = nullptr;
    std::atomic<float>* pOutGain   = nullptr;

    std::atomic<float>* pDelayTime = nullptr;
    std::atomic<float>* pSpeed     = nullptr;
    std::atomic<float>* pRamp      = nullptr;
    std::atomic<float>* pHeads     = nullptr;
    std::atomic<float>* pFeedback  = nullptr;
    std::atomic<float>* pTapeTone  = nullptr;
    std::atomic<float>* pPitch     = nullptr;
    std::atomic<float>* pPitchFb   = nullptr;
    std::atomic<float>* pReverse   = nullptr;
    std::atomic<float>* pFreeze    = nullptr;
    std::atomic<float>* pEchoMix   = nullptr;

    std::atomic<float>* pGrainMix     = nullptr;
    std::atomic<float>* pGrainSize    = nullptr;
    std::atomic<float>* pGrainDensity = nullptr;
    std::atomic<float>* pGrainSpray   = nullptr;
    std::atomic<float>* pGrainSpread  = nullptr;
    std::atomic<float>* pGrainJitter  = nullptr;

    std::atomic<float>* pResMix       = nullptr;
    std::atomic<float>* pResFreq      = nullptr;
    std::atomic<float>* pResStructure = nullptr;
    std::atomic<float>* pResBright    = nullptr;
    std::atomic<float>* pResDamp      = nullptr;
    std::atomic<float>* pResPos       = nullptr;

    std::atomic<float>* pRevMix      = nullptr;
    std::atomic<float>* pRevSize     = nullptr;
    std::atomic<float>* pRevDecay    = nullptr;
    std::atomic<float>* pRevDamp     = nullptr;
    std::atomic<float>* pRevShimmer  = nullptr;
    std::atomic<float>* pRevPredelay = nullptr;
    std::atomic<float>* pRevMod      = nullptr;
    std::atomic<float>* pRevFreeze   = nullptr;

    std::atomic<float>* pCutoff    = nullptr;
    std::atomic<float>* pResonance = nullptr;

    std::atomic<float>* pLfo1Rate  = nullptr;
    std::atomic<float>* pLfo1Shape = nullptr;
    std::atomic<float>* pLfo2Rate  = nullptr;
    std::atomic<float>* pLfo2Shape = nullptr;
    std::atomic<float>* pRandRate  = nullptr;
    std::atomic<float>* pRandSlew  = nullptr;
    std::atomic<float>* pSeed      = nullptr;

    std::atomic<float>* pModSrc[gush::ModMatrix::kSlots] { nullptr, nullptr, nullptr, nullptr };
    std::atomic<float>* pModDst[gush::ModMatrix::kSlots] { nullptr, nullptr, nullptr, nullptr };
    std::atomic<float>* pModAmt[gush::ModMatrix::kSlots] { nullptr, nullptr, nullptr, nullptr };
};
