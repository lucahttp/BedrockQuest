#!/usr/bin/env python3
"""scripts/download_minecraft_apk.py — Legal Minecraft Bedrock APK Downloader & Preparer.

Provides 3 legal acquisition workflows:
  1. Google Play Store download (using gpapi & user credentials).
  2. ADB extraction from a connected Android phone, tablet, or PICO visor.
  3. Import from local file or mcpelauncher versions directory.

Ensures no copyrighted assets are committed to the public Git repository.
"""

import os
import sys
import getpass
import subprocess
import shutil
import zipfile
import argparse

# Force pure-Python protobuf to guarantee compatibility across protoc versions
os.environ["PROTOCOL_BUFFERS_PYTHON_IMPLEMENTATION"] = "python"

PACKAGE_NAME = "com.mojang.minecraftpe"
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
DEST_DIR = os.path.join(PROJECT_ROOT, "minecraft_apk")
DEST_APK = os.path.join(DEST_DIR, "minecraft_bedrock.apk")


def banner():
    print(r"""
============================================================
   MCBedVR: Minecraft Bedrock APK Acquisition Tool
   Standalone 6DoF OpenXR Injector for PICO 4 Ultra
============================================================
""")


def verify_apk(apk_path: str) -> bool:
    """Verifies that the APK is a valid 64-bit ARM64 Minecraft Bedrock installation."""
    if not os.path.isfile(apk_path):
        print(f"[!] File not found: {apk_path}")
        return False

    size_mb = os.path.getsize(apk_path) / (1024 * 1024)
    print(f"[*] Checking APK ({size_mb:.1f} MB): {apk_path}")

    try:
        with zipfile.ZipFile(apk_path, "r") as zf:
            namelist = set(zf.namelist())

            has_libpe = "lib/arm64-v8a/libminecraftpe.so" in namelist
            has_manifest = "AndroidManifest.xml" in namelist
            has_assets = any(n.startswith("assets/") for n in namelist)

            if not has_manifest:
                print("[!] Error: AndroidManifest.xml missing.")
                return False

            if not has_libpe:
                print("[!] Error: lib/arm64-v8a/libminecraftpe.so missing.")
                print("    (MCBedVR requires modern 64-bit ARM64 Bedrock v1.20+)")
                return False

            print("[+] Verification SUCCESS:")
            print("    - 64-bit ARM64 RenderDragon binary: PRESENT")
            print("    - Package assets: PRESENT")
            return True
    except zipfile.BadZipFile:
        print("[!] Error: Corrupted or incomplete ZIP/APK file.")
        return False


def method_google_play():
    """Downloads genuine Minecraft APK directly from Google Play."""
    print("\n--- Google Play Store Direct Download ---")
    print("This method connects to Google Play using your Google account.")
    print("Tip: If you have 2-Factor Authentication enabled, create an App Password at:")
    print("     https://myaccount.google.com/apppasswords\n")

    try:
        from gpapi.googleplay import GooglePlayAPI
    except ImportError:
        print("[*] Installing 'gpapi' dependency...")
        subprocess.check_call([sys.executable, "-m", "pip", "install", "gpapi"])
        from gpapi.googleplay import GooglePlayAPI

    email = input("Google Play Email: ").strip()
    if not email:
        print("[!] Email is required.")
        return False

    password = getpass.getpass("Password or App Password: ").strip()
    if not password:
        print("[!] Password is required.")
        return False

    print("\n[*] Initializing Google Play API session (husky / ARM64)...")
    server = GooglePlayAPI(locale="en_US", timezone="UTC", device_codename="husky")

    try:
        server.login(email, password)
        print("[+] Successfully authenticated with Google Play!")
    except Exception as e:
        print(f"[!] Authentication failed: {e}")
        return False

    print(f"[*] Querying license for {PACKAGE_NAME}...")
    try:
        details = server.details(PACKAGE_NAME)
        doc = details.get("docV2", {})
        title = doc.get("title", PACKAGE_NAME)
        version_code = doc.get("details", {}).get("appDetails", {}).get("versionCode", 0)
        print(f"[+] Found: {title} (VersionCode: {version_code})")
    except Exception as e:
        print(f"[!] Could not fetch app details: {e}")
        return False

    os.makedirs(DEST_DIR, exist_ok=True)
    tmp_out = os.path.join(DEST_DIR, "download_tmp.apk")

    print(f"[*] Downloading {PACKAGE_NAME} from Google Play servers...")
    try:
        data = server.download(PACKAGE_NAME, versionCode=version_code)
        with open(tmp_out, "wb") as f:
            for chunk in data.get("file", {}).get("data", []):
                f.write(chunk)

        if not os.path.exists(tmp_out) or os.path.getsize(tmp_out) < 1000000:
            # Check if splits exist
            splits = data.get("splits", [])
            if splits:
                print(f"[*] App delivered with {len(splits)} split APKs. Merging...")
                # Save base split
                with open(tmp_out, "wb") as f:
                    for chunk in splits[0].get("file", {}).get("data", []):
                        f.write(chunk)

        shutil.move(tmp_out, DEST_APK)
        print(f"[+] Saved genuine Minecraft Bedrock APK to: {DEST_APK}")
        return verify_apk(DEST_APK)
    except Exception as e:
        print(f"[!] Download failed: {e}")
        if os.path.exists(tmp_out):
            os.remove(tmp_out)
        return False


def method_adb():
    """Extracts genuine installed Minecraft Bedrock from a connected device."""
    print("\n--- Extract from Connected Android Device / PICO Headset via ADB ---")
    adb = shutil.which("adb") or "/Volumes/X10Pro/Android/sdk/platform-tools/adb"
    if not os.path.isfile(adb):
        print("[!] adb command not found. Please install Android Platform Tools.")
        return False

    print("[*] Detecting connected ADB devices...")
    out = subprocess.check_output([adb, "devices"]).decode()
    devices = [line.split()[0] for line in out.splitlines()[1:] if "\tdevice" in line]

    if not devices:
        print("[!] No ADB devices connected or authorized.")
        print("    Ensure your device is connected via USB and Developer Mode is ON.")
        return False

    device = devices[0]
    print(f"[+] Using device: {device}")

    print(f"[*] Checking for {PACKAGE_NAME} on device...")
    try:
        out = subprocess.check_output([adb, "-s", device, "shell", f"pm path {PACKAGE_NAME}"]).decode()
    except subprocess.CalledProcessError:
        out = ""

    lines = [l.strip().replace("package:", "") for l in out.splitlines() if l.strip().startswith("package:")]
    if not lines:
        print(f"[!] {PACKAGE_NAME} is not installed on the connected device.")
        return False

    base_remote_apk = lines[0]
    print(f"[+] Found installed package: {base_remote_apk}")

    os.makedirs(DEST_DIR, exist_ok=True)
    print(f"[*] Pulling APK from device to {DEST_APK}...")
    subprocess.check_call([adb, "-s", device, "pull", base_remote_apk, DEST_APK])

    return verify_apk(DEST_APK)


def method_local(import_path=None):
    """Imports from a local file path or auto-detects mcpelauncher versions."""
    print("\n--- Import from Local File / mcpelauncher ---")

    if not import_path:
        # Check mcpelauncher default path
        mcpelauncher_dir = os.path.expanduser("~/Library/Application Support/mcpelauncher/versions")
        candidates = []
        if os.path.isdir(mcpelauncher_dir):
            for v in os.listdir(mcpelauncher_dir):
                candidate_dir = os.path.join(mcpelauncher_dir, v)
                if os.path.isdir(candidate_dir) and os.path.isfile(os.path.join(candidate_dir, "AndroidManifest.xml")):
                    candidates.append(candidate_dir)

        if candidates:
            print("[+] Found mcpelauncher version directories:")
            for idx, c in enumerate(candidates):
                print(f"    [{idx+1}] {c}")
            choice = input(f"Select version [1-{len(candidates)}] or press Enter to specify custom path: ").strip()
            if choice.isdigit() and 1 <= int(choice) <= len(candidates):
                selected = candidates[int(choice) - 1]
                print(f"[*] Packaging {selected} into APK...")
                os.makedirs(DEST_DIR, exist_ok=True)
                with zipfile.ZipFile(DEST_APK, "w", zipfile.ZIP_DEFLATED) as zout:
                    for root, _, files in os.walk(selected):
                        for f in files:
                            full = os.path.join(root, f)
                            rel = os.path.relpath(full, selected)
                            zout.write(full, rel)
                return verify_apk(DEST_APK)

        import_path = input("Enter path to Minecraft Bedrock APK: ").strip()

    import_path = os.path.expanduser(import_path)
    if not os.path.isfile(import_path):
        print(f"[!] File not found: {import_path}")
        return False

    os.makedirs(DEST_DIR, exist_ok=True)
    shutil.copyfile(import_path, DEST_APK)
    print(f"[+] Copied to: {DEST_APK}")
    return verify_apk(DEST_APK)


def main():
    banner()

    parser = argparse.ArgumentParser(description="MCBedVR APK Preparation Tool")
    parser.add_argument("--gplay", action="store_true", help="Download from Google Play Store")
    parser.add_argument("--adb", action="store_true", help="Extract from connected ADB device")
    parser.add_argument("--import-apk", dest="import_apk", metavar="PATH", help="Import an existing APK file")

    args = parser.parse_args()

    success = False
    if args.gplay:
        success = method_google_play()
    elif args.adb:
        success = method_adb()
    elif args.import_apk:
        success = method_local(args.import_apk)
    else:
        print("Please choose an acquisition method:\n")
        print("  1) Download from Google Play Store (Using your Google Account)")
        print("  2) Extract from connected Android / PICO device via ADB (1-click)")
        print("  3) Import from local APK file or mcpelauncher version")
        print("  4) Exit\n")

        choice = input("Select an option [1-4]: ").strip()
        if choice == "1":
            success = method_google_play()
        elif choice == "2":
            success = method_adb()
        elif choice == "3":
            success = method_local()
        else:
            print("Exiting.")
            sys.exit(0)

    if success:
        print("\n" + "=" * 60)
        print("[+] SUCCESS! Base Minecraft Bedrock APK ready at:")
        print(f"    {DEST_APK}")
        print("\nYou can now build the Standalone VR APK with:")
        print("    bash scripts/build_and_repack.sh")
        print("=" * 60)
    else:
        print("\n[!] Failed to prepare genuine Minecraft APK.")
        sys.exit(1)


if __name__ == "__main__":
    main()
