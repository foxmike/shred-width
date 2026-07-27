// =============================================================================
// ShredWidth - Engine Integration Tests
// =============================================================================
// Engine-level tests for ShredWidthEngine covering the §14 verification
// items 2, 3, 5, and 9: determinism / null, Mix=0 passthrough, mono-lows
// invariant, and latency report. Public API only — no engine internals are
// inspected.
//
// Bypass policy
// -------------
// The plugin-level "true bypass" (output = input, ignoring the engine
// entirely) lives in the PluginProcessor; at the engine level the equivalent
// is Mix=0 (the wet path is silenced). §14 item 3 is therefore exercised
// here as the engine-level Mix=0 passthrough rather than by instantiating
// an AudioProcessor fixture.
//
// Test inventory
// --------------
//   * §14 item 2  — Determinism / null: identical input + params over two
//                   runs produce bit-identical output (FNV-1a checksum over
//                   IEEE-754 bit patterns + per-sample exact equality).
//   * §14 item 3  — Mix=0 dry-aligned passthrough: with Mix=0 the engine
//                   outputs the input delayed by getReportedLatencySamples()
//                   (integer-sample alignment ⇒ sample-exact).
//   * §14 item 5  — Mono-lows invariant: sub-fc sine (100 Hz, fc=180 Hz) ⇒
//                   L(n) == R(n) across width ∈ {0, 1, 2} after settling.
//                   The test uses detune=0 and τ_L == τ_R to make the HP
//                   paths bit-equal so the §4.7 shared-LP property reduces
//                   to a direct L/R equality check.
//   * §14 item 9  — Latency report: getReportedLatencySamples is 1024 in
//                   Studio, 256 in Live, and updates immediately on mode
//                   switch without re-prepare (D5: pre-allocated Studio
//                   worst case, mode switch repoints without realloc).
//
// Uses juce::UnitTest only (mirrors tests/DSPPrimitiveTests.cpp). No
// third-party testing framework is pulled in.
// =============================================================================

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include "Source/DSP/ShredWidthEngine.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace
{

constexpr double kSampleRate = 48000.0;
constexpr int kMaxBlockSize = 512;
constexpr int kStudioLatency = 1024; // 2048 / 2
constexpr int kLiveLatency = 256;   // 512 / 2
constexpr double kPi = 3.14159265358979323846;

// Build a deterministic `numSamples`-sample sine buffer. Pure function;
// produces the same bytes every call so two runs against the same engine
// produce byte-equal stimuli.
std::vector<float> makeSine(double frequencyHz, int numSamples,
                            double sampleRate, float amplitude = 0.5f,
                            double phaseOffset = 0.0)
{
    std::vector<float> out(static_cast<std::size_t>(numSamples));
    for (int i = 0; i < numSamples; ++i)
    {
        out[static_cast<std::size_t>(i)] = static_cast<float>(
            amplitude * std::sin(2.0 * kPi * frequencyHz
                                 * (static_cast<double>(i) + phaseOffset)
                                 / sampleRate));
    }
    return out;
}

// FNV-1a 32-bit hash over the IEEE-754 bit patterns of `data`. Two buffers
// are bit-identical iff their checksums match (collision probability
// ≈ 2^-32, well below FP-rounding noise).
uint32_t checksumFloats(const float *data, int numSamples)
{
    uint32_t hash = 0x811c9dc5u; // FNV-1a offset basis
    for (int i = 0; i < numSamples; ++i)
    {
        const uint32_t bits =
            std::bit_cast<uint32_t>(data[static_cast<std::size_t>(i)]);
        hash ^= bits;
        hash *= 0x01000193u; // FNV-1a prime
    }
    return hash;
}

// Max absolute difference between two contiguous float ranges of length
// `numSamples`, optionally starting at `startIndex` of each range. Used
// for the mono-lows invariant test where the comparison begins past the
// pitch-shifter latency + filter settling.
double maxAbsDiff(const float *a, const float *b, int numSamples,
                  int startIndex = 0)
{
    double maxDiff = 0.0;
    for (int i = 0; i < numSamples; ++i)
    {
        const std::size_t idx = static_cast<std::size_t>(startIndex + i);
        const double d = std::abs(static_cast<double>(a[idx])
                                  - static_cast<double>(b[idx]));
        maxDiff = std::max(maxDiff, d);
    }
    return maxDiff;
}

} // namespace

class EngineTests final : public juce::UnitTest
{
public:
    EngineTests()
        : juce::UnitTest("ShredWidth Engine Integration",
                         "ShredWidth/Engine")
    {}

    void runTest() override
    {
        testDeterminismIdenticalRunsProduceBitIdenticalOutput();
        testMixZeroIsDryAlignedToReportedLatency();
        testMonoLowsInvariantAcrossWidths();
        testLatencyReportAndModeSwitchUpdatesValue();
    }

    // -------------------------------------------------------------------------
    // §14 item 2 — Determinism / null
    //
    // §1 (D1): "same input + same params ⇒ bit-identical output". Two
    // ShredWidthEngine instances, prepared identically and fed identical
    // stimuli, must produce byte-equal L and R output buffers. Catches
    // hidden state (uninitialised reads, race conditions, time-dependent
    // randomness).
    // -------------------------------------------------------------------------
    void testDeterminismIdenticalRunsProduceBitIdenticalOutput()
    {
        beginTest("ShredWidthEngine: two identical runs produce bit-identical output (FNV-1a checksum + per-sample exact equality)");

        // Non-trivial deterministic stimulus: 440 Hz on L, 880 Hz on R so
        // neither channel is a degenerate copy of the other. The phase
        // offset on R prevents accidental L/R co-linearity.
        constexpr int N = 4096;
        std::vector<float> inL = makeSine(440.0, N, kSampleRate, 0.5f, 0.0);
        std::vector<float> inR = makeSine(880.0, N, kSampleRate, 0.3f, 0.25);

        auto runOnce = [&](ShredWidthEngine &engine,
                           const std::vector<float> &srcL,
                           const std::vector<float> &srcR,
                           std::vector<float> &dstL,
                           std::vector<float> &dstR) {
            engine.setDetuneCents(7.0f);
            engine.setDelayMsL(12.5f);
            engine.setDelayMsR(18.0f);
            engine.setCrossoverHz(180.0f);
            engine.setWidth(1.5f);
            engine.setMix(0.6f);
            engine.setLatencyMode(ShredWidthEngine::LatencyMode::Studio);
            engine.setMonoSource(ShredWidthEngine::MonoSource::Sum);
            engine.prepare(kSampleRate, kMaxBlockSize);

            dstL = srcL;
            dstR = srcR;
            engine.processBlock(dstL.data(), dstR.data(), N, 2);
        };

        ShredWidthEngine engine1;
        std::vector<float> outL1;
        std::vector<float> outR1;
        runOnce(engine1, inL, inR, outL1, outR1);

        ShredWidthEngine engine2;
        std::vector<float> outL2;
        std::vector<float> outR2;
        runOnce(engine2, inL, inR, outL2, outR2);

        // FNV-1a over IEEE-754 bit patterns — bit-identical ⇔ hashes match.
        const uint32_t hashL1 = checksumFloats(outL1.data(), N);
        const uint32_t hashL2 = checksumFloats(outL2.data(), N);
        const uint32_t hashR1 = checksumFloats(outR1.data(), N);
        const uint32_t hashR2 = checksumFloats(outR2.data(), N);

        expect(hashL1 == hashL2,
               "L-channel FNV-1a checksum must match between identical runs");
        expect(hashR1 == hashR2,
               "R-channel FNV-1a checksum must match between identical runs");

        // Per-sample exact equality as a secondary guard; the first
        // disagreement (if any) localises the failure for diagnostics.
        int firstLdiff = -1;
        int firstRdiff = -1;
        for (int i = 0; i < N; ++i)
        {
            if (firstLdiff < 0 && outL1[i] != outL2[i])
                firstLdiff = i;
            if (firstRdiff < 0 && outR1[i] != outR2[i])
                firstRdiff = i;
        }
        expectEquals(firstLdiff, -1,
                     "L-channel output must be bit-exact across identical runs");
        expectEquals(firstRdiff, -1,
                     "R-channel output must be bit-exact across identical runs");
    }

    // -------------------------------------------------------------------------
    // §14 item 3 — Mix=0 dry-aligned passthrough
    //
    // The engine-level "true bypass" equivalent is Mix=0: the wet path is
    // silenced (1 - mix = 1, mix = 0), so the output is exactly the
    // original input routed through DryAlignDelay, integer-aligned to the
    // reported PDC. The plugin-level true-bypass (which short-circuits the
    // engine entirely) lives above the engine in PluginProcessor — this
    // test exercises the engine strictly.
    //
    // The dry-align delay uses an integer read index with no Lagrange
    // interpolation, so the alignment is sample-exact — not just "near".
    // The first `latency` samples come from the zero-initialised dry-align
    // buffer (no input has reached the read slot yet) and must be exactly
    // zero.
    // -------------------------------------------------------------------------
    void testMixZeroIsDryAlignedToReportedLatency()
    {
        beginTest("ShredWidthEngine: Mix=0 ⇒ output == input delayed by getReportedLatencySamples() (Studio = 1024 samples, integer-exact)");

        ShredWidthEngine engine;
        // Set Mix=0 BEFORE prepare so mixSmoothed starts at 0 (no 20 ms
        // ramp from the default 0.4 to 0). Setters called pre-prepare only
        // populate the parameter fields; the (unallocated) smoother is not
        // touched.
        engine.setMix(0.0f);
        engine.setLatencyMode(ShredWidthEngine::LatencyMode::Studio);
        engine.setDetuneCents(0.0f);
        engine.setDelayMsL(12.5f);
        engine.setDelayMsR(18.0f);
        engine.setCrossoverHz(180.0f);
        engine.setWidth(1.0f);
        engine.prepare(kSampleRate, kMaxBlockSize);

        expectEquals(engine.getReportedLatencySamples(), kStudioLatency,
                     "Studio mode must report 1024 (= 2048/2) samples latency");

        constexpr int N = 4096;
        std::vector<float> inL = makeSine(440.0, N, kSampleRate, 0.5f, 0.0);
        std::vector<float> inR = makeSine(880.0, N, kSampleRate, 0.3f, 0.25);

        std::vector<float> outL = inL;
        std::vector<float> outR = inR;
        engine.processBlock(outL.data(), outR.data(), N, 2);

        // First `latency` samples read from the zero-initialised buffer.
        // Must be exactly zero — integer alignment, no Lagrange, no
        // interpolation rounding.
        for (int i = 0; i < kStudioLatency; ++i)
        {
            expectEquals(outL[i], 0.0f,
                         "Output L during pre-latency window must be exactly zero");
            expectEquals(outR[i], 0.0f,
                         "Output R during pre-latency window must be exactly zero");
        }

        // Post-latency window: output is exactly the input delayed by 1024
        // samples, sample-for-sample bit-equal.
        for (int i = kStudioLatency; i < N; ++i)
        {
            expectEquals(outL[i], inL[i - kStudioLatency],
                         "Output L must equal input L delayed by 1024 samples");
            expectEquals(outR[i], inR[i - kStudioLatency],
                         "Output R must equal input R delayed by 1024 samples");
        }
    }

    // -------------------------------------------------------------------------
    // §14 item 5 — Mono-lows invariant (the key correctness property)
    //
    // §4.7 proof: the shared LP lives in Mid only; S = (hpL - hpR)/2
    // exactly cancels the LP contribution, so width scaling cannot move
    // the lows off-center. Feed a sub-fc sine (100 Hz with Xover 180 Hz)
    // on both channels and assert L(n) == R(n) across width ∈ {0, 1, 2}
    // after settling past the L/2 = 1024 sample latency + filter settling.
    //
    // Test configuration notes
    // ------------------------
    //   * detune = 0     — makes pitch_up(x) == pitch_down(x) == x
    //                       delayed by L/2. Eliminates the pitch path as
    //                       a stereo source so the equality reduces to a
    //                       direct §4.7 shared-LP check.
    //   * τ_L == τ_R     — symmetric delay offsets make the HP paths
    //                       bit-equal. With sub-fc input the HP
    //                       contribution is small but non-zero; the
    //                       symmetric delays are what lets us assert the
    //                       equality to FP precision rather than "near
    //                       zero with HP-leakage tolerance".
    // -------------------------------------------------------------------------
    void testMonoLowsInvariantAcrossWidths()
    {
        beginTest("ShredWidthEngine: sub-fc sine (100 Hz, fc=180 Hz) ⇒ L(n) == R(n) across width ∈ {0, 1, 2} (mono-lows invariant, FP-exact)");

        constexpr double kSubFcHz = 100.0;
        constexpr int kTotalSamples = 8192;
        constexpr int kSkipSamples = 4096; // > 1024 latency + IIR settling

        // Each width setting gets a freshly-prepared engine so the
        // pitch-shift / dry-align / filter state is identical at sample 0
        // (no manual reset needed between iterations).
        for (float width : {0.0f, 1.0f, 2.0f})
        {
            ShredWidthEngine engine;
            engine.setDetuneCents(0.0f);    // pin pitch to fixed delay
            engine.setDelayMsL(12.5f);
            engine.setDelayMsR(12.5f);      // symmetric τ → HP paths identical
            engine.setCrossoverHz(180.0f);
            engine.setMix(1.0f);            // full wet path
            engine.setWidth(width);
            engine.setLatencyMode(ShredWidthEngine::LatencyMode::Studio);
            engine.setMonoSource(ShredWidthEngine::MonoSource::Sum);
            engine.prepare(kSampleRate, kMaxBlockSize);

            // Sub-fc sine on both channels (L == R input).
            std::vector<float> inL = makeSine(kSubFcHz, kTotalSamples,
                                              kSampleRate, 0.5f, 0.0);
            std::vector<float> inR = inL;

            std::vector<float> outL = inL;
            std::vector<float> outR = inR;
            engine.processBlock(outL.data(), outR.data(), kTotalSamples, 2);

            // L == R exactly in the settled region. With detune=0 and
            // symmetric τ the HP paths are bit-equal, M/S scaling is
            // deterministic, and the shared LP assembly gives the same
            // `lp` to both channels — so any L/R disagreement is a §4.7
            // bug, not HP leakage.
            const double diff = maxAbsDiff(outL.data(), outR.data(),
                                           kTotalSamples - kSkipSamples,
                                           kSkipSamples);
            expect(diff == 0.0,
                   "L(n) - R(n) must be exactly zero for sub-fc input at width="
                   + juce::String(width, 1)
                   + "; maxAbsDiff=" + juce::String(diff));
        }
    }

    // -------------------------------------------------------------------------
    // §14 item 9 — Latency report
    //
    // getReportedLatencySamples() returns L/2 for the active LatencyMode
    // (Studio → 1024, Live → 256). Switching modes updates the reported
    // value immediately and does not require re-prepare (D5: buffers
    // pre-allocated for Studio worst case; mode switch repoints without
    // realloc). After the switch the engine must still process finite
    // audio samples (the internal masked crossfade masks the buffer
    // discontinuity at the switch instant).
    // -------------------------------------------------------------------------
    void testLatencyReportAndModeSwitchUpdatesValue()
    {
        beginTest("ShredWidthEngine: getReportedLatencySamples is 1024 (Studio) / 256 (Live) and updates on mode switch without re-prepare");

        ShredWidthEngine engine;
        engine.setLatencyMode(ShredWidthEngine::LatencyMode::Studio);
        engine.prepare(kSampleRate, kMaxBlockSize);

        expectEquals(engine.getReportedLatencySamples(), kStudioLatency,
                     "Studio mode must report 1024 (= 2048/2) samples latency");
        expect(engine.getLatencyMode() == ShredWidthEngine::LatencyMode::Studio,
               "getLatencyMode should mirror Studio after prepare");

        // Switch to Live. No re-prepare; the reported value must update
        // immediately because getReportedLatencySamples() reads the
        // requested mode (the internal crossfade is a separate concern).
        engine.setLatencyMode(ShredWidthEngine::LatencyMode::Live);
        expectEquals(engine.getReportedLatencySamples(), kLiveLatency,
                     "After switch to Live, reported latency must be 256 (= 512/2)");
        expect(engine.getLatencyMode() == ShredWidthEngine::LatencyMode::Live,
               "getLatencyMode should mirror Live after switch");

        // The engine must still process audio correctly after the switch
        // (D5: no realloc; the PitchShifter repoints to the Live ring
        // length and a short internal crossfade masks the discontinuity).
        {
            constexpr int N = 512;
            std::vector<float> inL = makeSine(440.0, N, kSampleRate, 0.5f);
            std::vector<float> inR = makeSine(880.0, N, kSampleRate, 0.5f);
            std::vector<float> outL = inL;
            std::vector<float> outR = inR;
            engine.processBlock(outL.data(), outR.data(), N, 2);
            for (int i = 0; i < N; ++i)
            {
                expect(std::isfinite(outL[i]),
                       "Output L must remain finite after Live switch; sample "
                       + juce::String(i));
                expect(std::isfinite(outR[i]),
                       "Output R must remain finite after Live switch; sample "
                       + juce::String(i));
            }
        }
        expectEquals(engine.getReportedLatencySamples(), kLiveLatency,
                     "Reported latency must still be 256 after processing post-switch");

        // Switch back to Studio. Reported value updates; engine keeps working.
        engine.setLatencyMode(ShredWidthEngine::LatencyMode::Studio);
        expectEquals(engine.getReportedLatencySamples(), kStudioLatency,
                     "After switch back to Studio, reported latency must be 1024");

        // Idempotency: setting the same mode (no transition active) must
        // not change the reported value.
        engine.setLatencyMode(ShredWidthEngine::LatencyMode::Studio);
        expectEquals(engine.getReportedLatencySamples(), kStudioLatency,
                     "Setting the same LatencyMode must leave the reported value unchanged");
    }
};

// Static registration: discovered by juce::UnitTestRunner::runAllTests().
static EngineTests engineTestsInstance;
