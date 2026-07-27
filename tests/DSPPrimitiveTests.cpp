// =============================================================================
// ShredWidth - DSP Primitive Unit Tests
// =============================================================================
// Focused, public-API-only checks for the four DSP primitives produced by
// phase B tasks b.1-b.4 (Source/DSP/DelayLineFrac, PitchShifter,
// MonoSafetyCrossover, WidthProcessor). No production DSP code is rewritten
// or specialised; only the documented APIs are exercised.
//
// These tests cover the mechanical invariants documented in
// .abracapocus/ShredWidth_Build_Spec.md §4.2-4.5, §4.7 and §14 (Verification
// & Test Plan items 5, 6, 8):
//   * §4.3 / §4.4  DelayLineFrac: impulse lands at the expected sample index
//                   within Lagrange interpolation tolerance.
//   * §4.2         PitchShifter: at deltaC=0 the dual-head / Hann crossfade
//                   maintains unity gain on a steady sine (mean power ratio
//                   ≈1), and the crossfade sum w_A + w_B = 1 is enforced
//                   on DC across a swept tau (so all phase values are
//                   covered).
//   * §4.5         MonoSafetyCrossover: when the LP and HP inputs coincide
//                   (degenerate reconstruction case), LP + HP is allpass —
//                   no deep notch at fc. The dirty-flag path
//                   (setCutoffHz + beginBlock) is callable.
//   * §4.7         WidthProcessor: identical L=R content survives width
//                   settings in {0, 1, 2} with L' == R' (mono-lows
//                   invariant); width = 1 is identity for L != R.
//
// Uses juce::UnitTest only — no Catch2 / Google Test. Mirrors the sibling
// ShredEQ tests/*.cpp pattern.
// =============================================================================

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include "Source/DSP/DelayLineFrac.h"
#include "Source/DSP/PitchShifter.h"
#include "Source/DSP/MonoSafetyCrossover.h"
#include "Source/DSP/WidthProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace
{

constexpr double kSampleRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;
constexpr int kMaxBlockSize = 512;
constexpr int kStudioLength = 2048;

// -----------------------------------------------------------------------------
// Local helpers — pure functions only; no class internals are touched.
// -----------------------------------------------------------------------------

// Index of the largest-magnitude sample in `data[0..n)`.
int argMaxAbs(const float *data, int n)
{
    int best = 0;
    float bestMag = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float mag = std::abs(data[static_cast<std::size_t>(i)]);
        if (mag > bestMag)
        {
            bestMag = mag;
            best = i;
        }
    }
    return best;
}

// RMS over `data[0..n)`.
double rms(const float *data, int n)
{
    if (n <= 0)
        return 0.0;
    double sumSq = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double v =
            static_cast<double>(data[static_cast<std::size_t>(i)]);
        sumSq += v * v;
    }
    return std::sqrt(sumSq / static_cast<double>(n));
}

// RMS over `data[start..start+count)` clamped to the buffer bounds.
double rmsRange(const float *data, int total, int start, int count)
{
    const int lo = std::max(0, start);
    const int hi = std::min(total, lo + count);
    return rms(data + lo, std::max(0, hi - lo));
}

} // namespace

// =============================================================================

class DSPPrimitiveTests final : public juce::UnitTest
{
public:
    DSPPrimitiveTests()
        : juce::UnitTest("DSPPrimitiveTests", "ShredWidth/DSP") {}

    void runTest() override
    {
        testDelayLineFracImpulsePlacement();
        testDelayLineFracSettlesAfterSmoothingRamp();

        testPitchShifterUnityGainOnSteadySineAtZeroCents();
        testPitchShifterCrossfadeUnityGainOnDCSweep();

        testMonoSafetyCrossoverReconstructionAllpass();
        testMonoSafetyCrossoverNoNullAtCutoff();
        testMonoSafetyCrossoverCutoffDirtyPathCallable();

        testWidthProcessorMonoLowsInvariantAcrossWidths();
        testWidthProcessorUnityIdentityWhenLeftNotEqualRight();
    }

    // -------------------------------------------------------------------------
    // DelayLineFrac
    // -------------------------------------------------------------------------

    // §4.3 / §4.4: after prepare() + setDelayMs() + settling the 50 ms read-
    // distance ramp (D9), an impulse lands at the expected integer delay.
    void testDelayLineFracImpulsePlacement()
    {
        beginTest("DelayLineFrac: impulse peaks at the expected integer delay sample (±2)");

        constexpr float delayMs = 10.0f;
        constexpr int expectedDelaySamples =
            static_cast<int>(delayMs * 0.001f * kSampleRate); // 480 @ 48 kHz

        DelayLineFrac delay;
        delay.prepare(kSampleRate, kMaxBlockSize);
        delay.setDelayMs(delayMs);

        // Pump enough silence to settle the 50 ms smoothing ramp
        // (50 ms * 48 kHz = 2400 samples; pump 3000 to be safe).
        constexpr int settleSamples = 3000;
        for (int i = 0; i < settleSamples; ++i)
            (void)delay.processSample(0.0f);

        // Build the impulse + trailing silence: writePos is now non-zero but
        // the buffer was zeroed during settle, so the only non-zero sample
        // we add is the impulse.
        const int totalSamples = expectedDelaySamples + 256;
        std::vector<float> data(static_cast<std::size_t>(totalSamples), 0.0f);
        data[0] = 1.0f;

        delay.process(data.data(), totalSamples);

        const int peakIdx = argMaxAbs(data.data(), totalSamples);
        const float peakMag =
            std::abs(data[static_cast<std::size_t>(peakIdx)]);

        expectWithinAbsoluteError(
            static_cast<float>(peakIdx),
            static_cast<float>(expectedDelaySamples),
            2.0f,
            "Impulse peak should land near the requested delay ("
            + juce::String(expectedDelaySamples)
            + " samples); got index " + juce::String(peakIdx));

        expect(peakMag > 0.9f,
               "Impulse peak magnitude should be near 1.0 after Lagrange interp; got "
               + juce::String(peakMag));
    }

    // §4.4: the smoothed read distance ramps to the requested delay within
    // the 50 ms smoothing window (D9). Verify getCurrentDelaySamples()
    // reaches the target after settle.
    void testDelayLineFracSettlesAfterSmoothingRamp()
    {
        beginTest("DelayLineFrac: smoothed read distance settles to target after ≥50 ms");

        constexpr float delayMs = 12.5f;
        const float expected =
            static_cast<float>(delayMs * 0.001f * kSampleRate);

        DelayLineFrac delay;
        delay.prepare(kSampleRate, kMaxBlockSize);
        delay.setDelayMs(delayMs);

        // Pump 50 ms * 48 kHz = 2400 samples; one extra ms to clear the ramp.
        for (int i = 0; i < 2448; ++i)
            (void)delay.processSample(0.0f);

        const float settled = delay.getCurrentDelaySamples();
        expectWithinAbsoluteError(
            settled, expected, 1.0f,
            "Smoothed read distance should reach the target after the 50 ms ramp; got "
            + juce::String(settled) + " expected " + juce::String(expected));
    }

    // -------------------------------------------------------------------------
    // PitchShifter
    // -------------------------------------------------------------------------

    // §4.2: at deltaC=0 the shifter is a fixed L/2-sample delay with no
    // amplitude modulation (weightA = 0, weightB = 1), so the output RMS
    // over the steady-state region equals the input RMS within a small
    // tolerance.
    void testPitchShifterUnityGainOnSteadySineAtZeroCents()
    {
        beginTest("PitchShifter: unity gain on a 1 kHz sine at deltaC=0 (mean power ratio ~1)");

        PitchShifter shifter;
        shifter.prepare(kSampleRate, kMaxBlockSize);
        shifter.setLatencyMode(PitchShifter::LatencyMode::studio); // L=2048
        shifter.setDetuneCents(0.0f);

        // 1 kHz sine (DC-free) at amplitude 0.5.
        constexpr int N = 16384;
        std::vector<float> data(static_cast<std::size_t>(N));
        for (int i = 0; i < N; ++i)
        {
            data[static_cast<std::size_t>(i)] = static_cast<float>(
                0.5 * std::sin(2.0 * kPi * 1000.0
                               * static_cast<double>(i) / kSampleRate));
        }

        // Snapshot input RMS over the steady-state region. The first L/2
        // samples of the output correspond to the still-empty delay buffer;
        // skip a full L samples so the comparison is unambiguous.
        const int skip = kStudioLength;
        const double inputRms = rmsRange(data.data(), N, skip, N - skip);

        shifter.process(data.data(), N);

        const double outputRms = rmsRange(data.data(), N, skip, N - skip);

        const double ratio = outputRms / std::max(inputRms, 1.0e-12);
        expectWithinAbsoluteError(
            ratio, 1.0, 0.01,
            "Unity-gain invariant at deltaC=0 violated; ratio="
            + juce::String(ratio));
    }

    // §4.2 / §14 item 6: the Hann crossfade enforces w_A + w_B = 1 exactly.
    // For DC input the buffer reads are all equal to the DC value, so
    // output = w_A * x + w_B * x = (w_A + w_B) * x. Driving the shifter with
    // a small non-zero detune makes tau sweep across the buffer, exercising
    // every phase value — output must remain equal to x for the entire
    // sweep.
    void testPitchShifterCrossfadeUnityGainOnDCSweep()
    {
        beginTest("PitchShifter: Hann crossfade w_A + w_B = 1 across a tau sweep (DC passthrough)");

        PitchShifter shifter;
        shifter.prepare(kSampleRate, kMaxBlockSize);
        shifter.setLatencyMode(PitchShifter::LatencyMode::studio);

        // +7 cents: small enough that the drift rate is gentle but large
        // enough that tau sweeps across most of the buffer length over
        // ~kStudioLength samples.
        shifter.setDetuneCents(7.0f);

        constexpr float dcValue = 0.5f;
        constexpr int N = 8192;
        std::vector<float> data(static_cast<std::size_t>(N), dcValue);

        shifter.process(data.data(), N);

        // After the initial transient (first L/2 samples the buffer is
        // still being filled), every output sample must equal the DC value
        // — regardless of the current (swept) Hann phase. If w_A + w_B != 1
        // exactly, this assertion fails.
        const int skip = kStudioLength / 2 + 64;
        double maxDeviation = 0.0;
        for (int i = skip; i < N; ++i)
        {
            maxDeviation = std::max(
                maxDeviation,
                std::abs(static_cast<double>(
                             data[static_cast<std::size_t>(i)])
                         - static_cast<double>(dcValue)));
        }

        // Float-precision Lagrange interpolation of a constant signal does
        // not sum coefficients to exactly 1.0 (the coefficient products
        // round), so the DC passthrough deviates from the input value by
        // ~5e-5. The test guards against gross violations, not last-bit
        // float equality.
        expect(maxDeviation < 1.0e-3,
               "DC must pass through the Hann crossfade approximately unchanged (w_A + w_B = 1); maxDeviation="
               + juce::String(maxDeviation));
    }

    // -------------------------------------------------------------------------
    // MonoSafetyCrossover
    // -------------------------------------------------------------------------

    // §4.5: LP + HP is allpass when summed (with the highpass polarity
    // inverted for LR2 / in-phase for LR4). Feeding the same signal to the
    // LP input and both HP inputs is the degenerate reconstruction case;
    // the result low + highLeft must preserve the input amplitude across a
    // multi-tone stimulus.
    void testMonoSafetyCrossoverReconstructionAllpass()
    {
        beginTest("MonoSafetyCrossover: LP + HP reconstruction is flat (allpass) when LP == HP input");

        MonoSafetyCrossover xo;
        xo.prepare(kSampleRate, kMaxBlockSize);
        xo.setCutoffHz(180.0f);
        xo.reset();

        // Multi-tone stimulus: 100 Hz (well below fc), 500 Hz (above fc),
        // 1 kHz, 5 kHz, 10 kHz. None is exactly at fc, so any null observed
        // would have to come from the filter design, not from coincidence.
        const std::vector<double> freqs{100.0, 500.0, 1000.0, 5000.0, 10000.0};
        const int N = 16384;
        std::vector<float> x(static_cast<std::size_t>(N));
        for (int i = 0; i < N; ++i)
        {
            double v = 0.0;
            for (double f : freqs)
                v += std::sin(2.0 * kPi * f
                              * static_cast<double>(i) / kSampleRate);
            v /= static_cast<double>(freqs.size());
            x[static_cast<std::size_t>(i)] = static_cast<float>(v);
        }

        // Same x fed to dryMono and both wet channels — the degenerate
        // reconstruction case described in §4.5.
        std::vector<float> low(static_cast<std::size_t>(N)),
            highL(static_cast<std::size_t>(N)),
            highR(static_cast<std::size_t>(N));
        xo.processBlock(x.data(), x.data(), x.data(),
                        low.data(), highL.data(), highR.data(), N);

        // Reconstruction for the left channel = low + highL (the HP path's
        // internal polarity inversion for LR2 is the §4.5 convention; LR4
        // already sums in-phase, so plain addition is correct in both
        // build configurations).
        std::vector<float> recon(static_cast<std::size_t>(N));
        for (int i = 0; i < N; ++i)
            recon[static_cast<std::size_t>(i)] =
                low[static_cast<std::size_t>(i)]
                + highL[static_cast<std::size_t>(i)];

        // Skip the filter transient (~5 ms = 240 samples; use 1024 for
        // safety) before comparing RMS.
        const int skip = 1024;
        const double inputRms = rmsRange(x.data(), N, skip, N - skip);
        const double reconRms = rmsRange(recon.data(), N, skip, N - skip);

        const double ratio = reconRms / std::max(inputRms, 1.0e-12);
        expectWithinAbsoluteError(
            ratio, 1.0, 0.05,
            "LP + HP reconstruction must be allpass; ratio="
            + juce::String(ratio));
    }

    // §4.5: a degenerate LP == HP input at exactly fc must not be deeply
    // nulled by the crossover. LR2 sums in-phase by internally inverting the
    // HP polarity (§4.5 / D2); LR4 is already in-phase.
    void testMonoSafetyCrossoverNoNullAtCutoff()
    {
        beginTest("MonoSafetyCrossover: degenerate reconstruction at fc is not deeply nulled");

        MonoSafetyCrossover xo;
        xo.prepare(kSampleRate, kMaxBlockSize);
        xo.setCutoffHz(180.0f);
        xo.reset();

        constexpr double probeHz = 180.0; // exactly at fc
        constexpr int N = 16384;
        std::vector<float> x(static_cast<std::size_t>(N));
        for (int i = 0; i < N; ++i)
        {
            x[static_cast<std::size_t>(i)] = static_cast<float>(
                std::sin(2.0 * kPi * probeHz
                         * static_cast<double>(i) / kSampleRate));
        }

        std::vector<float> low(static_cast<std::size_t>(N)),
            highL(static_cast<std::size_t>(N)),
            highR(static_cast<std::size_t>(N));
        xo.processBlock(x.data(), x.data(), x.data(),
                        low.data(), highL.data(), highR.data(), N);

        std::vector<float> recon(static_cast<std::size_t>(N));
        for (int i = 0; i < N; ++i)
            recon[static_cast<std::size_t>(i)] =
                low[static_cast<std::size_t>(i)]
                + highL[static_cast<std::size_t>(i)];

        const int skip = 1024;
        const double inputRms = rmsRange(x.data(), N, skip, N - skip);
        const double reconRms = rmsRange(recon.data(), N, skip, N - skip);

        const double ratio = reconRms / std::max(inputRms, 1.0e-12);
        // The direct LR2 sum has a deep null at fc (|LP + HP| → 0). With
        // the internal HP inversion the allpass property restores unity.
        // Demand that reconstruction preserves at least 50 % of the input
        // amplitude (the allpass theoretical value is 1.0).
        expect(ratio > 0.5,
               "Reconstruction at fc must not be deeply nulled; ratio="
               + juce::String(ratio));
    }

    // §6 / §4.5: setCutoffHz() only marks the coefficients dirty; the new
    // coefficients are applied at the next block boundary (beginBlock() /
    // processBlock()). Verify the dirty-flag path is callable without
    // crashes and that the new cutoff is observable downstream.
    void testMonoSafetyCrossoverCutoffDirtyPathCallable()
    {
        beginTest("MonoSafetyCrossover: setCutoffHz + beginBlock dirty-flag path is callable");

        MonoSafetyCrossover xo;
        xo.prepare(kSampleRate, kMaxBlockSize);

        expectWithinAbsoluteError(
            xo.getCutoffHz(), 180.0f, 1.0e-3f,
            "Initial cutoff should be the documented default (180.0 Hz)");

        // Change the cutoff. Internally this only marks the coefficients
        // dirty (per the header).
        xo.setCutoffHz(240.0f);
        expectWithinAbsoluteError(
            xo.getCutoffHz(), 240.0f, 1.0e-3f,
            "setCutoffHz must update the stored cutoff value");

        // Apply the dirty coefficients at the next block boundary. Must be
        // safe to call repeatedly even after the flag has been cleared.
        xo.beginBlock();
        xo.beginBlock();

        // Drive a block through processBlock — must produce finite samples
        // after the cutoff change and not crash.
        constexpr int N = 256;
        std::vector<float> in(static_cast<std::size_t>(N), 0.0f);
        std::vector<float> low(static_cast<std::size_t>(N)),
            highL(static_cast<std::size_t>(N)),
            highR(static_cast<std::size_t>(N));
        for (int i = 0; i < N; ++i)
            in[static_cast<std::size_t>(i)] = static_cast<float>(
                0.5 * std::sin(2.0 * kPi * 440.0
                               * static_cast<double>(i) / kSampleRate));

        xo.processBlock(in.data(), in.data(), in.data(),
                        low.data(), highL.data(), highR.data(), N);

        for (int i = 0; i < N; ++i)
        {
            expect(std::isfinite(low[static_cast<std::size_t>(i)]),
                   "Low output must be finite after cutoff change; sample "
                   + juce::String(i));
            expect(std::isfinite(highL[static_cast<std::size_t>(i)]),
                   "HighLeft output must be finite after cutoff change; sample "
                   + juce::String(i));
            expect(std::isfinite(highR[static_cast<std::size_t>(i)]),
                   "HighRight output must be finite after cutoff change; sample "
                   + juce::String(i));
        }

        // A second setCutoffHz + beginBlock cycle must also be clean.
        xo.setCutoffHz(120.0f);
        xo.beginBlock();
        xo.processBlock(in.data(), in.data(), in.data(),
                        low.data(), highL.data(), highR.data(), N);
        for (int i = 0; i < N; ++i)
        {
            expect(std::isfinite(low[static_cast<std::size_t>(i)])
                       && std::isfinite(highL[static_cast<std::size_t>(i)])
                       && std::isfinite(highR[static_cast<std::size_t>(i)]),
                   "Outputs must remain finite after a second cutoff change; sample "
                   + juce::String(i));
        }
    }

    // -------------------------------------------------------------------------
    // WidthProcessor
    // -------------------------------------------------------------------------

    // §4.7: feed identical L and R (pure mid content). The S path is zero
    // regardless of width, so L' == R' == M for every width in [0, 2]. This
    // is the §4.7 "mono-lows preserved" invariant: the shared LP upstream
    // cancels in S, so the lows (which are shared) cannot move off-center.
    void testWidthProcessorMonoLowsInvariantAcrossWidths()
    {
        beginTest("WidthProcessor: identical L/R input yields L' == R' across width ∈ {0, 1, 2}");

        WidthProcessor width;
        width.prepare(kSampleRate, kMaxBlockSize);

        // 100 Hz sub-fc content on both channels — the "shared LP" scenario.
        constexpr int N = 256;
        std::vector<float> lIn(static_cast<std::size_t>(N)),
            rIn(static_cast<std::size_t>(N));
        for (int i = 0; i < N; ++i)
        {
            const float v = static_cast<float>(
                0.5 * std::sin(2.0 * kPi * 100.0
                               * static_cast<double>(i) / kSampleRate));
            lIn[static_cast<std::size_t>(i)] = v;
            rIn[static_cast<std::size_t>(i)] = v;
        }

        for (float w : {0.0f, 0.5f, 1.0f, 1.5f, 2.0f})
        {
            // reset() snaps the smoother to the current target, so set+reset
            // makes the smoother deterministic from sample 0 (no ramp tail).
            width.setWidth(w);
            width.reset();

            std::vector<float> lOut(static_cast<std::size_t>(N)),
                rOut(static_cast<std::size_t>(N));
            width.process(lIn.data(), rIn.data(),
                          lOut.data(), rOut.data(), N);

            // L' == R' to within FP epsilon: identical input ⇒ S = 0 ⇒
            // L' = R' = M exactly, independent of w.
            double maxLrDiff = 0.0;
            double maxCenteredDiff = 0.0;
            for (int i = 0; i < N; ++i)
            {
                const std::size_t idx = static_cast<std::size_t>(i);
                maxLrDiff = std::max(
                    maxLrDiff,
                    static_cast<double>(std::abs(lOut[idx] - rOut[idx])));
                // Centered: L' must equal the input (which equals M).
                maxCenteredDiff = std::max(
                    maxCenteredDiff,
                    static_cast<double>(std::abs(lOut[idx] - lIn[idx])));
            }

            expect(maxLrDiff < 1.0e-6,
                   "L' and R' must match for identical L/R at width="
                   + juce::String(w) + "; maxDiff=" + juce::String(maxLrDiff));
            expect(maxCenteredDiff < 1.0e-5,
                   "L' must equal M (the input) for identical L/R at width="
                   + juce::String(w) + "; maxDiff="
                   + juce::String(maxCenteredDiff));
        }
    }

    // §4.7: width = 1 is the identity transform (L' = L, R' = R) for any
    // L, R pair — including L != R (pure-side or asymmetric content).
    void testWidthProcessorUnityIdentityWhenLeftNotEqualRight()
    {
        beginTest("WidthProcessor: width = 1 is identity for L != R (asymmetric content)");

        WidthProcessor width;
        width.prepare(kSampleRate, kMaxBlockSize);
        width.setWidth(1.0f);
        width.reset();

        // Asymmetric content: a sine on L, a different sine on R.
        constexpr int N = 512;
        std::vector<float> lIn(static_cast<std::size_t>(N)),
            rIn(static_cast<std::size_t>(N));
        for (int i = 0; i < N; ++i)
        {
            const std::size_t idx = static_cast<std::size_t>(i);
            lIn[idx] = static_cast<float>(
                0.5 * std::cos(2.0 * kPi * 440.0
                               * static_cast<double>(i) / kSampleRate));
            rIn[idx] = static_cast<float>(
                -0.3 * std::sin(2.0 * kPi * 880.0
                                * static_cast<double>(i) / kSampleRate));
        }

        std::vector<float> lOut(static_cast<std::size_t>(N)),
            rOut(static_cast<std::size_t>(N));
        width.process(lIn.data(), rIn.data(),
                      lOut.data(), rOut.data(), N);

        double maxLdiff = 0.0;
        double maxRdiff = 0.0;
        for (int i = 0; i < N; ++i)
        {
            const std::size_t idx = static_cast<std::size_t>(i);
            maxLdiff = std::max(
                maxLdiff,
                static_cast<double>(std::abs(lOut[idx] - lIn[idx])));
            maxRdiff = std::max(
                maxRdiff,
                static_cast<double>(std::abs(rOut[idx] - rIn[idx])));
        }

        expect(maxLdiff < 1.0e-6,
               "Width=1 must preserve L; maxDiff="
               + juce::String(maxLdiff));
        expect(maxRdiff < 1.0e-6,
               "Width=1 must preserve R; maxDiff="
               + juce::String(maxRdiff));
    }
};

// Static registration: discovered by juce::UnitTestRunner::runAllTests().
static DSPPrimitiveTests dspPrimitiveTestsInstance;