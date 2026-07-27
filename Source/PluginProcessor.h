#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class ShredWidthAudioProcessor final : public juce::AudioProcessor {
public:
  ShredWidthAudioProcessor();
  ~ShredWidthAudioProcessor() override = default;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;
  bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
  void processBlock(juce::AudioBuffer<float> &buffer,
                    juce::MidiBuffer &midiMessages) override;

  juce::AudioProcessorEditor *createEditor() override;
  bool hasEditor() const override;

  const juce::String getName() const override;
  bool acceptsMidi() const override;
  bool producesMidi() const override;
  bool isMidiEffect() const override;
  double getTailLengthSeconds() const override;

  int getNumPrograms() override;
  int getCurrentProgram() override;
  void setCurrentProgram(int index) override;
  const juce::String getProgramName(int index) override;
  void changeProgramName(int index, const juce::String &newName) override;

  void getStateInformation(juce::MemoryBlock &destData) override;
  void setStateInformation(const void *data, int sizeInBytes) override;

  juce::AudioProcessorValueTreeState &getAPVTS() noexcept { return apvts; }
  const juce::AudioProcessorValueTreeState &getAPVTS() const noexcept {
    return apvts;
  }

private:
  static juce::AudioProcessorValueTreeState::ParameterLayout
  createParameterLayout();
  void updateReportedLatencyFromParameter();

  static constexpr int studioPitchBufferLength = 2048;
  static constexpr int pitchInterpolationGuard = 4;
  static constexpr int studioLatencySamples = studioPitchBufferLength / 2;
  static constexpr int liveLatencySamples = 512 / 2;
  static constexpr int dryAlignmentCapacity = studioLatencySamples;
  static constexpr int meterFifoCapacity = 4096;

  juce::AudioProcessorValueTreeState apvts;

  // Engine-ready storage. DSP classes will take ownership/use these buffers in
  // a later phase; all variable-sized memory is reserved in prepareToPlay.
  juce::AudioBuffer<float> pitchCircularBuffers;
  juce::AudioBuffer<float> delayLineBuffers;
  juce::AudioBuffer<float> dryAlignmentBuffer;
  juce::AudioBuffer<float> meterFifoBuffer;
  juce::AbstractFifo meterFifo{meterFifoCapacity};

  double preparedSampleRate = 0.0;
  int preparedMaxBlockSize = 0;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShredWidthAudioProcessor)
};
