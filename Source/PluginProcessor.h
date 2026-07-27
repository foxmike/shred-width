#pragma once
 
#include <juce_audio_processors/juce_audio_processors.h>
#include "StateManager.h"
#include "DSP/ShredWidthEngine.h"
 
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
  StateManager &getStateManager() noexcept { return stateManager; }
  const StateManager &getStateManager() const noexcept { return stateManager; }
 
  ShredWidthEngine& getEngine() noexcept { return engine; }
  const ShredWidthEngine& getEngine() const noexcept { return engine; }
 
private:
  static juce::AudioProcessorValueTreeState::ParameterLayout
  createParameterLayout();
  void updateReportedLatencyFromParameter();
 
  juce::AudioProcessorValueTreeState apvts;
  StateManager stateManager;
  ShredWidthEngine engine;
 
  ShredWidthEngine::LatencyMode lastLatencyMode = ShredWidthEngine::LatencyMode::Studio;
  juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outTrimSmoothed;
 
  double preparedSampleRate = 0.0;
  int preparedMaxBlockSize = 0;
 
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShredWidthAudioProcessor)
};
