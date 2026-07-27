#include "ShredWidthEngine.h"

#include <algorithm>
#include <cmath>

//==============================================================================
void ShredWidthEngine::DryAlignDelay::prepare() {
  leftBuffer.allocate(static_cast<size_t>(capacity), true);
  rightBuffer.allocate(static_cast<size_t>(capacity), true);
  reset();
}

void ShredWidthEngine::DryAlignDelay::reset() noexcept {
  if (leftBuffer.getData() == nullptr || rightBuffer.getData() == nullptr)
    return;

  std::fill(leftBuffer.getData(), leftBuffer.getData() + capacity, 0.0f);
  std::fill(rightBuffer.getData(), rightBuffer.getData() + capacity, 0.0f);
  writePosition = 0;
}

void ShredWidthEngine::DryAlignDelay::process(float inputLeft, float inputRight,
                                              int delayA, int delayB,
                                              float blend, float &outputLeft,
                                              float &outputRight) noexcept {
  leftBuffer[writePosition] = inputLeft;
  rightBuffer[writePosition] = inputRight;

  const int indexA = readIndex(delayA);
  const int indexB = readIndex(delayB);
  const float inverseBlend = 1.0f - blend;

  outputLeft = inverseBlend * leftBuffer[indexA] + blend * leftBuffer[indexB];
  outputRight =
      inverseBlend * rightBuffer[indexA] + blend * rightBuffer[indexB];

  if (++writePosition >= capacity)
    writePosition = 0;
}

int ShredWidthEngine::DryAlignDelay::readIndex(
    int delaySamples) const noexcept {
  const int clampedDelay = juce::jlimit(0, maximumDelaySamples, delaySamples);
  int index = writePosition - clampedDelay;
  if (index < 0)
    index += capacity;
  return index;
}

//==============================================================================
void ShredWidthEngine::prepare(double sampleRate, int maximumBlockSize) {
  jassert(sampleRate > 0.0);
  jassert(maximumBlockSize >= 0);

  // PitchShifter::prepare always reserves its Studio-sized ring. Selecting
  // Live below only changes the active length inside that storage.
  pitchUp.prepare(sampleRate, maximumBlockSize);
  pitchDown.prepare(sampleRate, maximumBlockSize);
  delayLeft.prepare(sampleRate, maximumBlockSize);
  delayRight.prepare(sampleRate, maximumBlockSize);
  crossover.prepare(sampleRate, maximumBlockSize);
  widthProcessor.prepare(sampleRate, maximumBlockSize);
  dryAlign.prepare();
  meterTap.prepare(sampleRate, maximumBlockSize);

  mixSmoothed.reset(sampleRate, mixRampSeconds);
  mixSmoothed.setCurrentAndTargetValue(targetMix);

  transitionLengthSamples = juce::jmax(
      2, static_cast<int>(std::lround(sampleRate * modeCrossfadeSeconds)));

  processingLatencyMode = requestedLatencyMode;
  applyProcessingLatencyMode(processingLatencyMode);

  pitchUp.setDetuneCents(detuneCents);
  pitchDown.setDetuneCents(-detuneCents);
  delayLeft.setDelayMs(delayMillisecondsLeft);
  delayRight.setDelayMs(delayMillisecondsRight);
  crossover.setCutoffHz(crossoverHz);
  crossover.beginBlock();
  widthProcessor.setWidth(width);

  prepared = true;
  reset();
}

void ShredWidthEngine::reset() noexcept {
  if (!prepared)
    return;

  processingLatencyMode = requestedLatencyMode;
  applyProcessingLatencyMode(processingLatencyMode);

  pitchUp.reset();
  pitchDown.reset();
  delayLeft.reset();
  delayRight.reset();
  crossover.reset();
  widthProcessor.reset();
  dryAlign.reset();
  meterTap.reset();
  mixSmoothed.setCurrentAndTargetValue(targetMix);

  transitionSamplesRemaining = 0;
  transitionOldDryLatency = getReportedLatencySamples();
  transitionModeApplied = true;
  hasProcessedAudio = false;
}

//==============================================================================
void ShredWidthEngine::setDetuneCents(float cents) noexcept {
  if (!std::isfinite(cents))
    return;

  detuneCents = std::abs(cents);
  pitchUp.setDetuneCents(detuneCents);
  pitchDown.setDetuneCents(-detuneCents);
}

void ShredWidthEngine::setDelayMsL(float milliseconds) noexcept {
  if (!std::isfinite(milliseconds))
    return;

  delayMillisecondsLeft = milliseconds;
  delayLeft.setDelayMs(delayMillisecondsLeft);
}

void ShredWidthEngine::setDelayMsR(float milliseconds) noexcept {
  if (!std::isfinite(milliseconds))
    return;

  delayMillisecondsRight = milliseconds;
  delayRight.setDelayMs(delayMillisecondsRight);
}

void ShredWidthEngine::setCrossoverHz(float hertz) noexcept {
  if (!std::isfinite(hertz))
    return;

  crossoverHz = hertz;
  crossover.setCutoffHz(crossoverHz);
}

void ShredWidthEngine::setWidth(float factor) noexcept {
  if (!std::isfinite(factor))
    return;

  width =
      juce::jlimit(WidthProcessor::minWidth, WidthProcessor::maxWidth, factor);
  widthProcessor.setWidth(width);
}

void ShredWidthEngine::setWidthPercent(float percent) noexcept {
  if (!std::isfinite(percent))
    return;

  setWidth(percent * 0.01f);
}

void ShredWidthEngine::setMix(float wetProportion) noexcept {
  if (!std::isfinite(wetProportion))
    return;

  targetMix = juce::jlimit(0.0f, 1.0f, wetProportion);
  if (prepared)
    mixSmoothed.setTargetValue(targetMix);
}

void ShredWidthEngine::setLatencyMode(LatencyMode mode) noexcept {
  if (mode == requestedLatencyMode && transitionSamplesRemaining == 0)
    return;

  requestedLatencyMode = mode;

  if (!prepared || !hasProcessedAudio) {
    processingLatencyMode = requestedLatencyMode;
    if (prepared)
      applyProcessingLatencyMode(processingLatencyMode);

    transitionSamplesRemaining = 0;
    transitionOldDryLatency = getReportedLatencySamples();
    transitionModeApplied = true;
    return;
  }

  if (requestedLatencyMode == processingLatencyMode) {
    transitionSamplesRemaining = 0;
    transitionOldDryLatency = getReportedLatencySamples();
    transitionModeApplied = true;
    return;
  }

  transitionOldDryLatency = latencyForMode(processingLatencyMode);
  transitionSamplesRemaining = transitionLengthSamples;
  transitionModeApplied = false;
}

void ShredWidthEngine::setMonoSource(MonoSource source) noexcept {
  monoSource = source;
}

//==============================================================================
void ShredWidthEngine::processSample(float inputLeft, float inputRight,
                                     float &outputLeft, float &outputRight,
                                     bool stereoInput) noexcept {
  jassert(prepared);
  crossover.beginBlock();
  processFrame(inputLeft, inputRight, stereoInput, outputLeft, outputRight);
}

void ShredWidthEngine::processBlock(float *left, float *right, int numSamples,
                                    int numInputChannels) noexcept {
  juce::ScopedNoDenormals noDenormals;

  jassert(prepared);
  jassert(numSamples >= 0);
  jassert(numSamples == 0 || (left != nullptr && right != nullptr));
  jassert(numInputChannels == 1 || numInputChannels == 2);

  crossover.beginBlock();
  const bool stereoInput = numInputChannels > 1;

  for (int sample = 0; sample < numSamples; ++sample) {
    const float inputLeft = left[sample];
    const float inputRight = stereoInput ? right[sample] : inputLeft;
    processFrame(inputLeft, inputRight, stereoInput, left[sample],
                 right[sample]);
  }
}

void ShredWidthEngine::process(float *left, float *right, int numSamples,
                               int numInputChannels) noexcept {
  processBlock(left, right, numSamples, numInputChannels);
}

void ShredWidthEngine::processBlock(juce::AudioBuffer<float> &buffer,
                                    int numInputChannels) noexcept {
  jassert(buffer.getNumChannels() >= 2);
  processBlock(buffer.getWritePointer(0), buffer.getWritePointer(1),
               buffer.getNumSamples(), numInputChannels);
}

int ShredWidthEngine::getReportedLatencySamples() const noexcept {
  return latencyForMode(requestedLatencyMode);
}

//==============================================================================
int ShredWidthEngine::latencyForMode(LatencyMode mode) noexcept {
  return (mode == LatencyMode::Studio ? studioPitchLength : livePitchLength) /
         2;
}

PitchShifter::LatencyMode
ShredWidthEngine::pitchModeFor(LatencyMode mode) noexcept {
  return mode == LatencyMode::Studio ? PitchShifter::LatencyMode::studio
                                     : PitchShifter::LatencyMode::live;
}

void ShredWidthEngine::applyProcessingLatencyMode(LatencyMode mode) noexcept {
  const auto pitchMode = pitchModeFor(mode);
  pitchUp.setLatencyMode(pitchMode);
  pitchDown.setLatencyMode(pitchMode);
  processingLatencyMode = mode;
}

void ShredWidthEngine::processFrame(float inputLeft, float inputRight,
                                    bool stereoInput, float &outputLeft,
                                    float &outputRight) noexcept {
  hasProcessedAudio = true;

  const float dryInputRight = stereoInput ? inputRight : inputLeft;
  const float mono = (stereoInput && monoSource == MonoSource::Sum)
                         ? 0.5f * (inputLeft + inputRight)
                         : inputLeft;

  float transitionProgress = 1.0f;
  float wetModeGain = 1.0f;
  int dryLatencyA = getReportedLatencySamples();
  int dryLatencyB = dryLatencyA;

  if (transitionSamplesRemaining > 0) {
    const int elapsed = transitionLengthSamples - transitionSamplesRemaining;
    transitionProgress =
        static_cast<float>(elapsed) /
        static_cast<float>(juce::jmax(1, transitionLengthSamples - 1));

    dryLatencyA = transitionOldDryLatency;
    dryLatencyB = getReportedLatencySamples();

    // At the midpoint the output is the crossfaded, PDC-aligned dry
    // signal. Relengthing both pitch rings here is therefore masked.
    if (!transitionModeApplied && transitionProgress >= 0.5f) {
      applyProcessingLatencyMode(requestedLatencyMode);
      transitionModeApplied = true;
    }

    wetModeGain = transitionProgress < 0.5f ? 1.0f - 2.0f * transitionProgress
                                            : 2.0f * transitionProgress - 1.0f;
    wetModeGain = juce::jlimit(0.0f, 1.0f, wetModeGain);
  }

  const float shiftedLeft = pitchUp.processSample(mono);
  const float shiftedRight = pitchDown.processSample(mono);
  const float delayedLeft = delayLeft.processSample(shiftedLeft);
  const float delayedRight = delayRight.processSample(shiftedRight);

  const auto bands = crossover.process(mono, delayedLeft, delayedRight);
  const float assembledLeft = bands.low + bands.highLeft;
  const float assembledRight = bands.low + bands.highRight;

  float wetLeft = 0.0f;
  float wetRight = 0.0f;
  widthProcessor.processSample(assembledLeft, assembledRight, wetLeft,
                               wetRight);

  float dryLeft = 0.0f;
  float dryRight = 0.0f;
  dryAlign.process(inputLeft, dryInputRight, dryLatencyA, dryLatencyB,
                   transitionProgress, dryLeft, dryRight);

  const float wetMix = mixSmoothed.getNextValue() * wetModeGain;
  const float dryMix = 1.0f - wetMix;
  outputLeft = dryMix * dryLeft + wetMix * wetLeft;
  outputRight = dryMix * dryRight + wetMix * wetRight;

  meterTap.pushSample(outputLeft, outputRight);

  if (transitionSamplesRemaining > 0) {
    --transitionSamplesRemaining;
    if (transitionSamplesRemaining == 0) {
      if (!transitionModeApplied)
        applyProcessingLatencyMode(requestedLatencyMode);
      transitionOldDryLatency = getReportedLatencySamples();
      transitionModeApplied = true;
    }
  }
}
