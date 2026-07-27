# a

## Goal

Project scaffold, build system, APVTS parameter infrastructure, and state.

See ShredWidth_Build_Spec.md sections 5 (parameters), 10 (formats), 11 (bus layout), 13 (file structure), and 15 (build integration).

CMakeLists.txt: juce_add_plugin targeting VST3, CLAP (via clap-juce-extensions), LV2 (JUCE native LV2 target), and Standalone. Expose the SHREDWIDTH_XOVER_ORDER compile option (default 2 for LR2). CMakePresets.json and scripts/build_all.sh; build_all.sh must locate CLion's bundled cmake because cmake is not on PATH. Add JUCE as a git submodule pinned to the same tag aural-maximizer-pro uses (so the drawPopupMenuItem signature and LV2 support match a known-good version); inspect aural-maximizer-pro CMakeLists.txt for the known-working structure and follow the same nSpace conventions (plugin code, manufacturer code, company, bundle ID, version, copy-after-build). JUCE may already be present on this machine via symlink.

ParameterIDs.h: stable v1 inventory with never-renumbered string IDs: detuneAmount (0-25 cents, default 7), delayL (1-50 ms, 12.5), delayR (1-50 ms, 18.0), monoXover (80-300 Hz, 180, log skew ~0.35), width (0-200%, 100), mix (0-100% wet, 40), latencyMode ({Studio,Live}, Studio), outTrim (-12..+12 dB, 0), bypass (bool), and the hidden monoSource ({Sum,Left}, default Sum, not surfaced on the GUI). detuneAmount is one control applied as +amount to the L shifter and -amount to the R shifter.

PluginProcessor spine: APVTS with NormalisableRange skews; isBusesLayoutSupported accepts {mono in, stereo out} and {stereo in, stereo out} and rejects others (mono->mono collapses to Mix=0 dry passthrough); processBlock stub with ScopedNoDenormals; prepareToPlay allocates all buffers sized for the Studio worst case and never reallocates on the audio thread (2x pitch circular buffers L=2048+4 guard, 2x delay lines ceil(50ms*maxSampleRate)+guard, 1x dry-align delay 1024, meter FIFO ring); setLatencySamples PDC hooks.

StateManager: full APVTS XML getStateInformation/setStateInformation with parameterLayoutVersion for forward migration and A/B slots.

PluginEditor skeleton: window, Timer, ResizableCornerComponent.

.gitignore (build/, cmake-build-*, state/, .abracapocus/state/, .abracapocus/logs/, .abracapocus/reports/) and README.

## Notes

- Async: Wave 1: a.1 (CMakeLists + CMakePresets + build_all.sh + JUCE submodule; sole CMakeLists owner) and a.4 (.gitignore + README) are independent. Wave 2: a.2 (ParameterIDs + APVTS + bus layout + PluginProcessor spine; sole Processor owner) depends on a.1. Wave 3: a.3 (StateManager + PluginEditor skeleton) depends on a.2.
- Risk: a.1 sole CMakeLists owner; a.2 sole Processor owner. Bus layout must accept only mono->stereo and stereo->stereo. JUCE pin must match aural-maximizer-pro for drawPopupMenuItem and LV2. Param IDs are stable strings and must never be renumbered.
