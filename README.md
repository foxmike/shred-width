# ShredWidth

**Static Micro-Pitch Detuner & Lead Widener**  
Developed by nSpace Systems.

ShredWidth turns a single mono guitar (or any monophonic-in-intent source) into a wide, focused stereo image **without pitch wobble and without mono-collapse**. It is a deterministic plugin — there is no LFO anywhere in the signal path. The pitch offset is a fixed micro-detune (the H3000 / SPX90 "wide lead" trick), and the stereo width comes from static detune combined with independent short delays, with the sub-band folded back to dead-center so the effect survives mono fold-down.

Formats: **VST3**, **CLAP**, **LV2**, **Standalone**

Language standard: **C++20**  
Framework: **JUCE** (pinned tag `8.0.13`)

> For the complete design specification see [`.abracapocus/ShredWidth_Build_Spec.md`](.abracapocus/ShredWidth_Build_Spec.md).

---

## Build Instructions

### Prerequisites

- **CMake** — CLion-bundled CMake is the primary toolchain; `cmake` may not be on `$PATH`.
- **C++20-capable compiler** (GCC 11+, Clang 14+, MSVC 2022+).

### Dependencies

1. **JUCE** — managed as a Git submodule, pinned to tag `8.0.13` (commit `7c9d3783`).  
2. **clap-juce-extensions** (optional) — required to build the CLAP format. If placed at `clap-juce-extensions/` as a submodule, the build system will automatically enable the CLAP target. Without it, `ShredWidth_CLAP` is skipped with a message.

### Steps

```bash
# 1. Clone and initialize submodules
git clone <repository-url> ShredWidth
cd ShredWidth
git submodule update --init --recursive

# 2. Build all supported format targets
./scripts/build_all.sh
```

The build script (`scripts/build_all.sh`) automatically searches for CLion-bundled CMake in standard locations (Toolbox, snap, flatpak) before falling back to a system `cmake` on `$PATH`.

### Build-time options

| Option | Default | Description |
|--------|---------|-------------|
| `SHREDWIDTH_XOVER_ORDER` | `2` | Crossover slope: `2` = LR2 (gentle, 12 dB/oct), `4` = LR4 (steeper, 24 dB/oct, requires `#define`). |
| `SHREDWIDTH_CONFIGURE_ONLY` | `OFF` | When `ON`, configure runs without requiring JUCE (for CI validation). |

---

## Submodule Pins

| Submodule | URL | Tag / Commit |
|-----------|-----|-------------|
| `JUCE` | `https://github.com/juce-framework/JUCE.git` | `8.0.13` / `7c9d3783` |

The JUCE pin matches the version used by sibling nSpace plugins (AuralMaximizerPro) to ensure consistent `drawPopupMenuItem` signatures, LV2 support, and API compatibility. Do not change the pin without coordinating across all nSpace plugin repositories.

`clap-juce-extensions` is **not** pinned as a submodule at this time. If CLAP builds are needed, populate it manually (e.g., `git submodule add https://github.com/free-audio/clap-juce-extensions`), or add it to `.gitmodules` in a future phase.

---

## Documentation

- **[`.abracapocus/ShredWidth_Build_Spec.md`](.abracapocus/ShredWidth_Build_Spec.md)** — Full build specification: signal flow, DSP math, parameter map, GUI behavior, bus handling, file layout, and verification plan.
- **`scripts/build_all.sh`** — One-shot build script with CLion CMake resolution.