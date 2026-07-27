# ShredWidth — CI Verification Checklist

**Source:** ShredWidth_Build_Spec.md §14  
**Determinism note:** ShredWidth is fully deterministic (D1) — same input + same params ⇒
bit-identical output. This makes every item below mechanically verifiable without
subjective ear passes.

---

## CI-Runnable Checks (1–10)

| # | Check | What it asserts | How to run | Pass/fail signal |
|---|-------|-----------------|------------|------------------|
| **1** | **RT-safety static/dynamic** | No heap allocation, no locks, no `juce::String`, no forbidden APIs on the audio thread. `juce::ScopedNoDenormals` present in `processBlock`. | `bash scripts/check_rt_safety.sh` from repo root. This is a pure source-text grep gate over the extracted `processBlock`, `MeterTap::pushSample`, and `MeterTap::pushBlock` bodies (see script header for scope rationale). | Exit 0 = PASS. Exit 1 = forbidden pattern or missing `ScopedNoDenormals`. |
| **2** | **Null / determinism** | Same input + same params across two separate runs produces bit-identical output (checksum). Guards against hidden state / uninitialised reads. | `ctest -R Determinism` or run the `ShredWidth_Tests` executable (juce::UnitTest-based). | Test failure on any checksum mismatch. |
| **3** | **Bypass null** | True-bypass output == input (sample-exact, accounting for reported latency). | `ctest -R Bypass` or the dedicated unit test. | Test failure if bypass output differs from input. |
| **4** | **Mix=0 passthrough** | Output == dry input delayed by the reported PDC value (sample-exact within the alignment delay — should be exact if alignment is integer; documented tolerance). | `ctest -R MixZero` or the dedicated unit test. | Test failure if passthrough output deviates beyond expected tolerance. |
| **5** | **Mono-lows invariant** | **Key correctness property.** Feed a sub-`f_cross` sine (e.g. 100 Hz with Xover 180 Hz). Assert `L(n) == R(n)` to within FP epsilon at all Width 0–200%. Proves §4.7 holds in code. | `ctest -R MonoLows` or the dedicated unit test. | Test failure if L ≠ R below crossover at any Width setting. |
| **6** | **Unity-gain crossfade** | Assert `w_A + w_B == 1` across the buffer sweep — no amplitude ripple in the dual-head granular shifter. | `ctest -R UnityCrossfade` or the dedicated unit test. | Test failure if crossfade-weight sum deviates from unity. |
| **7** | **Pitch accuracy** | FFT the +ΔC shifter output of a pure tone; assert peak lands within tolerance of `f · 2^(ΔC/1200)`. | `ctest -R PitchAccuracy` or the dedicated unit test. | Test failure if FFT peak is outside the allowed cent tolerance. |
| **8** | **Correlation sanity** | At Width=200% + large detune, assert correlation `ρ` drops (wide image). At Width=0, assert `ρ ≈ 1` (mono). Folded-to-mono sum has no null below `f_cross`. | `ctest -R Correlation` or the dedicated unit test. | Test failure if correlation bounds are violated. |
| **9** | **Latency report** | Assert `getLatencySamples()` == `L/2` for the active mode (1024 for Studio, 256 for Live). Assert it updates on mode switch and the reported value is never sample-exact (nominal). | `ctest -R LatencyReport` or the dedicated unit test. | Test failure if reported latency is wrong or unchanged after mode switch. |
| **10** | **pluginval** | Run JUCE `pluginval` at strictness level 8+ on VST3 and CLAP builds. Must pass all validation. LV2 and Standalone smoke-load only (load & unload). | `bash scripts/run_pluginval.sh` from repo root (see script for target paths). | Exit 0 = PASS. Exit non-zero if any validation fails. |

### Test target shorthand

All CI-runnable checks (1–9) are exposed as juce::UnitTest cases compiled into the `ShredWidth_Tests` executable and registered with CTest:

```bash
# Build tests (assumes CMake configured; use scripts/build_all.sh if needed)
cmake --build build/linux-relwithdebinfo --target ShredWidth_Tests

# Run all unit tests
ctest --test-dir build/linux-relwithdebinfo -V

# Run a specific check by regex
ctest --test-dir build/linux-relwithdebinfo -R MonoLows
```

Check **10** requires a separate `scripts/run_pluginval.sh` invocation after the plugin binaries have been built.

---

## Reference scripts

| Script | Purpose | Located at |
|--------|---------|------------|
| `check_rt_safety.sh` | Static grep gate for forbidden audio-thread APIs (check 1). Does not require CMake on PATH. | `scripts/check_rt_safety.sh` |
| `run_pluginval.sh` | Runs JUCE pluginval at strictness 8+ on VST3/CLAP; smoke-loads LV2 and Standalone. | `scripts/run_pluginval.sh` |
| `build_all.sh` | Locates CLion-bundled CMake, configures with `linux-relwithdebinfo` preset, and builds all format targets. Not on PATH — run from repo root. | `scripts/build_all.sh` |

> **Note:** `cmake` is resolved by `build_all.sh` via a known CLion bundle path list (see script). It is **not** assumed to be on the general `PATH`. CTest invocations should use the same CMake binary.

---

## Manual / Non-CI-Gated Checks

The following are **not** automated in CI and require a human ear pass or host-matrix testing:

- **Ear pass / sonic review.** Does it *sound* like a wide lead? Items 1–10 catch every mechanical failure, but tonal quality, stereo image character, and the musical feel of the crossfade still need an experienced listener with monitoring headphones. Suggested: A/B against an H3000 or SPX90 wide-lead patch, or against a reference recording.
- **Full host matrix.** Verify that VST3, CLAP, LV2, and Standalone binaries load, process audio, report latency, and survive session save/restore in:
  - REAPER (Linux + Windows)
  - Bitwig Studio (Linux)
  - Ardour / Mixbus (LV2)
  - Renoise (VST3)
  - JUCE AudioPluginHost (all formats)
  
  Key LV2 caveat: some hosts require the port metadata (including latency) to be explicitly refreshed after a latency-mode switch — see the [LV2 port-metadata note in the manual](../shredwidth_manual.md#lv2-host-port-metadata-and-latency-refresh).

---

## CI pipeline integration hints

1. **Order matters:** Run `scripts/check_rt_safety.sh` first — it is the fastest and catches build-stopping violations.
2. **Build → Test → pluginval:** Build all targets via `scripts/build_all.sh`, then run `ctest` for checks 1–9, then `scripts/run_pluginval.sh` for check 10.
3. **Determinism baseline:** Keep a known-good checksum file committed alongside the test data. A CI job that fails determinism on identical code indicates a non-deterministic code path has been introduced.
4. **Cross-platform:** Checks 1–9 are platform-agnostic; `scripts/check_rt_safety.sh` relies on `rg` or `grep -E` (available on Linux, macOS, Windows Git Bash). `scripts/run_pluginval.sh` is Linux-first; adapt for macOS/Windows as needed.

---

*End of verification checklist. For build-system details see `scripts/build_all.sh`; for the full DSP design rationale see ShredWidth_Build_Spec.md.*