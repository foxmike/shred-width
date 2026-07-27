#pragma once

#include "DelayLineFrac.h"
#include "MeterTap.h"
#include "MonoSafetyCrossover.h"
#include "PitchShifter.h"
#include "WidthProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

/**
    Deterministic, stereo ShredWidth signal-path orchestrator.

    All variable-sized storage is allocated by prepare(). Processing composes
    the phase-B DSP primitives sample by sample and performs no allocation,
    locking, logging, or message-thread work.
*/
class ShredWidthEngine {
public:
  enum class LatencyMode { Studio, Live };

  enum class MonoSource { Sum, Left };

  void prepare(double sampleRate, int maximumBlockSize);
  void reset() noexcept;

  void setDetuneCents(float cents) noexcept;
  void setDelayMsL(float milliseconds) noexcept;
  void setDelayMsR(float milliseconds) noexcept;
  void setCrossoverHz(float hertz) noexcept;
  void setWidth(float factor) noexcept;
  void setWidthPercent(float percent) noexcept;
  void setMix(float wetProportion) noexcept;
  void setLatencyMode(LatencyMode mode) noexcept;
  void setMonoSource(MonoSource source) noexcept;

  /**
      Processes one stereo output frame. Set stereoInput false for a mono
      input; inputRight is then ignored and the aligned dry input is
      duplicated to both output channels.
  */
  void processSample(float inputLeft, float inputRight, float &outputLeft,
                     float &outputRight, bool stereoInput = true) noexcept;

  /** In-place raw-channel processing. numInputChannels must be one or two. */
  void processBlock(float *left, float *right, int numSamples,
                    int numInputChannels = 2) noexcept;

  /** Alias used by engine-level clients that prefer process(). */
  void process(float *left, float *right, int numSamples,
               int numInputChannels = 2) noexcept;

  /** In-place JUCE buffer convenience wrapper with stereo output. */
  void processBlock(juce::AudioBuffer<float> &buffer,
                    int numInputChannels) noexcept;

  int getReportedLatencySamples() const noexcept;
  LatencyMode getLatencyMode() const noexcept { return requestedLatencyMode; }
  MonoSource getMonoSource() const noexcept { return monoSource; }

  MeterTap &getMeterTap() noexcept { return meterTap; }
  const MeterTap &getMeterTap() const noexcept { return meterTap; }

private:
  class DryAlignDelay {
  public:
    void prepare();
    void reset() noexcept;
    void process(float inputLeft, float inputRight, int delayA, int delayB,
                 float blend, float &outputLeft, float &outputRight) noexcept;

  private:
    static constexpr int maximumDelaySamples = 1024;
    static constexpr int capacity = maximumDelaySamples + 1;

    juce::HeapBlock<float> leftBuffer;
    juce::HeapBlock<float> rightBuffer;
    int writePosition = 0;

    int readIndex(int delaySamples) const noexcept;
  };

  static constexpr int studioPitchLength = 2048;
  static constexpr int livePitchLength = 512;
  static constexpr double mixRampSeconds = 0.02;
  static constexpr double modeCrossfadeSeconds = 0.01;

  PitchShifter pitchUp;
  PitchShifter pitchDown;
  DelayLineFrac delayLeft;
  DelayLineFrac delayRight;
  MonoSafetyCrossover crossover;
  WidthProcessor widthProcessor;
  DryAlignDelay dryAlign;
  MeterTap meterTap;

  juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;

  float detuneCents = 7.0f;
  float delayMillisecondsLeft = 12.5f;
  float delayMillisecondsRight = 18.0f;
  float crossoverHz = 180.0f;
  float width = 1.0f;
  float targetMix = 0.4f;

  LatencyMode requestedLatencyMode = LatencyMode::Studio;
  LatencyMode processingLatencyMode = LatencyMode::Studio;
  MonoSource monoSource = MonoSource::Sum;

  int transitionLengthSamples = 0;
  int transitionSamplesRemaining = 0;
  int transitionOldDryLatency = studioPitchLength / 2;
  bool transitionModeApplied = true;
  bool prepared = false;
  bool hasProcessedAudio = false;

  static int latencyForMode(LatencyMode mode) noexcept;
  static PitchShifter::LatencyMode pitchModeFor(LatencyMode mode) noexcept;

  void applyProcessingLatencyMode(LatencyMode mode) noexcept;
  void processFrame(float inputLeft, float inputRight, bool stereoInput,
                    float &outputLeft, float &outputRight) noexcept;
};
