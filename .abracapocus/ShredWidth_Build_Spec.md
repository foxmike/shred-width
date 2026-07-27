# ShredWidth — Complete Build Specification
**Static Micro-Pitch Detuner & Lead Widener**
nSpace Systems · C++20 / JUCE · VST3 · CLAP · LV2 · Standalone

---

## 1. Executive Summary

ShredWidth turns a single mono guitar (or any monophonic-in-intent source) into a wide, focused stereo image **without pitch wobble and without mono-collapse**. It is not a chorus. There is **no LFO anywhere in the signal path** — the pitch offset is a fixed micro-detune (the H3000 / SPX90 "wide lead" trick), and the width comes from static detune + independent short delays, with the sub-band folded back to dead-center so the effect survives a mono fold-down.

The distinguishing constraints versus a stock chorus/detuner:

- **Static detune only.** Sustained leads and fast runs keep their pitch focus. No seasick modulation.
- **Mono-safe by construction.** Lows are shared/centered via a Linkwitz-Riley crossover; only the highs spread. Correlation stays well above −1 even folded to mono.
- **Honest metering.** A goniometer + correlation meter give the player a real mono-compatibility readout, not a decorative animation.

This document specifies everything required to build the plugin end-to-end: signal flow, per-stage DSP math, full parameter map, real-time safety rules, GUI/visualization behavior, format/bus handling, file layout, and a verification plan.

---

## 2. Design Principles & Locked Decisions

These are the non-obvious calls made while turning the source spec into a buildable design. Each is a **Decision** with rationale; override any of them before build kickoff.

- **D1 — No LFO, no randomness.** ShredWidth is fully deterministic. (Random-walk humanization belongs to ShredDouble, not here.) Same input + same params ⇒ bit-identical output. This makes null-testing trivial and CI-verifiable.
- **D2 — LR2 crossover with explicit polarity handling.** A 2nd-order Linkwitz-Riley lowpass and highpass are **180° out of phase at all frequencies**; a naïve `LP + HP` reconstruction produces a null at the crossover. Because our LP and HP act on *different* signals (LP on the dry centered source, HP on the shifted/delayed source) a hard null does not occur, but the phase relationship still colors the transition. We standardize on the LR2 formulation and document the polarity so the crossover region is predictable. (LR4 is offered as a build-time option in §4.5 if a steeper, in-phase transition is preferred.)
- **D3 — M/S width is applied *after* the crossover assembly.** This gives mono-lows-for-free: the shared LP content lives entirely in Mid, so scaling Side never moves the lows. Proven in §4.7. Width can range 0–200% without ever destabilizing the bass image.
- **D4 — Delay offsets are *effect*, not latency.** The 1–50 ms L/R offsets are the Haas-style width mechanism and are **not** PDC-compensated. Only the pitch-shifter's nominal algorithmic delay is reported to the host (§4.9). Dry path is time-aligned to that reported delay.
- **D5 — Buffers are sized for the largest mode at `prepareToPlay` and never reallocated.** Studio↔Live is a *pointer/length* change into a pre-allocated circular buffer, not a `malloc`. Latency Mode may glitch by design at the switch instant (buffer content discontinuity) — acceptable, and gated behind a short internal crossfade to mask it.
- **D6 — Correlation is computed on the audio thread; only smoothed scalars cross to the UI.** Raw sample pairs for the goniometer cross via a lock-free FIFO (decimated). No locks, no allocations, on the audio thread ever.
- **D7 — No oversampling.** There is no nonlinearity in this plugin (no saturation, no waveshaping). Lagrange interpolation + IIR crossover do not require oversampling. Keeps CPU and latency low.

---

## 3. Signal Flow

```
                                 ShredWidth signal path (per processBlock)
                                 =========================================

  in (stereo or mono)
        │
        ▼
  ┌───────────────┐     x(n)  (mono source: 0.5*(L+R) if stereo, else L)
  │ Mono summing  │─────────────────────────────────────────────────────┐
  └───────────────┘                                                       │
        │                                                                 │
        │  x(n)                                                           │  x(n) → DRY
        ├──────────────────────────────┐                                 │  (aligned to
        │                              │                                  │   reported PDC)
        ▼                              ▼                                  │
  ┌──────────────┐              ┌──────────────┐                          │
  │ PitchShifter │  +ΔC cents   │ PitchShifter │  −ΔC cents               │
  │  (head A/B)  │              │  (head A/B)  │                          │
  └──────┬───────┘              └──────┬───────┘                          │
         │ x_+C(n)                     │ x_-C(n)                          │
         ▼                             ▼                                  │
  ┌──────────────┐              ┌──────────────┐                          │
  │ Delay  τ_L   │              │ Delay  τ_R   │   (independent offsets)   │
  └──────┬───────┘              └──────┬───────┘                          │
         │                             │                                  │
         ▼                             ▼                                  │
  ┌──────────────┐              ┌──────────────┐                          │
  │  LR2 HP @fc  │              │  LR2 HP @fc  │   (spread only the highs) │
  └──────┬───────┘              └──────┬───────┘                          │
         │ hpL                         │ hpR                              │
         │                             │                                  │
         │        ┌──────────────┐     │                                  │
         │        │  LR2 LP @fc  │◄────┘── shared LP taken from x(n) ──────┤
         │        └──────┬───────┘  (centered sub-band → both channels)    │
         │               │ lp                                             │
         ▼               ▼                                                │
   L_wet = lp + hpL                                                       │
   R_wet = lp + hpR                                                       │
         │                                                                │
         ▼                                                                │
  ┌───────────────┐                                                       │
  │ M/S Width (D3)│  M=(L+R)/2, S=(L-R)/2·(width%); lows stay in M         │
  └──────┬────────┘                                                       │
         │ L_wet', R_wet'                                                 │
         ▼                                                                ▼
  ┌───────────────────────────────────────────────────────────────────────┐
  │  Dry/Wet Mix:  out = (1−mix)·dry + mix·wet                              │
  └───────────────────────────────────────────────────────────────────────┘
         │
         ▼
   out (stereo)  ──►  [tap L/R to Goniometer FIFO + Correlation estimator]
```

Everything above runs sample-accurate inside `processBlock`. The only thread crossing is the metering tap (audio → UI), which is lock-free.

---

## 4. DSP Architecture (Detailed)

### 4.1 Input conditioning / mono source

The effect is defined on a single mono excitation `x(n)`:

- Stereo input: `x(n) = 0.5·(L_in(n) + R_in(n))`.
- Mono input: `x(n) = L_in(n)`.

Rationale: the "one guitar sounds enormous" trick requires a single decorrelation source. Feeding independent L/R into independent shifters would double CPU and produce an unpredictable image. The **dry** signal preserved through the mix is the *original* input (see §4.8), so stereo material passed at Mix=0 is untouched.

**Decision D8:** provide a hidden/advanced `monoSource` toggle (Sum vs. Left-only) defaulting to Sum. Not surfaced on the main GUI in v1.

### 4.2 Dual-head granular crossfading pitch shifter

The workhorse. One instance per detune polarity (+ΔC and −ΔC). Each instance owns a length-`L` circular buffer `b[]` (write pointer `w` advancing 1 sample/sample).

Cents → ratio → read-pointer speed:

$$
r = 2^{\Delta C / 1200}, \qquad \Delta v = 1 - r
$$

For a micro-detune of +7 cents, `r ≈ 1.004050`, `Δv ≈ −0.004050`. The read-head *delay* `τ` drifts slowly; because `Δv` is tiny the drift period is long (`≈ L/|Δv|` samples ≈ several seconds), so a single grain covers many notes before it wraps.

Two read heads, offset by `L/2`, both expressed as **delays behind the write pointer**:

$$
\tau_A(n) = \big(\tau_A(n{-}1) + \Delta v\big) \bmod L, \qquad
\tau_B(n) = \big(\tau_A(n) + \tfrac{L}{2}\big) \bmod L
$$

Read positions (fractional): `p_A = w − τ_A`, `p_B = w − τ_B` (both taken mod `L`).

Raised-cosine (Hann) crossfade weights keyed to head position in the buffer, so whichever head is near the wrap discontinuity is faded out:

$$
w_A(n) = \tfrac12\Big(1 - \cos\tfrac{2\pi\,\tau_A(n)}{L}\Big), \qquad
w_B(n) = \tfrac12\Big(1 - \cos\tfrac{2\pi\,\tau_B(n)}{L}\Big)
$$

Note `w_A + w_B = 1` exactly, because the two positions are `L/2` apart and `cos(θ) + cos(θ+π) = 0`. This guarantees **unity gain** across the crossfade with no amplitude ripple.

Shifted output:

$$
x_{shift}(n) = w_A(n)\,\tilde{x}\!\big(p_A(n)\big) + w_B(n)\,\tilde{x}\!\big(p_B(n)\big)
$$

where `x̃(·)` denotes a fractional-index read (§4.3).

**Implementation notes**
- `τ_A` is a `double` accumulator (single-precision drift error accumulates audibly over minutes). Keep the accumulator in `double`, read as `float`.
- Wrap with a branchless `fmod`-free subtract (`if (τ >= L) τ -= L; if (τ < 0) τ += L;`) — cheaper and denormal-safe.
- Buffer length `L` is set by Latency Mode (§4.9): Studio `L = 2048`, Live `L = 512`. Always allocate the Studio size; Live uses the first 512 samples.

### 4.3 Fractional read — 3rd-order (4-point) Lagrange interpolation

For read index `p = i + f` (`i = floor(p)`, `f ∈ [0,1)`), sample the four neighbors `y_{-1}, y_0, y_1, y_2` and apply cubic Lagrange:

$$
\begin{aligned}
c_{-1} &= -\tfrac{1}{6} f (f-1)(f-2) \\
c_{0}  &=  \tfrac{1}{2} (f+1)(f-1)(f-2) \\
c_{1}  &= -\tfrac{1}{2} (f+1) f (f-2) \\
c_{2}  &=  \tfrac{1}{6} (f+1) f (f-1)
\end{aligned}
\qquad
\tilde{x}(p) = c_{-1}y_{-1} + c_0 y_0 + c_1 y_1 + c_2 y_2
$$

Lagrange chosen over linear (audible HF loss + aliasing on the detuned copy) and over sinc (unnecessary CPU for a ±25-cent shift). 3rd-order is the standard sweet spot for granular detune. All four neighbor reads must be individually wrapped mod `L`.

### 4.4 Independent delay offsets (Haas)

Each shifted channel passes through its own delay line before the crossover:

- `τ_L` from **Delay Offset L** (default 12.5 ms), `τ_R` from **Delay Offset R** (default 18.0 ms).
- Fractional (ms → samples at current `sampleRate`), so parameter automation is smooth; use the same Lagrange reader or a `juce::dsp::DelayLine<float, Lagrange3rd>`-equivalent.
- These are the primary width mechanism at low detune settings and give the image asymmetric, non-comb-filtered depth. They are **not** PDC-compensated (D4).

**Decision D9:** delay offsets are smoothed with a long ramp (≥ 50 ms) so live automation doesn't pitch-glitch; the ramp is on the *read distance*, not the write.

### 4.5 Mono-safety crossover (Linkwitz-Riley, 2nd order)

Split at `f_cross` (Low Mono Crossover, 80–300 Hz, log taper). Two roles:

- **LP path** operates on the **dry mono source** `x(n)` → shared, centered lows.
- **HP path** operates on each **shifted+delayed** signal → the spread highs.

LR2 = two cascaded matched 1st-order sections per band (Butterworth Q). In `s`-domain, normalized to `ω_c`:

$$
H_{LP}(s) = \frac{1}{(s/\omega_c + 1)^2}, \qquad
H_{HP}(s) = \frac{(s/\omega_c)^2}{(s/\omega_c + 1)^2}
$$

**Polarity note (D2):** the direct sum has a null at `f_cross`:

$$
H_{LP}(j\omega_c) + H_{HP}(j\omega_c) = \frac{1 + (j)^2}{(1+j)^2} = 0
$$

Here LP and HP act on *different* signals, so no hard null appears in the output — but a coherent design keeps the phase behavior in mind. Implement each band as `juce::dsp::IIR::Filter` with Butterworth LP/HP coefficients cascaded ×2 (LR2). Standardize band summation with the **highpass polarity inverted** internally so that, in the degenerate case where the shifted and dry signals coincide (Mix=1, ΔC→0, τ→0), the reconstruction is flat rather than notched.

**Build-time option:** `#define SHREDWIDTH_XOVER_ORDER 4` swaps LR2 for LR4 (two cascaded 2nd-order Butterworth per band, in-phase summation, steeper 24 dB/oct). Default is LR2 for the gentler, more "musical" bass hand-off and lower CPU/latency.

Coefficient recompute on `f_cross` change: recompute off a parameter-change flag, apply on the next block boundary; do **not** recompute per-sample.

### 4.6 Channel assembly

$$
\begin{aligned}
y_L(n) &= H_{LP}[x(n)] \; + \; H_{HP}\big[x_{shift,+C}(n-\tau_L)\big] \\
y_R(n) &= H_{LP}[x(n)] \; + \; H_{HP}\big[x_{shift,-C}(n-\tau_R)\big]
\end{aligned}
$$

The `H_{LP}[x(n)]` term is computed **once** and shared to both channels (single filter instance) — this is what guarantees the sub-band is bit-identical L=R and therefore perfectly centered.

### 4.7 Stereo Width (M/S) — mono-lows-preserved (proof)

Convert the assembled wet pair to Mid/Side, scale Side by `width ∈ [0,2]` (0–200%), reconstruct:

$$
M = \tfrac12(y_L + y_R), \quad S = \tfrac12(y_L - y_R), \quad
y_L' = M + \text{width}\cdot S, \quad y_R' = M - \text{width}\cdot S
$$

Substitute the shared-LP assembly (`lp` identical on both channels):

$$
S = \tfrac12\big[(lp + hp_L) - (lp + hp_R)\big] = \tfrac12(hp_L - hp_R)
$$

The `lp` term cancels exactly. **The low band contributes zero to Side**, therefore width scaling can never move the lows off-center regardless of the width setting. QED — this is why width is applied post-assembly (D3) and why 200% width is safe.

At `width = 0`: `y_L' = y_R' = M` → fully mono. At `width = 1`: identity. At `width = 2`: Side doubled (aggressive, correlation-meter should be watched).

### 4.8 Dry/Wet mix

$$
\text{out}_{L/R}(n) = (1-\text{mix})\cdot d_{L/R}(n) + \text{mix}\cdot y'_{L/R}(n)
$$

- **Dry `d`** = the *original input* (stereo preserved if stereo in; mono dupe if mono in), delayed by the reported PDC latency (§4.9) so dry and wet are phase-coherent at the crossover hand-off.
- Equal-gain (not equal-power) crossfade — this is a widener, not a reverb; the wet is derived from the dry and largely correlated, so equal-gain avoids a loudness bump at 50%.
- Mix default **40% wet**.

### 4.9 Latency modes & PDC

| Mode | Buffer `L` | Nominal shifter delay to report | Character |
|---|---|---|---|
| **Studio** | 2048 | `L/2 = 1024` samples | Smoothest detune, longest grains, cleanest sustain |
| **Live** | 512 | `L/2 = 256` samples | Lower latency for tracking; shorter grains → slightly more warble on long sustains |

- A granular shifter's true delay is **not constant** (the read head drifts across the buffer). We report a fixed **nominal** value = `L/2` for the active mode via `setLatencySamples()`, and time-align the dry path to that same value so dry/wet stay coherent. This is transparent to the host and honest about the average delay; document it as nominal, not sample-exact, in the manual.
- **Delay offsets τ_L/τ_R are excluded** from the reported latency (D4) — they are the effect.
- Example @ 48 kHz: Studio ≈ 21.3 ms reported; Live ≈ 5.3 ms reported.
- On Latency Mode change: recompute `L`, call `setLatencySamples()` and `updateHostDisplay()` (or `AudioProcessorListener` latency-changed path). Mask the buffer discontinuity with a short (~10 ms) internal dry/wet-neutral crossfade (D5). Prefer switching when transport is stopped; if switched live, the crossfade prevents a click at the cost of a brief character morph.

---

## 5. Parameters (APVTS)

All parameters live in a single `juce::AudioProcessorValueTreeState`. IDs are stable strings (never renumber — presets and host automation depend on them). Version the layout with a `parameterLayoutVersion` in the value tree state so future migrations are clean.

| GUI Name | Param ID | Type | Range | Default | Skew / Taper | Unit | Smoothing |
|---|---|---|---|---|---|---|---|
| Detune Amount | `detuneAmount` | float | 0 … 25 | 7.0 | linear | cents (±, split L/R) | 20 ms |
| Delay Offset L | `delayL` | float | 1 … 50 | 12.5 | linear | ms | 50 ms (read-dist) |
| Delay Offset R | `delayR` | float | 1 … 50 | 18.0 | linear | ms | 50 ms (read-dist) |
| Low Mono Crossover | `monoXover` | float | 80 … 300 | 180 | log (skew ≈ 0.35) | Hz | block-rate coeff |
| Stereo Width | `width` | float | 0 … 200 | 100 | linear | % | 20 ms |
| Mix | `mix` | float | 0 … 100 | 40 | linear | % wet | 20 ms |
| Latency Mode | `latencyMode` | choice | {Studio, Live} | Studio | — | — | special (§4.9) |
| Output Trim | `outTrim` | float | −12 … +12 | 0.0 | linear | dB | 20 ms |
| Bypass | `bypass` | bool | — | false | — | — | host-managed |

Notes:
- `detuneAmount` is a single control applied as `+amount` to the L shifter and `−amount` to the R shifter (symmetric spread). Do **not** expose two independent detune params in v1.
- `outTrim` added for gain-matching wet vs. dry during A/B; not in the source spec but standard on a widener. **Decision D10** — include it, default 0 dB, no audible effect at default.
- Skew for `monoXover`: use `NormalisableRange<float>` with a logarithmic skew so the knob feels even across 80–300 Hz.

---

## 6. Parameter Smoothing & Coefficient Updates

- Use `juce::SmoothedValue<float, ValueSmoothingTypes::Linear>` for gain-like params (`width`, `mix`, `outTrim`, `detuneAmount`).
- `detuneAmount` smoothing feeds the `r`/`Δv` computation; smoothing here prevents zipper on the drift rate. Because the shifter integrates `Δv`, jumps in detune must be ramped or the pitch audibly glitches.
- Crossover coefficients: recompute only when `monoXover` changed since last block (dirty flag), applied at block start. Never per-sample.
- All `SmoothedValue` objects `reset(sampleRate, rampSeconds)` in `prepareToPlay`.
- Read smoothed values **once per block** where per-sample smoothing isn't needed (crossover), and per-sample where it is (width, mix, detune drift).

---

## 7. Real-Time Safety (`processBlock` contract)

Hard rules — CI must be able to assert these:

- `juce::ScopedNoDenormals` at the top of `processBlock`.
- **Zero heap allocation** on the audio thread. All buffers (circular pitch buffers ×2, delay lines ×2, crossover state, dry-align delay) allocated in `prepareToPlay`, sized for the **Studio** worst case.
- **No locks.** Audio→UI metering uses `juce::AbstractFifo` (lock-free SPSC) for goniometer sample pairs; correlation scalars cross as `std::atomic<float>`.
- No `std::vector` resize, no `String`, no `dbg`/logging, no file/GUI calls on the audio thread.
- No unbounded loops; block processing is O(numSamples × fixed work).
- Denormal-safe: crossover IIR states flushed via `ScopedNoDenormals`; branchless wrap in circular buffers.
- Latency Mode switch does **not** reallocate (D5) — it repoints/relengths pre-allocated storage and triggers a masked crossfade.

---

## 8. Memory & Buffer Sizing

Allocated once in `prepareToPlay(sampleRate, maxBlock)`:

- 2 × pitch circular buffers, each `L_max = 2048` floats (+4 guard for Lagrange neighbors) → padded to `2048 + 4`.
- 2 × delay lines sized for `ceil(50 ms × maxSampleRate) + Lagrange guard`. At 192 kHz that's ≈ 9600 + 4 samples each.
- 1 × dry-alignment delay sized for the max reported latency (`1024` samples Studio) × channels.
- Crossover: fixed IIR state (small).
- Metering FIFO: ring buffer of `L/R` pairs, e.g. 4096 frames; decimate writes so the UI thread at 30–60 Hz never starves or overflows.

Total steady-state audio-thread footprint is a few hundred KB — trivial. The point is it's allocated **before** audio starts and never grows.

---

## 9. GUI & Visualizations

### 9.1 Layout

Single window, dark theme, custom `LookAndFeel`. Top-to-bottom / left-to-right:

- **Header:** plugin name, preset selector, A/B compare, bypass LED.
- **Left column (controls):** Detune Amount (large rotary), Width, Mix, Output Trim.
- **Center column (controls):** Delay Offset L, Delay Offset R, Low Mono Crossover, Latency Mode toggle (segmented Studio/Live).
- **Right column (metering):** **Goniometer** (square) on top, **Correlation Meter** (horizontal bar, −1…+1) beneath it.
- Resizable via `juce::ResizableCornerComponent` + constrained aspect; store size in the value tree state.

### 9.2 Goniometer

- Plots the **output** L/R rotated 45° (Lissajous / vectorscope):
  $$
  x_{plot} = \tfrac{1}{\sqrt2}(R - L)\ (\text{Side}),\qquad
  y_{plot} = \tfrac{1}{\sqrt2}(L + R)\ (\text{Mid})
  $$
  Vertical line = mono; a fat vertical spread = healthy width; a lean off-vertical = L/R imbalance; a horizontal smear = anti-correlation (danger).
- Persistence/decay: draw with alpha fade (older dots dimmer) for a phosphor look; decimate to ~1–2k points per frame.
- Reads from the lock-free FIFO; if the FIFO underflows (paused audio), hold last frame.

### 9.3 Correlation meter

- Normalized Pearson cross-correlation of output L/R, one-pole smoothed on the **audio thread**:
  $$
  \rho = \frac{\overline{L R}}{\sqrt{\overline{L^2}\,\overline{R^2}} + \varepsilon}, \qquad \rho \in [-1, 1]
  $$
  where `\overline{(\cdot)}` are one-pole running means with time constant ≈ 300 ms; `ε` guards silence.
- Display: horizontal bar centered at 0. Green `+1…+0.3`, yellow `+0.3…−0.3`, red `<−0.3` (mono-cancellation warning). A held peak-toward-−1 indicator flashes when correlation dips low — the honest "you're about to lose the lows in mono" alarm.
- Only the smoothed `ρ` (a single `std::atomic<float>`) crosses to the UI.

### 9.4 Audio → UI transport

- `juce::AbstractFifo` sized ~4096 stereo frames. Audio thread pushes decimated output pairs (e.g. every Nth sample so the goniometer gets ~a few k points per UI frame). UI thread (`Timer` @ 30–60 Hz) drains what's available.
- Correlation `ρ` and any level readouts: `std::atomic<float>` written on audio thread, read on UI thread. No FIFO needed for scalars.
- Never block, never lock, never allocate in the push path.

### 9.5 LookAndFeel

- Reuse the `CustomLookAndFeel` pattern from AuralMaximizerPro (rotary drawing, colors, fonts). **Watch the `drawPopupMenuItem` API mismatch** already logged on the other project: implement the `LookAndFeel_V4` signature with individual parameters (`isSeparator, isActive, isHighlighted, isTicked, hasSubMenu, text, shortcutKeyText, icon, textColour`), **not** the newer `PopupMenu::Item&` overload — that overload does not exist in the pinned JUCE version. Carry the corrected implementation over from the start rather than re-hitting it.

---

## 10. Plugin Formats & Connectivity

- **Targets:** VST3, CLAP, LV2, Standalone (matching the nSpace house set). AU optional if macOS is in scope — flag if so.
- CLAP via `clap-juce-extensions`; LV2 via JUCE's native LV2 target (JUCE 7.0.6+). Confirm the pinned JUCE version supports LV2 export before wiring the CMake target.
- Parameter automation: every APVTS param is host-automatable; `latencyMode` and `bypass` included. Ensure `latencyMode` automation is discouraged in the manual (latency changes mid-render confuse some hosts) — it's automatable but not recommended.
- Standalone: expose device/sample-rate selection via JUCE's standalone wrapper; the goniometer/correlation UI works identically.
- State: full `AudioProcessorValueTreeState` save/restore via `getStateInformation`/`setStateInformation` (XML → binary). Include `parameterLayoutVersion` for forward migration.
- Latency reporting works in all four formats via `setLatencySamples`; verify each host reads it (some LV2 hosts need the port metadata refreshed).

---

## 11. Bus Layout & Channel Handling

- Supported layouts: **mono→stereo**, **stereo→stereo**. (mono→mono is pointless for a widener; if the host requests it, collapse to Mix=0 dry passthrough and report it.)
- `isBusesLayoutSupported`: accept `{mono in, stereo out}` and `{stereo in, stereo out}`; reject others.
- Internal processing is always stereo out. Mono input is summed trivially (it's already the source `x`).
- Sample-rate agnostic: all ms→sample and Hz→coefficient conversions use the live `sampleRate`. Re-`prepareToPlay` on rate change reallocates the delay lines for the new max — acceptable (not on the audio thread).

---

## 12. Presets

Ship a small factory bank that demonstrates the range and doubles as regression fixtures:

- **Tight Lead** — Detune ±5, Delay 10/14 ms, Xover 200 Hz, Width 90%, Mix 30%.
- **Wide Lead (default-ish)** — ±7, 12.5/18 ms, 180 Hz, 100%, 40%.
- **Huge Solo** — ±14, 20/28 ms, 150 Hz, 140%, 55%.
- **Mono-Safe Subtle** — ±4, 8/11 ms, 250 Hz, 70%, 25% (correlation stays high, verify ρ > 0.6 folded).
- **Live Tracking** — Live mode, ±6, 10/13 ms, 200 Hz, 90%, 35% (low latency).

Presets stored as APVTS XML. The factory bank is embedded via `BinaryData`.

---

## 13. File & Class Structure

Matches the nSpace `Source/` convention (DSP under `DSP/`, GUI under `GUI/`):

```
aural-shredwidth/                      (repo: foxmike/shred-width)
├── CMakeLists.txt
├── scripts/build_all.sh
├── JUCE/                              (git submodule, pinned tag)
└── Source/
    ├── PluginProcessor.h / .cpp       APVTS layout, processBlock, PDC, bus layout
    ├── PluginEditor.h  / .cpp         window, layout, timer, wiring to metering
    ├── DSP/
    │   ├── PitchShifter.h  / .cpp     dual-head granular + Lagrange (one per polarity)
    │   ├── DelayLineFrac.h / .cpp     fractional delay (or wrap juce::dsp::DelayLine)
    │   ├── MonoSafetyCrossover.h/.cpp LR2 (build-opt LR4), shared-LP design
    │   ├── WidthProcessor.h / .cpp    M/S width (§4.7)
    │   └── ShredWidthEngine.h / .cpp  orchestrates the full per-sample path
    └── GUI/
        ├── CustomLookAndFeel.h / .cpp (corrected drawPopupMenuItem signature)
        ├── GoniometerComponent.h/.cpp FIFO drain + phosphor draw
        ├── CorrelationMeter.h  / .cpp atomic read + colored bar
        └── RotarySlider.h      / .cpp (if factored out of LookAndFeel)
```

- `ShredWidthEngine` owns the two `PitchShifter`s, two `DelayLineFrac`s, one `MonoSafetyCrossover`, one `WidthProcessor`, the dry-align delay, and the metering tap. `PluginProcessor` owns one `ShredWidthEngine` and the APVTS; it stays thin.
- All DSP classes: `prepare(spec)`, `process(...)`, `reset()`. No allocation outside `prepare`.

---

## 14. Verification & Test Plan

ShredWidth is unusually testable because it's deterministic (D1). Grounded, CI-runnable checks — not just "unit tests pass":

1. **RT-safety static/dynamic checks.** Assert no allocation on the audio thread (allocator hook / `-fsanitize` harness around a `processBlock` loop). Assert `ScopedNoDenormals` present. Fail the build if `processBlock` calls into forbidden APIs (grep gate on `new`, `malloc`, `juce::String`, `lock`).
2. **Null / determinism test.** Same input + same params over two runs ⇒ bit-identical output (checksum). Guards against hidden state/uninitialized reads.
3. **Bypass null test.** True-bypass output == input (sample-exact, accounting for reported latency).
4. **Mix=0 passthrough.** Output == dry input delayed by reported PDC (sample-exact within Lagrange interpolation error at the align delay — should be exact if align delay is integer; document).
5. **Mono-lows invariant (the key correctness property).** Feed a sub-`f_cross` sine (e.g. 100 Hz with Xover 180 Hz). Assert `L(n) == R(n)` to within FP epsilon at all width settings 0–200% — proves §4.7 holds in code.
6. **Unity-gain crossfade.** Assert `w_A + w_B == 1` across the buffer sweep (no amplitude ripple in the shifter on a steady tone).
7. **Pitch accuracy.** FFT the +ΔC output of a pure tone; assert the peak lands within a tolerance of `f·2^(ΔC/1200)`.
8. **Correlation sanity.** With Width=200% and large detune, assert `ρ` drops (wide); with Width=0 assert `ρ ≈ 1` (mono). Folded-to-mono sum has no null below `f_cross`.
9. **Latency report.** Assert `getLatencySamples()` == `L/2` for the active mode; assert it updates on mode switch.
10. **pluginval.** Run JUCE `pluginval` at strictness 8+ on VST3/CLAP; must pass. LV2/Standalone smoke-load.

The test oracle problem (does it *sound* like a wide lead vs. just measure correct?) still requires an ear pass — but items 1–10 catch every mechanical failure before that.

---

## 15. Build Integration (nSpace workflow)

This spec is the input document for the abracapocus plan; it is not the build script. When materializing:

- Set `ABRACAPOCUS_WORKING_ROOT` and `PROJECT_NAME` **before** any run (missing them lands files in the tool dir).
- Route DSP-heavy files (`PitchShifter`, `MonoSafetyCrossover`, `WidthProcessor`, `ShredWidthEngine`) to `codex_cli`; GUI/header work (`GoniometerComponent`, `CorrelationMeter`, `CustomLookAndFeel`, editor) to `antigravity_cli`; boilerplate/glue to `pi_cli` + OpenRouter (GLM). Never `opencode_cli` for plan-materialize.
- Generate `CMakeLists.txt` + `scripts/build_all.sh` complete, into the project dir, no placeholders. Add JUCE as a submodule pinned to the same tag as AuralMaximizerPro so the `drawPopupMenuItem` signature and LV2 support match a known-good version.
- CMake is CLion-bundled only (not on PATH) — `build_all.sh` must locate it accordingly or the build runs from CLion.
- After each backend step: check `changed_files` is non-empty (exit_code=0 with empty changes = silent no-op; do not assume success).

---

## 16. Open Decisions for Sign-Off

Everything below is decided with a default; call out any you want changed before plan kickoff — none of them block starting:

- **D2** — LR2 default vs. LR4 build option. (Default LR2.)
- **D8** — hidden `monoSource` Sum vs. Left-only. (Default Sum, not on GUI.)
- **D10** — include Output Trim. (Default: included, 0 dB.)
- **AU target** — in scope? (Default: no; VST3/CLAP/LV2/Standalone only.)
- **Latency Mode live-switch** — masked crossfade vs. only-when-stopped. (Default: masked crossfade, allowed live.)

---

*End of ShredWidth specification. ShredDouble spec to follow on the same template.*
