#!/usr/bin/env bash
# scripts/build_android_arm64.sh — Bootstrap reproducible para compilar
# libmcbedvr.so (arm64-v8a) desde un checkout limpio.
#
# Requisitos:
#   - ANDROID_NDK_HOME o NDK r27 instalado (default: /Volumes/X10Pro/Android/sdk/ndk/27.0.11718014)
#   - CMake 3.22+ en PATH
#   - Khronos OpenXR-SDK 1.1.x clonado (exporta MCBEDVR_OPENXR_DIR o pasá -D)
#   - (Opcional) Dobby compilado para ARM64 — ver tools/DOBBY.README.md
#
# Uso:
#   MCBEDVR_OPENXR_DIR=/path/to/OpenXR-SDK ./scripts/build_android_arm64.sh
#   # o: -D flags al final, p.ej. ./scripts/build_android_arm64.sh -DMCBEDVR_DOBBY_DIR=/p/dobby

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
DIST_DIR="${BUILD_DIR}/dist/arm64-v8a"

# 1. Localizar NDK.
if [ -z "${ANDROID_NDK_HOME:-}" ]; then
    if [ -d "/Volumes/X10Pro/Android/sdk/ndk/27.0.11718014" ]; then
        export ANDROID_NDK_HOME="/Volumes/X10Pro/Android/sdk/ndk/27.0.11718014"
    else
        echo "[mcbedvr] FATAL: NDK no encontrado. Exportá ANDROID_NDK_HOME o instalá el NDK r27." >&2
        exit 1
    fi
fi
# ANDROID_ABI es la variable legacy que la toolchain del NDK también respeta;
# sin esto, con cmake del SDK 3.22.1 en algunos hosts cae a armeabi-v7a
# aunque se pase -DCMAKE_ANDROID_ARCH_ABI=arm64-v8a.
export ANDROID_ABI="arm64-v8a"
echo "[mcbedvr] NDK           = ${ANDROID_NDK_HOME}"
echo "[mcbedvr] ANDROID_ABI   = ${ANDROID_ABI}"

# 2. Toolchain de CMake del NDK.
TOOLCHAIN="${ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake"
if [ ! -f "${TOOLCHAIN}" ]; then
    echo "[mcbedvr] FATAL: toolchain no encontrado en ${TOOLCHAIN}" >&2
    exit 1
fi

# 3. Configurar CMake en build/ y compilar.
#    Los flags -DMCBEDVR_* se pasan tal cual desde la línea de comandos.
#
#    Importante: usar el cmake 3.22.1 que viene con el SDK de Android. El
#    cmake 4.x del Homebrew activa el camino "legacy" de la toolchain del
#    NDK r27 e ignora -DCMAKE_ANDROID_ARCH_ABI=arm64-v8a (default cae a
#    armeabi-v7a). El bundle 3.22.1 del SDK es la versión que el NDK
#    declara como soportada.
NDK_CMAKE="/Volumes/X10Pro/Android/sdk/cmake/3.22.1/bin/cmake"
if [ ! -x "${NDK_CMAKE}" ]; then
    echo "[mcbedvr] FATAL: no se encontró ${NDK_CMAKE}" >&2
    exit 1
fi

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

"${NDK_CMAKE}" "${PROJECT_ROOT}" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" \
    -DCMAKE_SYSTEM_NAME=Android \
    -DCMAKE_ANDROID_ARCH_ABI=arm64-v8a \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-29 \
    -DCMAKE_ANDROID_API=29 \
    -DCMAKE_BUILD_TYPE=Release \
    "$@"

cmake --build . -- -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)"

if [ -f "${DIST_DIR}/libmcbedvr.so" ]; then
    echo ""
    echo "[mcbedvr] OK: ${DIST_DIR}/libmcbedvr.so"
    file "${DIST_DIR}/libmcbedvr.so"
else
    echo ""
    echo "[mcbedvr] FAIL: libmcbedvr.so no apareció en ${DIST_DIR}" >&2
    exit 2
fi
