#pragma once

#include "PluginProcessor.h"

//==============================================================================
/**
    A skeletal PluginEditor for ShredWidth.
*/
class ShredWidthAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer
{
public:
    explicit ShredWidthAudioProcessorEditor (ShredWidthAudioProcessor&);
    ~ShredWidthAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    ShredWidthAudioProcessor& audioProcessor;

    std::unique_ptr<juce::ResizableCornerComponent> resizer;
    juce::ComponentBoundsConstrainer resizeLimits;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShredWidthAudioProcessorEditor)
};
