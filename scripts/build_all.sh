#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
PRESET="linux-relwithdebinfo"

find_cmake() {
    local cmake_path
    local -a clion_cmake_candidates=()

    if command -v cmake >/dev/null 2>&1; then
        command -v cmake
        return
    fi

    shopt -s nullglob
    clion_cmake_candidates=(
        "${HOME}"/.local/share/JetBrains/Toolbox/apps/clion/bin/cmake/linux/x64/bin/cmake
        "${HOME}"/.local/share/JetBrains/Toolbox/apps/clion/*/bin/cmake/linux/x64/bin/cmake
        "${HOME}"/.local/share/JetBrains/Toolbox/apps/CLion/bin/cmake/linux/x64/bin/cmake
        "${HOME}"/.local/share/JetBrains/Toolbox/apps/CLion/*/bin/cmake/linux/x64/bin/cmake
        /opt/clion/bin/cmake/linux/x64/bin/cmake
        /opt/clion/*/bin/cmake/linux/x64/bin/cmake
        /opt/clion-*/bin/cmake/linux/x64/bin/cmake
        /snap/clion/current/bin/cmake/linux/x64/bin/cmake
        "${HOME}"/snap/clion/current/bin/cmake/linux/x64/bin/cmake
        "${HOME}"/.var/app/com.jetbrains.CLion/data/JetBrains/Toolbox/apps/clion/bin/cmake/linux/x64/bin/cmake
        "${HOME}"/.var/app/com.jetbrains.CLion/data/JetBrains/Toolbox/apps/clion/*/bin/cmake/linux/x64/bin/cmake
        /var/lib/flatpak/app/com.jetbrains.CLion/current/active/files/extra/clion/bin/cmake/linux/x64/bin/cmake
    )
    shopt -u nullglob

    for cmake_path in "${clion_cmake_candidates[@]}"; do
        if [[ -x "${cmake_path}" ]]; then
            printf '%s\n' "${cmake_path}"
            return
        fi
    done

    printf '%s\n' \
        "ERROR: cmake was not found on PATH or in a supported CLion bundle." >&2
    return 1
}

main() {
    local cmake_bin
    cmake_bin="$(find_cmake)"

    cd "${ROOT_DIR}"

    printf '==> Configuring ShredWidth (preset: %s)\n' "${PRESET}"
    "${cmake_bin}" --preset "${PRESET}"

    printf '==> Building VST3, CLAP, LV2, and Standalone targets\n'
    "${cmake_bin}" --build --preset "${PRESET}" --target \
        ShredWidth_VST3 \
        ShredWidth_CLAP \
        ShredWidth_LV2 \
        ShredWidth_Standalone

    printf '==> All ShredWidth format targets built successfully\n'
}

main "$@"
