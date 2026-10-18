// XrContext.h — Estado OpenXR pesado: instance, session, swapchain, space.
//
// Fase 2.A (contrato lockado turno 12). Reemplaza el cuerpo de Xr.cpp
// (init/shutdown/step frame/resize). La API vieja de Xr.h queda como
// wrapper deprecado — no se borra porque JNI_OnLoad + JNI_Hooks ya la
// consumen. Toda la lógica de runtime (XR_NO_PROTOTYPES + weak refs + dlopen
// del forwardloader de PICO) vive ahora acá.
//
// Lifecycle:
//   XrContextInit       — Uninitialized → InstanceCreated → SessionActive
//                         → StereoRendering (o Failed en cualquier punto).
//   XrContextShutdown   — * → Uninitialized (idempotente).
//
// Threading: el Init/Shutdown/ApplySwapchainDims toman g_mtx. Las lecturas
// de estado (XrContextState, XrContextReady) son lock-free. SubmitFrame
// toma el mutex brevemente para xrWaitFrame y xrEndFrame — Fase 3 enchufa
// aquí el ring buffer de StereoSets (turno 12 §9.3) sin tocar la API.

#pragma once

#include <cstdint>
#include <jni.h>
#include <openxr/openxr.h>  // XrInstance, XrSession, XrSpace, XrSwapchain, XrSystemId

namespace mcbedvr::xr {

enum class XrState : std::int32_t {
    Uninitialized    = 0,
    InstanceCreated  = 1,  // forwardloader dlopen'd + xrCreateInstance OK
    SessionActive    = 2,  // xrCreateSession + reference space OK
    StereoRendering  = 3,  // swapchain color creada — frame loop capaz
    ShuttingDown     = 4,  // Shutdown en curso
    Failed           = 5,  // Init falló en algún paso; requiere Shutdown para reset
};

const char* XrStateName(XrState s);

// Snapshot read-only del contexto. Una vez en StereoRendering, los miembros
// son estables (salvo sc_* cuando se aplica ResizeSwapchain). Caller NO debe
// mutar — es un view interno.
struct XrContextInfo {
    XrInstance  instance{XR_NULL_HANDLE};
    XrSession   session{XR_NULL_HANDLE};
    XrSpace     app_space{XR_NULL_HANDLE};
    XrSystemId  system_id{XR_NULL_SYSTEM_ID};
    XrSwapchain sc_handle{XR_NULL_HANDLE};

    std::int32_t sc_width{0};
    std::int32_t sc_height{0};
    std::int32_t sc_image_count{0};

    // EGL handles: en Fase 2.A quedan en nullptr (los captura Fase 2.B cuando
    // tengamos acceso al EGLContext real vía JNI_OnLoad del ApplicationContext).
    // Los declaramos como void* para no forzar include de EGL en este header.
    void* egl_display{nullptr};
    void* egl_context{nullptr};
};

// ─── Lifecycle ──────────────────────────────────────────────────────────

// Carga runtime OpenXR (forwardloader de PICO primero, fallback libopenxr.so),
// crea instance + session + reference space + swapchain color estéreo. En host
// (no-Android) devuelve Failed controlado — el inyector no debe compilarse
// sin toolchain pero si lo hace no rompe el CI.
//
// Idempotente: si ya está en un estado > Uninitialized, retorna ese estado
// sin tocar nada. Si está en Failed, retorna Failed (requiere Shutdown para
// reintentar).
XrState XrContextInit();

// Fija la referencia global a MainActivity cuando Bedrock la crea / redimensiona.
void    XrContextSetActivity(JNIEnv* env, jobject activity);
jobject XrContextGetActivity();

// Asegura que la sesión OpenXR y la swapchain estéreo estén creadas sobre el
// thread que tiene un EGLContext activo (Render Thread / eglSwapBuffers).
bool    XrContextEnsureSession();

// Apaga todo en orden inverso (swapchain → space → session → instance).
// NO dlclose del forwardloader: la lib se queda viva hasta el exit del proceso
// (patrón Dubrovskiy gta-sa-vr-quest) — evita invalidar g_get_proc_addr si
// alguna ruta inesperada re-invoca xr* en JNI_OnUnload. Idempotente.
void    XrContextShutdown();

// Lock-free reads. Las snapshot no son atómicas entre sí; los callers que
// necesitan consistencia (p.ej. diagnostics) deben llamar bajo g_mtx ellos
// mismos — no expuesto para mantener la API simple.
XrState XrContextState();
bool    XrContextReady();   // true ⇔ XrContextState() == StereoRendering
const   XrContextInfo& GetXrContextInfo();

// Aplica dims (w,h) a la swapchain. Falla si no está en StereoRendering o si
// las dims son inválidas. Si la swapchain actual ya está en (w,h), noop.
// Retorna true si la swapchain quedó en (w,h).
bool    XrContextApplySwapchainDims(int w, int h);

// ─── Frame submission (Fase 2.A stub; Fase 3 enchufa composition layers) ──

struct FrameRequest {
    // Vacío en Fase 2.A. Fase 3 lleva layers[], view poses, eye textures, etc.
    std::int64_t predicted_display_time{0};
    std::int64_t predicted_display_period_ns{0};
};

struct FrameResult {
    bool          submitted{false};
    std::int64_t  duration_us{0};
    XrResult      xr_result{XR_SUCCESS};
};

// xrWaitFrame + xrBeginFrame + xrEndFrame con layerCount=0 (Fase 2.A no
// renderiza nada — sólo valida cadencia y emite logs de predicción).
// En Fase 3 este método se reemplaza por el ring-buffer submit real.
FrameResult XrContextSubmitFrame(const FrameRequest& req);

// ─── Layer presentation mode ──────────────────────────────────────────
// Quad layer (virtual cinema screen for 2D UI) vs Projection (immersive 360).
void SetQuadLayerMode(bool enable);
bool GetQuadLayerMode();
void ToggleLayerMode();

// ─── Diagnostics ────────────────────────────────────────────────────────

void XrContextDumpDiagnostics();

// ─── Getters Fase 3 (Fase 2.A los expone pero son NULL/0 hasta StereoRender) ─

XrSwapchain    XrContextColorSwapchain();
std::int32_t   XrContextColorSwapchainWidth();
std::int32_t   XrContextColorSwapchainHeight();
std::int32_t   XrContextColorSwapchainImageCount();

}  // namespace mcbedvr::xr
