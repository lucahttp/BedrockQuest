# BedrockQuest: Standalone 6DoF OpenXR Injector for Minecraft Bedrock

[![OpenXR](https://img.shields.io/badge/OpenXR-1.1-blue.svg)](https://www.khronos.org/openxr/)
[![Platform](https://img.shields.io/badge/Platform-PICO%204%20Ultra%20%2F%20Android%20ARM64-green.svg)](https://www.picoxr.com/)
[![Engine](https://img.shields.io/badge/Minecraft-Bedrock%20RenderDragon%20(v1.20--1.26%2B)-red.svg)](https://www.minecraft.net/)
[![Performance](https://img.shields.io/badge/Performance-90%20FPS%20Stereo-orange.svg)]()
[![License](https://img.shields.io/badge/License-GPL--3.0-purple.svg)](LICENSE)

**BedrockQuest** is an open-source, high-performance standalone 6DoF Virtual Reality injector for modern **Minecraft Bedrock Edition** (RenderDragon engine, 64-bit ARM64) on the **PICO 4 Ultra** (Snapdragon XR2 Gen 2 / PICO OS 5.x / Android 14) and OpenXR-compliant Android VR headsets.

It delivers full **QuestCraft control parity** and 6DoF immersion: natural 1:1 head tracking, 3D tracked Steve voxel arms anchored to physical controllers, physical swing mining with haptic impact, diegetic wrist inventory gestures, and floating spatial menu panels with anti-aliased laser pointer interaction.

---

## 🌟 Key Features (QuestCraft Parity)

* **6DoF Stereoscopic Rendering:** Native dual-eye OpenXR swapchain presentation (`XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR`) running locked at **90 FPS** with hardware TimeWarp / Reprojection compositor pacing.
* **1:1 Natural Head Tracking:** Head angular velocity (`headYawRate`, `headPitchRate`) mapped directly into Bedrock's native Gamepad Right Stick (`AXIS_Z` / `AXIS_RZ`). Completely eliminates Android VR pointer capture restrictions.
* **3D Tracked Steve Voxel Arms:** Fully shaded 3D Minecraft Steve arm models rendered in stereoscopic 3D anchored directly to your PICO 4 Ultra 6DoF controllers.
* **Physical Swing Mining:** Swing your right hand forward or downward to break blocks and attack mobs with realistic haptic vibration feedback.
* **Diegetic Wrist Gesture:** Glancing at your left wrist (watch position) smoothly toggles the in-game inventory without pressing any button.
* **Spatial Floating Menus:** When opening inventory (E / Y button), pause, chat, or settings, Bedrock's native `mCursorLocked` state is intercepted to dynamically project the UI onto a **floating 3D Quad Panel at 1.8m gaze distance**, activating a precision laser pointer and trigger touch clicks. When returning to gameplay, it smoothly transitions back to 360° full immersion.
* **Calibrated 110° VR FOV:** Auto-injects `gfx_field_of_view:110.0` into `options.txt` at startup, perfectly matching PICO lenses and eliminating the cramped/zoomed "binoculars" effect.
* **Vanilla Hand Suppression:** Auto-injects `gfx_hidehand:1` into `options.txt`, preventing duplicate arm rendering.

---

## 🎮 Controller Layout (QuestCraft Scheme)

| Action | Control (PICO 4 Ultra) | Bedrock Native Binding |
|---|---|---|
| **Locomotion (Walk / Strafe)** | Left Thumbstick (Analog) | `AXIS_X` / `AXIS_Y` |
| **Manual Turn (Snap / Smooth)** | Right Thumbstick (Analog) | `AXIS_Z` / `AXIS_RZ` |
| **Head Tracking Look** | Head Movement (6DoF HMD) | Dynamic Rate -> `AXIS_Z` / `AXIS_RZ` |
| **Jump** | Right Controller **A** | `AKEYCODE_BUTTON_A` |
| **Drop Item / Cancel** | Right Controller **B** | `AKEYCODE_BUTTON_B` |
| **Chat / Command** | Left Controller **X** | `AKEYCODE_BUTTON_X` |
| **Inventory Toggle** | Left Controller **Y** or **Look at Left Wrist** | `AKEYCODE_BUTTON_Y` |
| **Mine / Attack** | Right Trigger or **Physical Hand Swing** | `AXIS_GAS` / `AKEYCODE_BUTTON_R2` |
| **Place / Use Item** | Left Trigger | `AXIS_BRAKE` / `AKEYCODE_BUTTON_L2` |
| **Hotbar Scroll Right** | Right Grip | `AKEYCODE_BUTTON_R1` |
| **Hotbar Scroll Left** | Left Grip | `AKEYCODE_BUTTON_L1` |
| **Sprint Toggle** | Left Thumbstick Click | `AKEYCODE_BUTTON_THUMBL` |
| **Sneak / Crouch** | Right Thumbstick Click | `AKEYCODE_BUTTON_THUMBR` |
| **Pause / Menu** | Left Controller Menu Button | `AKEYCODE_BUTTON_START` |
| **Menu UI Navigation** | Left Thumbstick (D-Pad Mode) | `AKEYCODE_DPAD_*` |
| **Menu Pointer & Click** | Right Controller Ray + Trigger | 3D Ray-Plane Touch Dispatch |
| **VR Screen Mode Toggle** | Dual-Grip Hold (~0.2s) | Cinema Quad <-> 360° Immersion |

---

## 🏗️ Architectural Blueprint

```
[ PICO OS / Android 14 (ARM64) ]
               │
               ▼
 [ Minecraft Bedrock APK (ARM64) ]
   ├── classes3.dex ──► System.loadLibrary("mcbedvr")
   └── lib/arm64-v8a/libminecraftpe.so (RenderDragon)
               │
               ▼
     [ libmcbedvr.so ] (BedrockQuest Standalone OpenXR Injector)
   ┌───────────┴─────────────────────────────────────────────┐
   ▼                                                         ▼
[ 1. Lifecycle & Input Bridge ]             [ 2. RenderDragon Interception ]
- JNI RegisterNatives Batch Replacement     - SwappyGL_injectTracer (preSwap)
- Zero W⊕X / Zero SELinux trampolines       - Zero code-patching in RenderDragon
- GameActivity 48-axis motion dispatcher    - Intercepts frame before eglSwapBuffers
- Head yaw/pitch rate -> Right Stick        - Auto-switches Quad (UI) vs Projection (World)
   │                                                         │
   └───────────────────────────┬─────────────────────────────┘
                               ▼
                    [ 3. OpenXR Runtime ]
                    - Dynamic dlopen of PICO forwardloader
                    - Dual Eye Stereo Swapchains (kStereoSets = 4)
                    - PICO 4 Ultra 6DoF Controller tracking
                    - 90 FPS TimeWarp / Reprojection compositor
```

### Clean JNI Batching (Dubrovskiy Architecture)
Rather than patching `.text` segment instructions in memory (which triggers SELinux policy violations and `W⊕X` crashes on modern Android 14), BedrockQuest replaces Java native method bindings cleanly through `env->RegisterNatives` during `JNI_OnLoad`.

### Non-Invasive Frame Pacing Interception
RenderDragon paces its rendering loop via Google AGDK's `libswappy.so`. BedrockQuest registers a tracer callback via `SwappyGL_injectTracer`, capturing rendered framebuffers at the API boundary immediately before display presentation.

---

## ⚖️ Legal & Clean Room Design

* **No Piracy:** This repository contains **NO copyrighted game assets, textures, sounds, or Mojang binaries**.
* **Legitimate Ownership Required:** Users must legally own Minecraft Bedrock Edition on the **Google Play Store**.
* **Automated Acquisition:** The included `scripts/download_minecraft_apk.py` script downloads the genuine APK directly from Google Play using your authenticated Google account, or extracts it from your own connected Android/PICO device via ADB.

---

## 🚀 Quickstart & Build Guide

### Prerequisites
* **macOS** or **Linux** (ARM64 or x86_64)
* **Android NDK:** r27b or higher (`export ANDROID_NDK_HOME=/path/to/ndk`)
* **Android Build Tools:** 34.0.0+ (`zipalign`, `apksigner`, `adb`)
* **CMake:** 3.22.1+ (included in Android SDK)
* **Python:** 3.10+
* **Rust:** Stable toolchain (`cargo` for `pvr_manifest` AXML generator)

### Step 1: Clone Repository
```bash
git clone --recurse-submodules https://github.com/lucahttp/BedrockQuest.git
cd BedrockQuest
```

### Step 2: Acquire Legitimate Minecraft Bedrock APK
Run the interactive downloader to acquire your legally purchased Minecraft APK:
```bash
python3 scripts/download_minecraft_apk.py
```
* **Option 1:** Download directly from Google Play (enter your Google email and App Password).
* **Option 2:** 1-Click extraction from your connected Android phone, tablet, or PICO visor via ADB (`pm path com.mojang.minecraftpe`).
* **Option 3:** Import an existing genuine APK or mcpelauncher installation.

### Step 3: Compile and Repack
Run the automated pipeline script:
```bash
bash scripts/build_and_repack.sh
```
This script will:
1. Compile `libmcbedvr.so` for `arm64-v8a` with Android NDK.
2. Compile and run `pvr_manifest` to generate the PICO OpenXR `IMMERSIVE_HMD` AXML manifest.
3. Repack `libmcbedvr.so` and `libopenxr_loader.so` into the APK.
4. Align with `zipalign -p 4`.
5. Sign with APK Signature Scheme v2/v3.
6. Verify signature and export to `dist/minecraft_vr_signed.apk`.

### Step 4: Install to PICO 4 Ultra
Connect your PICO 4 Ultra via USB (or Wi-Fi ADB) and install:
```bash
adb install -r dist/minecraft_vr_signed.apk
```

Launch **Minecraft** from your PICO library. Put on your visor and enjoy full 6DoF standalone Minecraft Bedrock in VR!

---

## 📚 References & Acknowledgments

This project is built upon the invaluable research and open-source foundations of:
* **[QuestCraft](https://github.com/QuestCraftPlusPlus/QuestCraft)** — Controls paradigm, physical swing mining, diegetic wrist inventory gesture, and spatial menu concepts.
* **[Dubrovskiy Yevhen](https://github.com/dubrovskiy-yevhen-stakelogic/gta-sa-vr-quest)** (`gta-sa-vr-quest` & `rdr1-quest`) — JNI `RegisterNatives` batch swapping architecture, OpenXR swapchains, and controller aiming decoupling.
* **[MCPE Launcher](https://github.com/minecraft-linux/mcpelauncher-manifest)** — Bedrock JNI symbol reversing and Google Play extraction methods.
* **[Khronos Group](https://github.com/KhronosGroup/OpenXR-SDK)** — OpenXR 1.1 specification and cross-platform VR runtime.
* **[ByteDance PICO](https://github.com/picoxr/OpenXR_Native_SDK)** — PICO OpenXR SDK and controller interaction profiles.
* **[Google AGDK](https://developer.android.com/games/agdk)** — GameActivity input dispatcher and SwappyGL frame pacing library.
* **[Dobby](https://github.com/jmpews/Dobby)** — Native hook engine.

For complete architectural notes, deep-links, and citation details, see [REFERENCES.md](REFERENCES.md).

---

## 📄 License

The injector code and build tooling in this repository are licensed under the **GNU General Public License v3.0 (GPL-3.0)**. See [LICENSE](LICENSE) for details.

*Disclaimer: Minecraft is a registered trademark of Mojang AB / Microsoft Corporation. BedrockQuest is an independent open-source project and is not affiliated with, endorsed by, or connected to Mojang AB, Microsoft, ByteDance, or Meta.*
