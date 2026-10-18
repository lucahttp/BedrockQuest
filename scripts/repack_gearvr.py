#!/usr/bin/env python3
"""Repacks com.mojang.minecraftvr.apk with the 32-bit OpenXR libvrapi.so bridge,
libopenxr_loader.so, and patched PICO VR AndroidManifest.xml.
"""
import zipfile, shutil, os, subprocess, sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))

SRC_APK = "/Volumes/X10Pro/Downloads/com.mojang.minecraftvr.apk"
BUILD_DIR = os.path.join(PROJECT_ROOT, "build/_repack")
WORK_APK = os.path.join(BUILD_DIR, "minecraft_gearvr_pico.apk")
ALIGNED_APK = os.path.join(BUILD_DIR, "minecraft_gearvr_pico_aligned.apk")
SIGNED_APK = os.path.join(BUILD_DIR, "minecraft_gearvr_pico_signed.apk")

NEW_VRAPI_SO = os.path.join(PROJECT_ROOT, "build/dist/armeabi-v7a/libvrapi.so")
NEW_OPENXR_SO = os.path.join(PROJECT_ROOT, "build/dist/armeabi-v7a/libopenxr_loader.so")
PVR_MANIFEST_BIN = os.path.join(PROJECT_ROOT, "tools/pvr_manifest/target/release/pvr_manifest")
PATCHED_MANIFEST = os.path.join(PROJECT_ROOT, "build/AndroidManifest.gearvr.pvr.xml")

KEYSTORE = os.path.join(PROJECT_ROOT, "build/mcbedvr_dev.keystore")
ZIPALIGN = "/Volumes/X10Pro/Android/sdk/build-tools/34.0.0/zipalign"
APKSIGNER = "/Volumes/X10Pro/Android/sdk/build-tools/34.0.0/apksigner"

assert os.path.exists(SRC_APK), f"Missing {SRC_APK}"
assert os.path.exists(NEW_VRAPI_SO), f"Missing {NEW_VRAPI_SO}"
assert os.path.exists(NEW_OPENXR_SO), f"Missing {NEW_OPENXR_SO}"
assert os.path.exists(PVR_MANIFEST_BIN), f"Missing {PVR_MANIFEST_BIN}"
assert os.path.exists(KEYSTORE), f"Missing {KEYSTORE}"

os.makedirs(BUILD_DIR, exist_ok=True)

# 1. Generate patched AndroidManifest.xml
print(f"[repack_gearvr] Patching AndroidManifest.xml with PICO VR metadata...")
subprocess.run([PVR_MANIFEST_BIN, SRC_APK, PATCHED_MANIFEST], check=True)
manifest_content = open(PATCHED_MANIFEST, 'rb').read()
vrapi_content = open(NEW_VRAPI_SO, 'rb').read()
openxr_content = open(NEW_OPENXR_SO, 'rb').read()

# 2. Copy source APK to work APK
shutil.copyfile(SRC_APK, WORK_APK)
TMP_APK = WORK_APK + ".tmp"

TARGET_VRAPI = "lib/armeabi-v7a/libvrapi.so"
TARGET_OPENXR = "lib/armeabi-v7a/libopenxr_loader.so"
TARGET_MANIFEST = "AndroidManifest.xml"

print(f"[repack_gearvr] Rebuilding APK entries...")
with zipfile.ZipFile(WORK_APK, 'r') as zin:
    ref_item = None
    for item in zin.infolist():
        if item.filename.startswith("lib/armeabi-v7a/"):
            ref_item = item
            break

    seen_openxr = False
    with zipfile.ZipFile(TMP_APK, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zout:
        for item in zin.infolist():
            # Skip old signature files
            if item.filename.startswith("META-INF/") and (item.filename.endswith(".RSA") or item.filename.endswith(".SF") or item.filename.endswith(".MF")):
                continue

            if item.filename == TARGET_VRAPI:
                print(f"  REPLACING {item.filename}: {item.file_size} -> {len(vrapi_content)} bytes")
                ni = zipfile.ZipInfo(filename=item.filename, date_time=item.date_time)
                ni.compress_type = item.compress_type
                ni.external_attr = item.external_attr
                ni.flag_bits = (item.flag_bits & ~0x08)
                zout.writestr(ni, vrapi_content)
            elif item.filename == TARGET_OPENXR:
                print(f"  REPLACING {item.filename}: {item.file_size} -> {len(openxr_content)} bytes")
                ni = zipfile.ZipInfo(filename=item.filename, date_time=item.date_time)
                ni.compress_type = item.compress_type
                ni.external_attr = item.external_attr
                ni.flag_bits = (item.flag_bits & ~0x08)
                zout.writestr(ni, openxr_content)
                seen_openxr = True
            elif item.filename == TARGET_MANIFEST:
                print(f"  REPLACING {item.filename}: {item.file_size} -> {len(manifest_content)} bytes")
                ni = zipfile.ZipInfo(filename=item.filename, date_time=item.date_time)
                ni.compress_type = item.compress_type
                ni.external_attr = item.external_attr
                ni.flag_bits = (item.flag_bits & ~0x08)
                zout.writestr(ni, manifest_content)
            else:
                zout.writestr(item, zin.read(item.filename))

        if not seen_openxr:
            print(f"  ADDING {TARGET_OPENXR}: {len(openxr_content)} bytes")
            dt = ref_item.date_time if ref_item else (2026, 10, 10, 22, 55, 30)
            compress_type = ref_item.compress_type if ref_item else zipfile.ZIP_DEFLATED
            external_attr = ref_item.external_attr if ref_item else 0
            ni = zipfile.ZipInfo(filename=TARGET_OPENXR, date_time=dt)
            ni.compress_type = compress_type
            ni.external_attr = external_attr
            ni.flag_bits = (ref_item.flag_bits & ~0x08) if ref_item else 0
            zout.writestr(ni, openxr_content)

os.replace(TMP_APK, WORK_APK)

# 3. Zipalign
print(f"[repack_gearvr] Zipaligning APK to 4-byte boundaries...")
if os.path.exists(ALIGNED_APK): os.remove(ALIGNED_APK)
subprocess.run([ZIPALIGN, "-p", "-f", "4", WORK_APK, ALIGNED_APK], check=True)

# 4. Sign
print(f"[repack_gearvr] Signing APK with apksigner (v2 + v3 scheme)...")
if os.path.exists(SIGNED_APK): os.remove(SIGNED_APK)
subprocess.run([
    APKSIGNER, "sign",
    "--ks", KEYSTORE,
    "--ks-pass", "pass:android",
    "--key-pass", "pass:android",
    "--out", SIGNED_APK,
    ALIGNED_APK
], check=True)

# 5. Verify
print(f"[repack_gearvr] Verifying signature...")
subprocess.run([APKSIGNER, "verify", "--verbose", SIGNED_APK], check=True)

print(f"\n[repack_gearvr] SUCCESS! Output APK: {SIGNED_APK} ({os.path.getsize(SIGNED_APK)} bytes)")
