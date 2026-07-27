#!/usr/bin/env bash
# scripts/check_rt_safety.sh — ShredWidth RT-safety grep gate.
#
# Implements Build Spec §14 item 1 (RT-safety static checks) — the *static*
# grep-gate portion. It does NOT replace a proper allocator-instrumented
# dynamic check (which remains a manual / host-required step per the spec).
#
# The gate is intentionally cheap and CI-friendly:
#   * No cmake, no configure, no build.
#   * No code generation.
#   * No third-party tool beyond ripgrep (preferred) or POSIX grep -E.
#
# ---------------------------------------------------------------------------
# Strategy (and why we scope, not whole-file grep)
# ---------------------------------------------------------------------------
# Source/PluginProcessor.cpp contains many non-audio-thread bodies:
# prepareToPlay, releaseResources, createEditor, getStateInformation,
# setStateInformation, createPluginFilter, getName, getProgramName,
# changeProgramName, etc. These legitimately use `new`, `juce::String`,
# etc. during construction or state-restore. A naive whole-file grep over
# the file would false-positive and be unusable.
#
# We therefore:
#   1. Brace-counted awk extracts ONLY the body of
#      `ShredWidthAudioProcessor::processBlock(...)` from
#      Source/PluginProcessor.cpp.
#   2. Brace-counted awk extracts the bodies of MeterTap::pushSample and
#      MeterTap::pushBlock from Source/DSP/MeterTap.cpp (the audio→UI
#      metering push path, called from ShredWidthEngine::processFrame →
#      meterTap.pushSample on the audio thread).
#   3. Inside those scoped regions we grep for the forbidden APIs.
#   4. We additionally assert that `ScopedNoDenormals` is present in the
#      processBlock body (spec §7 hard rule).
#
# ---------------------------------------------------------------------------
# Forbidden patterns
# ---------------------------------------------------------------------------
#   \bnew\b                                — heap allocator keyword
#   \b(malloc|free|realloc)\b              — C allocator primitives
#   juce::String                           — heap-allocating string type
#   lock primitives:
#       \bstd::mutex\b, \bstd::scoped_lock\b,
#       \bstd::lock_guard\b, \bstd::unique_lock\b,
#       \bCriticalSection\b,
#       MessageManager::callAsync
#
# We deliberately do NOT grep a bare `\block\b` regex: that would also
# match the ordinary identifier "block" (it appears in `processBlock`,
# `pushBlock`, `pushSample`/`pushBlock` signatures, etc.). The chosen
# primitives are the actual lock-related identifiers that an audio-hostile
# implementation would introduce.
#
# ---------------------------------------------------------------------------
# Usage
# ---------------------------------------------------------------------------
#   From the repo root:
#       bash scripts/check_rt_safety.sh
#
# This script does NOT require cmake on PATH. It is a pure source-text
# grep gate over the audio-thread hot paths defined above.
#
# ---------------------------------------------------------------------------
# Exit codes
# ---------------------------------------------------------------------------
#   0  PASS — every region clean, ScopedNoDenormals present
#   1  FAIL — at least one forbidden pattern, ScopedNoDenormals missing,
#             or a required source file not found

set -euo pipefail

# --- Repo root resolution -------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

PLUGIN_PROCESSOR_CPP="Source/PluginProcessor.cpp"
METER_TAP_CPP="Source/DSP/MeterTap.cpp"

# --- Pre-flight: required source files exist -----------------------------
for f in "${PLUGIN_PROCESSOR_CPP}" "${METER_TAP_CPP}"; do
    if [[ ! -f "${f}" ]]; then
        printf 'check_rt_safety.sh: FAIL: required source not found: %s\n' "${f}"
        exit 1
    fi
done

# --- Select grep backend -------------------------------------------------
# Prefer ripgrep; fall back to POSIX grep -E. The invocation is:
#   "${GREP_CMD[@]}" <pattern> <stdin->-
#
# Note: rg 13.x uses `-E` for `--encoding <ENCODING>`, NOT for PCRE2 (rg's
# `--pcre2` is the PCRE2 toggle). For our ERE-compatible patterns we rely
# on rg's default Rust regex engine, which already accepts `\b`, `|`, `\s`,
# etc. (verified against ripgrep 13.0.0). The grep fallback uses `-E`.
if command -v rg >/dev/null 2>&1; then
    GREP_CMD=(rg --no-heading --line-number --color=never)
else
    GREP_CMD=(grep -E -n -H --)
fi

# --- Function-body extractor ---------------------------------------------
# extract_body <file> <signature-regex>
#
# Prints the line matching <signature-regex> and every subsequent line up
# to (and including) the closing brace of that function. Brace counting
# uses a per-character scan; `started` is flipped on the first `{` so any
# `}` that appears before the function's own opening brace (e.g. in the
# signature line itself) does not corrupt the depth counter.
extract_body() {
    local file="$1"
    local sig="$2"
    awk -v sig="${sig}" '
        BEGIN { depth = 0; capturing = 0; started = 0 }
        {
            line = $0
            if (!started && match(line, sig)) {
                capturing = 1
            }
            if (capturing) {
                print line
                n = length(line)
                for (i = 1; i <= n; i++) {
                    c = substr(line, i, 1)
                    if (c == "{") {
                        depth++
                        started = 1
                    } else if (c == "}" && started) {
                        depth--
                        if (depth == 0) { exit }
                    }
                }
            }
        }
    ' "${file}"
}

# --- Extract the regions under test --------------------------------------
PROCESS_BLOCK_BODY="$(extract_body "${PLUGIN_PROCESSOR_CPP}" 'processBlock[[:space:]]*\(')"
PUSH_SAMPLE_BODY="$(extract_body "${METER_TAP_CPP}" 'pushSample[[:space:]]*\(')"
PUSH_BLOCK_BODY="$(extract_body "${METER_TAP_CPP}" 'pushBlock[[:space:]]*\(')"

if [[ -z "${PROCESS_BLOCK_BODY}" ]]; then
    printf 'check_rt_safety.sh: FAIL: could not extract processBlock body from %s\n' \
        "${PLUGIN_PROCESSOR_CPP}"
    exit 1
fi
if [[ -z "${PUSH_SAMPLE_BODY}" ]]; then
    printf 'check_rt_safety.sh: FAIL: could not extract MeterTap::pushSample body from %s\n' \
        "${METER_TAP_CPP}"
    exit 1
fi
if [[ -z "${PUSH_BLOCK_BODY}" ]]; then
    printf 'check_rt_safety.sh: FAIL: could not extract MeterTap::pushBlock body from %s\n' \
        "${METER_TAP_CPP}"
    exit 1
fi

# --- Pattern definitions -------------------------------------------------
# Each entry: "label|ERE-pattern".
# Patterns are POSIX ERE (compatible with both rg -E and grep -E).
RT_PATTERNS=(
    "new_alloc|\\bnew\\b"
    "c_alloc|\\b(malloc|free|realloc)\\b"
    "juce_string|juce::String"
    "lock_primitive|\\bstd::mutex\\b|\\bstd::scoped_lock\\b|\\bstd::lock_guard\\b|\\bstd::unique_lock\\b|\\bCriticalSection\\b|MessageManager::callAsync"
)

# --- Region scan helper --------------------------------------------------
# scan_region "<region-label>" "<pattern-label>" "<ERE>" "<body>"
# Prints a PASS line and returns 0 if the pattern is absent from the body.
# Prints a FAIL block with the offending lines and returns 1 if present.
scan_region() {
    local region_label="$1"
    local pat_label="$2"
    local pat="$3"
    local body="$4"
    local hits
    hits="$(printf '%s\n' "${body}" | "${GREP_CMD[@]}" "${pat}" - 2>/dev/null || true)"
    if [[ -n "${hits}" ]]; then
        printf 'check_rt_safety.sh: FAIL: forbidden pattern [%s/%s] found:\n%s\n' \
            "${region_label}" "${pat_label}" "${hits}"
        return 1
    fi
    printf 'check_rt_safety.sh: PASS: no %s in %s.\n' "${pat_label}" "${region_label}"
    return 0
}

# --- Run the gate --------------------------------------------------------
FAIL_COUNT=0

# 1. Forbidden patterns in processBlock body (Source/PluginProcessor.cpp)
for entry in "${RT_PATTERNS[@]}"; do
    label="${entry%%|*}"
    pat="${entry#*|}"
    if ! scan_region "processBlock" "${label}" "${pat}" "${PROCESS_BLOCK_BODY}"; then
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
done

# 2. Forbidden patterns in MeterTap::pushSample (audio→UI metering)
for entry in "${RT_PATTERNS[@]}"; do
    label="${entry%%|*}"
    pat="${entry#*|}"
    if ! scan_region "MeterTap::pushSample" "${label}" "${pat}" "${PUSH_SAMPLE_BODY}"; then
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
done

# 3. Forbidden patterns in MeterTap::pushBlock (audio→UI metering)
for entry in "${RT_PATTERNS[@]}"; do
    label="${entry%%|*}"
    pat="${entry#*|}"
    if ! scan_region "MeterTap::pushBlock" "${label}" "${pat}" "${PUSH_BLOCK_BODY}"; then
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
done

# 4. ScopedNoDenormals assertion in processBlock body (spec §7 hard rule)
if printf '%s\n' "${PROCESS_BLOCK_BODY}" \
        | "${GREP_CMD[@]}" 'ScopedNoDenormals' - >/dev/null 2>&1; then
    printf 'check_rt_safety.sh: PASS: ScopedNoDenormals present in processBlock.\n'
else
    printf 'check_rt_safety.sh: FAIL: ScopedNoDenormals missing in processBlock body.\n'
    FAIL_COUNT=$((FAIL_COUNT + 1))
fi

# --- Summary -------------------------------------------------------------
echo
if [[ "${FAIL_COUNT}" -eq 0 ]]; then
    printf '=================================================================\n'
    printf '  check_rt_safety.sh: PASS — RT-safety grep gate clean.\n'
    printf '=================================================================\n'
    exit 0
else
    printf '=================================================================\n'
    printf '  check_rt_safety.sh: FAIL — %d check(s) failed.\n' "${FAIL_COUNT}"
    printf '=================================================================\n'
    exit 1
fi
