#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParameterIDs.h"

//==============================================================================
ShredWidthAudioProcessorEditor::ShredWidthAudioProcessorEditor (ShredWidthAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p),
      goniometer (&p.getEngine().getMeterTap()),
      correlationMeter (&p.getEngine().getMeterTap())
{
    // Apply custom Look and Feel
    setLookAndFeel (&lookAndFeel);

    // 1. Setup Preset ComboBox
    addAndMakeVisible (presetComboBox);
    presetComboBox.addItem ("Presets", 1);
    presetComboBox.addItem ("Tight Lead", 2);
    presetComboBox.addItem ("Wide Lead", 3);
    presetComboBox.addItem ("Huge Solo", 4);
    presetComboBox.addItem ("Mono-Safe Subtle", 5);
    presetComboBox.addItem ("Live Tracking", 6);
    presetComboBox.setSelectedId (1, juce::dontSendNotification);
    presetComboBox.onChange = [this] {
        auto id = presetComboBox.getSelectedId();
        if (id > 1)
            applyPreset (id);
    };

    // 2. Setup A/B Buttons
    addAndMakeVisible (aButton);
    addAndMakeVisible (bButton);
    addAndMakeVisible (copyButton);
    
    aButton.onClick = [this] {
        audioProcessor.getStateManager().switchActiveSlot (StateManager::Slot::A, audioProcessor.getAPVTS());
        updateABButtonStates();
    };
    bButton.onClick = [this] {
        audioProcessor.getStateManager().switchActiveSlot (StateManager::Slot::B, audioProcessor.getAPVTS());
        updateABButtonStates();
    };
    copyButton.onClick = [this] {
        auto& stateManager = audioProcessor.getStateManager();
        if (stateManager.getActiveSlot() == StateManager::Slot::A)
            stateManager.copyAtoB();
        else
            stateManager.copyBtoA();
        updateABButtonStates();
    };

    // 3. Setup Bypass Button
    addAndMakeVisible (bypassButton);

    // Style buttons to fit the dark theme
    auto setButtonColors = [] (juce::TextButton& btn, juce::Colour accent) {
        btn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff141923));
        btn.setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.4f));
        btn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe2e8f0));
        btn.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    };

    setButtonColors (aButton, CustomLookAndFeel::accentCyan);
    setButtonColors (bButton, CustomLookAndFeel::accentCyan);
    setButtonColors (copyButton, CustomLookAndFeel::accentCyan);
    setButtonColors (studioButton, CustomLookAndFeel::accentOrange);
    setButtonColors (liveButton, CustomLookAndFeel::accentOrange);
    setButtonColors (bypassButton, juce::Colour (0xffff3b30));

    // 4. Setup Sliders
    addAndMakeVisible (detuneSlider);
    addAndMakeVisible (widthSlider);
    addAndMakeVisible (mixSlider);
    addAndMakeVisible (outTrimSlider);
    addAndMakeVisible (delayLSlider);
    addAndMakeVisible (delayRSlider);
    addAndMakeVisible (monoXoverSlider);

    // 5. Setup Latency Section
    addAndMakeVisible (latencyLabel);
    latencyLabel.setJustificationType (juce::Justification::centred);
    latencyLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::shredMutedText);
    latencyLabel.setFont (juce::Font (juce::FontOptions{}.withHeight (11.0f).withStyle ("Bold")));

    addAndMakeVisible (studioButton);
    addAndMakeVisible (liveButton);
    
    studioButton.onClick = [this] {
        if (auto* param = audioProcessor.getAPVTS().getParameter (ShredWidth::ParameterIDs::latencyMode))
            param->setValueNotifyingHost (0.0f);
        updateLatencyButtonStates();
    };
    liveButton.onClick = [this] {
        if (auto* param = audioProcessor.getAPVTS().getParameter (ShredWidth::ParameterIDs::latencyMode))
            param->setValueNotifyingHost (1.0f);
        updateLatencyButtonStates();
    };

    // 6. Setup Meters
    addAndMakeVisible (goniometer);
    addAndMakeVisible (correlationMeter);

    // 7. Establish APVTS Attachments
    auto& apvts = audioProcessor.getAPVTS();
    detuneAttachment = std::make_unique<SliderAttachment> (apvts, ShredWidth::ParameterIDs::detuneAmount, detuneSlider);
    widthAttachment  = std::make_unique<SliderAttachment> (apvts, ShredWidth::ParameterIDs::width, widthSlider);
    mixAttachment    = std::make_unique<SliderAttachment> (apvts, ShredWidth::ParameterIDs::mix, mixSlider);
    outTrimAttachment = std::make_unique<SliderAttachment> (apvts, ShredWidth::ParameterIDs::outTrim, outTrimSlider);
    delayLAttachment  = std::make_unique<SliderAttachment> (apvts, ShredWidth::ParameterIDs::delayL, delayLSlider);
    delayRAttachment  = std::make_unique<SliderAttachment> (apvts, ShredWidth::ParameterIDs::delayR, delayRSlider);
    monoXoverAttachment = std::make_unique<SliderAttachment> (apvts, ShredWidth::ParameterIDs::monoXover, monoXoverSlider);
    bypassAttachment = std::make_unique<ButtonAttachment> (apvts, ShredWidth::ParameterIDs::bypass, bypassButton);

    // 8. Configure Resizing
    setResizable (true, false);
    resizeLimits.setMinimumWidth (600);
    resizeLimits.setMinimumHeight (360);
    resizeLimits.setMaximumWidth (1600);
    resizeLimits.setMaximumHeight (960);
    resizeLimits.setFixedAspectRatio (5.0 / 3.0);
    setConstrainer (&resizeLimits);

    resizer = std::make_unique<juce::ResizableCornerComponent> (this, &resizeLimits);
    addAndMakeVisible (resizer.get());

    // 9. Restore Bounds from ValueTree
    auto& state = apvts.state;
    int initialWidth = 800;
    int initialHeight = 480;
    if (state.isValid())
    {
        initialWidth = state.getProperty ("editorWidth", 800);
        initialHeight = state.getProperty ("editorHeight", 480);
    }
    setSize (initialWidth, initialHeight);

    // Initialize States
    updateABButtonStates();
    updateLatencyButtonStates();
    lastBypassedState = apvts.getRawParameterValue (ShredWidth::ParameterIDs::bypass)->load() >= 0.5f;

    // Start UI update timer (30-60 Hz)
    startTimerHz (40);
}

ShredWidthAudioProcessorEditor::~ShredWidthAudioProcessorEditor()
{
    // Save window size on shutdown to APVTS ValueTree state
    auto& state = audioProcessor.getAPVTS().state;
    if (state.isValid())
    {
        state.setProperty ("editorWidth", getWidth(), nullptr);
        state.setProperty ("editorHeight", getHeight(), nullptr);
    }

    setLookAndFeel (nullptr);
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

    // Draw header divider line
    auto headerH = getHeight() * 0.125f;
    g.setColour (CustomLookAndFeel::shredOutline);
    g.drawHorizontalLine (static_cast<int> (headerH), 0.0f, static_cast<float> (getWidth()));

    // Draw title and subtitle
    g.setColour (CustomLookAndFeel::accentCyan);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("SHREDWIDTH", 20, 12, 150, 20, juce::Justification::left);

    g.setColour (CustomLookAndFeel::shredMutedText);
    g.setFont (juce::FontOptions (9.0f, juce::Font::plain).withStyle("Bold"));
    g.drawText ("LEAD WIDENER & DETUNER", 20, 32, 200, 14, juce::Justification::left);

    // Draw columns borders/panels for beautiful structured feel
    auto margin = 10;
    auto availableWidth = getWidth() - (2 * margin) - 20;
    auto colW = availableWidth / 3;
    auto startY = static_cast<int> (headerH) + 15;
    auto colH = getHeight() - startY - 15;

    auto drawColumnPanel = [&g, startY, colH] (int x, int width, const juce::String& title, juce::Colour headerColor) {
        // Panel background (slightly lighter than main bg)
        g.setColour (CustomLookAndFeel::shredPanelBg.withAlpha (0.4f));
        g.fillRoundedRectangle (x, startY, width, colH, 6.0f);
        
        // Panel outline
        g.setColour (CustomLookAndFeel::shredOutline);
        g.drawRoundedRectangle (x, startY, width, colH, 6.0f, 1.5f);

        // Header background band
        g.setColour (CustomLookAndFeel::shredPanelBg);
        g.fillRoundedRectangle (x, startY, width, 25, 6.0f);
        g.fillRect (x, startY + 20, width, 5); // flatten bottom of the header band

        // Divider under column title
        g.setColour (CustomLookAndFeel::shredOutline);
        g.drawHorizontalLine (startY + 25, static_cast<float> (x), static_cast<float> (x + width));

        // Column title
        g.setColour (headerColor);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (title, x, startY, width, 25, juce::Justification::centred);
    };

    int leftX = 10;
    int centerX = leftX + colW + margin;
    int rightX = centerX + colW + margin;

    drawColumnPanel (leftX, colW, "DETUNE & WIDTH", CustomLookAndFeel::accentCyan);
    drawColumnPanel (centerX, colW, "DELAY & CROSSOVER", CustomLookAndFeel::accentOrange);
    drawColumnPanel (rightX, colW, "STAGE MONITORING", CustomLookAndFeel::shredText);

    // Draw bypass LED
    auto bypassX = getWidth() - 110;
    auto ledRect = juce::Rectangle<float> (bypassX - 22.0f, 23.0f, 14.0f, 14.0f);
    
    const bool bypassed = audioProcessor.getAPVTS().getRawParameterValue (ShredWidth::ParameterIDs::bypass)->load() >= 0.5f;
    juce::Colour ledColor = bypassed ? juce::Colour (0xffff3b30) : juce::Colour (0xff34c759); // Red for bypassed, Green for active
    
    // Glow effect
    g.setColour (ledColor.withAlpha (0.25f));
    g.fillEllipse (ledRect.expanded (3.0f));
    
    // Core LED
    g.setColour (ledColor);
    g.fillEllipse (ledRect);
    
    // Glossy highlight
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.fillEllipse (ledRect.getX() + 3.0f, ledRect.getY() + 3.0f, 4.0f, 4.0f);
}

void ShredWidthAudioProcessorEditor::resized()
{
    // Save size dynamically when resized
    auto& state = audioProcessor.getAPVTS().state;
    if (state.isValid())
    {
        state.setProperty ("editorWidth", getWidth(), nullptr);
        state.setProperty ("editorHeight", getHeight(), nullptr);
    }

    if (resizer != nullptr)
    {
        resizer->setBounds (getWidth() - 16, getHeight() - 16, 16, 16);
    }

    auto headerH = static_cast<int> (getHeight() * 0.125f);

    // Header components layout
    presetComboBox.setBounds (static_cast<int> (getWidth() * 0.32f), (headerH - 30) / 2, 140, 30);

    auto abX = static_cast<int> (getWidth() * 0.52f);
    aButton.setBounds (abX, (headerH - 30) / 2, 30, 30);
    bButton.setBounds (abX + 35, (headerH - 30) / 2, 30, 30);
    copyButton.setBounds (abX + 70, (headerH - 30) / 2, 95, 30);

    auto bypassX = getWidth() - 110;
    bypassButton.setBounds (bypassX, (headerH - 30) / 2, 80, 30);

    // Three-column layout
    auto margin = 10;
    auto availableWidth = getWidth() - (2 * margin) - 20;
    auto colW = availableWidth / 3;
    auto startY = static_cast<int> (headerH) + 15;
    auto colH = getHeight() - startY - 15;
    auto innerY = startY + 30;
    auto innerH = colH - 40;

    int leftX = 10;
    int centerX = leftX + colW + margin;
    int rightX = centerX + colW + margin;

    // --- Left Column ---
    detuneSlider.setBounds (leftX + 10, innerY, colW - 20, static_cast<int> (innerH * 0.55f));
    
    auto bottomY = innerY + static_cast<int> (innerH * 0.58f);
    auto bottomH = innerH - static_cast<int> (innerH * 0.58f);
    auto subW = (colW - 10) / 3;
    widthSlider.setBounds (leftX + 5, bottomY, subW, bottomH);
    mixSlider.setBounds (leftX + 5 + subW, bottomY, subW, bottomH);
    outTrimSlider.setBounds (leftX + 5 + 2 * subW, bottomY, subW, bottomH);

    // --- Center Column ---
    auto topH = static_cast<int> (innerH * 0.45f);
    auto halfW = (colW - 10) / 2;
    delayLSlider.setBounds (centerX + 5, innerY, halfW, topH);
    delayRSlider.setBounds (centerX + 5 + halfW, innerY, halfW, topH);

    auto centerBottomY = innerY + topH + 10;
    auto centerBottomH = innerH - topH - 10;
    monoXoverSlider.setBounds (centerX + 5, centerBottomY, halfW, centerBottomH);

    auto rightSideX = centerX + 5 + halfW;
    auto rightSideW = halfW;
    auto buttonH = 26;
    auto buttonY = centerBottomY + (centerBottomH - 2 * buttonH - 25) / 2;
    latencyLabel.setBounds (rightSideX, buttonY, rightSideW, 18);
    studioButton.setBounds (rightSideX, buttonY + 20, rightSideW, buttonH);
    liveButton.setBounds (rightSideX, buttonY + 20 + buttonH + 4, rightSideW, buttonH);

    // --- Right Column ---
    auto gonioSize = juce::jmin (colW - 20, innerH - 55);
    auto gonioX = rightX + (colW - gonioSize) / 2;
    goniometer.setBounds (gonioX, innerY + 5, gonioSize, gonioSize);
    correlationMeter.setBounds (gonioX, innerY + 5 + gonioSize + 15, gonioSize, 28);
}

//==============================================================================
void ShredWidthAudioProcessorEditor::timerCallback()
{
    goniometer.update();
    correlationMeter.update();

    updateABButtonStates();
    updateLatencyButtonStates();

    // Check for bypass LED repaint only when changed
    const bool bypassed = audioProcessor.getAPVTS().getRawParameterValue (ShredWidth::ParameterIDs::bypass)->load() >= 0.5f;
    if (bypassed != lastBypassedState)
    {
        lastBypassedState = bypassed;
        repaint();
    }
}

void ShredWidthAudioProcessorEditor::updateABButtonStates()
{
    const auto activeSlot = audioProcessor.getStateManager().getActiveSlot();
    aButton.setToggleState (activeSlot == StateManager::Slot::A, juce::dontSendNotification);
    bButton.setToggleState (activeSlot == StateManager::Slot::B, juce::dontSendNotification);

    if (activeSlot == StateManager::Slot::A)
        copyButton.setButtonText ("Copy A \u27a1 B");
    else
        copyButton.setButtonText ("Copy B \u27a1 A");
}

void ShredWidthAudioProcessorEditor::updateLatencyButtonStates()
{
    if (auto* param = audioProcessor.getAPVTS().getParameter (ShredWidth::ParameterIDs::latencyMode))
    {
        const bool isLive = param->getValue() >= 0.5f;
        studioButton.setToggleState (! isLive, juce::dontSendNotification);
        liveButton.setToggleState (isLive, juce::dontSendNotification);
    }
}

void ShredWidthAudioProcessorEditor::applyPreset (int id)
{
    auto& apvts = audioProcessor.getAPVTS();
    auto setParam = [&apvts] (const juce::String& paramId, float value) {
        if (auto* param = apvts.getParameter (paramId))
        {
            auto range = apvts.getParameterRange (paramId);
            param->setValueNotifyingHost (range.convertTo0to1 (value));
        }
    };

    switch (id)
    {
        case 2: // Tight Lead
            setParam (ShredWidth::ParameterIDs::detuneAmount, 5.0f);
            setParam (ShredWidth::ParameterIDs::delayL, 10.0f);
            setParam (ShredWidth::ParameterIDs::delayR, 14.0f);
            setParam (ShredWidth::ParameterIDs::monoXover, 200.0f);
            setParam (ShredWidth::ParameterIDs::width, 90.0f);
            setParam (ShredWidth::ParameterIDs::mix, 30.0f);
            break;
        case 3: // Wide Lead
            setParam (ShredWidth::ParameterIDs::detuneAmount, 7.0f);
            setParam (ShredWidth::ParameterIDs::delayL, 12.5f);
            setParam (ShredWidth::ParameterIDs::delayR, 18.0f);
            setParam (ShredWidth::ParameterIDs::monoXover, 180.0f);
            setParam (ShredWidth::ParameterIDs::width, 100.0f);
            setParam (ShredWidth::ParameterIDs::mix, 40.0f);
            break;
        case 4: // Huge Solo
            setParam (ShredWidth::ParameterIDs::detuneAmount, 14.0f);
            setParam (ShredWidth::ParameterIDs::delayL, 20.0f);
            setParam (ShredWidth::ParameterIDs::delayR, 28.0f);
            setParam (ShredWidth::ParameterIDs::monoXover, 150.0f);
            setParam (ShredWidth::ParameterIDs::width, 140.0f);
            setParam (ShredWidth::ParameterIDs::mix, 55.0f);
            break;
        case 5: // Mono-Safe Subtle
            setParam (ShredWidth::ParameterIDs::detuneAmount, 4.0f);
            setParam (ShredWidth::ParameterIDs::delayL, 8.0f);
            setParam (ShredWidth::ParameterIDs::delayR, 11.0f);
            setParam (ShredWidth::ParameterIDs::monoXover, 250.0f);
            setParam (ShredWidth::ParameterIDs::width, 70.0f);
            setParam (ShredWidth::ParameterIDs::mix, 25.0f);
            break;
        case 6: // Live Tracking
            setParam (ShredWidth::ParameterIDs::detuneAmount, 6.0f);
            setParam (ShredWidth::ParameterIDs::delayL, 10.0f);
            setParam (ShredWidth::ParameterIDs::delayR, 13.0f);
            setParam (ShredWidth::ParameterIDs::monoXover, 200.0f);
            setParam (ShredWidth::ParameterIDs::width, 90.0f);
            setParam (ShredWidth::ParameterIDs::mix, 35.0f);
            setParam (ShredWidth::ParameterIDs::latencyMode, 1.0f);
            break;
        default:
            break;
    }

    // Reset selection to default indicator
    presetComboBox.setSelectedId (1, juce::dontSendNotification);
}
