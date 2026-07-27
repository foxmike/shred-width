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
  setLatencySamples(studioLatencySamples);
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

  pitchCircularBuffers.setSize(
      2, studioPitchBufferLength + pitchInterpolationGuard, false, false, true);

  constexpr double maximumDelaySeconds = 0.05;
  const auto delayCapacity =
      static_cast<int>(
          std::ceil(maximumDelaySeconds * juce::jmax(0.0, sampleRate))) +
      pitchInterpolationGuard;
  delayLineBuffers.setSize(2, delayCapacity, false, false, true);

  dryAlignmentBuffer.setSize(2, dryAlignmentCapacity, false, false, true);
  meterFifoBuffer.setSize(2, meterFifoCapacity, false, false, true);

  pitchCircularBuffers.clear();
  delayLineBuffers.clear();
  dryAlignmentBuffer.clear();
  meterFifoBuffer.clear();
  meterFifo.reset();

  updateReportedLatencyFromParameter();
}

void ShredWidthAudioProcessor::releaseResources() {
  pitchCircularBuffers.clear();
  delayLineBuffers.clear();
  dryAlignmentBuffer.clear();
  meterFifoBuffer.clear();
  meterFifo.reset();
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
  juce::ignoreUnused(midiMessages, preparedSampleRate, preparedMaxBlockSize);

  const auto inputChannels = getTotalNumInputChannels();
  const auto outputChannels = getTotalNumOutputChannels();

  for (auto channel = inputChannels; channel < outputChannels; ++channel)
    buffer.clear(channel, 0, buffer.getNumSamples());

  // DSP is intentionally introduced by later phase-owned engine classes.
}

void ShredWidthAudioProcessor::updateReportedLatencyFromParameter() {
  const auto *latencyMode =
      apvts.getRawParameterValue(ShredWidth::ParameterIDs::latencyMode);
  const auto liveMode = latencyMode != nullptr &&
                        latencyMode->load(std::memory_order_relaxed) >= 0.5f;

  setLatencySamples(liveMode ? liveLatencySamples : studioLatencySamples);
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
