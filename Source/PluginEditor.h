#pragma once

#include "PluginProcessor.h"
#include "GUI/CustomLookAndFeel.h"
#include "GUI/RotarySlider.h"
#include "GUI/GoniometerComponent.h"
#include "GUI/CorrelationMeter.h"

//==============================================================================
/**
    A fully featured GUI Editor for the ShredWidth Audio Processor.
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
    void updateABButtonStates();
    void updateLatencyButtonStates();
    void applyPreset (int id);

    ShredWidthAudioProcessor& audioProcessor;

    // Custom Look and Feel
    CustomLookAndFeel lookAndFeel;

    // Header Components
    juce::ComboBox presetComboBox;
    juce::TextButton aButton { "A" };
    juce::TextButton bButton { "B" };
    juce::TextButton copyButton { "Copy" };
    juce::TextButton bypassButton { "BYPASS" };
    bool lastBypassedState = false;

    // Left Column: Detune & Width
    RotarySlider detuneSlider { "Detune Amount", " cents", false };
    RotarySlider widthSlider { "Stereo Width", " %", false };
    RotarySlider mixSlider { "Mix", " %", false };
    RotarySlider outTrimSlider { "Output Trim", " dB", false };

    // Center Column: Delay Offset, Crossover & Latency Mode
    RotarySlider delayLSlider { "Delay Offset L", " ms", false };
    RotarySlider delayRSlider { "Delay Offset R", " ms", false };
    RotarySlider monoXoverSlider { "Low Mono Crossover", " Hz", false };
    
    juce::Label latencyLabel { "latencyLabel", "LATENCY MODE" };
    juce::TextButton studioButton { "STUDIO" };
    juce::TextButton liveButton { "LIVE" };

    // Right Column: Meters
    GoniometerComponent goniometer;
    CorrelationMeter correlationMeter;

    // APVTS Attachments
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> detuneAttachment;
    std::unique_ptr<SliderAttachment> widthAttachment;
    std::unique_ptr<SliderAttachment> mixAttachment;
    std::unique_ptr<SliderAttachment> outTrimAttachment;
    std::unique_ptr<SliderAttachment> delayLAttachment;
    std::unique_ptr<SliderAttachment> delayRAttachment;
    std::unique_ptr<SliderAttachment> monoXoverAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;

    // Resize limits and corner resizer
    std::unique_ptr<juce::ResizableCornerComponent> resizer;
    juce::ComponentBoundsConstrainer resizeLimits;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShredWidthAudioProcessorEditor)
};
