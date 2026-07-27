# e

## Goal

Factory presets, correctness tests, verification scripts, and release documentation.

See ShredWidth_Build_Spec.md section 12 (presets), section 14 (verification and test plan), section 15 (build integration), and section 16 (open decisions, all defaulted).

Factory presets as APVTS XML in Resources/Presets/, embedded via BinaryData: Tight Lead (Detune +/-5, Delay 10/14 ms, Xover 200 Hz, Width 90%, Mix 30%); Wide Lead (+/-7, 12.5/18 ms, 180 Hz, 100%, 40%); Huge Solo (+/-14, 20/28 ms, 150 Hz, 140%, 55%); Mono-Safe Subtle (+/-4, 8/11 ms, 250 Hz, 70%, 25%); Live Tracking (Live mode, +/-6, 10/13 ms, 200 Hz, 90%, 35%).

Correctness tests that need the full output path: pitch accuracy (FFT the +deltaC output of a pure tone and assert the peak lands within tolerance of f*2^(deltaC/1200)) and correlation sanity (Width=200% with large detune drops rho; Width=0 gives rho ~ 1; the folded-to-mono sum has no null below f_cross). Register preset BinaryData and the final test target in CMakeLists.

Verification scripts: an RT-safety grep gate that fails the build if processBlock references new, malloc, juce::String, or a lock and asserts ScopedNoDenormals is present; and a pluginval runner at strictness 8+ on VST3 and CLAP with LV2 and Standalone smoke-load. Scripts resolve cmake and pluginval through build_all.sh / CLion because cmake is not on PATH.

Documentation: a verification checklist covering the section 14 CI-runnable checks (RT-safety, determinism/null, bypass null, Mix=0 passthrough, mono-lows invariant, unity-gain crossfade, pitch accuracy, correlation sanity, latency report, pluginval) and an honest note that the ear-pass and full host matrix are manual; and a user manual covering nominal (not sample-exact) latency, the hidden monoSource toggle, latencyMode being automatable but discouraged, the LV2 host port-metadata note, and the factory preset descriptions.

## Notes

- Async: Wave 1: e.1 (preset XML assets), e.3 (RT-safety grep-gate script), e.4 (pluginval + smoke-load script), and e.5 (verification + manual docs) are all independent. Wave 2: e.2 (correctness tests + preset BinaryData embedding + CMakeLists; sole CMakeLists owner) depends on e.1.
- Risk: e.2 sole CMakeLists owner (preset BinaryData + final tests). Verification scripts assume cmake/pluginval resolved via build_all.sh / CLion (cmake not on PATH). Host pluginval beyond CI and the ear-pass are manual (section 14).
