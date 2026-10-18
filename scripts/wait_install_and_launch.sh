#!/usr/bin/env bash
set -e

ADB="/Volumes/X10Pro/Android/sdk/platform-tools/adb"
APK="/Volumes/X10Pro/Projects/mcbedvr/build/_repack/minecraft_gearvr_orig_manifest_signed.apk"

echo "[wait_install] Waiting for PICO 4 Ultra to connect / wake up..."
$ADB wait-for-device

echo "[wait_install] Device connected!"
$ADB devices -l

echo "[wait_install] Installing repacked APK..."
$ADB install -r --no-incremental "$APK"

echo "[wait_install] Granting permissions..."
$ADB shell "pm grant com.mojang.minecraftvr android.permission.READ_EXTERNAL_STORAGE 2>/dev/null || true"
$ADB shell "pm grant com.mojang.minecraftvr android.permission.WRITE_EXTERNAL_STORAGE 2>/dev/null || true"
$ADB shell "pm grant com.mojang.minecraftvr android.permission.RECORD_AUDIO 2>/dev/null || true"
$ADB shell "appops set com.mojang.minecraftvr MANAGE_EXTERNAL_STORAGE allow 2>/dev/null || true"

echo "[wait_install] Configuring VR options..."
$ADB shell "mkdir -p /sdcard/games/com.mojang/minecraftpe 2>/dev/null || true"
$ADB push /Volumes/X10Pro/Projects/mcbedvr/tools/options_vr.txt /sdcard/games/com.mojang/minecraftpe/options.txt 2>/dev/null || true

echo "[wait_install] Clearing logcat and launching Minecraft VR..."
$ADB logcat -c
$ADB shell "am start -n com.mojang.minecraftvr/com.mojang.minecraftpe.MainActivity"

echo "[wait_install] Capturing execution logcat..."
sleep 6
$ADB logcat -d | grep -E "mcbedvr|vrapi|OpenXR|Minecraft|ovrplatform|ovrfmod|CRASH|FATAL" | tail -n 120
