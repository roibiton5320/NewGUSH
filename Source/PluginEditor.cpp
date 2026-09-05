#include "PluginEditor.h"

GushAudioProcessorEditor::GushAudioProcessorEditor (GushAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), generic (p)
{
    title.setText ("GUSH", juce::dontSendNotification);
    title.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    title.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (title);

    subtitle.setText ("granular tape echo / resonator / shimmer", juce::dontSendNotification);
    subtitle.setFont (juce::FontOptions (13.0f));
    subtitle.setJustificationType (juce::Justification::centredRight);
    subtitle.setAlpha (0.6f);
    addAndMakeVisible (subtitle);

    addAndMakeVisible (generic);

    setResizable (true, true);
    setResizeLimits (460, 400, 1200, 1400);
    setSize (560, 760);
}

void GushAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff14161a));
    g.setColour (juce::Colour (0xff2a2f38));
    g.drawLine (12.0f, 52.0f, (float) getWidth() - 12.0f, 52.0f, 1.0f);
}

void GushAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (12);
    auto header = area.removeFromTop (40);

    title.setBounds (header.removeFromLeft (140));
    subtitle.setBounds (header);

    area.removeFromTop (8);
    generic.setBounds (area);
}
