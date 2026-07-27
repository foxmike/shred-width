# b

## Goal

DSP primitives: fractional delay, dual-head granular pitch shifter, mono-safety crossover, and M/S width.

See ShredWidth_Build_Spec.md sections 3 (signal flow), 4.2-4.5 and 4.7 (per-stage DSP math), 6 (smoothing), 7 (RT-safety), and 8 (memory) for complete math. All DSP classes expose prepare(spec)/process(...)/reset() and allocate nothing outside prepare.

DelayLineFrac: ms->samples fractional delay at the live sampleRate using 3rd-order Lagrange (or a juce::dsp::DelayLine<float, Lagrange3rd> equivalent), preallocated for ceil(50ms*maxSampleRate)+guard; the read distance is smoothed over a >=50 ms ramp (D9) so automation does not pitch-glitch.

PitchShifter (one instance per detune polarity): length-L circular buffer, write pointer advancing one sample/sample, two read heads offset by L/2 expressed as delays behind the write pointer, a DOUBLE drift accumulator tau advanced by dv = 1 - 2^(deltaC/1200) with branchless wrap (if tau>=L tau-=L; if tau<0 tau+=L), 4-point cubic Lagrange interpolation with each neighbor read individually wrapped mod L, and raised-cosine (Hann) crossfade weights keyed to head position so w_A + w_B == 1 exactly (unity gain, no ripple). detuneAmount smoothing feeds the dv computation. Buffer L=2048 for Studio (always allocated); Live uses the first 512 samples.

MonoSafetyCrossover: LR2 = two cascaded 1st-order Butterworth sections per band (juce::dsp::IIR). The LP path operates on the dry mono source x(n) as a single shared instance (centered lows); the HP path operates per shifted+delayed channel (spread highs) with the highpass polarity inverted internally. Provide the SHREDWIDTH_XOVER_ORDER=4 build option (two cascaded 2nd-order Butterworth per band, in-phase LR4). Recompute coefficients only on a monoXover dirty flag at block start, never per-sample.

WidthProcessor: post-assembly M/S width (section 4.7). M=(yL+yR)/2, S=(yL-yR)/2, yL'=M+width*S, yR'=M-width*S with width in [0,2]. The shared LP cancels in S, so the low band contributes zero to Side and width can never move the lows off-center.

## Notes

- Async: Wave 1: b.1 (DelayLineFrac), b.2 (PitchShifter), b.3 (MonoSafetyCrossover), and b.4 (WidthProcessor) are all independent (disjoint files). Wave 2: b.5 (TestMain harness + DSP primitive tests + CMakeLists source/test registration; sole CMakeLists owner) depends on b.1, b.2, b.3, and b.4.
- Risk: b.2 drift accumulator must be double (single precision drifts audibly over minutes); all four Lagrange neighbor reads wrapped mod L. b.3 highpass polarity inverted; LR2/LR4 selected by compile option. b.5 sole CMakeLists owner.
