#!/usr/bin/env bash
# scripts/build_and_repack.sh — Automated build, repack, zipalign, sign, verify, and export pipeline.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

cd "${PROJECT_ROOT}"

# 0. Locate genuine base Minecraft APK
BASE_APK="${1:-${MINECRAFT_BASE_APK:-}}"
if [[ -z "${BASE_APK}" ]]; then
    for candidate in \
        "${PROJECT_ROOT}/minecraft_apk/minecraft_bedrock.apk" \
        "${PROJECT_ROOT}/minecraft_apk/base.apk" \
        "/Volumes/X10Pro/Downloads/minecraft_vr.apk" \
        "${HOME}/Downloads/minecraft_vr.apk" \
        "${HOME}/Downloads/com.mojang.minecraftpe.apk"; do
        if [[ -f "${candidate}" ]]; then
            BASE_APK="${candidate}"
            break
        fi
    done
fi

if [[ -z "${BASE_APK}" || ! -f "${BASE_APK}" ]]; then
    echo "[mcbedvr] ERROR: Base Minecraft Bedrock APK not found!"
    echo "Please download your legally purchased Minecraft APK using:"
    echo "  python3 scripts/download_minecraft_apk.py"
    echo "Or specify the path manually:"
    echo "  bash scripts/build_and_repack.sh /path/to/com.mojang.minecraftpe.apk"
    exit 1
fi

echo "[mcbedvr] Using Base Minecraft APK: ${BASE_APK}"

# 1. Locate Android build-tools (zipalign, apksigner)
ZIPALIGN=""
APKSIGNER=""
for candidate_dir in \
    "${ANDROID_HOME:-}/build-tools/34.0.0" \
    "${ANDROID_HOME:-}/build-tools/33.0.1" \
    "${ANDROID_SDK_ROOT:-}/build-tools/34.0.0" \
    "/Volumes/X10Pro/Android/sdk/build-tools/34.0.0" \
    "/Volumes/X10Pro/Android/sdk/build-tools/36.0.0" \
    "${HOME}/Library/Android/sdk/build-tools/34.0.0"; do
    if [[ -x "${candidate_dir}/zipalign" && -x "${candidate_dir}/apksigner" ]]; then
        ZIPALIGN="${candidate_dir}/zipalign"
        APKSIGNER="${candidate_dir}/apksigner"
        break
    fi
done

if [[ -z "${ZIPALIGN}" ]]; then
    ZIPALIGN="$(which zipalign 2>/dev/null || true)"
fi
if [[ -z "${APKSIGNER}" ]]; then
    APKSIGNER="$(which apksigner 2>/dev/null || true)"
fi

if [[ -z "${ZIPALIGN}" || -z "${APKSIGNER}" ]]; then
    echo "[mcbedvr] ERROR: zipalign or apksigner not found. Set ANDROID_HOME or install Android build-tools."
    exit 1
fi

# 2. Ensure self-signed dev keystore exists
KEYSTORE="${PROJECT_ROOT}/build/mcbedvr_dev.keystore"
mkdir -p "${PROJECT_ROOT}/build"
if [[ ! -f "${KEYSTORE}" ]]; then
    echo "[mcbedvr] Generating self-signed debug keystore..."
    keytool -genkey -v -keystore "${KEYSTORE}" \
        -alias androiddebugkey \
        -storepass android \
        -keypass android \
        -keyalg RSA \
        -keysize 2048 \
        -validity 10000 \
        -dname "CN=MCBedVR Debug,O=MCBedVR,C=US"
fi

echo "[mcbedvr] 1/7: Building libmcbedvr.so (arm64-v8a)..."
"${PROJECT_ROOT}/scripts/build_android_arm64.sh"

echo "[mcbedvr] 2/7: Generating PICO OpenXR IMMERSIVE manifest..."
cargo build --release --manifest-path "${PROJECT_ROOT}/tools/pvr_manifest/Cargo.toml"
"${PROJECT_ROOT}/tools/pvr_manifest/target/release/pvr_manifest" \
    "${BASE_APK}" \
    "${PROJECT_ROOT}/build/AndroidManifest.pvr.xml"

echo "[mcbedvr] 3/7: Repacking APK with python3..."
python3 "${PROJECT_ROOT}/scripts/repack.py" "${BASE_APK}"

echo "[mcbedvr] 4/7: Zipaligning APK..."
"${ZIPALIGN}" -p -f 4 \
    "${PROJECT_ROOT}/build/_repack/minecraft_vr.apk" \
    "${PROJECT_ROOT}/build/_repack/minecraft_vr_aligned.apk"

echo "[mcbedvr] 5/7: Signing APK..."
"${APKSIGNER}" sign \
    --ks "${KEYSTORE}" \
    --ks-pass pass:android \
    --key-pass pass:android \
    --out "${PROJECT_ROOT}/build/_repack/minecraft_vr_signed.apk" \
    "${PROJECT_ROOT}/build/_repack/minecraft_vr_aligned.apk"

echo "[mcbedvr] 6/7: Verifying signed APK..."
"${APKSIGNER}" verify \
    "${PROJECT_ROOT}/build/_repack/minecraft_vr_signed.apk"

echo "[mcbedvr] 7/7: Exporting signed APK..."
mkdir -p "${PROJECT_ROOT}/dist"
cp -f "${PROJECT_ROOT}/build/_repack/minecraft_vr_signed.apk" \
    "${PROJECT_ROOT}/dist/minecraft_vr_signed.apk"

if [[ -d "/Volumes/X10Pro/Downloads" ]]; then
    cp -f "${PROJECT_ROOT}/build/_repack/minecraft_vr_signed.apk" \
        "/Volumes/X10Pro/Downloads/minecraft_vr_v6_signed.apk"
fi

echo ""
echo "============================================================"
echo "[mcbedvr] SUCCESS! Standalone 6DoF VR APK ready:"
echo "  ${PROJECT_ROOT}/dist/minecraft_vr_signed.apk"
echo ""
echo "To install on your PICO 4 Ultra:"
echo "  adb install -r ${PROJECT_ROOT}/dist/minecraft_vr_signed.apk"
echo "============================================================"
