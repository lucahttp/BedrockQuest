#!/usr/bin/env bash
# scripts/build_vrapi_arm32.sh — Compiles 32-bit libvrapi.so OpenXR Bridge
# for Minecraft Bedrock on PICO 4 Ultra.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DIST_DIR="${PROJECT_ROOT}/build/dist/armeabi-v7a"

NDK_HOME="${ANDROID_NDK_HOME:-/Volumes/X10Pro/Android/sdk/ndk/27.0.11718014}"
CXX="${NDK_HOME}/toolchains/llvm/prebuilt/darwin-x86_64/bin/armv7a-linux-androideabi26-clang++"
STRIP="${NDK_HOME}/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-strip"

if [ ! -x "${CXX}" ]; then
    echo "[build_vrapi] FATAL: Compiler not found at ${CXX}" >&2
    exit 1
fi

mkdir -p "${DIST_DIR}"

echo "[build_vrapi] Compiling src/vrapi/vrapi_bridge.cpp (armeabi-v7a)..."
"${CXX}" \
    -std=c++20 -fPIC -O2 \
    -D__ANDROID__ \
    -mfloat-abi=softfp -mfpu=neon \
    -I "${PROJECT_ROOT}/vendor/vrapi/include" \
    -I "${PROJECT_ROOT}/vendor/openxr/include" \
    -I "${PROJECT_ROOT}/src" \
    -L "${PROJECT_ROOT}/vendor/openxr/lib/armeabi-v7a" \
    -shared -Wl,-soname,libvrapi.so \
    "${PROJECT_ROOT}/src/vrapi/vrapi_bridge.cpp" \
    -lopenxr_loader -llog -landroid -lEGL -lGLESv3 \
    -o "${DIST_DIR}/libvrapi.so"

echo "[build_vrapi] Built ${DIST_DIR}/libvrapi.so"
file "${DIST_DIR}/libvrapi.so"

# Also compile 32-bit libovrplatformloader.so stub to bypass Oculus Platform DRM
echo "[build_vrapi] Compiling src/platform/ovr_platform_stub.cpp (armeabi-v7a)..."
"${CXX}" \
    -std=c++20 -fPIC -O2 \
    -D__ANDROID__ \
    -mfloat-abi=softfp -mfpu=neon \
    -I "${PROJECT_ROOT}/src" \
    -shared -Wl,-soname,libovrplatformloader.so \
    "${PROJECT_ROOT}/src/platform/ovr_platform_stub.cpp" \
    -llog \
    -o "${DIST_DIR}/libovrplatformloader.so"

echo "[build_vrapi] Built ${DIST_DIR}/libovrplatformloader.so"
file "${DIST_DIR}/libovrplatformloader.so"

# Also compile 32-bit libovrfmod.so stub to eliminate GCC unwinder collision
echo "[build_vrapi] Compiling src/platform/ovr_fmod_stub.cpp (armeabi-v7a)..."
BUILTINS_A="${NDK_HOME}/android-ndk-r27-beta1/toolchains/llvm/prebuilt/darwin-x86_64/lib/clang/18/lib/linux/libclang_rt.builtins-arm-android.a"
if [ ! -f "${BUILTINS_A}" ]; then
    BUILTINS_A=$(find "${NDK_HOME}" -name "libclang_rt.builtins-arm-android.a" | head -n 1)
fi

"${CXX}" \
    -std=c++20 -fPIC -O2 \
    -D__ANDROID__ \
    -march=armv7-a -mcpu=cortex-a15 \
    -mfloat-abi=softfp -mfpu=neon \
    -I "${PROJECT_ROOT}/src" \
    -shared -Wl,-soname,libovrfmod.so \
    "${BUILTINS_A}" \
    -Wl,--export-dynamic-symbol=__aeabi_ldiv0 \
    -Wl,--export-dynamic-symbol=__aeabi_idiv0 \
    -Wl,--export-dynamic-symbol=__aeabi_idiv \
    -Wl,--export-dynamic-symbol=__aeabi_uidiv \
    -Wl,--export-dynamic-symbol=__aeabi_idivmod \
    -Wl,--export-dynamic-symbol=__aeabi_uidivmod \
    -Wl,--export-dynamic-symbol=__aeabi_uldivmod \
    -Wl,--export-dynamic-symbol=__aeabi_ldivmod \
    -Wl,--export-dynamic-symbol=__aeabi_ui2d \
    -Wl,--export-dynamic-symbol=__aeabi_i2d \
    -Wl,--export-dynamic-symbol=__aeabi_ul2d \
    -Wl,--export-dynamic-symbol=__aeabi_l2d \
    -Wl,--export-dynamic-symbol=__aeabi_f2d \
    -Wl,--export-dynamic-symbol=__aeabi_d2uiz \
    -Wl,--export-dynamic-symbol=__aeabi_d2ulz \
    -Wl,--export-dynamic-symbol=__aeabi_ddiv \
    -Wl,--export-dynamic-symbol=__aeabi_dmul \
    -Wl,--export-dynamic-symbol=__aeabi_dsub \
    -Wl,--export-dynamic-symbol=__aeabi_unwind_cpp_pr0 \
    -Wl,--export-dynamic-symbol=__aeabi_unwind_cpp_pr1 \
    -Wl,--export-dynamic-symbol=_Unwind_Resume \
    -Wl,--export-dynamic-symbol=__gnu_ldivmod_helper \
    "${PROJECT_ROOT}/src/platform/ovr_fmod_stub.cpp" \
    -llog \
    -o "${DIST_DIR}/libovrfmod.so"

echo "[build_vrapi] Built ${DIST_DIR}/libovrfmod.so"
file "${DIST_DIR}/libovrfmod.so"

# Also copy 32-bit libopenxr_loader.so to DIST_DIR
cp -f "${PROJECT_ROOT}/vendor/openxr/lib/armeabi-v7a/libopenxr_loader.so" "${DIST_DIR}/libopenxr_loader.so"
echo "[build_vrapi] Staged libopenxr_loader.so to ${DIST_DIR}"


