// Xr.cpp — Wrapper deprecado de mcbedvr::xr (Fase 2.A refactor).
//
// La lógica pesada (instance, session, swapchain, runtime dlopen) vive en
// `xr/XrContext.{h,cpp}` desde Fase 2.A (turno 13). Este archivo conserva
// la API pública vieja (Init/Shutdown/CurrentState/StepFrame/ResizeSwapchain
// /DumpDiagnostics) que JNI_OnLoad y JNI_Hooks ya consumen. La meta es que
// la refactorización sea backwards-compatible: nada cambia en los call sites.
//
// Comportamiento preservado de Fase 1:
//   - pending dims: si Bedrock llama nativeResize antes de XrContextInit()
//     terminar, las dims se persisten y se aplican en el próximo Init().
//   - LOG_XR / LOG_XRW / LOG_XRE: tag sigue siendo `mcbedvr.xr` (las macros
//     viven en Log.h; no se cambian).
//   - OnFailed: el wrapper devuelve false (no Failed explícito) para que
//     los call sites no necesiten chequear el enum.

#include "Xr.h"
#include "xr/XrContext.h"
#include "Log.h"

#include <mutex>

namespace mcbedvr::xr {
namespace {

// Dims pendientes de aplicar. Si Bedrock llama nativeResize antes de que
// XrContext termine de inicializar, guardamos acá y aplicamos en el próximo
// Init() exitoso.
std::mutex g_pending_mtx;
int        g_pending_w{0};
int        g_pending_h{0};
bool       g_pending_valid{false};

}  // namespace

State CurrentState() {
    return static_cast<State>(XrContextState());
}

void DumpDiagnostics() {
    XrContextDumpDiagnostics();
}

bool Init() {
    auto r = XrContextInit();
    if (r == XrState::Failed) {
        LOG_XRW("Init() failed — XrContext state=%s", XrStateName(r));
        return false;
    }
    // Si Bedrock publicó dims antes que Init terminara, aplicamos ahora.
    std::lock_guard<std::mutex> lk(g_pending_mtx);
    if (g_pending_valid) {
        LOG_XR("aplicando dims pendientes (%dx%d) post-Init",
               g_pending_w, g_pending_h);
        if (XrContextApplySwapchainDims(g_pending_w, g_pending_h)) {
            g_pending_valid = false;
        }
    }
    return true;
}

void Shutdown() {
    XrContextShutdown();
    std::lock_guard<std::mutex> lk(g_pending_mtx);
    g_pending_valid = false;
}

bool StepFrame() {
    auto r = XrContextSubmitFrame({});
    return r.submitted;
}

bool ResizeSwapchain(int w, int h) {
    if (w <= 0 || h <= 0) {
        LOG_XRW("ResizeSwapchain(%dx%d) dims inválidas — ignorado", w, h);
        return false;
    }
    if (!XrContextReady()) {
        // XR todavía no está listo — guardamos las dims para aplicar post-Init.
        std::lock_guard<std::mutex> lk(g_pending_mtx);
        LOG_XR("ResizeSwapchain(%dx%d) deferred (state=%s)",
               w, h, XrStateName(XrContextState()));
        g_pending_w = w;
        g_pending_h = h;
        g_pending_valid = true;
        return true;  // "aplicado" en el sentido que no se perdió.
    }
    return XrContextApplySwapchainDims(w, h);
}

}  // namespace mcbedvr::xr
