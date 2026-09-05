/*
    PluginEditor.h
    ---------------------------------------------------------------------------
    A working interface, not the final face.

    Every parameter is exposed through JUCE's generic editor, which means the
    plugin is fully playable and automatable in a DAW today. The designed
    front panel is a separate job; nothing in Source/DSP depends on it.
*/

#pragma once

#include "PluginProcessor.h"

class GushAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit GushAudioProcessorEditor (GushAudioProcessor&);
    ~GushAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    GushAudioProcessor& proc;
    juce::GenericAudioProcessorEditor generic;
    juce::Label title, subtitle;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GushAudioProcessorEditor)
};
