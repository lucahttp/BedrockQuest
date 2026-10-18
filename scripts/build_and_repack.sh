#!/usr/bin/env bash
# scripts/build_and_repack.sh — Automated build, repack, zipalign, sign, verify, and export pipeline.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

cd "${PROJECT_ROOT}"

echo "[mcbedvr] 1/7: Building libmcbedvr.so (arm64-v8a)..."
"${PROJECT_ROOT}/scripts/build_android_arm64.sh"

echo "[mcbedvr] 2/7: Generating PICO OpenXR IMMERSIVE manifest..."
cargo build --release --manifest-path "${PROJECT_ROOT}/tools/pvr_manifest/Cargo.toml"
"${PROJECT_ROOT}/tools/pvr_manifest/target/release/pvr_manifest" \
    "/Volumes/X10Pro/Downloads/minecraft_vr.apk" \
    "${PROJECT_ROOT}/build/AndroidManifest.pvr.xml"

echo "[mcbedvr] 3/7: Repacking APK with python3..."
python3 "${PROJECT_ROOT}/build/_repack/repack.py"

echo "[mcbedvr] 4/7: Zipaligning APK..."
/Volumes/X10Pro/Android/sdk/build-tools/34.0.0/zipalign -p -f 4 \
    "${PROJECT_ROOT}/build/_repack/minecraft_vr.apk" \
    "${PROJECT_ROOT}/build/_repack/minecraft_vr_aligned.apk"

echo "[mcbedvr] 5/7: Signing APK..."
/Volumes/X10Pro/Android/sdk/build-tools/34.0.0/apksigner sign \
    --ks "${PROJECT_ROOT}/build/mcbedvr_dev.keystore" \
    --ks-pass pass:android \
    --key-pass pass:android \
    --out "${PROJECT_ROOT}/build/_repack/minecraft_vr_signed.apk" \
    "${PROJECT_ROOT}/build/_repack/minecraft_vr_aligned.apk"

echo "[mcbedvr] 6/7: Verifying signed APK..."
/Volumes/X10Pro/Android/sdk/build-tools/34.0.0/apksigner verify \
    "${PROJECT_ROOT}/build/_repack/minecraft_vr_signed.apk"

echo "[mcbedvr] 7/7: Copying output APK to /Volumes/X10Pro/Downloads/minecraft_vr_v6_signed.apk..."
cp -f "${PROJECT_ROOT}/build/_repack/minecraft_vr_signed.apk" \
    "/Volumes/X10Pro/Downloads/minecraft_vr_v6_signed.apk"

echo "[mcbedvr] Build and repack complete!"
