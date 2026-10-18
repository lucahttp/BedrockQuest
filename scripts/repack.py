#!/usr/bin/env python3
"""Atomic libmcbedvr.so and libopenxr_loader.so replacement/addition inside APK using zipfile module.

Avoids all the timestamp gotchas of `zip -f` by rebuilding ZIP entries
with the new payload but preserving original ZipInfo metadata.
"""
import zipfile, shutil, os, hashlib, re, sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))

SRC = None
if len(sys.argv) > 1 and os.path.isfile(sys.argv[1]):
    SRC = os.path.abspath(sys.argv[1])
elif os.environ.get("MINECRAFT_BASE_APK") and os.path.isfile(os.environ["MINECRAFT_BASE_APK"]):
    SRC = os.path.abspath(os.environ["MINECRAFT_BASE_APK"])
else:
    candidates = [
        os.path.join(PROJECT_ROOT, "minecraft_apk/minecraft_bedrock.apk"),
        os.path.join(PROJECT_ROOT, "minecraft_apk/base.apk"),
        "/Volumes/X10Pro/Downloads/minecraft_vr.apk",
        os.path.expanduser("~/Downloads/minecraft_vr.apk"),
        os.path.expanduser("~/Downloads/com.mojang.minecraftpe.apk"),
    ]
    for c in candidates:
        if os.path.isfile(c):
            SRC = c
            break

if not SRC:
    print("[ERROR] Base Minecraft APK not found!")
    print("Run: python3 scripts/download_minecraft_apk.py")
    sys.exit(1)

DST = os.path.join(PROJECT_ROOT, "build/_repack/minecraft_vr.apk")
NEW_SO = os.path.join(PROJECT_ROOT, "build/dist/arm64-v8a/libmcbedvr.so")
TARGET = "lib/arm64-v8a/libmcbedvr.so"

OPENXR_SO = os.path.join(PROJECT_ROOT, "vendor/openxr/lib/arm64-v8a/libopenxr_loader.so")
TARGET_OPENXR = "lib/arm64-v8a/libopenxr_loader.so"

os.makedirs(os.path.dirname(DST), exist_ok=True)
shutil.copyfile(SRC, DST)
print(f"copied {SRC} -> {DST}  ({os.path.getsize(DST)} bytes)")

new_content = open(NEW_SO, 'rb').read()
openxr_content = open(OPENXR_SO, 'rb').read()
TMP = DST + ".tmp"
seen_openxr = False

TARGET_MANIFEST = "AndroidManifest.xml"
MANIFEST_PATH = os.path.join(PROJECT_ROOT, "build/AndroidManifest.pvr.xml")
manifest_content = open(MANIFEST_PATH, 'rb').read() if os.path.exists(MANIFEST_PATH) else None

with zipfile.ZipFile(DST, 'r') as zin:
    # Find a reference native library to clone metadata for new entries
    ref_item = None
    for item in zin.infolist():
        if item.filename.startswith("lib/arm64-v8a/"):
            ref_item = item
            break

    with zipfile.ZipFile(TMP, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zout:
        for item in zin.infolist():
            if item.filename == TARGET:
                print(f"REPLACING {item.filename}: old_size={item.file_size} -> new_size={len(new_content)}")
                ni = zipfile.ZipInfo(filename=item.filename, date_time=item.date_time)
                ni.compress_type = item.compress_type
                ni.external_attr = item.external_attr
                ni.create_system = item.create_system
                ni.create_version = item.create_version
                ni.extract_version = item.extract_version
                ni.flag_bits = (item.flag_bits & ~0x08)  # clear data-descriptor flag → force CRC recompute
                zout.writestr(ni, new_content)
            elif item.filename == TARGET_OPENXR:
                print(f"REPLACING {item.filename}: old_size={item.file_size} -> new_size={len(openxr_content)}")
                ni = zipfile.ZipInfo(filename=item.filename, date_time=item.date_time)
                ni.compress_type = item.compress_type
                ni.external_attr = item.external_attr
                ni.create_system = item.create_system
                ni.create_version = item.create_version
                ni.extract_version = item.extract_version
                ni.flag_bits = (item.flag_bits & ~0x08)
                zout.writestr(ni, openxr_content)
                seen_openxr = True
            elif item.filename == TARGET_MANIFEST and manifest_content:
                print(f"REPLACING {item.filename}: old_size={item.file_size} -> new_size={len(manifest_content)}")
                ni = zipfile.ZipInfo(filename=item.filename, date_time=item.date_time)
                ni.compress_type = item.compress_type
                ni.external_attr = item.external_attr
                ni.create_system = item.create_system
                ni.create_version = item.create_version
                ni.extract_version = item.extract_version
                ni.flag_bits = (item.flag_bits & ~0x08)
                zout.writestr(ni, manifest_content)
            else:
                zout.writestr(item, zin.read(item.filename))

        if not seen_openxr:
            print(f"ADDING {TARGET_OPENXR}: size={len(openxr_content)}")
            dt = ref_item.date_time if ref_item else (2026, 10, 10, 22, 55, 30)
            compress_type = ref_item.compress_type if ref_item else zipfile.ZIP_DEFLATED
            external_attr = ref_item.external_attr if ref_item else 0
            create_system = ref_item.create_system if ref_item else 0
            create_version = ref_item.create_version if ref_item else 20
            extract_version = ref_item.extract_version if ref_item else 20
            flag_bits = (ref_item.flag_bits & ~0x08) if ref_item else 0

            ni = zipfile.ZipInfo(filename=TARGET_OPENXR, date_time=dt)
            ni.compress_type = compress_type
            ni.external_attr = external_attr
            ni.create_system = create_system
            ni.create_version = create_version
            ni.extract_version = extract_version
            ni.flag_bits = flag_bits
            zout.writestr(ni, openxr_content)

os.replace(TMP, DST)

# Verify
with zipfile.ZipFile(DST, 'r') as z:
    embedded = z.read(TARGET)
    embedded_oxr = z.read(TARGET_OPENXR)

    build_md5 = hashlib.md5(new_content).hexdigest()
    embed_md5 = hashlib.md5(embedded).hexdigest()
    print(f"embedded size : {len(embedded)} bytes")
    print(f"embedded md5  : {embed_md5}")
    print(f"build md5     : {build_md5}")
    print(f"match         : {embed_md5 == build_md5}")

    oxr_build_md5 = hashlib.md5(openxr_content).hexdigest()
    oxr_embed_md5 = hashlib.md5(embedded_oxr).hexdigest()
    print(f"embedded oxr size : {len(embedded_oxr)} bytes")
    print(f"embedded oxr md5  : {oxr_embed_md5}")
    print(f"build oxr md5     : {oxr_build_md5}")
    print(f"oxr match         : {oxr_embed_md5 == oxr_build_md5}")

    # Quick scan for the corrected JNI descriptors
    found = []
    for sig in [b'(III)Z', b'(I)V', b'(II)V', b'(JF)V', b'(Ljava/lang/String;)Z']:
        if sig in embedded:
            found.append(sig.decode('ascii'))
    print(f"signatures embedded: {found}")

    # Verify manifest if patched
    manifest_ok = True
    if manifest_content:
        embedded_manifest = z.read(TARGET_MANIFEST)
        manifest_md5 = hashlib.md5(manifest_content).hexdigest()
        embedded_manifest_md5 = hashlib.md5(embedded_manifest).hexdigest()
        has_vr_cat = ('IMMERSIVE_HMD'.encode('utf-16le') in embedded_manifest) or (b'IMMERSIVE_HMD' in embedded_manifest)
        manifest_ok = (manifest_md5 == embedded_manifest_md5 and has_vr_cat)
        print(f"manifest match     : {manifest_ok} (IMMERSIVE_HMD in manifest: {has_vr_cat})")

    all_ok = (embed_md5 == build_md5 and
              oxr_embed_md5 == oxr_build_md5 and
              manifest_ok)
sys.exit(0 if all_ok else 1)
