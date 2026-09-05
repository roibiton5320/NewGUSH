#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
GushAudioProcessor::GushAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "GUSH", createParameterLayout()),
      refs (apvts)
{
}

//==============================================================================
void GushAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    engine.reset();

    // Allocated here so processBlock never has to.
    scratch.setSize (2, juce::jmax (1, samplesPerBlock));
    scratch.clear();

    // The chain is zero latency: the saturator is antiderivative anti-aliased
    // rather than oversampled, and the pitch shifters are time domain.
    setLatencySamples (0);
}

void GushAudioProcessor::releaseResources()
{
    engine.reset();
}

bool GushAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in  = layouts.getMainInputChannelSet();

    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;

    // Mono in / stereo out is allowed: a guitar goes in, a stereo field
    // comes out, which is the whole point.
    if (in != out && in != juce::AudioChannelSet::mono())
        return false;

    return true;
}

//==============================================================================
void GushAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn      = getTotalNumInputChannels();
    const int numOut     = getTotalNumOutputChannels();

    if (numSamples <= 0 || numOut <= 0)
        return;

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    // One lock-free read of the whole parameter tree, once per block.
    engine.setParameters (refs.read());

    if (numOut >= 2)
    {
        // Mono source into a stereo bus: duplicate before processing, so the
        // engine's stereo grain spread and reverb have something to work with.
        if (numIn < 2)
            buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

        engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);

        for (int ch = 2; ch < numOut; ++ch)
            buffer.clear (ch, 0, numSamples);
    }
    else
    {
        // Mono out. Run the engine in stereo anyway and fold down, rather than
        // running a different, untested mono path.
        if (scratch.getNumSamples() < numSamples)
            scratch.setSize (2, numSamples, false, false, true);

        scratch.copyFrom (0, 0, buffer, 0, 0, numSamples);
        scratch.copyFrom (1, 0, buffer, 0, 0, numSamples);

        engine.process (scratch.getWritePointer (0), scratch.getWritePointer (1), numSamples);

        buffer.copyFrom (0, 0, scratch, 0, 0, numSamples);
        buffer.addFrom  (0, 0, scratch, 1, 0, numSamples);
        buffer.applyGain (0, 0, numSamples, 0.5f);
    }
}

//==============================================================================
juce::AudioProcessorEditor* GushAudioProcessor::createEditor()
{
    return new GushAudioProcessorEditor (*this);
}

void GushAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void GushAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GushAudioProcessor();
}
