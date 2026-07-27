# c

## Goal

Engine orchestration, audio-thread metering tap, and processBlock integration.

See ShredWidth_Build_Spec.md sections 3 (signal flow), 4.1 (mono source), 4.6 (channel assembly), 4.8 (dry/wet mix), 4.9 (latency modes and PDC), 7 (RT-safety), 9.3-9.4 (correlation and audio->UI transport), and 14 (tests).

MeterTap: audio-thread normalized Pearson correlation rho of the output L/R, one-pole smoothed with a ~300 ms time constant and an epsilon guard, published as a std::atomic<float>; decimated output L/R pairs pushed into a juce::AbstractFifo (lock-free SPSC, ~4096 frames). No lock, allocation, or juce::String on the push path.

ShredWidthEngine: owns the two PitchShifters (+deltaC and -deltaC), two DelayLineFracs (tau_L, tau_R), one MonoSafetyCrossover (shared LP computed once and added to both channels), one WidthProcessor, the dry-align delay, and the MeterTap. Per-sample path: mono sum x = 0.5*(L+R) for stereo input or L for mono input (honor hidden monoSource) -> +/-deltaC pitch shift -> tau_L / tau_R delays -> HP(shifted) + shared LP(x) channel assembly -> M/S width -> equal-gain dry/wet mix against the original input delayed by the reported PDC. Latency-mode change repoints/relengths the pre-allocated buffers (no realloc, D5) and is masked by a ~10 ms internal crossfade. Delay offsets are excluded from reported latency (D4).

PluginProcessor processBlock: ScopedNoDenormals at top; read smoothed parameters (crossover coefficients per block; width, mix, and detune drift per sample); call engine.process; push the final output pairs to the MeterTap; report latency via setLatencySamples = L/2 for the active mode and call updateHostDisplay on mode switch. Register the new sources in CMakeLists.

## Notes

- Async: Wave 1: c.2 (MeterTap) is standalone. Wave 2: c.1 (ShredWidthEngine, which holds a MeterTap member) depends on c.2. Wave 3: c.3 (processBlock wiring + latency reporting + CMakeLists; sole Processor/CMakeLists owner) depends on c.1. Wave 4: c.4 (engine integration tests) depends on c.1.
- Risk: c.3 is the Processor fan-in and sole CMakeLists owner. Latency-mode switch must repoint pre-allocated buffers (no realloc) and mask with a crossfade (D5). Dry path is aligned to the reported PDC only; delay offsets are excluded (D4).
