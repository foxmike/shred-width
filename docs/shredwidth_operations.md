# ShredWidth — Operations Manual

## Plugin Overview
ShredWidth turns a single mono guitar (or any monophonic-in-intent source) into a wide, focused stereo image without pitch wobble and without mono-collapse. It is a static dual-head micro-pitch detuner, Haas-style delay widener, and mono-safe highs width processor. It is not a chorus: there is no LFO anywhere in the signal path. Instead, the pitch offset is a fixed micro-detune, and the width comes from static detune and independent short delays, with the low frequencies folded back to dead-center so the effect survives a mono fold-down.

## Intended Use
ShredWidth is designed to widen mono track sources (especially rhythm or lead electric guitar and bass) while maintaining rock-solid low-frequency phase coherence and mono fold-down compatibility.

## Signal Flow
The internal audio signal path runs from input to output through the following sequential stages:
1. **Mono Source Selector (`monoSource`)**: Extracts a mono signal `x(n)` from a stereo input using either a Sum `0.5 * (L + R)` or Left-only `L` configuration.
2. **Crossover Filter Bank (`monoXover`)**: Splits the mono signal into Low and High frequency bands using a 2nd-order Linkwitz-Riley crossover filter.
   - *Low Band Path*: Routed identically to both Left and Right output channels, remaining dead-centered.
   - *High Band Path (Wet)*: Fed into the granular detuner and delay lines.
3. **Granular Pitch Shifter (`detuneAmount`)**: Shifts the Left channel by $+\text{cents}$ and the Right channel by $-\text{cents}$ using a dual-head crossfading granular pitch-shift engine (utilizing fractional 3rd-order Lagrange interpolation).
4. **Haas Delay Line (`delayL`, `delayR`)**: Delays the Left and Right shifted high-band channels independently by static offsets. Using asymmetric L/R delays avoids comb filtering.
5. **Reconstruction**: Low-band (dry mono) and High-band (processed stereo) paths are summed.
6. **M/S Width Scaler (`width`)**: Applies Mid/Side gain scaling to the high-band portion of the summed signal. Low-frequency signals are excluded from side channel amplification.
7. **Master Mix (`mix`)**: Blends the processed signal with the dry input using an equal-gain crossfader.
8. **Output Trim (`outTrim`)**: Applies final output gain trim.

## Controls and Parameters

| Control | Parameter ID | Range | Default | Unit | Description & DSP Reference |
|:---|:---|:---|:---|:---|:---|
| **Detune Amount** | `detuneAmount` | 0.0 to 25.0 | 7.0 | cents | Symmetrical micro-pitch offset ($+L$, $-R$). Higher values increase the pitch spread. (Granular Pitch Shifter) |
| **Delay Offset L** | `delayL` | 1.0 to 50.0 | 12.5 | ms | Haas delay offset applied to the Left shifted channel. (Haas Delay Line) |
| **Delay Offset R** | `delayR` | 1.0 to 50.0 | 18.0 | ms | Haas delay offset applied to the Right shifted channel. (Haas Delay Line) |
| **Low Mono Crossover** | `monoXover` | 80.0 to 300.0 | 180.0 | Hz | Crossover frequency below which all audio is kept centered. (Crossover Filter Bank) |
| **Stereo Width** | `width` | 0.0% to 200.0% | 100.0% | % | Mid/Side stereo width scaling applied post-crossover to the high band. (M/S Width Scaler) |
| **Mix** | `mix` | 0.0% to 100.0% | 40.0% | % | Crossfades between the dry input and wet processed signal. (Master Mix) |
| **Latency Mode** | `latencyMode` | Studio (0) / Live (1) | Studio (0) | - | Selects granular circular buffer size (2048 vs 512 samples) to trade grain smoothness for lower latency. (Granular Pitch Shifter) |
| **Output Trim** | `outTrim` | -12.0 to +12.0 | 0.0 | dB | Adjusts the final output gain to level-match processing. (Output Trim) |
| **Bypass** | `bypass` | Off (0.0) / On (1.0) | Off (0.0) | - | Bypasses all processing. (Master Bypass) |
| **Mono Source** | `monoSource` | Sum (0) / Left (1) | Sum (0) | - | Selects the mono channel extraction source from a stereo input. Accessible via DAW parameter automation. (Mono Source Selector) |

## Stereo and Mono Behavior
ShredWidth is designed to be mono-safe by construction:
1. Lows are shared/centered via a 2nd-order Linkwitz-Riley crossover; only the high band is spread.
2. Mid/Side width scaling is applied **after** the crossover assembly. Since the lowpass contribution is identical on both channels, it cancels out of the Side channel, ensuring width scaling never moves the lows.
3. This guarantees that folding down the output to mono preserves full low-end energy and phase coherence.

## Latency and Lookahead (PDC)
ShredWidth reports a nominal (average) latency to the host for plugin delay compensation (PDC) based on the Latency Mode:
- **Studio Mode**: Buffer size $L = 2048$ samples. Reports **1024 samples** of latency ($\approx 21.3$ ms @ 48 kHz). Provides the smoothest detune, longest grains, and cleanest sustain.
- **Live Mode**: Buffer size $L = 512$ samples. Reports **256 samples** of latency ($\approx 5.3$ ms @ 48 kHz). Recommended for low-latency real-time tracking (grains are shorter, which can introduce slight warbling on long sustains).

The dry path is delayed internally to align with the nominal shifting delay. The Haas delay offsets (`delayL` / `delayR`) are excluded from PDC. Hosts that cache latency metadata may require a transport restart or audio-engine reset after changing Latency Mode.

## Bypass Behavior
Controlled via the host-managed `bypass` parameter. When bypassed, the input audio is passed transparently to the output.

## Automation Behavior
- All parameters (including `latencyMode`, `bypass`, and `monoSource`) are host-automatable.
- **`latencyMode` Switching**: Changing latency mode mid-render re-points the circular buffer length and calls `setLatencySamples()`. An internal 10 ms crossfade masks the buffer discontinuity. To ensure smooth PDC updates, switching when the host transport is stopped is recommended.
- **Control Smoothing**: `delayL` and `delayR` use a long smoothing ramp ($\ge 50$ ms on read distance), and `detuneAmount` uses 20 ms smoothing, to keep automation transitions smooth.

## Metering and Visual Feedback
- **Goniometer (`GoniometerComponent`)**: Real-time vectorscope displaying the Left/Right stereo phase distribution of the output.
- **Correlation Meter (`CorrelationMeter`)**: Displays phase correlation between Left and Right output channels from -1.0 to +1.0, aiding in mono compatibility validation.

## Presets and State Recall
Five factory presets are embedded in the plugin binary as APVTS XML:

### 1. Tight Lead
*A focused, slightly widened lead tone that sits in a dense mix without washing out. Smaller detune and tighter delays keep the image narrow.*
- **Detune Amount**: $\pm5$ cents
- **Delay L / R**: 10.0 / 14.0 ms
- **Low Mono Crossover**: 200 Hz
- **Width**: 90%
- **Mix**: 30%
- **Latency Mode**: Studio
- **Output Trim**: 0 dB

### 2. Wide Lead (Default)
*Classic wide lead stereo spread with asymmetric delays and moderate detune. Retains dry punch.*
- **Detune Amount**: $\pm7$ cents
- **Delay L / R**: 12.5 / 18.0 ms
- **Low Mono Crossover**: 180 Hz
- **Width**: 100%
- **Mix**: 40%
- **Latency Mode**: Studio
- **Output Trim**: 0 dB

### 3. Huge Solo
*Stereo width designed for solo sections. watch the correlation meter—mono compatibility is reduced but the bass remains centered.*
- **Detune Amount**: $\pm14$ cents
- **Delay L / R**: 20.0 / 28.0 ms
- **Low Mono Crossover**: 150 Hz
- **Width**: 140%
- **Mix**: 55%
- **Latency Mode**: Studio
- **Output Trim**: 0 dB

### 4. Mono-Safe Subtle
*Designed for broadcast or live situations where mono fold-down is guaranteed. Keeps correlation high ($P > 0.6$).*
- **Detune Amount**: $\pm4$ cents
- **Delay L / R**: 8.0 / 11.0 ms
- **Low Mono Crossover**: 250 Hz
- **Width**: 70%
- **Mix**: 25%
- **Latency Mode**: Studio
- **Output Trim**: 0 dB

### 5. Live Tracking
*Optimized low-latency preset running in Live Mode for real-time tracking.*
- **Detune Amount**: $\pm6$ cents
- **Delay L / R**: 10.0 / 13.0 ms
- **Low Mono Crossover**: 200 Hz
- **Width**: 90%
- **Mix**: 35%
- **Latency Mode**: Live
- **Output Trim**: 0 dB

## DAW and Format Notes
- **Supported Formats**: VST3, CLAP, LV2, Standalone.
- **System Requirements**: Linux, Windows, macOS hosts with any VST3/CLAP/LV2 supporting DAW. All millisecond/cent calculations adapt to the host's active sample rate.
