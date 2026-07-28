# ShredWidth — Operations Guide

## Purpose
ShredWidth is a static dual-head micro-pitch detuner, Haas-style delay widener, and mono-safe highs width processor designed for electric guitar and bass. By shifting the pitch of the left and right channels by a fixed cent amount in opposite directions and applying independent short delays, it generates a wide and focused stereo lead image without LFO pitch wobbles. A mono-safety crossover ensures that all low frequencies remain phase-aligned and centered, preventing mono-collapse when folded down.

## Signal Flow
The internal audio signal path runs from input to output through the following sequential stages:
1. **Mono Source Selector (`monoSource`)**: Extracts a mono signal from a stereo input using either a Sum `0.5 * (L + R)` or Left-only `L` configuration.
2. **Crossover Filter Bank (`monoXover`)**: Splits the mono signal into Low and High frequency bands using a 2nd-order Linkwitz-Riley crossover.
   - *Low Band Path*: Routed identically to both Left and Right output channels, remaining dead-centered.
   - *High Band Path (Wet)*: Fed into the granular detuner and delay lines.
3. **Granular Pitch Shifter (`detuneAmount`)**: Shifts the Left channel by $+\text{cents}$ and the Right channel by $-\text{cents}$ using a dual-head crossfading granular pitch-shift engine (fractional 3rd-order Lagrange interpolation).
4. **Haas Delay Line (`delayL`, `delayR`)**: Delays the Left and Right shifted high-band channels independently by static offsets.
5. **Reconstruction**: Low-band (dry mono) and High-band (processed stereo) paths are summed.
6. **M/S Width Scaler (`width`)**: Applies Mid/Side gain scaling to the high-band portion of the summed signal. Low-frequency signals are excluded from side channel amplification.
7. **Master Mix (`mix`)**: Blends the processed signal with the dry input using an equal-gain crossfader.
8. **Output Trim (`outTrim`)**: Applies final output gain trim.

---

## Controls Reference

All parameters are located on a single main interface panel:

### Detune Amount
- **Parameter ID:** `detuneAmount`
- **Type:** float
- **Range:** 0.0 to 25.0 cents, default 7.0 cents
- **Unit:** cents
- **What it does:** Sets the static micro-pitch offset applied symmetrically ($+L$, $-R$). Higher values increase the pitch spread. Reference DSP stage: Granular Pitch Shifter.

### Delay Offset L
- **Parameter ID:** `delayL`
- **Type:** float
- **Range:** 1.0 to 50.0 ms, default 12.5 ms
- **Unit:** ms
- **What it does:** Sets the Haas delay offset applied to the Left shifted channel. Reference DSP stage: Haas Delay Line.

### Delay Offset R
- **Parameter ID:** `delayR`
- **Type:** float
- **Range:** 1.0 to 50.0 ms, default 18.0 ms
- **Unit:** ms
- **What it does:** Sets the Haas delay offset applied to the Right shifted channel. Using asymmetric left/right delays avoids comb filtering. Reference DSP stage: Haas Delay Line.

### Low Mono Crossover
- **Parameter ID:** `monoXover`
- **Type:** float
- **Range:** 80.0 to 300.0 Hz, default 180.0 Hz
- **Unit:** Hz
- **What it does:** Sets the crossover frequency below which all audio is kept centered. Reference DSP stage: Crossover Filter Bank.

### Stereo Width
- **Parameter ID:** `width`
- **Type:** float
- **Range:** 0.0% to 200.0%, default 100.0%
- **Unit:** %
- **What it does:** Controls Mid/Side stereo width scaling applied post-crossover to the high band. Reference DSP stage: M/S Width Scaler.

### Mix
- **Parameter ID:** `mix`
- **Type:** float
- **Range:** 0.0% to 100.0%, default 40.0%
- **Unit:** %
- **What it does:** Crossfades between the dry input signal and the wet processed signal. Reference DSP stage: Master Mix.

### Latency Mode
- **Parameter ID:** `latencyMode`
- **Type:** choice
- **Range:** Studio (0), Live (1), default Studio (0)
- **Unit:** none
- **What it does:** Sets the granular circular buffer size. Studio uses a 2048-sample buffer for maximum smoothness; Live uses a 512-sample buffer to reduce tracking latency. Reference DSP stage: Granular Pitch Shifter.

### Output Trim
- **Parameter ID:** `outTrim`
- **Type:** float
- **Range:** -12.0 to +12.0 dB, default 0.0 dB
- **Unit:** dB
- **What it does:** Adjusts the final output gain to level-match processing. Reference DSP stage: Output Trim.

### Bypass
- **Parameter ID:** `bypass`
- **Type:** bool
- **Range:** Off (0.0) to On (1.0), default Off (0.0)
- **Unit:** none
- **What it does:** Host-controlled bypass.

### Mono Source (Hidden)
- **Parameter ID:** `monoSource`
- **Type:** choice
- **Range:** Sum (0), Left (1), default Sum (0)
- **Unit:** none
- **What it does:** Selects the mono channel extraction source from a stereo input. Accessible via DAW parameter automation. Reference DSP stage: Mono Source Selector.

---

## Metering and Visualization
- **Goniometer (`GoniometerComponent`)**: Real-time vectorscope displaying the Left/Right stereo phase distribution of the output signal.
- **Correlation Meter (`CorrelationMeter`)**: Shows the phase correlation between Left and Right output channels from -1.0 to +1.0. Allows the user to verify mono compatibility.

## Preset System
Five factory presets are compiled into the binary via `ShredWidthPresets`:
1. `Tight Lead`: Moderate detuning ($\pm5$ cents) and short delays ($10/14$ ms) with a $200$ Hz crossover, optimized for tracking.
2. `Wide Lead`: The default preset. Detuning at $\pm7$ cents, delays at $12.5/18$ ms, $180$ Hz crossover.
3. `Huge Solo`: Widest solo image. Detuning at $\pm14$ cents, delays at $20/28$ ms, $150$ Hz crossover, width at $140\%$.
4. `Mono-Safe Subtle`: Clean broadcast preset. Detuning at $\pm4$ cents, delays at $8/11$ ms, $250$ Hz crossover, width at $70\%$, maintaining high correlation.
5. `Live Tracking`: Optimized low-latency preset running in Live Mode, detuning at $\pm6$ cents.
Presets are saved and loaded by the DAW using APVTS XML serialization.

## Latency
- Latency is determined by the circular buffer size of the active Latency Mode.
- Studio Mode introduces **1024 samples** of latency ($\approx 21.3$ ms at 48 kHz).
- Live Mode introduces **256 samples** of latency ($\approx 5.3$ ms at 48 kHz).
- The dry path is delayed internally to align with the nominal shifting delay. Latency changes are reported to the host via `setLatencySamples()`.

## Supported Formats
- VST3, CLAP, LV2, Standalone.

## Known Limitations
- **LV2 Latency Updates**: In some LV2 hosts (e.g. Ardour), changes to `latencyMode` during active playback may not trigger host PDC update automatically, requiring the plugin to be bypassed or the project to be reloaded.
- **Buffer Mode Switch Click**: Changing `latencyMode` mid-render causes a brief 10 ms crossfade to mask buffer re-pointing glitches. Active automation is discouraged.
- **Mono Source Control**: The `monoSource` parameter is a hidden parameter not exposed on the main GUI.
