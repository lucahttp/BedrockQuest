# `architecture.locked.md` — Contrato arquitectónico de mcbedvr (Bedrock 1.26 → PICO 4 Ultra)

**Turno 12, 2026-10-10 — Gabriel Medina (Tech Intelligence)**
**Audiencia**: @Mario Santos, @Pablo Lamponne, @Emilio Ravenna
**Estado**: **CONTRATO CERRADO**. Decisiones ratificadas en turno 11 (Mario) sobre `xr_render_plan.md` v8. Este documento ata Fase 2 (`xr::XrContext` + `xr::SyncClock`) al bridge ya en disco de Fase 1 (`src/xr/SwappyBridge.{h,cpp}`).

> **Regla de versionado**: cualquier cambio en una decisión marcada **[LOCKED]** requiere PR con sign-off de Mario + Gabriel + Pablo. Cambios en **[OPEN]** son ajuste de fase 3/4 sin reabrir el contrato.

---

## 0. TL;DR

- **Frame loop**: tracer API de Swappy (`SwappyGL_injectTracer` AGDK 1.5+ / `swappyGLInjectTracer` AGDK 1.4) como vía primaria; `ChoreographerCallback_nOnChoreographer` vía `RegisterNatives` como complemento de sincronización; fallback F1 (Choreographer-only). And64 reservado para fase 4. **Cero W⊕X, cero SELinux drama, cero trampolines ARM64 en path gráfico.**
- **Lifecycle + input**: `RegisterNatives` A1 sobre 5 lifecycle (`nativeResize/Suspend/OnDestroy/StopThis/Shutdown`) + 6 input (`nativeBackPressed/ReturnKeyPressed/ShiftKeyPressed/ClearAButtonState/BackSpacePressed/KeyHandler`) + 1 Choreographer. Sin parches de memoria.
- **Swapchain**: 2 swapchains independientes (`arraySize=1`, `faceCount=1`) — un par por ojo, blit vía `glBlitFramebuffer`. Lado plano.
- **Ring buffer**: `kStereoSets = 4` (G1) con 1 frame de lag. 30 MB VRAM constante, cero tearing. Lado plano.
- **Pose injection**: C3 (TimeWarp-only) en primer encendido del device. C2/C1 auditables en fase 4. Lado plano.
- **Multiview**: D2 (off, MVP). Lado plano.
- **Fase 2 entregable**: este archivo define los contratos `xr::XrContext` y `xr::SyncClock` que reemplazan el `StepFrame()` stub de `Xr.cpp` Fase 0.

---

## 1. Decisiones ratificadas (LOCKED)

| # | decisión | fuente | estado |
|---|---|---|---|
| **A1** | Lifecycle + input + Choreographer vía `RegisterNatives` | turno 7 (biblia §1.a) + turno 11 ratificación Mario | **LOCKED** |
| **A2** | And64 inline-hook reservado para fase 4 (cámara) | turno 7 + turno 11 | **LOCKED** (reserva) |
| **B1** | Ring `kStereoSets = 4` con 1 frame de lag | turno 7 + turno 11 | **LOCKED** |
| **C3** | TimeWarp-only como primer encendido | turno 7 + turno 11 | **LOCKED** (MVP) |
| **C2/C1** | Parche shader/UBO de cámara en fase 4 | turno 7 | **OPEN** (post-MVP) |
| **D2** | Sin multiview, 2 swapchains secuenciales | turno 7 + turno 11 | **LOCKED** |
| **9.1** | Frame loop = 5.3 tracer + 5.4 Choreographer | turno 8 + turno 11 | **LOCKED** |
| **9.2** | Fallback F1 (Choreographer-only) si tracer NULL | turno 8 + turno 11 | **LOCKED** |
| **9.3** | Ring G1 fijo 4 slots | turno 8 + turno 11 | **LOCKED** |
| **9.4** | And64 fase 4 | turno 8 + turno 11 | **LOCKED** |
| **A2 toolchain** | Recompilación `libmcbedvr.so` en bloque, sesión NDK r27 de Mario | turno 10 | **OPEN** (bloqueante operacional, código en disco) |

---

## 2. Topografía de la solución (resumen ejecutivo)

### 2.1 Pipeline end-to-end (post-Fase 3)

```
[Java main thread]
Choreographer$FrameCallback.doFrame(frameTimeNanos)
  │  RegisterNatives (src/JNI_Hooks.cpp, Mcbedvr_nOnChoreographer)
  ▼
xr::SyncClock::OnChoreographerTick(frameTimeNanos)
  │   ├─ xrWaitFrame implícito (incrementa g_frame_id, cachea predicted_display_time)
  │   └─ sincroniza el reloj interno con la cadencia Java (~60 Hz)
  ▼
[GameThread nativo en libminecraftpe.so]
engine.tick()
RenderDragon::renderFrame()  ──► Swappy target EGL surface
  ▼
SwappyGL_swap(dpy, surf)
  │ preSwap callback (registrado por SwappyBridgeInit vía SwappyGL_injectTracer)
  ▼
xr::SyncClock::OnFrameBegin(fd)            ← ATRAPA el fence antes de eglSwapBuffers
  │
  ├─ xrBeginFrame (si no consumido ya para g_frame_id)
  ├─ xrLocateViews(PRIMARY_STEREO, predicted_display_time)
  ├─ g_current_pose = ... , g_current_fov = ...
  ├─ ring_slot = g_frame_id % kStereoSets
  ├─ PosePublisher::Publish(ring_slot, pose, fov)    ← fence GPU
  ├─ BlitEye::BlitL(ring_slot, swapchain_l_image)
  ├─ BlitEye::BlitR(ring_slot, swapchain_r_image)
  ├─ xrEndFrame(layer_pro_2_views)         ← composición
  │
  ▼ postSwap callback
xr::SyncClock::OnFrameEnd(timeout_ns)      ← libera ring_slot para próximo frame
  ▼
SwappyGL_swap prosigue → eglSwapBuffers → SurfaceFlinger → display
```

### 2.2 Componentes y archivos

| componente | archivo | fase | estado |
|---|---|---|---|
| `JNI_OnLoad` | `src/JNI_OnLoad.cpp` | 1 | ✅ en disco |
| `JNI_Hooks` (tabla `RegisterNatives`) | `src/JNI_Hooks.cpp` | 1 | ✅ en disco |
| `xr::SwappyBridge` (tracer) | `src/xr/SwappyBridge.{h,cpp}` | 1 | ✅ en disco (stubs) |
| `xr::XrContext` (OpenXR session+swapchain) | `src/xr/XrContext.{h,cpp}` | **2** | 📐 contrato aquí |
| `xr::SyncClock` (timing) | `src/xr/SyncClock.{h,cpp}` | **2** | 📐 contrato aquí |
| `xr::PosePublisher` (ring pose) | `src/xr/PosePublisher.{h,cpp}` | 3 | pendiente |
| `xr::BlitEye` (FBO→swapchain blit) | `src/xr/BlitEye.{h,cpp}` | 3 | pendiente |
| `CMakeLists.txt` | `CMakeLists.txt` | 1,2,3 | pendiente extender |
| `Xr.cpp` legacy (`Init/Shutdown/StepFrame`) | `src/Xr.cpp` | 0 | refactor → delega a `XrContext` |

---

## 3. Acoplamiento `xr::XrContext` ↔ `xr::SyncClock` ↔ `SwappyBridge`

La Fase 2 introduce dos módulos nuevos. Sus contratos deben calzar con los stubs que ya viven en `SwappyBridge.h` (Fase 1) y reemplazar el `StepFrame()` standalone de `Xr.cpp` (Fase 0). Diseño:

- **`xr::XrContext`**: estado pesado de OpenXR (instance, session, swapchains, spaces). Vive en `g_xrContext`, accesible desde el render thread (mismo que `preSwap`/`postSwap`) con mutex.
- **`xr::SyncClock`**: estado liviano de timing (`g_frame_id`, `g_predicted_display_time`, cadencia Choreographer). Vive en `g_clock`, accesible desde **dos** threads: Choreographer (main UI) y render thread (preSwap). Mutex liviano.
- **`SwappyBridge`**: ya en disco, **no se toca** en Fase 2. Sus callbacks son el entrypoint que dispara el frame OpenXR.

Reglas:
1. `SwappyBridge` **no conoce** OpenXR. Sólo expone `preSwap`/`postSwap` y deja que `SyncClock` los conecte.
2. `SyncClock` **no conoce** PICO. Sólo cuenta frames y coordina threads.
3. `XrContext` **no sabe** qué thread lo invoca. Recibe `frame_id` y `predicted_display_time` ya horneados por `SyncClock` y arma el frame OpenXR.

---

## 4. Contrato de `xr::XrContext` (Fase 2)

### 4.1 Archivo
`src/xr/XrContext.h` (declaración) + `src/xr/XrContext.cpp` (implementación). Reemplaza el `StepFrame()` standalone de `Xr.cpp`.

### 4.2 API pública (locked)

```cpp
// src/xr/XrContext.h
#pragma once
#include <cstdint>
#include <openxr/openxr.h>

namespace mcbedvr::xr {

// Estado runtime del contexto OpenXR (sustituye al enum State de Xr.h).
enum class XrState : std::uint8_t {
    Uninitialized = 0,
    InstanceCreated,        // xrCreateInstance OK, no session
    SessionLost,            // session perdida por xrPollEvent → ShuttingDown
    StereoRendering,        // swapchains listas + frame loop corriendo
    ShuttingDown,
    Failed,
};

const char* StateName(XrState s);

// Inicializa el contexto XR. Equivale a Xr.cpp::Init() pero devuelve un
// XrState en lugar de bool. Idempotente: re-llamar en StereoRendering es OK.
//
// Implementación:
//   - dlopen("libopenxr_loader.so", RTLD_NOW | RTLD_GLOBAL) con __attribute__((weak))
//     para XR_NO_PROTOTYPES (ver memory/mcbedvr-build-arm64.md)
//   - xrCreateInstance("mcbedvr", engineVersion=1)
//   - xrGetSystem(PICO 4 Ultra form factor) → ojo: VERIFIED en device, falla en host
//   - xrCreateSession(XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR)
//   - xrCreateReferenceSpace(LOCAL, identity) → g_space
//   - xrCreateSwapchain(2) → s.swapchains[0,1] (formato RGBA8 | SRGB8_ALPHA8)
//   - xrEnumerateSwapchainImages para ambos ojos
//   - Si todo OK → retorna StereoRendering. Si falla → Failed y loggea no
XrState XrContextInit();

// Apaga en orden inverso. Idempotente.
void XrContextShutdown();

// Acceso de sólo lectura al estado actual (sin mutex; XrState es std::atomic).
XrState XrContextState();

// ============================================================================
// API de frame (entrypoint único desde SyncClock). Reemplaza Xr.cpp::StepFrame.
// ============================================================================

struct FrameRequest {
    std::int64_t frame_id;                          // de xr::SyncClock::current_frame_id()
    XrTime       predicted_display_time;            // de xr::SyncClock::current_predicted_display_time()
    bool         want_headset_pose;                  // false → modo Theater (no pose HMD)
};

struct FrameResult {
    bool         presented;                          // true si xrEndFrame retornó XR_SUCCESS
    XrPosef      hmd_pose;                           // última pose usada (para diagnostics)
    float        tan_x;                              // tan(fov_x/2) con que se pintó el par
    float        tan_y;                              // tan(fov_y/2) con que se pintó el par
    std::int32_t swapchain_index_l;                  // slot consumido ojo izq.
    std::int32_t swapchain_index_r;                  // slot consumido ojo der.
};

// Consume un frame OpenXR. Llamado desde xr::SyncClock::OnFrameBegin() una vez
// por frame_id. Devuelve FrameResult con métricas para diagnóstico de Emilio
// (correlación pose-HMD vs pose-GameThread).
//
// Si el contexto no está en StereoRendering, retorna {presented=false} sin error.
// Si predicted_display_time está en el pasado (xrLocateViews returning invalid),
// retorna {presented=false, hmd_pose=identity} y loggea warning.
//
// Thread-safety: NO thread-safe. Asume que el caller (SyncClock) serializa.
FrameResult XrContextSubmitFrame(const FrameRequest& req);

// ============================================================================
// API de diagnóstico (para Emilio / logcat).
// ============================================================================

struct XrDiagnostics {
    XrState      state;
    XrSystemId   system_id;
    XrSession    session;                            // handle opaco, sólo para log
    XrSpace      space;
    float        recommended_fov_l[4];                 // angle{Left,Right,Up,Down} ojo izq.
    float        recommended_fov_r[4];
    std::int32_t swapchain_width_l, swapchain_height_l;
    std::int32_t swapchain_width_r, swapchain_height_r;
    std::int64_t frame_count;                        // current frame_id
    std::int64_t last_present_ns;                    // último xrEndFrame exitoso (CLOCK_MONOTONIC)
};

XrDiagnostics XrContextGetDiagnostics();
void          XrContextDumpDiagnostics();           // alias de Xr.cpp::DumpDiagnostics

// ============================================================================
// Getters para módulos Fase 3 (PosePublisher / BlitEye).
// ============================================================================

// Devuelve el handle de la swapchain del ojo e (0=izq, 1=der).
// Precondición: XrContextState() == StereoRendering.
XrSwapchain XrContextSwapchain(std::uint32_t eye);

// Adquiere la siguiente imagen disponible de la swapchain del ojo.
// Bloquea con xrWaitSwapchainImage hasta `timeout_ns`. Retorna el índice.
struct AcquireResult {
    bool          ok;
    std::int32_t  index;
    std::int64_t  acquire_ns;                       // diagnóstico Emilio
};
AcquireResult XrContextAcquireSwapchainImage(std::uint32_t eye, std::int64_t timeout_ns);

// Libera la imagen adquirida. Idempotente (re-release es no-op).
void XrContextReleaseSwapchainImage(std::uint32_t eye, std::int32_t index);

}  // namespace mcbedvr::xr
```

### 4.3 Garantías

1. **No thread-safe por sí mismo**. `SyncClock` debe serializar. Si en fase 4 hay otro thread que toque `XrContext`, se le pasa un `std::mutex` interno.
2. **Mutex interno sólo para `g_space` reuse**: `xrLocateViews` requiere el mismo `XrSpace` que `XrContextInit` creó; el lifetime se garantiza en `g_xrContext` con un `std::shared_mutex`.
3. **Sin global mutable**. Todo el estado vive en `g_xrContext` (anon namespace, `XrContext.cpp`). Sólo las funciones públicas arriba acceden.
4. **Recuperación de `SessionLost`**: si `xrPollEvent` retorna `XR_EVENT_TYPE_SESSION_STATE_LOST`, `XrContextState` pasa a `SessionLost`, `XrContextSubmitFrame` retorna `{presented=false}` sin reintentos. Requiere intervención de `SyncClock` para gatillar `XrContextShutdown` + `XrContextInit`.

### 4.4 Lo que **no** es Fase 2

- `xrGetVisibilityMaskKHR` (HAM): sí si PICO OpenXR Mobile 1.4.0 declara la extensión — verificar en device. Si no declara, skip.
- `XR_COMPOSITION_LAYER_QUAD` para theater: Fase 4.
- `xrPerfSettingsEXT`: Fase 4.

---

## 5. Contrato de `xr::SyncClock` (Fase 2)

### 5.1 Archivo
`src/xr/SyncClock.h` + `src/xr/SyncClock.cpp`. **No** reemplaza código existente — es un módulo nuevo.

### 5.2 Por qué existe
Bedrock corre dos cadencias distintas en threads distintos:
- **Choreographer main UI thread**: ~60 Hz, `ChoreographerCallback_nOnChoreographer` vía `RegisterNatives`.
- **Render GameThread**: 30-60 Hz real, llama `SwappyGL_swap` que invoca `preSwap`.

`SyncClock` fusiona ambas en un único `frame_id` monotónico + un `predicted_display_time` consistente, y serializa el acceso al `XrContext`.

### 5.3 API pública (locked)

```cpp
// src/xr/SyncClock.h
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>

namespace mcbedvr::xr {

enum class ClockSource : std::uint8_t {
    NotInitialized = 0,
    ChoreographerOnly,                              // F1 fallback (tracer NULL)
    ChoreographerPlusTracer,                        // primaria (Fase 1.1)
    TracerOnly,                                     // si Choreographer JNI falla
};

const char* ClockSourceName(ClockSource s);

// ============================================================================
// Lifecycle
// ============================================================================

// Inicializa el clock. Si SwappyBridgeStatus_() == TracerActive, queda en
// ChoreographerPlusTracer. Si no, cae a ChoreographerOnly (F1).
// Precondición: xr::SwappyBridgeInit() llamado antes.
//
// Una sola llamada. Re-llamar retorna false.
bool SyncClockInit();

// Apaga. Idempotente.
void SyncClockShutdown();

ClockSource SyncClockCurrentSource();

// ============================================================================
// Entry points (llamados desde los callbacks ya en disco)
// ============================================================================

// Llamado desde Mcbedvr_nOnChoreographer (RegisterNatives de Fase 1). Corre en
// main UI thread, ~60 Hz en PICO 4 Ultra. Es idempotente: si llega otro tick
// antes que el frame se haya presentado, simplemente actualiza
// g_last_choreo_frame_id y deja que el siguiente preSwap lo use.
void SyncClockOnChoreographerTick(std::int64_t frame_time_nanos);

// Llamado desde PreSwapCb (registrado en src/xr/SwappyBridge.cpp). Corre en
// GameThread. Hace el trabajo pesado: invoca XrContextSubmitFrame con los
// parámetros ya horneados por el último OnChoreographerTick.
//
// Si el source es ChoreographerOnly, este entry NO existe (tracer no registrado).
// En ese caso SyncClockOnChoreographerTick llama internamente al submit.
void SyncClockOnFrameBegin(int fence_fd);

// Llamado desde PostSwapCb. Libera el frame_id (g_frame_at_swap_done++).
void SyncClockOnFrameEnd(long timeout_ns);

// ============================================================================
// Getters (lock-free o mutex liviano)
// ============================================================================

std::int64_t SyncClockCurrentFrameId();             // g_frame_id
XrTime       SyncClockCurrentPredictedDisplayTime();// g_predicted_display_time (ns)
XrTime       SyncClockLastChoreoFrameTime();        // nanos desde boot del último tick Choreographer

// ============================================================================
// Diagnóstico (para Emilio / logcat / nc_device_correlation)
// ============================================================================

struct ClockStats {
    ClockSource source;
    std::int64_t frame_id;
    std::int64_t total_frames_submitted;            // contador histórico
    std::int64_t total_frames_presented;            // submitted que volvieron presented=true
    std::int64_t total_choreo_ticks;                // número de OnChoreographerTick
    std::int64_t last_choreo_dt_ns;                 // delta entre los últimos 2 ticks
    std::int64_t last_frame_submit_ns;              // CLOCK_MONOTONIC del último submit
    std::int64_t last_frame_present_ns;             // CLOCK_MONOTONIC del último present
    std::int64_t last_frame_latency_ns;             // present - submit
    bool         last_frame_was_repeated;            // true si se presentó sin OnChoreographerTick nuevo
};

ClockStats SyncClockGetStats();

}  // namespace mcbedvr::xr
```

### 5.4 Garantías

1. **Mutex interno `g_clock_mutex`**: protege las regiones críticas entre los dos threads (main UI vs GameThread). Locks **cortos**, no dentro de `glBlitFramebuffer`.
2. **`g_frame_id` es `std::atomic<int64_t>`**, monotonically increasing. Reset a 0 sólo en `SyncClockShutdown`.
3. **`g_predicted_display_time` se cachea en `OnChoreographerTick` y se consume en `OnFrameBegin`**. Si entre el tick y el preSwap pasaron más de 20 ms (jitter grande), `SyncClockOnFrameBegin` calcula un nuevo `predicted_display_time` con `xrWaitFrame` (sigue siendo legal: OpenXR permite refresh del predicted time en cualquier momento del frame).
4. **No bloquea en `glBlitFramebuffer` ni en `xrEndFrame`**. Esos viven en `XrContextSubmitFrame`, llamado **dentro** del lock de `g_clock_mutex`. El lock se mantiene durante la duración del frame OpenXR — coherencia con la biblia Dubrovskiy §2.4 (un solo thread hace el submit).

### 5.5 Interacción con `SwappyBridge` ya en disco

`SyncClockInit` **no toca** `SwappyBridge.h`. Pero cuando se enchufe la fase 3, los stubs de `SwappyBridge.cpp` (`PreSwapCb` y `PostSwapCb`) se reemplazarán por:

```cpp
// src/xr/SwappyBridge.cpp — stubs a reemplazar en Fase 3
void PreSwapCb(void* user, int fd) {
    (void)user;
    mcbedvr::xr::SyncClockOnFrameBegin(fd);
}

void PostSwapCb(void* user, long timeout_ns) {
    (void)user;
    mcbedvr::xr::SyncClockOnFrameEnd(timeout_ns);
}
```

**Fase 2 no modifica `SwappyBridge.cpp`**. La conexión se materializa en Fase 3.

---

## 6. Compatibilidad con el código en disco (Fase 1 ya entregada)

Verificación de que los contratos de arriba calzan con los archivos que Pablo ya dejó:

| contrato nuevo | match en disco | nota |
|---|---|---|
| `SyncClockOnFrameBegin(int)` | `SwappyTracerPreSwapCallback(void*, int)` ← `fd` es `int` | ✅ firma coincide |
| `SyncClockOnFrameEnd(long)` | `SwappyPostSwapCallback(void*, long)` ← `timeout_ns` es `long` | ✅ firma coincide |
| `SyncClockCurrentSource()` ∈ {ChoreographerOnly, ChoreographerPlusTracer, TracerOnly} | `SwappyBridgeStatus_()` ∈ {NotInitialized, TracerActive, FallbackChoreographerOnly, Failed} | mapeo 1-a-1: NotInitialized/Failed→NotInitialized, TracerActive→ChoreographerPlusTracer, FallbackChoreographerOnly→ChoreographerOnly |
| `SyncClockInit` se llama **después** de `SwappyBridgeInit` | orden en `JNI_OnLoad`: ResolveGameSymbols → RegisterBedrockNatives → SwappyBridgeInit → xr::Init → (aquí iría SyncClockInit) | ✅ Pablo dejó el orden canónico; sólo añadimos la línea |

**Firma del callback Choreographer instalada en `JNI_Hooks.cpp`**: Pablo dejó `nOnChoreographer(JF)V` (frameTimeNanos + dt). `SyncClockOnChoreographerTick` toma `frame_time_nanos` (jlong). El bridge entre ambos es trivial:

```cpp
// En src/JNI_Hooks.cpp
static void Mcbedvr_nOnChoreographer(JNIEnv* env, jclass cls, jlong frameTimeNanos, jfloat dt) {
    (void)env; (void)cls; (void)dt;
    mcbedvr::xr::SyncClockOnChoreographerTick(static_cast<std::int64_t>(frameTimeNanos));
}
```

---

## 7. Plan de implementación Fase 2 (3 subfases)

### Fase 2.A — `xr::XrContext` skeleton
- `src/xr/XrContext.{h,cpp}` con la API de §4 (init/shutdown/state/diagnostics).
- `Xr.cpp::Init/Shutdown` se refactoriza: pasa a ser wrapper delgado que delega en `XrContextInit/Shutdown`.
- `Xr.cpp::StepFrame` queda deprecated (sigue compilando pero ya nadie lo llama).
- **Compila headless** en host (devuelve Failed, no rompe el toolchain). En device retorna StereoRendering.
- **Smoke test**: `adb logcat -s mcbedvr | grep "xr: state=StereoRendering"`.

### Fase 2.B — `xr::SyncClock` skeleton
- `src/xr/SyncClock.{h,cpp}` con la API de §5.
- `JNI_OnLoad` invoca `SyncClockInit` después de `SwappyBridgeInit` y `xr::Init`.
- **Smoke test**: `adb logcat -s mcbedvr | grep "clock: source="` debe decir `ChoreographerPlusTracer` si tracer OK, `ChoreographerOnly` si F1.

### Fase 2.C — Conectar Choreographer JNI → `SyncClock`
- Modificar `src/JNI_Hooks.cpp::Mcbedvr_nOnChoreographer` para invocar `SyncClockOnChoreographerTick`.
- `SwappyBridge.cpp::PreSwapCb/PostSwapCb` siguen stubs en Fase 2 (no se cambian hasta Fase 3).
- **Smoke test**: `adb logcat -s mcbedvr | grep "clock: choreo tick"` debe aparecer ~60 veces por segundo en device.

### Gate para Fase 3
- `mcbedvr` arranca, no crashea, `xr: state=StereoRendering`, `clock: source=ChoreographerPlusTracer`, `choreo tick` se loggea con cadencia ~60 Hz. Si todo eso pasa, Fase 3 enchufa `PosePublisher` + `BlitEye` y el frame aparece en el HMD.

---

## 8. Deuda explícita (todo lo que sigue en Fase 3/4)

- **Fase 3** (`PosePublisher`, `BlitEye`): enchufa los stubs de `PreSwapCb`/`PostSwapCb` para hacer el blit real y publicar pose.
- **Fase 4** (cámara): C1/C2 progresivo cuando device muestre cuánto molesta la divergencia TimeWarp-only.
- **Fase 4** (multiview `XR_FB_multiview`): D1 si hay holgura de perf post-MVP.
- **Fase 4** (theater layer): menús de Minecraft como `XR_COMPOSITION_LAYER_QUAD`.
- **HAM**: `xrGetVisibilityMaskKHR` si PICO OpenXR Mobile 1.4.0 declara la extensión (verificar en device).
- **Haptics PICO 4 Ultra**: Pablo lo trabaja por su cuenta.
- **Bloqueante operacional**: `libmcbedvr.so` recompilación (turno 10). Cuando Mario rearme el `.a`, esta Fase 2 se compila sin drama.

---

## 9. Riesgos y mitigaciones

| riesgo | probabilidad | impacto | mitigación |
|---|---|---|---|
| `swappyGLInjectTracer` devuelve NULL en device | baja (Pablo confirmó símbolos Swappy en APK) | medio (cae a F1, performance subóptimo) | F1 ya implementado en `SwappyBridge`; el clock pasa a `ChoreographerOnly` sin error |
| `ChoreographerCallback_nOnChoreographer` no es invocable por Re-register (versión Mojang ofusca la firma) | baja | medio | Emilio audita firmas reales del DEX en fase 2.C; si divergen, se ajusta tabla `RegisterNatives` |
| `XrContextInit` falla en host (no hay PICO runtime) | **alta** | bajo (sólo Fase 2.A smoke test) | `XrContextInit` debe retornar `Failed` sin crash, log clean |
| Drift de `frame_id` entre Choreographer y tracer | media | bajo (falsa repetición de frame, visible en stats) | `ClockStats.last_frame_was_repeated` flag → Emilio correlaciona |
| Memoria: 4 × (2 swapchains L+R × viewW × viewH × 4 bytes) ≈ 30 MB VRAM constante | baja | bajo (dentro del budget PICO 4 Ultra) | G1 confirmado en turno 11 |

---

## 10. Referencias

- **Biblia**: [`dubrovskiy-yevhen-stakelogic/gta-sa-vr-quest`](https://github.com/dubrovskiy-yevhen-stakelogic/gta-sa-vr-quest). Topografía en `xr_render_plan.md` §2 (turno 8).
- **Plan previo**: [`vendor/mcbedvr/xr_render_plan.md`](file:///Volumes/X10Pro/Projects/mcbedvr/vendor/mcbedvr/xr_render_plan.md) — la base ratificada en turn 11.
- **Fase 1 entregada**: `src/JNI_OnLoad.cpp`, `src/JNI_Hooks.cpp`, `src/xr/SwappyBridge.{h,cpp}`, `CMakeLists.txt` (turno 10).
- **Símbolos Bedrock 1.26**: `memory/apk-bedrock-126-jni-symbols.md` (turno 9).
- **APK Gear VR 1.14 forensia**: `memory/apk-gear-vr-analysis.md` — para entender qué cambió entre 1.14 y 1.26.
- **Toolchain build**: `memory/mcbedvr-build-arm64.md` — patrón `XR_NO_PROTOTYPES + __attribute__((weak)) + RTLD_GLOBAL` para XrContextInit.
- **Biblia bindings PICO**: `memory/pico4s-controller-binding.md` — para fase 4 input.
- **Google AGDK Swappy tracer**: [developer.android.com/games/sdk/frame-pacing/opengl/add-functions](https://developer.android.com/games/sdk/frame-pacing/opengl/add-functions).
- **OpenXR 1.1 spec**: [Khronos registry](https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html) §6.4 (Wait-Begin-End), §9.2 (composition layers).

---

**Cierre operativo:**
- **Contrato firmado**. `xr::XrContext` y `xr::SyncClock` quedan con la API de §4 y §5.
- **Compatibilidad verificada** contra `SwappyBridge.h` ya en disco (turno 10).
- **Plan de implementación** en 3 subfases (2.A skeleton context, 2.B skeleton clock, 2.C conectar Choreographer).
- **Sin bloqueantes nuevos**. El código de Fase 2 es fuente puro — no requiere NDK r27 hasta el rebuild de `libmcbedvr.so` que ya estaba pendiente.
- **Próximo paso**: Pablo recibe el contrato y arranca Fase 2.A en este mismo turno (si Mario autoriza) o cuando le toque. Emilio prepara el cableado de `nc_device_correlation` contra `ClockStats` y `XrDiagnostics`.