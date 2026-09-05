/*
    PluginProcessor.h
    ---------------------------------------------------------------------------
    The JUCE shell. Deliberately thin: it owns the parameter tree, converts it
    into a plain struct once per block, and hands two float pointers to the
    engine. Every decision that affects the sound lives in Source/DSP.
*/

#pragma once

#include "DSP/Engine.h"
#include "Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>

class GushAudioProcessor : public juce::AudioProcessor
{
public:
    GushAudioProcessor();
    ~GushAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }

    // Long enough for a 60 s reverb decay; hosts use this to decide how long
    // to keep rendering after the last note.
    double getTailLengthSeconds() const override { return 60.0; }

    int  getNumPrograms() override    { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    /** For a future visualiser: how many grains are sounding right now. */
    int getActiveGrainCount() const noexcept { return engine.activeGrainCount(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    gush::Engine  engine;
    ParameterRefs refs;

    // Only used when the host gives us a mono output bus.
    juce::AudioBuffer<float> scratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GushAudioProcessor)
};
