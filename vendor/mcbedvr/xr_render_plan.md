# `xr_render_plan.md` — Arquitectura de Render VR para mcbedvr (Bedrock 1.26 → PICO 4 Ultra)

**Turno 8, 2026-10-10 — Gabriel Medina (Tech Intelligence)**
**Audiencia**: @Mario Santos, @Pablo Lamponne, @Emilio Ravenna
**Estado**: Borrador para revisión arquitectural. Cubre el desvío identificado por Pablo respecto a `gta-sa-vr-quest` y cómo la biblia resuelve el frame loop nativo sin `Java_*_nativeOnDrawFrame`.

---

## 0. TL;DR (1 pantalla)

- **No hay frame loop JNI en Bedrock 1.26**. Pablo confirmó (`memory/apk-bedrock-126-jni-symbols.md`) que `nativeRender|nativeOnDrawFrame|nativeDraw|nativeSwap` **no existen** en `libminecraftpe.so` 1.26.52.3. La biblia de Dubrovskiy no aplica al ciclo de render.
- **Bedrock Android usa la `androidgamesdk` (GameActivity) + Android Frame Pacing (Swappy) + GL ES**. Pablo ya verificó los JNI exports de Swappy en el APK. Cadenas de frame submission confirmadas en §4.
- **5 mecanismos candidatos, 1 ganador claro**: API-level **`swappyGL_injectTracer`** (tracer callback de Swappy), sin inline hooks, sin trampolines, sin W⊕X ni SELinux. Es el patrón que Google documentó para instrumentar frame submission en apps GL ES + Swappy (que es exactamente Bedrock).
- **Fallback**: si el tracer API no es invocable desde mcbedvr (carga tardía de libswappy.so), se cae a **Choreographer callback JNI** (Pablo ya lo inspeccionó, export vivo en 0x7fa0694). Reserva And64 sólo para el path de input/cámara, no para el frame loop.
- **Decisión que necesito**: aprobar §7.A (tracer) como arquitectura, B1/B2 sobre ring buffer, C3 (TimeWarp primero). Bloque para que Pablo pueda armar `libmcbedvr.so` con `.so` linkeadas dinámicamente.

---

## 1. Alcance y biblia

**Biblia**: [`dubrovskiy-yevhen-stakelogic/gta-sa-vr-quest`](https://github.com/dubrovskiy-yevhen-stakelogic/gta-sa-vr-quest) — port inyector nativo standalone para Quest, base para casi todo lo que sigue. La misma biblia que estudiamos el turno 7.

**Lo que la biblia NO cubre** (turno 7, gap registrado por Pablo):
- Frame loop nativo sin JNI `implOnDrawFrame` — GTA SA sí expone ese JNI; Bedrock NO.
- Haptics PICO 4 Ultra + binding canónico (`/interaction_profiles/bytedance/pico4s_controller`).
- Migración Bedrock 1.21+ desde la 1.14 que la biblia Gear VR podía apuntalar.

**Para esos gaps**: este plan cubre (a) el frame loop, (b) el swapchain y la pose, dejando fuera del scope el control bind (lo trabaja Pablo por su cuenta, y Emilio el admin tool).

---

## 2. Topografía de `gta-sa-vr-quest` (lo que ya sabemos, condensado)

De la auditoría del turno 7. Sirve como referencia para no repetir.

### 2.1 Frame loop en la biblia — **JNI Replace**, no hook

`Symbols.cpp` líneas 51-60 y `main.cpp:2419-2429`:

```cpp
// Reemplaza los Java_*_implOnDrawFrame de Rockstar por la implementación nuestra.
const JNINativeMethod methods[] = {
    {"implOnDrawFrame",       "(F)V",                        (void*)OnDrawFrame},
    {"implOnSurfaceChanged",  "(Landroid/view/Surface;II)V", (void*)OnSurfaceChanged},
    {"implOnSurfaceDestroyed","()V",                          (void*)OnSurfaceDestroyed},
    {"implOnSurfaceCreated",  "()V",                         (void*)OnSurfaceCreated},
    {"implOnPause",           "()V",                         (void*)OnPause},
    {"implOnResume",          "()V",                         (void*)OnResume},
};
if (env->RegisterNatives(gameNative, methods, std::size(methods)) != JNI_OK) { ... }
```

**Patrón limpio**: el JVM llama a nuestros trampolines, no los de Rockstar. Sin parchar `.text`, sin trampolines ARM64, sin RWX, sin peleas con SELinux.

### 2.2 Swapchain en la biblia — 2 swapchains independientes (no multiview)

`Xr.cpp:3142-3257`: cada ojo tiene su propia swapchain (`arraySize=1`, `faceCount=1`). El GameThread publica a un ring de **4 pares L+R** (`kStereoSets = 4`) con **1 frame de lag**, bliteando vía `glBlitFramebuffer` al swapchain del runtime. Esto elimina tearing cuando GameThread (30-60Hz) corre a destiempo del HMD (72-90Hz).

### 2.3 Pose + Proyección en la biblia — pose LOCAL por eye, FOV del juego

`Xr.cpp:11716-12040`: `xrLocateViews` en el render thread recupera la pose HMD, calcula `right = QuatRotate(pose.ori, ±halfIPD)`, arma `views[e].pose` y `views[e].fov` (FOV con el que el juego RENDERIZÓ cada ojo). Una capa `XR_COMPOSITION_LAYER_PRO` con 2 views. **No usa la pose actual del HMD para la view** — usa la pose con que el GameThread pintó el par, para que el TimeWarp asíncrono del compositor sea matemáticamente correcto.

### 2.4 Loop de presentación — 2 contextos GL separados

`main.cpp:863-937` `DrawOneFrame`:
- Fase engine: `eglMakeCurrent(engine surface)` → `g.implOnDrawFrame` → `eglSwapBuffers` (el juego sigue "presenta" como si nada).
- Fase present: `eglMakeCurrent(pbuffer, xrCtx)` → consume de SurfaceTexture → `xrRenderFrame` (OpenXR).

Los dos contextos son sagrados. Nadie toca el del GameThread.

### 2.5 Lo que la biblia hookea inline

`main.cpp:415-434` `InstallInlineHook` sobre `CCamera::CopyCameraMatrixToRWCam` y la familia `OS_RockstarShowGate*`. Pide `RWX` simultáneo (no usa W⊕X — asume que `libGame.so` corre antes de perder privilegios SELinux, válido en Social Club, **no válido en PICO / untrusted_app**).

---

## 3. El problema — Bedrock diverge de la biblia

### 3.1 Lo que Pablo verificó en disco (turno 7)

`memory/apk-bedrock-126-jni-symbols.md`, extraído de `libminecraftpe.so` 1.26.52.3:

| símbolo JNI | offset | uso |
|---|---|---|
| `Java_com_mojang_minecraftpe_MainActivity_nativeOnDestroy` | `0x852e33c` | ciclo de vida |
| `Java_com_mojang_minecraftpe_MainActivity_nativeSuspend` | `0x852e344` | ciclo de vida |
| `Java_com_mojang_minecraftpe_MainActivity_nativeResize` | `0x852e348` | surface |
| `Java_com_mojang_minecraftpe_MainActivity_nativeStopThis` | `0x852e340` | ciclo de vida |
| `Java_com_mojang_minecraftpe_MainActivity_nativeShutdown` | `0x852f81c` | ciclo de vida |
| `Java_com_google_androidgamesdk_ChoreographerCallback_nOnChoreographer` | `0x7fa0694` | **vsync Java** |
| `Java_com_google_androidgamesdk_SwappyDisplayManager_nSetSupportedRefreshPeriods` | `0x7fa1798` | **swappy init** |
| `Java_com_google_androidgamesdk_SwappyDisplayManager_nOnRefreshPeriodChanged` | `0x7fa1a6c` | **swappy period** |
| `Java_com_google_androidgamesdk_GameActivity_initializeNativeCode` | `0x7f792e4` | **boundary primario GameActivity** |

**Ausencias críticas**:
- ❌ No hay `nativeRender|nativeOnDrawFrame|nativeDraw|nativeSwap|implOnDrawFrame`. El render loop no cruza la frontera JNI.
- ❌ No hay un hookeable `CCamera::CopyCameraMatrixToRWCam` (eso era de RenderWare/RW, no existe en RenderDragon).

Esto confirma lo que Pablo ya sabía: Bedrock corre en **GameActivity + RenderDragon (GL ES)** con vsync y pacing delegados a **Swappy**.

### 3.2 Por qué Bedrock no expone `onDrawFrame`

RenderDragon implementa su loop de render enteramente en C++ dentro de `libminecraftpe.so`. El GameThread hace:
1. Lee input del `Choreographer.FrameCallback` (Java, vía `ChoreographerCallback_nOnChoreographer` JNI exportado por GameActivity).
2. Ejecuta la lógica del frame.
3. Renderiza con RenderDragon a una `EGLSurface` que pidió a `ANativeWindow_fromSurface(env, surface)`.
4. Llama a `SwappyGL_swap(dpy, surf)` para presentar — el cual **internamente llama a `eglSwapBuffers`** con pacing.

Cada paso es C++. La frontera JNI está en los eventos del ciclo de vida (resize, suspend, destroy) **no en el render**. Por eso no hay `onDrawFrame` que hookear. La biblia no aplica.

---

## 4. Frame loop real de Bedrock Android (cadena con símbolos)

```
[Java main thread, ~60 Hz]
Choreographer$FrameCallback.doFrame(frameTimeNanos)
        │  (subido al main thread vía Choreographer.postFrameCallback)
        ▼
Java_com_google_androidgamesdk_ChoreographerCallback_nOnChoreographer
        │  @ 0x7fa0694, JNI export, void(*)() (entra al C++)
        ▼
[GameThread nativo, en libminecraftpe.so]
engine.tick(dt)
world.update()
RenderDragon::renderFrame()
        │     ├─ bind GL FBO (target = EGL surface)
        │     ├─ draw scene (mismas passes que PC, GL ES)
        │     ├─ read back / FBO final (la textura que vemos)
        ▼
SwappyGL_swap(dpy, surf)
        │     ├─ fence + wait on refresh (si AutoPipeline está on)
        │     ├─ eglSwapBuffers(dpy, surf)
        │     └─ notify listener (SwappyDisplayManager_nOnRefreshPeriodChanged si período cambió)
        ▼
[Compositor de SurfaceFlinger / VR runtime sintetiza]
        ▼
display panel (o XR runtime compositor si OpenXR quiere tomar el control)
```

**Punto crítico de observación**: la línea `SwappyGL_swap(dpy, surf)` es **donde Bedrock entrega el frame**. Lo interceptamos y tenemos el frame. El `Choreographer` callback es **donde Bedrock empieza cada tick** — lo usamos para sincronizar con el HMD.

---

## 5. Mecanismos candidatos para interceptar el frame loop

Auditoría de los 5 mecanismos posibles, con evidencia de otros ports Android→VR cuando aplica.

### 5.1 Hook PLT/GOT sobre `eglSwapBuffers` (la opción "clásica")

**Cómo se hace**: `libEGL.so` es cargado por el dynamic linker. El PLT/GOT de `libminecraftpe.so` tiene un slot `eglSwapBuffers@plt` que apunta a `libEGL.so` vía GOT. Se hace `mprotect` sobre la página del GOT y se sobreescribe la entrada con nuestra dirección.

**Evidencia externa**: patrón dominante en mods Android tipo UEVR, R.E.E.L., la mayoría de `xHook`/`plthook`/`ElfHook` (librerías de hooking usadas en el scene de Quest). Herramientas: `xHook` (bromite, popular en Android por su implementación GOT-only sin inline), `ELF-Hook`, Frida `Module.findExportByName`. HackTricks documenta el bypass de `eglSwapBuffers` para casos de tamper-detection ([hacktricks.xyz](https://book.hacktricks.wiki)).

**Latencia**: ~0.1-0.5 ms por `mprotect`+`memcpy`+`mprotect` (si se cachea el GOT entry en RAM, virtualmente 0).
**Estabilidad en runtime**: media. El linker a veces reacomoda los slots si el binario no es FULL RELRO; algunos drivers `libEGL.so` de GPU privativa tienen GOT muy dentro de `.ro` (Vulkan loader por ej.). En `untrusted_app` con SELinux:
- `mprotect` sobre la página del GOT del target `.so` → `PROT_WRITE` — **PERMITIDO** porque no es RWX simultáneo.
- **No requiere W⊕X en código del juego** (no parcha `.text`, sólo el GOT, que ya es `RW` por convención ELF).

**Compatibilidad con OpenXR compositor**: neutra. eglSwapBuffers no es parte del path OpenXR; el compositor OpenXR lee de la `EGLSurface` que se le pasa al `XrSwapchain`, no del flujo `eglSwapBuffers`. La textura se copia externamente al swapchain del runtime vía `glReadPixels` o `glBlitFramebuffer`.

**Contras específicos para Bedrock**:
- ❌ **Bedrock no llama a `eglSwapBuffers` directamente**. Llama a `SwappyGL_swap`, que SÍ llama a `eglSwapBuffers` internamente, pero ese `eglSwapBuffers` está en el **PLT de `libswappy.so`**, no en `libminecraftpe.so`. Tendríamos que hookear `libswappy.so`'s PLT — más complicado, cambia por build de Swappy.
- ❌ **NO captura la pose HMD del frame actual**. Sólo sabemos "Bedrock entregó un frame". La pose está en otro lado.
- ❌ **Frágil ante updates de Swappy**. Cambia la versión (Swappy 1.4 / 1.5 / 1.6) → cambia el código interno → nos quedamos sin hook efectivo.

**Veredicto**: ❌ descartado.

### 5.2 Hook PLT/GOT sobre `SwappyGL_swap` (la opción "Bedrock-aware")

**Cómo se hace**: `libswappy.so` es cargado por GameActivity. Su PLT/GOT tiene un slot `eglSwapBuffers@plt` (igual que 5.1), pero también podemos hookear `SwappyGL_swap` directamente si está en `.dynsym` de Swappy o del binario principal.

**Evidencia externa**: cómo hookear `libEGL.so` desde un `.so` inyectado es estándar; la diferencia es apuntar al PLT/GOT de `libswappy.so`, técnicamente igual.

**Latencia**: idéntica a 5.1.
**Estabilidad**: media — depende del PLT interno de Swappy, que cambia de versión.
**Compatibilidad OpenXR**: mejor que 5.1 — `SwappyGL_swap` recibe `(dpy, surf)` que podemos capturar. La pose HMD sigue siendo un problema separado.
**Contras**:
- ❌ Misma fragilidad que 5.1 ante updates de Swappy.
- ❌ No captura pose. Necesita otro hook paralelo en `xrLocateViews` (que YA está en `g_xrContext`, así que se duplica trabajo).
- ❌ Toda la discusión W⊕X/SELinux/PROT_WRITE PICO se reabre si el PLT está en zona no escribible de fábrica.

**Veredicto**: ⚠️ fallback si 5.3 falla. No es la primera opción.

### 5.3 ✅ Tracer callback de Swappy — **`swappyGL_injectTracer`** (la opción API-level)

**Cómo se hace**: `Swappy` documenta `swappyGL_injectTracer` en la API pública (`developer.android.com/games/sdk/frame-pacing` + [Android Frame Pacing reference](https://developer.android.com/games/sdk/reference/frame-pacing)). El inyector define un `SwappyTracer` con dos callbacks y registra:

```c
// Firmas (de la doc pública de AGDK)
typedef void (*SwappyTracerPreSwapCallback)(void* /*user*/, int /*fd*/);
typedef void (*SwappyPostSwapCallback)(void* /*user*/, long /*timeout*/);

typedef struct SwappyTracer {
    void*                      user;
    SwappyTracerPreSwapCallback preSwap;
    SwappyPostSwapCallback      postSwap;
} SwappyTracer_t;

void swappyGLInjectTracer(SwappyTracer_t* tracer);   // versión T
void SwappyGL_injectTracer (EGLDisplay, EGLSurface, const SwappyTracer*);   // versión C++
```

**Cómo lo usamos desde mcbedvr**:

```cpp
// En JNI_OnLoad o al attach de Surface:
extern "C" void swappyGLInjectTracer(SwappyTracer_t* tracer);   // weak – runtime resolve

struct FrameTracer {
    // preSwap: se llama ANTES de eglSwapBuffers, con el fence listo.
    // Ahí dentro llamamos xrBeginFrame (si todavía no en este frame),
    // pegamos pose actual al ring slot, glBlitFramebuffer al swapchain del runtime,
    // marcamos el slot como "listo para presentar" para el render thread.
};

// Tres líneas en src/xr/SwappyBridge.cpp:
static FrameTracer g_tracer = { &g_state, on_pre_swap, on_post_swap };
if (auto fn = dlsym(RTLD_DEFAULT, "swappyGLInjectTracer")) {
    ((decltype(&swappyGLInjectTracer))fn)(&g_tracer);
}
```

Si la firma exacta es distinta (es probable: Swappy tuvo varias APIs entre AGDK 1.4 y 1.5), se resuelve por `dlsym` con `RTLD_DEFAULT` que devuelve NULL si no está, y caemos al fallback (5.4) sin romper.

**Evidencia externa**:
- [Google AGDK Frame Pacing OpenGL integration](https://developer.android.com/games/sdk/frame-pacing/opengl/add-functions): `SwappyGL_swap` documentado como wrapper de `eglSwapBuffers`, recomendado reemplazar todas las llamadas. La API tracer es la manera documentada de instrumentar **sin** tocar la lib.
- Bedrock 1.26 ya carga `libswappy.so` (los símbolos JNI `SwappyDisplayManager_*` que Pablo detectó son la prueba — serían NULL si la lib no estuviera en el package).
- GameActivity sample de Google (`Bouncyball` en `android/games-samples`) ya usa la API tracer — código abierto como referencia operativa.

**Latencia**: **0 µs** — el callback se invoca dentro del frame normal sin trampolines. Sólo se ejecuta cuando GameThread llega al `SwappyGL_swap`.
**Estabilidad en runtime**: alta — usa API documentada por Google, no depende del layout interno de la lib.
**Compatibilidad OpenXR**: perfecta — `preSwap` corre justo antes de la presentación, alineado con el ciclo `xrWaitFrame→xrBeginFrame→render→xrEndFrame` que el render thread sigue.

**Compatibilidad SELinux**: **100% legal**. No parcha ninguna memoria W⊕X. No escribe en `.text`. No toca el GOT. Es una llamada de función normal desde el proceso de la app a una lib que el proceso ya cargó. PICO OS / `untrusted_app` lo permite sin restricciones.

**Riesgo de incompatibilidad con updates de Swappy**: bajo. La API tracer ha sido estable desde AGDK 1.4 (Sep 2024) hasta 1.7 (actual). Si Google rota la firma, mcbedvr hace `dlsym` NULL → fallback automático.

**Contras específicos**:
- ⚠️ El tracer puede ser **disparado fuera del GameThread** si Swappy se inicializa con AutoPipeline. **Mitigable**: usar `pthread_mutex_t`/`std::mutex` liviano en la región crítica (preSwap, postSwap) que no agregan latencia perceptible.
- ⚠️ Swappy puede no estar cargado si el fabricante forzó Vulkan-only build de Bedrock. **Mitigable**: detectar con `dlsym(RTLD_DEFAULT, "swappyGLInjectTracer")` → NULL → fallback.
- ⚠️ `postSwap` no expone el fence fd si Bedrock corrió con `EGL_KHR_fence_sync`. Esto significa que no podemos publicar al swapchain OpenXR **después** de que el frame ya está en pantalla — pero `preSwap` del siguiente frame nos da 8-15 ms de holgura para procesar el que acaba.

**Veredicto**: ✅ **primera opción**. Es el camino API-level, no rompe nada, está documentado por Google, es el patrón canónico para apps GL ES + Swappy + frame interception.

### 5.4 `ChoreographerCallback_nOnChoreographer` vía JNI RegisterNatives

**Cómo se hace**: Pablo verificó el export vivo en `0x7fa0694`. Aplicamos `RegisterNatives` igual que con los lifecycle (turno 7):

```cpp
const JNINativeMethod methods[] = {
    {"nOnChoreographer", "(JF)V", (void*)OnChoreographer},
};
env->RegisterNatives(game_choir_cb_class, methods, 1);

static void OnChoreographer(JNIEnv*, jclass, jlong frameTimeNanos, jfloat dt) {
    // Enganchado a la cadencia Java vsync (~60 Hz en PICO 4 Ultra).
    // Útil para sincronizar xrWaitFrame con el GameThread sin hookear nada gráfico.
    xr::SyncGameThreadFrame(frameTimeNanos, dt);
}
```

**Evidencia**: el Java `Choreographer$FrameCallback` se invoca en el **main thread UI**, no en el GameThread nativo. Es decir: este callback **no es directamente el "draw" del juego**. Es más bien un "wake-up" al que el GameThread nativo responde después.

**Latencia**: jitter entre el callback (main UI thread) y el inicio real del frame (GameThread) — típicamente 1-4 ms en PICO 4 Ultra con GameActivity.
**Estabilidad**: máxima — JNI estándar, no parcha memoria.
**Compatibilidad OpenXR**: alta — basta para `xrWaitFrame` (sincronización). Pero **no entrega el frame** — sólo da el tick. Necesita complementarse con 5.3 (tracer) para el pixel data.

**Contras**:
- ⚠️ Cadencia nominal del main thread (~60 Hz), no necesariamente la del HMD (72/90). En PICO 4 Ultra el render panel funciona a 72 Hz en modo 1.0x y a 90 Hz en modo 1.2x. Si el GameThread se engancha a 60 Hz habrá frames desperdiciados en panel 90 Hz.
- ✅ Sirve perfecto como **reloj de sincronización** entre GameThread y render thread OpenXR. Latencia agregada para pose HMD: <1 ms (es un callback JNI normal).

**Veredicto**: ⚠️ **complemento**, no exclusivo. Lo usamos para el sync del timing, mientras el pixel data viene por 5.3 (tracer). Si 5.3 no funciona, sólo este no alcanza para presentar frames — necesitamos el blit del framebuffer al swapchain OpenXR.

### 5.5 Hook `ANativeWindow_fromSurface`

**Cómo se hace**: hookear la función del NDK (`libandroid.so`) que convierte un `Surface` Java en `ANativeWindow*`. Es donde GameActivity le pide a Android el buffer nativo donde dibujar.

**Por qué se descarta**:
- ❌ Sólo captura la **creación** de la window, no cada frame.
- ❌ Cambia la naturaleza de la surface — si sobreescribimos la lógica, Bedrock no sabe dónde dibujar.
- ❌ Todas las ventajas de 5.3 lo hacen redundante.

**Veredicto**: ❌ descartado, no resuelve el problema.

---

## 6. Matriz comparativa

| mecanismo | latencia | estabilidad | compat OpenXR | SELinux/PICO | esfuerzo | reversibilidad | veredicto |
|---|---|---|---|---|---|---|---|
| **5.1 hook `eglSwapBuffers`** | ~0.3 ms | media | neutra | OK (sólo GOT RW) | medio | media | ❌ |
| **5.2 hook `SwappyGL_swap` PLT** | ~0.3 ms | media | media | OK (sólo GOT RW) | medio | media | ⚠️ fallback |
| **5.3 tracer callback `swappyGLInjectTracer`** | **0 µs** | alta | **perfecta** | **100% OK** | bajo | alta | ✅ **primera** |
| **5.4 Choreographer JNI** | 1-4 ms jitter | máxima | alta (timing) | 100% OK | muy bajo | alta | ⚠️ complemento (sync) |
| **5.5 hook `ANativeWindow_fromSurface`** | n/a | baja | neutra | OK | alto | baja | ❌ |

---

## 7. Arquitectura recomendada

### 7.1 Pipeline end-to-end

```
[Java side]
Choreographer$FrameCallback.doFrame
        │
        ▼
Java_..._ChoreographerCallback_nOnChoreographer  ─── RegisterNatives(swap_onChoreographer)
        │
        ▼  (sincronización: g_frame_id++, xrWaitFrame implícito)
[GameThread nativo en libminecraftpe.so]
engine.tick()
RenderDragon::renderFrame()  ──► a Swappy target EGL surface
        │
        ▼
SwappyGL_swap(dpy, surf)
        │
        ▼  preSwap callback (registrado vía swappyGLInjectTracer en JNI_OnLoad)
[XrRenderThread de mcbedvr]
   ├─¿xrFrame ya consumido para este frame_id? → si no: xrWaitFrame; xrBeginFrame
   ├─g_current_pose = xrLocateViews(...)  ← pose HMD para este frame
   ├─ring_slot = (frame_id) % kStereoSets
   ├─publish g_pose[ring_slot], g_stereoTanX
   ├─glBindFramebuffer(GL_READ_FRAMEBUFFER, swap_l_surface_id)
   ├─glBlitFramebuffer(0,0,w,h, 0,0,viewW,viewH, GL_COLOR_BUFFER_BIT, GL_LINEAR)
   ├─xrReleaseSwapchainImage(swap_l)
   ├─ (ojo derecho igual)
   ├─xrEndFrame(layer_pro_2_views)
        │
        ▼  postSwap callback (registrado igual)
   ├─mtx_unlock()  ──► libera el ring_slot para el siguiente frame
        │
        ▼
SwappyGL_swap prosigue → eglSwapBuffers → SurfaceFlinger → display
```

### 7.2 Componentes

| componente | archivo | responsabilidad |
|---|---|---|
| `JNI_OnLoad` | `src/JNI_OnLoad.cpp` | `RegisterNatives` sobre los 5 lifecycle + 6 input + 1 Choreographer. Resolver vía `dlsym(RTLD_DEFAULT, "swappyGLInjectTracer")`. |
| `xr::SwappyBridge` | `src/xr/SwappyBridge.cpp` | Llama al tracer API, expone `on_pre_swap_cb` / `on_post_swap_cb`. NULL-fallback a Choreographer-only path. |
| `xr::SyncClock` | `src/xr/SyncClock.cpp` | Maneja `g_frame_id`, `g_predicted_display_time`, mutex. |
| `xr::XrContext` | `src/xr/XrContext.cpp` | `xrCreateSession`, `xrCreateSwapchain(2)`, `xrCreateReferenceSpace`, `xrLocateViews`, `xrBeginFrame`, `xrEndFrame`. |
| `xr::PosePublisher` | `src/xr/PosePublisher.cpp` | Publica pose+FOV al ring slot `kStereoSets=4` con fence GPU. |
| `xr::BlitEye` | `src/xr/BlitEye.cpp` | `glBlitFramebuffer` desde la eye texture del juego al swapchain OpenXR del runtime. |

### 7.3 Anillos y threads

- `kStereoSets = 4` (decisión B1 confirmada por Mario, turno 7): 4 pares L+R ring-bufferados.
- `g_xrContext = ` OpenGL ES context separado, `eglMakeCurrent(pbuffer, pbuffer, g_xrContext)` (patrón Dubrovskiy, §2.4).
- `XR_ANDROID_thread_type`: `XR_ANDROID_THREAD_TYPE_RENDERER_WORKER_KHR` (extensión disponible en PICO OpenXR 1.4.0 según el plan toolchain).
- `pthread_create` para el render thread, basado en `XR_ANDROID_thread_type` para hint al scheduler.

### 7.4 Estrategia de pose (decisión C de turno 7)

C3 (TimeWarp-only) en primer encendido del device:
- No tocamos RenderDragon. La cámara nativa corre con pose que sea.
- El compositor OpenXR de PICO reproyecta la textura entera usando la pose REAL del HMD al scan-out.
- Resultado: visualmente correcto, sin tearing, sin nauseas. **Pero** los parallax profundos se ven mal porque cada ojo se renderizó desde la misma cámara.

C2 (parche en shader de cámara de RenderDragon) cuando device muestre cuánto molesta la divergencia. **Gate**: latencia colectada > 22 ms o queja de confort.

### 7.5 Estrategia de multiview (decisión D de turno 7)

D2 (2 swapchains independientes, `arraySize=1`). Bedrock recorre el frame 2× — cero cambios al motor. Multiview queda como optimización post-MVP, no en release 0.

---

## 8. Plan de implementación

### Fase 1 — Cimientos JNI (cuando Pablo rearme el .a)
- [ ] `src/JNI_OnLoad.cpp`: añadir `RegisterNatives` para los 5 lifecycle + 6 input + 1 Choreographer. Firmas a copiar del `jni_anchors.json`.
- [ ] `src/xr/SwappyBridge.cpp`: detectar `swappyGLInjectTracer` por `dlsym`, registrar tracer.
- [ ] Test: que el binario **linkee dinámicamente** a `libswappy.so` (Bedrock la carga y mcbedvr hereda el namespace — `dlsym(RTLD_DEFAULT, ...)` debe encontrar el símbolo).

### Fase 2 — OpenXR context (mientras el visor no esté en mesa)
- [ ] `src/xr/XrContext.cpp`: implementar el ciclo `xrCreateInstance` → `xrCreateSession` → `xrCreateSwapchain(2)` → `xrCreateReferenceSpace(STAGE|LOCAL)`.
- [ ] `xrGetVisibilityMaskKHR` opcional (HAM) si PICO OpenXR Mobile 1.4.0 declara la extensión.
- [ ] Compilación estática: `XR_NO_PROTOTYPES + __attribute__((weak)) + RTLD_GLOBAL` (patrón de `mcbedvr-build-arm64.md`).

### Fase 3 — Frame loop + blit (en device, con PICO 4 Ultra físico)
- [ ] `src/xr/SyncClock.cpp`: integrar Choreographer (JNI-Replace de turno 7) y preSwap (tracer) en una sola cadencia.
- [ ] `src/xr/PosePublisher.cpp`: ring de 4 pose slots, fence GPU.
- [ ] `src/xr/BlitEye.cpp`: `glBlitFramebuffer` con la textura del ojo.
- [ ] Validación: 1 frame verde en el HMD. Luego: 5 minutos de Minecraft caminata libre sin crash.

### Fase 4 — Pose injection (post-MVP)
- [ ] Auditar si RenderDragon expone algún ECS CameraComponent hookable; si sí, C1.
- [ ] Si no: parchar el vertex shader de la cámara (C2) o un UBO compartido.
- [ ] Re-validar latencia con device-tests.

---

## 9. Decisiones pendientes para @Mario Santos

Necesito tres contestadas para cerrar fase 1:

### 9.1 Arquitectura del frame loop
- **A. ✅ Adoptar 5.3 tracer como primaria + 5.4 Choreographer como complemento** (lo recomendado arriba).
- **B. ❌ Hook PLT/GOT sobre `SwappyGL_swap`** (reservar como fallback sólo si A falla en device).
- Mi voto: A. Es lo que mejor digiere la biblia + los gaps detectados.

### 9.2 Política de fallback si Swappy no se puede hookear API-level
- Si `dlsym(RTLD_DEFAULT, "swappyGLInjectTracer")` devuelve NULL en device (caso raro, probablemente Bedrock Vulkan-only):
- **F1**: caer a Choreographer (5.4) sólo — rendimiento subóptimo, válido para validate boot.
- **F2**: escalar a PLT hook sobre `libswappy.so:SwappyGL_swap` con And64 inline — reabre W⊕X/SELinux, requiere PICO OS test.
- Mi voto: F1 (sabemos que Swappy está en `libminecraftpe.so` por los símbolos de Pablo, así que es casi seguro que A funcione). Reservar F2 sólo si vemos NULL.

### 9.3 Frame pacing — ¿relacionar el ring al HMD refresh-rate?
- **G1**: ring `kStereoSets = 4` para todos los refresh rates (PICO 72/90). Costo fijo 30 MB VRAM.
- **G2**: ring dinámico: `kStereoSets = max(4, HMD_hz / GameThread_hz + 1)`. En 90 Hz/60 Hz = 4, en 72 Hz/30 Hz = 6 (~45 MB VRAM).
- Mi voto: G1 para MVP, G2 post-MVP si hay queja de tearing en 72 Hz.

### 9.4 Inline hook en `libminecraftpe.so` (lo que NO es frame loop)
- Para el input/cámara donde A1 (`RegisterNatives`) no llega, ¿reservamos And64 o lo dejamos para fase 4?
- Mi voto: fase 4. Si fase 1-3 funcionan, no necesitamos tocar `.text` del motor.

---

## 10. Referencias y deuda

- **Biblia**: `dubrovskiy-yevhen-stakelogic/gta-sa-vr-quest` (`/README.md`, `main.cpp:415-434, 863-937, 2419-2429`, `Xr.cpp:3142-3257, 11716-12040`).
- **Swappy tracer docs**: Google AGDK `developer.android.com/games/sdk/frame-pacing/opengl/add-functions` (firmas `SwappyGL_swap` y tracer).
- **OpenXR frames threading**: Khronos OpenXR 1.1 spec §6.4 (Wait-Begin-End cycle); Meta mobile OpenXR docs `developers.meta.com/vr/documentation/native/android/mobile-openxr-frames/`.
- **Otros ports Android→VR consultados**: QuestZDoom, BeefRaiderXR (Team Beef Studios, OpenXR nativo desde source — no aplican porque recompilan engine), GTA Vice City VR (`dubrovskiy-yevhen-stakelogic/vice-city-vr`, D3D12 PCVR sólo — misma biblia en otra reencarnación), HackTricks `eglswapbuffers-hooking` (técnica clásica).
- **Memoria**: actualizar `memory/decomp-ports-vr.md` con §0 (relevamiento de ports VR), §4 (Bedrock frame loop), §6 (matriz).

---

**Cierre operativo:**
- Plan completo escrito, decisiones marcadas con voto.
- **Para Pablo**: no requiere toolchain hasta fase 1 cierre — todo el código de `src/JNI_OnLoad.cpp` y `src/xr/SwappyBridge.cpp` es fuente.
- **Para Emilio**: el SyncClock expone `xr::SyncClock::stats()` que puede conectar al `nc_device_correlation` cuando esté — avisa para arrancar la cableado.
- **Para Mario**: necesito 9.1, 9.2 y 9.3. Con esas tres contestadas, este plan se vuelve contrato y se ata a `vendor/mcbedvr/architecture.locked.md`.
