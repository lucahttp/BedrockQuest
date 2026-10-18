#!/usr/bin/env python3
"""Repacks com.mojang.minecraftvr.apk with the 32-bit OpenXR libvrapi.so bridge
and libopenxr_loader.so, preserving the original GearVR AndroidManifest.xml.
"""
import zipfile, shutil, os, subprocess, sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))

SRC_APK = "/Volumes/X10Pro/Downloads/com.mojang.minecraftvr.apk"
BUILD_DIR = os.path.join(PROJECT_ROOT, "build/_repack")
WORK_APK = os.path.join(BUILD_DIR, "minecraft_gearvr_orig_manifest.apk")
ALIGNED_APK = os.path.join(BUILD_DIR, "minecraft_gearvr_orig_manifest_aligned.apk")
SIGNED_APK = os.path.join(BUILD_DIR, "minecraft_gearvr_orig_manifest_signed.apk")

NEW_VRAPI_SO = os.path.join(PROJECT_ROOT, "build/dist/armeabi-v7a/libvrapi.so")
NEW_OPENXR_SO = os.path.join(PROJECT_ROOT, "build/dist/armeabi-v7a/libopenxr_loader.so")
NEW_OVRPLATFORM_SO = os.path.join(PROJECT_ROOT, "build/dist/armeabi-v7a/libovrplatformloader.so")
NEW_OVRFMOD_SO = os.path.join(PROJECT_ROOT, "build/dist/armeabi-v7a/libovrfmod.so")

KEYSTORE = os.path.join(PROJECT_ROOT, "build/mcbedvr_dev.keystore")
ZIPALIGN = "/Volumes/X10Pro/Android/sdk/build-tools/34.0.0/zipalign"
APKSIGNER = "/Volumes/X10Pro/Android/sdk/build-tools/34.0.0/apksigner"

assert os.path.exists(SRC_APK), f"Missing {SRC_APK}"
assert os.path.exists(NEW_VRAPI_SO), f"Missing {NEW_VRAPI_SO}"
assert os.path.exists(NEW_OPENXR_SO), f"Missing {NEW_OPENXR_SO}"
assert os.path.exists(NEW_OVRPLATFORM_SO), f"Missing {NEW_OVRPLATFORM_SO}"
assert os.path.exists(NEW_OVRFMOD_SO), f"Missing {NEW_OVRFMOD_SO}"
assert os.path.exists(KEYSTORE), f"Missing {KEYSTORE}"

os.makedirs(BUILD_DIR, exist_ok=True)

print(f"[repack] Reading updated libraries...")
vrapi_data = open(NEW_VRAPI_SO, "rb").read()
openxr_data = open(NEW_OPENXR_SO, "rb").read()
ovrplatform_data = open(NEW_OVRPLATFORM_SO, "rb").read()
ovrfmod_data = open(NEW_OVRFMOD_SO, "rb").read()

print(f"[repack] Copying base APK...")
shutil.copyfile(SRC_APK, WORK_APK)
TMP = WORK_APK + ".tmp"

print(f"[repack] Rebuilding APK entries...")
with zipfile.ZipFile(WORK_APK, "r") as zin:
    with zipfile.ZipFile(TMP, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zout:
        seen_openxr = False
        ref_item = None
        for item in zin.infolist():
            # Exclude previous signature files
            if item.filename.startswith("META-INF/") and (item.filename.endswith(".RSA") or item.filename.endswith(".SF") or item.filename.endswith(".MF")):
                continue
            if item.filename == "AndroidManifest.xml":
                print(f"  Enabling android:debuggable=true in AndroidManifest.xml...")
                raw_xml = bytearray(zin.read(item.filename))
                assert raw_xml[0x1a00:0x1a04] == bytes.fromhex("00000000")
                raw_xml[0x1a00:0x1a04] = bytes.fromhex("ffffffff")
                ni = zipfile.ZipInfo(filename=item.filename, date_time=item.date_time)
                ni.compress_type = item.compress_type
                ni.external_attr = item.external_attr
                ni.flag_bits = (item.flag_bits & ~0x08)
                zout.writestr(ni, bytes(raw_xml))
            elif item.filename == "lib/armeabi-v7a/libvrapi.so":
                ref_item = item
                print(f"  Replacing {item.filename} ({len(vrapi_data)} bytes)...")
                ni = zipfile.ZipInfo(filename=item.filename, date_time=item.date_time)
                ni.compress_type = item.compress_type
                ni.external_attr = item.external_attr
                ni.flag_bits = (item.flag_bits & ~0x08)
                zout.writestr(ni, vrapi_data)
            elif item.filename == "lib/armeabi-v7a/libovrplatformloader.so":
                print(f"  Replacing {item.filename} ({len(ovrplatform_data)} bytes)...")
                ni = zipfile.ZipInfo(filename=item.filename, date_time=item.date_time)
                ni.compress_type = item.compress_type
                ni.external_attr = item.external_attr
                ni.flag_bits = (item.flag_bits & ~0x08)
                zout.writestr(ni, ovrplatform_data)
            elif item.filename == "lib/armeabi-v7a/libovrfmod.so":
                print(f"  Replacing {item.filename} ({len(ovrfmod_data)} bytes)...")
                ni = zipfile.ZipInfo(filename=item.filename, date_time=item.date_time)
                ni.compress_type = item.compress_type
                ni.external_attr = item.external_attr
                ni.flag_bits = (item.flag_bits & ~0x08)
                zout.writestr(ni, ovrfmod_data)
            elif item.filename == "lib/armeabi-v7a/libopenxr_loader.so":
                print(f"  Replacing {item.filename} ({len(openxr_data)} bytes)...")
                ni = zipfile.ZipInfo(filename=item.filename, date_time=item.date_time)
                ni.compress_type = item.compress_type
                ni.external_attr = item.external_attr
                ni.flag_bits = (item.flag_bits & ~0x08)
                zout.writestr(ni, openxr_data)
                seen_openxr = True
            elif item.filename == "lib/armeabi-v7a/libminecraftpe.so":
                print(f"  Patching {item.filename}...")
                raw_so = bytearray(zin.read(item.filename))
                # Patch 1: SensorDevices::init premature controller call (offset 0x00d75188)
                # Replace 'bl 0x1d18784' (7d 8d 3e eb) with 'nop' (00 f0 20 e3)
                assert raw_so[0x00d75188:0x00d7518c] == bytes.fromhex("7d8d3eeb"), "Unexpected bytes at 0x00d75188"
                raw_so[0x00d75188:0x00d7518c] = bytes.fromhex("00f020e3")
                
                # Patch 2: GameControllerHandler_Android::handleDeviceConnected null check (offset 0x00d5fdfc)
                # Replace 'cmn r1, #1' (01 00 71 e3) with 'cmp r0, #0' (00 00 50 e3)
                assert raw_so[0x00d5fdfc:0x00d5fe00] == bytes.fromhex("010071e3"), "Unexpected bytes at 0x00d5fdfc"
                raw_so[0x00d5fdfc:0x00d5fe00] = bytes.fromhex("000050e3")
                
                print(f"  Applied 2 stability patches to libminecraftpe.so successfully!")
                ni = zipfile.ZipInfo(filename=item.filename, date_time=item.date_time)
                ni.compress_type = item.compress_type
                ni.external_attr = item.external_attr
                ni.flag_bits = (item.flag_bits & ~0x08)
                zout.writestr(ni, bytes(raw_so))
            else:
                zout.writestr(item, zin.read(item.filename))
        if not seen_openxr and ref_item:
            print(f"  Adding lib/armeabi-v7a/libopenxr_loader.so ({len(openxr_data)} bytes)...")
            ni = zipfile.ZipInfo(filename="lib/armeabi-v7a/libopenxr_loader.so", date_time=ref_item.date_time)
            ni.compress_type = ref_item.compress_type
            ni.external_attr = ref_item.external_attr
            ni.flag_bits = (ref_item.flag_bits & ~0x08)
            zout.writestr(ni, openxr_data)

os.replace(TMP, WORK_APK)

print(f"[repack] Zipaligning to 4-byte boundaries...")
if os.path.exists(ALIGNED_APK): os.remove(ALIGNED_APK)
subprocess.run([ZIPALIGN, "-p", "-f", "4", WORK_APK, ALIGNED_APK], check=True)

print(f"[repack] Signing with keystore...")
if os.path.exists(SIGNED_APK): os.remove(SIGNED_APK)
subprocess.run([
    APKSIGNER, "sign",
    "--ks", KEYSTORE,
    "--ks-pass", "pass:android",
    "--key-pass", "pass:android",
    "--out", SIGNED_APK,
    ALIGNED_APK
], check=True)

print(f"[repack] Verifying signature...")
subprocess.run([APKSIGNER, "verify", SIGNED_APK], check=True)

print(f"[repack] SUCCESS! Created: {SIGNED_APK} ({os.path.getsize(SIGNED_APK)} bytes)")
