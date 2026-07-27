#include "MeterTap.h"

#include <algorithm>
#include <cmath>

void MeterTap::prepare(double sampleRate, int maxBlockSize) {
  jassert(sampleRate > 0.0);
  jassert(maxBlockSize >= 0);
  juce::ignoreUnused(maxBlockSize);

  const double validSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
  onePoleCoefficient = static_cast<float>(
      1.0 - std::exp(-1.0 / (correlationTimeSeconds * validSampleRate)));
  decimationFactor =
      juce::jmax(1, static_cast<int>(std::lround(validSampleRate /
                                                 goniometerPairsPerSecond)));

  prepared = true;
  reset();
}

void MeterTap::reset() noexcept {
  pairFifo.reset();
  meanLeftRight = 0.0f;
  meanLeftSquared = 0.0f;
  meanRightSquared = 0.0f;
  decimationCounter = 0;
  correlation.store(0.0f, std::memory_order_relaxed);
}

void MeterTap::pushSample(float left, float right) noexcept {
  if (!prepared)
    return;

  meanLeftRight += onePoleCoefficient * (left * right - meanLeftRight);
  meanLeftSquared += onePoleCoefficient * (left * left - meanLeftSquared);
  meanRightSquared += onePoleCoefficient * (right * right - meanRightSquared);

  const float denominator =
      std::sqrt(juce::jmax(0.0f, meanLeftSquared * meanRightSquared)) +
      silenceEpsilon;
  const float rho = juce::jlimit(-1.0f, 1.0f, meanLeftRight / denominator);
  correlation.store(rho, std::memory_order_relaxed);

  if (++decimationCounter < decimationFactor)
    return;

  decimationCounter = 0;

  int start1 = 0;
  int size1 = 0;
  int start2 = 0;
  int size2 = 0;
  pairFifo.prepareToWrite(1, start1, size1, start2, size2);

  if (size1 > 0) {
    leftPairs[static_cast<size_t>(start1)] = left;
    rightPairs[static_cast<size_t>(start1)] = right;
    pairFifo.finishedWrite(1);
  } else if (size2 > 0) {
    leftPairs[static_cast<size_t>(start2)] = left;
    rightPairs[static_cast<size_t>(start2)] = right;
    pairFifo.finishedWrite(1);
  }
  // A full FIFO drops the newest display pair; the audio thread never waits.
}

void MeterTap::pushBlock(const float *left, const float *right,
                         int numSamples) noexcept {
  if (left == nullptr || right == nullptr || numSamples <= 0)
    return;

  for (int sample = 0; sample < numSamples; ++sample)
    pushSample(left[sample], right[sample]);
}

float MeterTap::getCorrelation() const noexcept {
  return correlation.load(std::memory_order_relaxed);
}

int MeterTap::popPairs(float *leftDestination, float *rightDestination,
                       int maxPairs) noexcept {
  if (maxPairs <= 0)
    return 0;

  int start1 = 0;
  int size1 = 0;
  int start2 = 0;
  int size2 = 0;
  pairFifo.prepareToRead(maxPairs, start1, size1, start2, size2);

  if (leftDestination != nullptr) {
    std::copy_n(leftPairs.data() + start1, size1, leftDestination);
    std::copy_n(leftPairs.data() + start2, size2, leftDestination + size1);
  }

  if (rightDestination != nullptr) {
    std::copy_n(rightPairs.data() + start1, size1, rightDestination);
    std::copy_n(rightPairs.data() + start2, size2, rightDestination + size1);
  }

  const int numRead = size1 + size2;
  pairFifo.finishedRead(numRead);
  return numRead;
}

int MeterTap::getNumPairsReady() const noexcept {
  return pairFifo.getNumReady();
}
