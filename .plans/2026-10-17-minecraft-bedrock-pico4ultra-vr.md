# Engineering Plan: Minecraft Bedrock Standalone 6DoF VR on PICO 4 Ultra

**Date:** 2026-10-17  
**Status:** In Progress (Living Document)  
**Target Device:** PICO 4 Ultra (Snapdragon XR2 Gen 2, PICO OS 5.x / Android 14, 64-bit ARM64)  
**Target Game:** Minecraft Bedrock Edition v1.26.52.3 (ARM64)

---

## 1. Executive Summary & Forensic Audit

### 1.1 Input Analysis

| Input Artifact | Reality on Disk | Viability & Role |
|---|---|---|
| `/Volumes/X10Pro/Downloads/com.mojang.minecraftvr.apk` | 32-bit only (`lib/armeabi-v7a/`, 79.5 MB `libminecraftpe.so`, `libvrapi.so`). | **Not viable for direct execution.** Snapdragon XR2 Gen 2 (ARMv9 Cortex-X3 / A715) drops 32-bit hardware support at EL0. Serves as reference for legacy VR assets (`sounds/vr/`, `levelstage_copy_vr.fragment`). |
| `/Volumes/X10Pro/Downloads/minecraft-v1.26.52.3-final-mod2_sdk.apk` | 64-bit pure (`lib/arm64-v8a/libminecraftpe.so`, 328 MB). | **Primary target.** Modern Bedrock engine (RenderDragon + Google GameActivity + Swappy frame pacing). Contains residual VR UI hooks (`STEREO_RENDERING`, `HMD_POSITION_DISPLACEMENT`, `$can_swap_vr_mode`). |
| `minecraft-linux/mcpelauncher-manifest` & `macos-builder` | Custom Bionic linker & JNI shim for Linux/macOS. | **Design reference.** Informs viewport sizing (`App::setRenderingSize`), window events, and symbol mapping. Fake Bionic layer is discarded because PICO OS is already native Android. |
| `dubrovskiy-yevhen-stakelogic/gta-sa-vr-quest` & `vice-city-vr` | Standalone OpenXR injector (`libsavr.so`) for Quest. | **Core architectural blueprint.** Provides the gold-standard patterns: JNI `RegisterNatives` batch replacement, OpenXR swapchains, decoupled 6DoF controller aiming (`ScopeAim`), and diegetic UI. |

---

## 2. Architectural Blueprint

```
[ PICO OS / Android 14 (ARM64) ]
               │
               ▼
 [ Minecraft Bedrock v1.26.52.3 APK ]
   ├── classes3.dex ──► System.loadLibrary("mcbedvr")
   └── lib/arm64-v8a/libminecraftpe.so (RenderDragon)
               │
               ▼
       [ libmcbedvr.so ] (Injector)
   ┌───────────┴─────────────────────────────────────────────┐
   ▼                                                         ▼
[ 1. Lifecycle & Timing ]                   [ 2. Render Interception ]
- RegisterNatives A1                        - SwappyGL_injectTracer (preSwap)
- ChoreographerCallback_nOnChoreographer    - Zero W⊕X / Zero SELinux trampolines
- SyncClock (frame pacing & display time)   - Intercepts frame before eglSwapBuffers
   │                                                         │
   └───────────────────────────┬─────────────────────────────┘
                               ▼
                    [ 3. OpenXR Runtime ]
                    - Dynamic dlopen of PICO loader:
                      /system/lib64/libopenxr_forwardloader.so
                    - Stereo Swapchains (L/R eyes, kStereoSets = 4)
                    - PICO 4 Ultra 6DoF Controller tracking
                    - TimeWarp / Reprojection compositor
```

### 2.1 The 5 Delivery Principles Applied

1. **Question every requirement:**
   - *Do we need a `libvrapi.so` shim?* No. Modern Bedrock 1.26 has no VrApi references; VrApi is dead. We bind directly to OpenXR.
   - *Do we need a fake Bionic linker like mcpelauncher?* No. PICO OS runs native Android Bionic libc and ART.
   - *Do we need dangerous inline code hooks in RenderDragon?* No. Google AGDK's `SwappyGL_injectTracer` provides clean API-level frame interception.

2. **Delete unnecessary parts:**
   - Drop the 32-bit Gear VR execution path entirely.
   - Remove memory patching and code trampolines from the render hot path.

3. **Simplify and optimize:**
   - Deliver in 3 clean stages:
     - Stage 1: Boot + Theater / 6DoF TimeWarp projection (Immediate operational MVP).
     - Stage 2: 6DoF PICO Controller input and locomotion.
     - Stage 3: True stereoscopic parallax depth & spatial diegetic UI (wrist hotbar, floating inventory).

4. **Accelerate cycle time:**
   - Use automated repack scripts (`repack.py`) to inject updated `libmcbedvr.so` into the APK without full reassembly.

5. **Automate wherever possible:**
   - Build, repack, sign, and install scripted via single-command workflow.

---

## 3. Progress & Task Status

| Task ID | Task Description | Risk Level | Status | Notes / Output |
|---|---|---|---|---|
| **T01** | Forensic audit of APKs and reference repositories | Medium | **DONE** | Confirmed GearVR is 32-bit only; confirmed 1.26 is 64-bit pure with VR residual symbols. |
| **T02** | Lock injector architecture contract | High | **DONE** | Ratified `architecture.locked.md` and `xr_render_plan.md` using Swappy tracer API. |
| **T03** | Implement Phase 1/2.A OpenXR Context (`XrContext.cpp`) | High | **DONE** | Dynamic loader for PICO forwardloader, local space, stereo swapchains. |
| **T04** | Implement Phase 2.B Synchronization Clock (`SyncClock.cpp`) | Medium | **DONE** | Lock-free counters connecting Choreographer ticks with OpenXR time. |
| **T05** | Wire JNI `RegisterNatives` batching (`JNI_Hooks.cpp`) | Medium | **DONE** | Lifecycle and Choreographer methods hooked via clean JVM tables. |
| **T06** | Wire Swappy tracer bridge to SyncClock (`SwappyBridge.cpp`) | High | **DONE** | Connected `PreSwapCb` and `PostSwapCb` to `SyncClockOnFrameBegin`/`End`. |
| **T07** | Implement Framebuffer Blit to OpenXR Swapchain (`BlitEye.cpp`) | High | **DONE** | `glBlitFramebuffer` with `glFramebufferTextureLayer` blits game FBO to OpenXR stereo swapchain array layers 0/1; submits `XrCompositionLayerProjection`. |
| **T08** | Implement PICO 4 Ultra 6DoF Controller Input Mapping & Bridge | Medium | **DONE** | OpenXR action set (`mcbedvr_input`), bindings for `pico4s_controller`, `pico4_controller`, and `touch_controller`. Implemented `InputBridge` with native `activity.dispatchKeyEvent` (`SOURCE_GAMEPAD`), `activity.dispatchTouchEvent`, thumbstick D-Pad traversal, and registered Xbox controller in Bedrock via `JellyBeanDeviceManager`. |
| **T09** | Stereo Camera Parallax & Diegetic UI | High | **TODO** | Dual render pass / camera matrix hook and floating wrist hotbar / inventory. |
| **T10** | End-to-end repack, build, and deploy script | Low | **DONE** | `scripts/build_and_repack.sh` with `pvr_manifest` AXML patcher (IMMERSIVE_HMD category, OpenXR broker queries, PICO VR metadata), zipalign, and apksigner v3 scheme verified. Output: `minecraft_vr_v6_signed.apk`. |
| **T11** | Fix OpenXR Loader Linkage & EGL Session Deferral | High | **DONE** | Directly linked `libopenxr_loader.so` via CMake, initialized loader via `xrInitializeLoaderKHR`, enabled Android create instance extension, deferred `xrCreateSession` to render thread (`eglSwapBuffers`). |
| **T12** | Google Play API Automated APK Extraction | Low | **TODO** | Automated script pulling genuine Bedrock APKs via `~/Library/Application Support/mcpelauncher/versions/` and `google-play-api` integration. |
| **T13** | VR Cinema Quad Screen Laser Pointer & Procedural Reticle | Medium | **DONE** | OpenXR controller aim pose ray-plane intersection against Quad at `z = -1.8m`, procedural anti-aliased glowing reticle rendered directly to swapchain in GL ES 3.0, and trigger touch dispatch. |
| **T14** | Analog Joystick Locomotion & Trigger Dispatch (`InputBridge`) | High | **DONE** | Implemented `DispatchJoystick` via JNI `dispatchGenericMotionEvent` (`SOURCE_JOYSTICK`, `AXIS_X/Y/Z/RZ`, `AXIS_BRAKE/GAS`). Left stick walks/strafes smoothly, right stick turns, triggers mine and place. |
| **T15** | 6DoF Head Tracking Look Injection & VR Immersion Mode Gating | High | **DONE** | Gated Cinema Quad (menu with glowing laser reticle) vs 360 Immersive VR (gameplay with 6DoF HMD rotation look injection via `DispatchLookDelta` and dual-grip/Y button toggle). Verified in signed APK. |
| **T16** | Fix Control Conflicts & Head Tracking Camera Look Architecture | High | **DONE** | Audited legacy GearVR APK, eliminated triplicate trigger conflicts, enabled all 48 GameActivity pointer axes, decoupled right stick manual turn from head look, discovered valid device ID dynamically (`pvr-virtual-input-0`), and hooked `mCursorLocked` field for UI pointer capture. |
| **T17** | QuestCraft Controls Parity & PICO 4 Ultra 6DoF Immersion Delivery | High | **DONE** | Patched PICO OS manifest (`pvr.app.inputType="controller,hands"`, `handtrackingSupport="1"`), defaulted to 360 Immersive VR projection, calibrated 1:1 angular look sensitivity (191.0 px/rad), and implemented exact QuestCraft controls table. |
| **T18** | 32-bit GearVR libvrapi shim | Critical | **ABANDONED** | Confirmed obsolete and rejected by user; Snapdragon XR2 Gen 2 EL0 lacks 32-bit support. All effort on 64-bit modern Bedrock v1.26 (~1GB APK). |
| **T19** | 32-bit compilation scripts | Low | **ABANDONED** | Superceded by 64-bit modern pipeline. |
| **T20** | Repack GearVR APK | Medium | **ABANDONED** | Superceded by 64-bit modern pipeline. |
| **T21** | Forensic Root Cause Audit (Input & Immersion) | Critical | **DONE** | Disassembled `libminecraftpe.so` & `GameActivity`: discovered `nativeKeyHandler(keyCode, action, source)` mismatch in `JNI_Hooks.cpp` blocking all button forwarding, `HandDialogActivity` manifest intercept, double rotation in projection layer, and missing 3D arm shader in `BlitEye.cpp`. |
| **T22** | PICO OS Manifest Fix (`tools/pvr_manifest/src/main.rs`) | High | **DONE** | Removed `resizeableActivity="false"`, aligned with QuestCraft manifest (`pvr.app.type="vr"`, `pvr.display.orientation="180"`, `com.oculus.intent.category.VR="vr_only"`, `focusaware="true"`). |
| **T23** | Native Key & Joystick Input Repair (`JNI_Hooks.cpp` & `InputBridge.cpp`) | Critical | **DONE** | Hooked `setFoundXboxControllerNative` to force true (confirmed in logcat: `Minecraft: Found an Xbox Controller`), added RX/RY axes and generic motion event dispatch. |
| **T24** | 3D Tracked Minecraft Arms & Reticle Gating (`BlitEye.cpp`) | High | **DONE** | Implemented 3D shaded Steve voxel arms anchored to 6DoF controller poses with stereoscopic View-Projection matrices; hid laser pointer in immersive mode. |
| **T25** | 6DoF View Projection & Camera Calibration (`XrContext.cpp`) | High | **DONE** | Defaulted to 360 Immersive VR projection (`g_use_quad_layer{false}`), eliminated double-rotation conflicts, retained Y-button/dual-grip cinema toggle. |
| **T26** | Build, Repack 1GB Modern Bedrock APK, Sign, Deploy & Test on PICO 4 Ultra | Critical | **DONE** | Successfully built `libmcbedvr.so`, repacked genuine ~1GB 64-bit Bedrock v1.26.52.3 APK, signed (v3 scheme), installed to PICO 4 Ultra, verified 90 FPS OpenXR stereoscopic rendering in logcat. |
| **T27** | Head Tracking Camera Look via Gamepad Right Stick (`AXIS_Z`/`AXIS_RZ`) | Critical | **DONE** | Map HMD angular velocity (`headYawRate`, `headPitchRate`) directly into Bedrock's Gamepad Right Stick via `InputBridge::DispatchJoystick` to bypass Android pointer capture restrictions. |
| **T28** | Auto Menu Detection & Floating Quad Panel (`mCursorLocked`) | Critical | **DONE** | Hook `mCursorLocked` from `MainActivity`: dynamically switch between floating Quad panel at 1.8m gaze (menus, inventory, settings, start screen) with laser pointer, and 360° projection (gameplay). |
| **T29** | 3D Laser Pointer Ray-Plane Intersection on Dynamic Quad | High | **DONE** | Support rotated and translated Quad poses in `PicoInput::UpdateLaserAndTouch` so menus can open in any gaze direction. |
| **T30** | Wide VR FOV (110°) Injection (`gfx_field_of_view:110.0`) | High | **DONE** | Auto-patch Bedrock `options.txt` at startup to eliminate cramped/magnified first-person view. |
| **T31** | Suppress Vanilla Hand Model (`gfx_hidehand:1`) | High | **DONE** | Auto-patch Bedrock `options.txt` at startup so only 3D tracked Steve voxel arms render. |

---

## 4. Next Steps & Current State

1. **User Testing on Headset**: Conectar el PICO 4 Ultra e instalar el APK generado (`/Volumes/X10Pro/Downloads/minecraft_vr_v6_signed.apk`).
2. Probar los 5 puntos en el visor: giro de cabeza fluido en gameplay, menús flotantes a 1.8m con puntero láser (inicio e inventario), FOV ancho a 110°, y brazos 3D de Steve exclusivos.
