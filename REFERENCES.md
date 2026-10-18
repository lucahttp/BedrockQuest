# References & Prior Art

The **MCBedVR** project builds upon the foundational research, reverse engineering, and open-source contributions of several groundbreaking projects in the standalone VR and Minecraft communities.

---

## 1. Core Architectural Blueprints

### 1.1 QuestCraft
* **Repository:** [QuestCraftPlusPlus/QuestCraft](https://github.com/QuestCraftPlusPlus/QuestCraft)
* **License:** GPL-3.0
* **Contributions & Influence on MCBedVR:**
  * **Controls Paradigm:** Exact control mapping parity (locomotion on left stick, turning on right stick, sneak on stick click, sprint on stick click, jump on A, inventory on Y).
  * **Diegetic Wrist Gesture:** Looking at the player's left wrist (watch position) triggers the inventory toggle without needing a controller button.
  * **Physical Swing Mining:** Forward/downward physical hand velocity gestures triggering attack and mining actions with haptic impact feedback.
  * **Spatial Floating Menus:** Presenting 2D menus (start screen, inventory, settings) as floating panels at 1.8 meters in front of the player's gaze with procedural anti-aliased laser reticles and trigger click dispatch.

### 1.2 GTA: San Andreas VR for Oculus Quest (`libsavr.so`)
* **Author:** Dubrovskiy Yevhen (Stakelogic)
* **Repository:** [dubrovskiy-yevhen-stakelogic/gta-sa-vr-quest](https://github.com/dubrovskiy-yevhen-stakelogic/gta-sa-vr-quest)
* **Contributions & Influence on MCBedVR:**
  * **JNI `RegisterNatives` Batch Replacement:** Demonstrates that overriding Java native methods via standard ART `env->RegisterNatives` is cleaner, safer, and more stable than binary function hooking (`.text` patching). Completely avoids SELinux policy violations and `W⊕X` memory protection faults on Android 10+.
  * **OpenXR Swapchain Management:** Multi-buffered stereo swapchains (`XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR`) rendered via OpenGL ES blit buffers.
  * **Decoupled Aiming & Head Look:** Separating camera rotation from weapon/tool orientation in 6DoF space.

### 1.3 Red Dead Redemption 1 VR for Quest
* **Author:** Dubrovskiy Yevhen (Stakelogic)
* **Repository:** [dubrovskiy-yevhen-stakelogic/rdr1-quest](https://github.com/dubrovskiy-yevhen-stakelogic/rdr1-quest)
* **Contributions & Influence on MCBedVR:**
  * Viewport dimensioning and render target capture.
  * Diegetic HUD anchoring techniques in standalone mobile OpenXR.

---

## 2. Minecraft Bedrock Engine & Android Runtimes

### 2.1 MCPE Launcher (Linux & macOS)
* **Repositories:**
  * [minecraft-linux/mcpelauncher-manifest](https://github.com/minecraft-linux/mcpelauncher-manifest)
  * [minecraft-linux/mcpelauncher-extract](https://github.com/minecraft-linux/mcpelauncher-extract)
  * [minecraft-linux/macos-builder](https://github.com/minecraft-linux/macos-builder)
* **Contributions & Influence on MCBedVR:**
  * **Symbol Reverse Engineering:** Mapping JNI lifecycle symbols (`Java_com_mojang_minecraftpe_MainActivity_*`), keyboard/mouse handlers, and window resize hooks.
  * **Genuine Google Play Extraction:** Verified methods for downloading and verifying genuine Minecraft Bedrock APK packages directly from Google Play.

### 2.2 Google Android Game Development Kit (AGDK)
* **Documentation:** [developer.android.com/games/agdk](https://developer.android.com/games/agdk)
* **Components Used:**
  * **`GameActivity` (`com.google.androidgamesdk.GameActivity`):** Bedrock 1.26 uses AGDK GameActivity for Android input and window events. We enable all 48 pointer axes via `GameActivityPointerAxes_enableAxis` and dispatch motion events natively.
  * **`SwappyGL` (Android Frame Pacing Library):** Modern Bedrock RenderDragon paces frames through Google Swappy. We use `SwappyGL_injectTracer` to intercept frames at API level (`PreSwapCb` / `PostSwapCb`) right before buffer swap, avoiding invasive code hooks.

---

## 3. OpenXR & Device SDKs

### 3.1 Khronos Group OpenXR
* **Repository:** [KhronosGroup/OpenXR-SDK](https://github.com/KhronosGroup/OpenXR-SDK)
* **Specification:** [OpenXR 1.1 Specification](https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html)
* **Role:** Standard cross-platform VR/AR API providing 6DoF tracking, stereo composition layers (`XrCompositionLayerProjection`, `XrCompositionLayerQuad`), action spaces, and swapchains.

### 3.2 ByteDance PICO OpenXR Native SDK
* **Repository:** [picoxr/OpenXR_Native_SDK](https://github.com/picoxr/OpenXR_Native_SDK)
* **Role:**
  * Interaction profile `/interaction_profiles/bytedance/pico4s_controller` (PICO 4 Ultra / Hawk controllers).
  * System OpenXR runtime loader and forward loader resolution (`libopenxr_loader.so` / `/system/lib64/libopenxr_forwardloader.so`).
  * Android Manifest VR metadata specifications (`pvr.app.type="vr"`, `com.oculus.intent.category.VR="vr_only"`).

---

## 4. Reverse Engineering & Binary Tooling

### 4.1 Dobby Hook Framework
* **Author:** jmpews
* **Repository:** [jmpews/Dobby](https://github.com/jmpews/Dobby)
* **Role:** Multi-platform inline hook framework used as fallback for native graphic hooks (`eglSwapBuffers`).

### 4.2 LLVM Toolchain & Android NDK
* **Toolchain:** Android NDK r27b (`clang`, `llvm-objdump`, `llvm-nm`, `llvm-readelf`).
* **Role:** Forensic symbol audit, dynamic relocation disassembly, and ARM64-v8a compilation.

---

## 5. Legal & Fair Use Notice

* Minecraft is a registered trademark of Mojang AB / Microsoft Corporation.
* MCBedVR is an independent, non-commercial open-source research project not affiliated with, endorsed by, or sponsored by Mojang AB, Microsoft, ByteDance, or Meta.
* This repository contains **NO copyrighted game assets, textures, sounds, or game binaries**. Users must legally purchase Minecraft Bedrock Edition from the Google Play Store to run this software.
