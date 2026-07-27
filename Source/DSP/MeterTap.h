#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>

/**
    Real-time-safe stereo metering tap.

    The audio thread is the sole producer: it updates the running correlation
    and writes decimated stereo pairs. The UI thread is the sole consumer of
    popPairs(). prepare() and reset() must only be called while both threads
    are stopped.
*/
class MeterTap final {
public:
  /** Configures the one-pole coefficient and decimation rate. */
  void prepare(double sampleRate, int maxBlockSize);

  /** Clears the running statistics and pending goniometer pairs. */
  void reset() noexcept;

  /** Updates correlation and, when due, queues one stereo display pair. */
  void pushSample(float left, float right) noexcept;

  /** Convenience wrapper for contiguous stereo blocks. */
  void pushBlock(const float *left, const float *right,
                 int numSamples) noexcept;

  /** Returns the latest audio-thread correlation value. */
  [[nodiscard]] float getCorrelation() const noexcept;

  /**
      Copies up to maxPairs queued stereo frames into caller-owned buffers.

      Either destination may be null. Calling this function still consumes the
      returned number of complete pairs.
  */
  [[nodiscard]] int popPairs(float *leftDestination, float *rightDestination,
                             int maxPairs) noexcept;

  [[nodiscard]] int getNumPairsReady() const noexcept;

  static constexpr int fifoCapacity = 4096;

private:
  // AbstractFifo keeps one slot empty, so 4097 backing slots provide exactly
  // 4096 usable stereo frames.
  static constexpr int fifoBackingSize = fifoCapacity + 1;
  static constexpr double correlationTimeSeconds = 0.3;
  static constexpr double goniometerPairsPerSecond = 6000.0;
  static constexpr float silenceEpsilon = 1.0e-12f;

  std::array<float, fifoBackingSize> leftPairs{};
  std::array<float, fifoBackingSize> rightPairs{};
  juce::AbstractFifo pairFifo{fifoBackingSize};

  std::atomic<float> correlation{0.0f};
  float meanLeftRight = 0.0f;
  float meanLeftSquared = 0.0f;
  float meanRightSquared = 0.0f;
  float onePoleCoefficient = 0.0f;
  int decimationFactor = 1;
  int decimationCounter = 0;
  bool prepared = false;
};
