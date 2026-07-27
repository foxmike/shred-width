#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParameterIDs.h"

#include <atomic>
#include <cmath>
#include <memory>

namespace {
constexpr auto stateTreeType = "ShredWidthState";
constexpr auto parameterLayoutVersionProperty = "parameterLayoutVersion";

juce::ParameterID parameterID(const char *id) {
  return {id, ShredWidth::ParameterIDs::parameterLayoutVersion};
}

juce::NormalisableRange<float> linearRange(float minimum, float maximum,
                                           float interval) {
  return {minimum, maximum, interval};
}
} // namespace

ShredWidthAudioProcessor::ShredWidthAudioProcessor()
    : juce::AudioProcessor(
          BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, stateTreeType, createParameterLayout()) {
  apvts.state.setProperty(parameterLayoutVersionProperty,
                          ShredWidth::ParameterIDs::parameterLayoutVersion,
                          nullptr);
  updateReportedLatencyFromParameter();
}

juce::AudioProcessorValueTreeState::ParameterLayout
ShredWidthAudioProcessor::createParameterLayout() {
  namespace IDs = ShredWidth::ParameterIDs;

  juce::AudioProcessorValueTreeState::ParameterLayout layout;

  layout.add(std::make_unique<juce::AudioParameterFloat>(
      parameterID(IDs::detuneAmount), "Detune Amount",
      linearRange(0.0f, 25.0f, 0.01f), 7.0f, "cents"));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      parameterID(IDs::delayL), "Delay Offset L",
      linearRange(1.0f, 50.0f, 0.01f), 12.5f, "ms"));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      parameterID(IDs::delayR), "Delay Offset R",
      linearRange(1.0f, 50.0f, 0.01f), 18.0f, "ms"));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      parameterID(IDs::monoXover), "Low Mono Crossover",
      juce::NormalisableRange<float>{80.0f, 300.0f, 1.0f, 0.35f}, 180.0f,
      "Hz"));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      parameterID(IDs::width), "Stereo Width", linearRange(0.0f, 200.0f, 0.1f),
      100.0f, "%"));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      parameterID(IDs::mix), "Mix", linearRange(0.0f, 100.0f, 0.1f), 40.0f,
      "% wet"));
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      parameterID(IDs::latencyMode), "Latency Mode",
      juce::StringArray{"Studio", "Live"}, 0));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      parameterID(IDs::outTrim), "Output Trim",
      linearRange(-12.0f, 12.0f, 0.1f), 0.0f, "dB"));
  layout.add(std::make_unique<juce::AudioParameterBool>(
      parameterID(IDs::bypass), "Bypass", false));
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      parameterID(IDs::monoSource), "Mono Source",
      juce::StringArray{"Sum", "Left"}, 0));

  return layout;
}

void ShredWidthAudioProcessor::prepareToPlay(double sampleRate,
                                             int samplesPerBlock) {
  preparedSampleRate = sampleRate;
  preparedMaxBlockSize = juce::jmax(0, samplesPerBlock);

  engine.prepare(sampleRate, samplesPerBlock);

  // Initialize lastLatencyMode and set initial latency on the processor
  const float rawLatencyMode = apvts.getRawParameterValue(ShredWidth::ParameterIDs::latencyMode)->load(std::memory_order_relaxed);
  const auto currentLatencyMode = rawLatencyMode >= 0.5f ? ShredWidthEngine::LatencyMode::Live : ShredWidthEngine::LatencyMode::Studio;
  lastLatencyMode = currentLatencyMode;
  engine.setLatencyMode(currentLatencyMode);
  setLatencySamples(engine.getReportedLatencySamples());

  outTrimSmoothed.reset(sampleRate, 0.02);
  const float initialDb = apvts.getRawParameterValue(ShredWidth::ParameterIDs::outTrim)->load(std::memory_order_relaxed);
  outTrimSmoothed.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(initialDb));
}

void ShredWidthAudioProcessor::releaseResources() {
  engine.reset();
}

bool ShredWidthAudioProcessor::isBusesLayoutSupported(
    const BusesLayout &layouts) const {
  const auto input = layouts.getMainInputChannelSet();
  const auto output = layouts.getMainOutputChannelSet();

  return output == juce::AudioChannelSet::stereo() &&
         (input == juce::AudioChannelSet::mono() ||
          input == juce::AudioChannelSet::stereo());
}

void ShredWidthAudioProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                            juce::MidiBuffer &midiMessages) {
  juce::ScopedNoDenormals noDenormals;
  juce::ignoreUnused(midiMessages);

  const auto inputChannels = getTotalNumInputChannels();
  const auto outputChannels = getTotalNumOutputChannels();
  const auto numSamples = buffer.getNumSamples();

  // Clear extra output channels
  for (auto channel = inputChannels; channel < outputChannels; ++channel)
    buffer.clear(channel, 0, numSamples);

  if (numSamples == 0)
    return;

  // Handle latencyMode change
  const float rawLatencyMode = apvts.getRawParameterValue(ShredWidth::ParameterIDs::latencyMode)->load(std::memory_order_relaxed);
  const auto currentLatencyMode = rawLatencyMode >= 0.5f ? ShredWidthEngine::LatencyMode::Live : ShredWidthEngine::LatencyMode::Studio;
  if (currentLatencyMode != lastLatencyMode) {
      lastLatencyMode = currentLatencyMode;
      engine.setLatencyMode(currentLatencyMode);
      setLatencySamples(engine.getReportedLatencySamples());
      updateHostDisplay();
  }

  // Update all other parameters on the engine
  const float detuneVal = apvts.getRawParameterValue(ShredWidth::ParameterIDs::detuneAmount)->load(std::memory_order_relaxed);
  engine.setDetuneCents(detuneVal);

  const float delayLVal = apvts.getRawParameterValue(ShredWidth::ParameterIDs::delayL)->load(std::memory_order_relaxed);
  const float delayRVal = apvts.getRawParameterValue(ShredWidth::ParameterIDs::delayR)->load(std::memory_order_relaxed);
  engine.setDelayMsL(delayLVal);
  engine.setDelayMsR(delayRVal);

  const float xoverVal = apvts.getRawParameterValue(ShredWidth::ParameterIDs::monoXover)->load(std::memory_order_relaxed);
  engine.setCrossoverHz(xoverVal);

  const float widthVal = apvts.getRawParameterValue(ShredWidth::ParameterIDs::width)->load(std::memory_order_relaxed);
  engine.setWidthPercent(widthVal);

  const float mixVal = apvts.getRawParameterValue(ShredWidth::ParameterIDs::mix)->load(std::memory_order_relaxed);
  engine.setMix(mixVal * 0.01f);

  const float rawMonoSource = apvts.getRawParameterValue(ShredWidth::ParameterIDs::monoSource)->load(std::memory_order_relaxed);
  const auto currentMonoSource = rawMonoSource >= 0.5f ? ShredWidthEngine::MonoSource::Left : ShredWidthEngine::MonoSource::Sum;
  engine.setMonoSource(currentMonoSource);

  const float outTrimVal = apvts.getRawParameterValue(ShredWidth::ParameterIDs::outTrim)->load(std::memory_order_relaxed);
  outTrimSmoothed.setTargetValue(juce::Decibels::decibelsToGain(outTrimVal));

  // Handle Bypass
  const auto bypassed = apvts.getRawParameterValue(ShredWidth::ParameterIDs::bypass)->load(std::memory_order_relaxed) >= 0.5f;
  if (bypassed) {
      if (inputChannels == 1 && outputChannels >= 2) {
          buffer.copyFrom(1, 0, buffer.getReadPointer(0), numSamples);
      }
      for (auto channel = inputChannels; channel < outputChannels; ++channel) {
          if (channel != 1 || inputChannels != 1) {
              buffer.clear(channel, 0, numSamples);
          }
      }
      return;
  }

  // Call ShredWidthEngine to process the block
  engine.processBlock(buffer, inputChannels);

  // Apply outTrim (smoothed, per-sample)
  if (outTrimSmoothed.isSmoothing()) {
      for (int sample = 0; sample < numSamples; ++sample) {
          const float gain = outTrimSmoothed.getNextValue();
          for (int channel = 0; channel < outputChannels; ++channel) {
              buffer.setSample(channel, sample, buffer.getSample(channel, sample) * gain);
          }
      }
  } else {
      const float gain = outTrimSmoothed.getCurrentValue();
      if (std::abs(gain - 1.0f) > 1.0e-5f) {
          buffer.applyGain(gain);
      }
  }
}

void ShredWidthAudioProcessor::updateReportedLatencyFromParameter() {
  const auto *latencyMode =
      apvts.getRawParameterValue(ShredWidth::ParameterIDs::latencyMode);
  const auto liveMode = latencyMode != nullptr &&
                        latencyMode->load(std::memory_order_relaxed) >= 0.5f;

  const auto mode = liveMode ? ShredWidthEngine::LatencyMode::Live : ShredWidthEngine::LatencyMode::Studio;
  lastLatencyMode = mode;
  engine.setLatencyMode(mode);
  setLatencySamples(engine.getReportedLatencySamples());
}

juce::AudioProcessorEditor *ShredWidthAudioProcessor::createEditor() {
  return new ShredWidthAudioProcessorEditor(*this);
}

bool ShredWidthAudioProcessor::hasEditor() const { return true; }

const juce::String ShredWidthAudioProcessor::getName() const {
  return JucePlugin_Name;
}

bool ShredWidthAudioProcessor::acceptsMidi() const { return false; }

bool ShredWidthAudioProcessor::producesMidi() const { return false; }

bool ShredWidthAudioProcessor::isMidiEffect() const { return false; }

double ShredWidthAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int ShredWidthAudioProcessor::getNumPrograms() { return 1; }

int ShredWidthAudioProcessor::getCurrentProgram() { return 0; }

void ShredWidthAudioProcessor::setCurrentProgram(int) {}

const juce::String ShredWidthAudioProcessor::getProgramName(int) { return {}; }

void ShredWidthAudioProcessor::changeProgramName(int, const juce::String &) {}

void ShredWidthAudioProcessor::getStateInformation(
    juce::MemoryBlock &destData) {
  if (const auto xml = stateManager.createComparisonXml(apvts))
    copyXmlToBinary(*xml, destData);
}

void ShredWidthAudioProcessor::setStateInformation(const void *data,
                                                   int sizeInBytes) {
  if (const auto xml = getXmlFromBinary(data, sizeInBytes)) {
    stateManager.restoreComparisonXml(*xml, apvts);
    updateReportedLatencyFromParameter();
  }
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new ShredWidthAudioProcessor();
}
