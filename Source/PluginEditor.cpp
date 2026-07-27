#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
ShredWidthAudioProcessorEditor::ShredWidthAudioProcessorEditor (ShredWidthAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Set default size
    setSize (800, 480);

    // Set resizability limits and configure custom ResizableCornerComponent
    setResizable (true, false);
    resizeLimits.setMinimumWidth (600);
    resizeLimits.setMinimumHeight (360);
    resizeLimits.setMaximumWidth (1600);
    resizeLimits.setMaximumHeight (960);
    setConstrainer (&resizeLimits);

    resizer = std::make_unique<juce::ResizableCornerComponent> (this, &resizeLimits);
    addAndMakeVisible (resizer.get());

    // Start timer for GUI metering/updates (30 - 60 Hz)
    startTimerHz (30);
}

ShredWidthAudioProcessorEditor::~ShredWidthAudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
void ShredWidthAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Dark premium background gradient
    juce::ColourGradient grad (
        juce::Colour (0xff0f1219), 0.0f, 0.0f,
        juce::Colour (0xff080a0f), 0.0f, static_cast<float> (getHeight()),
        false
    );
    g.setGradientFill (grad);
    g.fillAll();

    // Draw header panel title
    g.setColour (juce::Colour (0xff00e5ff));
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("SHREDWIDTH", 20, 15, 150, 24, juce::Justification::left);

    g.setColour (juce::Colour (0xff8f9ba8));
    g.setFont (juce::FontOptions (11.0f, juce::Font::plain));
    g.drawText ("LEAD WIDENER & DETUNER", 145, 20, 300, 16, juce::Justification::left);
}

void ShredWidthAudioProcessorEditor::resized()
{
    if (resizer != nullptr)
    {
        resizer->setBounds (getWidth() - 16, getHeight() - 16, 16, 16);
    }
}

void ShredWidthAudioProcessorEditor::timerCallback()
{
    // Lightweight timer callback (phase A placeholder)
}
