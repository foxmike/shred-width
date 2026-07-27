# ShredWidth — User Manual

**Version 1.0.0**  
**Format:** VST3 · CLAP · LV2 · Standalone  
**Developer:** nSpace Systems

---

## What is ShredWidth?

ShredWidth turns a single mono guitar (or any monophonic-in-intent source) into a wide,
focused stereo image **without pitch wobble and without mono-collapse**.

It is a **static dual-head micro-pitch detuner + Haas-style delay widener + mono-safe
highs width processor**. It is not a chorus. There is **no LFO anywhere in the signal
path** — the pitch offset is a fixed micro-detune (the H3000 / SPX90 "wide lead" trick),
and the width comes from static detune ± independent short delays, with the sub-band
folded back to dead-centre so the effect survives a mono fold-down.

### Key characteristics

- **Static detune only.** Sustained leads and fast runs keep their pitch focus. No seasick modulation.
- **Mono-safe by construction.** Lows are shared/centred via a Linkwitz-Riley crossover; only the highs spread. Correlation stays well above −1 even folded to mono.
- **Fully deterministic.** Same input + same parameters = bit-identical output. No randomness, no hidden state.
- **Low CPU.** No oversampling (no nonlinearity in the signal path). Lagrange 3rd-order interpolation + IIR crossover.

---

## Controls

| Control | Parameter ID | Range | Default | Description |
|---------|-------------|-------|---------|-------------|
| Detune Amount | `detuneAmount` | 0–25 cents | 7.0 | Micro-pitch offset applied symmetrically (+L, −R). Larger values = more detune spread. |
| Delay Offset L | `delayL` | 1–50 ms | 12.5 | Left-channel Haas delay from the shifted signal. Primary width mechanism at low detune. |
| Delay Offset R | `delayR` | 1–50 ms | 18.0 | Right-channel Haas delay. Asymmetric defaults (12.5 / 18.0) give non-comb-filtered depth. |
| Low Mono Crossover | `monoXover` | 80–300 Hz | 180 Hz | Linkwitz-Riley 2nd-order crossover frequency. Below this, all content is shared/centred. |
| Stereo Width | `width` | 0–200 % | 100 % | Mid/Side width scaler applied post-crossover. Lows stay centred at any value (§4.7). |
| Mix | `mix` | 0–100 % wet | 40 % | Equal-gain dry/wet crossfade. Default emphasises the effect while retaining dry punch. |
| Latency Mode | `latencyMode` | Studio / Live | Studio | Trade-off between grain smoothness and latency (see [Latency & PDC](#latency--pdc)). |
| Output Trim | `outTrim` | −12 … +12 dB | **0 dB** | Post-mix gain trim for matching wet vs. dry during A/B. No audible effect at default. |
| Bypass | `bypass` | On / Off | Off | Host-managed true bypass. |

### Hidden parameter (not on main GUI in v1)

| Parameter ID | Type | Default | Description |
|-------------|------|---------|-------------|
| `monoSource` | Choice (Sum / Left) | **Sum** | Selects the mono source derived from a stereo input. **Sum** = `0.5·(L+R)`; **Left** = `L` only. Accessible via host automation or advanced parameter list. |

---

## Latency & PDC

ShredWidth reports a **nominal** (not sample-exact) latency to the host for plug-in delay
compensation (PDC). The reported value is `L/2` where `L` is the circular buffer size for
the selected Latency Mode.

| Mode | Buffer `L` | Reported latency (`L/2`) | Equivalent @ 48 kHz | Character |
|------|-----------|--------------------------|---------------------|-----------|
| **Studio** | 2048 | **1024 samples** | ≈ 21.3 ms | Smoothest detune, longest grains, cleanest sustain. Recommended for mixing / recording. |
| **Live** | 512 | **256 samples** | ≈ 5.3 ms | Lower latency for tracking; shorter grains → slightly more audible warble on very long sustains. |

**Important:** The granular shifter's true delay drifts as the read head moves across the
buffer. `L/2` is the **average** delay — it is a nominal value, not sample-exact.
The dry path is time-aligned to this same nominal value so dry and wet remain coherent.
The Haas delay offsets (Delay L / Delay R) are **excluded** from the reported latency —
they are the effect, not a compensation target.

### Latency Mode switching

`latencyMode` is a host-automatable parameter, but **live automation is discouraged**.
Switching mode mid-render re-points the circular buffer length and calls
`setLatencySamples()` + `updateHostDisplay()`. A brief (~10 ms) internal dry/wet-neutral
crossfade masks the buffer-content discontinuity, but some hosts may still exhibit a
brief audio glitch or confuse their PDC engine. Prefer to switch when transport is
stopped.

---

## Mono-safe design note

ShredWidth guarantees that content below the Low Mono Crossover (`monoXover`) is always
bit-identical on both channels. This is achieved by:

1. Extracting a shared mono source `x(n)` from the input (Sum or Left-only).
2. Applying the Linkwitz-Riley lowpass to `x(n)` once, and routing the result identically
   to both output channels.
3. Applying the highpass (shifted + delayed signal) independently per channel.
4. Applying Mid/Side width **after** the crossover assembly — the lowpass contribution
   cancels out of the Side signal, so width scaling never moves the lows.

**Result:** A mono fold-down of the output preserves full low-end energy and phase
coherence. The correlation meter will rarely dip below 0 even at extreme width settings.

---

## Factory presets

Five factory presets are shipped with ShredWidth. They are stored as APVTS XML and
embedded via `BinaryData`.

### Tight Lead

| Parameter | Value |
|-----------|-------|
| Detune Amount | ±5 cents |
| Delay L / R | 10 / 14 ms |
| Low Mono Crossover | 200 Hz |
| Width | 90 % |
| Mix | 30 % |
| Latency Mode | Studio |
| Output Trim | 0 dB |

A focused, slightly widened lead tone that sits in a dense mix without washing out.
The smaller detune and tighter delays keep the image narrow enough for rhythm work.

### Wide Lead (default-ish)

| Parameter | Value |
|-----------|-------|
| Detune Amount | ±7 cents |
| Delay L / R | 12.5 / 18 ms |
| Low Mono Crossover | 180 Hz |
| Width | 100 % |
| Mix | 40 % |
| Latency Mode | Studio |
| Output Trim | 0 dB |

The default patch. Asymmetric delays + moderate detune produce a classic "wide
lead" stereo spread. The 40 % mix retains dry punch.

### Huge Solo

| Parameter | Value |
|-----------|-------|
| Detune Amount | ±14 cents |
| Delay L / R | 20 / 28 ms |
| Low Mono Crossover | 150 Hz |
| Width | 140 % |
| Mix | 55 % |
| Latency Mode | Studio |
| Output Trim | 0 dB |

Aggressive width for solo sections. Pushed detune, wide delays, and 140 % width
create a large stereo image. Watch the correlation meter — mono compatibility is
reduced but the bass remains centred.

### Mono-Safe Subtle

| Parameter | Value |
|-----------|-------|
| Detune Amount | ±4 cents |
| Delay L / R | 8 / 11 ms |
| Low Mono Crossover | 250 Hz |
| Width | 70 % |
| Mix | 25 % |
| Latency Mode | Studio |
| Output Trim | 0 dB |

Minimum-width preset with very high mono compatibility (folded correlation
`ρ > 0.6`). Designed for broadcast or live situations where mono fold-down is
guaranteed. The higher crossover (250 Hz) keeps more of the mids centred.

### Live Tracking

| Parameter | Value |
|-----------|-------|
| Detune Amount | ±6 cents |
| Delay L / R | 10 / 13 ms |
| Low Mono Crossover | 200 Hz |
| Width | 90 % |
| Mix | 35 % |
| **Latency Mode** | **Live** |
| Output Trim | 0 dB |

Low-latency preset for real-time tracking (≈ 5.3 ms reported @ 48 kHz). Slightly
shorter grains and a moderate detune keep the latency low while still providing a
usable stereo spread.

---

## Plugin formats

| Format | Supported | Notes |
|--------|-----------|-------|
| **VST3** | ✅ | Full feature set, parameter automation, PDC |
| **CLAP** | ✅ | Full feature set (requires `clap-juce-extensions` submodule at build time) |
| **LV2** | ✅ | Full feature set. See [LV2 caveat](#lv2-host-port-metadata-and-latency-refresh) below. |
| **Standalone** | ✅ | JUCE standalone wrapper with device/sample-rate selection; identical GUI |
| **AU (macOS)** | ❌ | Not in scope for v1 |

---

## LV2 host port-metadata and latency refresh

When ShredWidth's `latencyMode` changes, it calls `setLatencySamples()` and
`updateHostDisplay()`. In VST3 and CLAP hosts this is sufficient to trigger a PDC
recalculation. **Some LV2 hosts** (e.g. Ardour, Mixbus) cache port metadata at plugin
instantiation and may **not** automatically re-read the latency port after a mode
switch.

If you change Latency Mode in an LV2 host and notice audio alignment drift:

1. Try toggling the plugin's bypass or re-instantiating the plugin.
2. Save and reload the session.
3. If the host supports it, manually refresh port metadata via the host's connection
   manager.

This is a known limitation of the LV2 port-metadata model and is not specific to
ShredWidth.

---

## Automation notes

- All controls (including `latencyMode` and `bypass`) are host-automatable.
- **`latencyMode` live automation is discouraged** — see [Latency Mode switching](#latency-mode-switching) for details.
- `delayL` / `delayR` use a long smoothing ramp (≥ 50 ms on the read distance) to
  prevent pitch glitches when automated mid-note.
- `detuneAmount` smoothing (20 ms) prevents zipper noise on the drift rate.
- Hidden parameter `monoSource` is automatable but is not surfaced on the main GUI
  in v1. It can be accessed via the host's parameter list or MIDI learn.

---

## System requirements

- **OS:** Linux (tested), Windows, macOS (buildable)
- **Host:** Any VST3, CLAP, or LV2 compatible DAW
- **CPU:** No special requirements (no oversampling, no SIMD dependencies)
- **Sample rate:** Any rate supported by the host (all ms/cm conversions use live `sampleRate`)

---

*For build and verification details see [docs/verification/shredwidth_verification.md](verification/shredwidth_verification.md).  
For the full DSP design specification see ShredWidth_Build_Spec.md.*