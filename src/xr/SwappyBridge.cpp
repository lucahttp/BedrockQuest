// SwappyBridge.cpp — Resolución + registro del tracer de Android Frame Pacing.
//
// Estrategia:
//   1. dlsym(RTLD_DEFAULT, "SwappyGL_injectTracer")    ← API C++ (AGDK 1.5+, canónica)
//   2. dlsym(RTLD_DEFAULT, "swappyGLInjectTracer")     ← API C   (AGDK 1.4 legacy)
//   3. Si ambos NULL → FallbackChoreographerOnly (F1 del xr_render_plan §9.2)
//
// Por qué dlsym(RTLD_DEFAULT): libminecraftpe.so carga libswappy.so en su
// namespace global (GameActivity fuerza RTLD_GLOBAL al dlopen). mcbedvr.so
// hereda ese namespace, así que el símbolo se resuelve sin reabrir el handle.
//
// Por qué NO declarar #include <swappy/Swappy.h>: el header oficial vive en
// <swappy/Swappy.h> dentro del AAR de AGDK que NO desplegamos al device.
// mcbedvr sólo necesita el ABI de los dos símbolos tracer; lo declaramos
// localmente con las firmas exactas de la doc pública.

#include "xr/SwappyBridge.h"
#include "xr/SyncClock.h"
#include "Log.h"

#include <dlfcn.h>

#include <atomic>
#include <cstring>

namespace mcbedvr::xr {
namespace {

// ─── Tipos del ABI tracer (fuente: AGDK public docs + Khronos headers) ───
// https://developer.android.com/games/sdk/frame-pacing/opengl/add-functions

typedef void (*SwappyTracerPreSwapCallback)(void* user, int fd);
typedef void (*SwappyPostSwapCallback)(void* user, long timeout_ns);

struct SwappyTracer {
    void*                       user;
    SwappyTracerPreSwapCallback preSwap;
    SwappyPostSwapCallback      postSwap;
};

// Firma C++ (AGDK 1.5+): void SwappyGL_injectTracer(EGLDisplay, EGLSurface, const SwappyTracer*)
// No usamos EGL types en mcbedvr (la sesión XR se maneja en Xr.cpp); los dos
// primeros args los pasamos nullptr — Swappy los ignora si están en NULL en
// builds donde el runtime ya conoce el display/surface vía el constructor del
// propio pipeline.
typedef void (*SwappyGLInjectTracerCxxFn)(void* /*EGLDisplay*/,
                                          void* /*EGLSurface*/,
                                          const SwappyTracer*);

// Firma C legacy (AGDK 1.4): void swappyGLInjectTracer(SwappyTracer*)
typedef void (*SwappyGLInjectTracerCApiFn)(const SwappyTracer*);

// ─── Estado del módulo ─────────────────────────────────────────────────

std::atomic<SwappyBridgeStatus> g_status{ SwappyBridgeStatus::NotInitialized };
const char*                     g_resolved_symbol = nullptr;  // sólo diagnóstico

// ─── Callbacks preSwap / postSwap ──────────────────────────────────────
//
// Fase 1: stubs que sólo loggean la cadencia del frame del juego. Sirven
// para confirmar en device que Swappy está invocando nuestros callbacks
// (con `adb logcat -s mcbedvr | grep swappy`). En Fase 3 estos callbacks
// se reemplazan por:
//
//   preSwap  → xr::SyncClock::OnFrameBegin + xr::BlitEye::BlitL/R
//               + xr::PosePublisher::Publish + xrEndFrame
//   postSwap → xr::SyncClock::OnFrameEnd (libera el ring slot)
//
// El callback corre dentro del GameThread de Bedrock (mismo thread que
// llama `SwappyGL_swap`). Toda la lógica debe ser thread-safe contra el
// render thread OpenXR que mantiene `g_xrContext`. El patrón será
// std::mutex en la región crítica, sin locks prolongados.

void PreSwapCb(void* user, int fd) {
    (void)user;
    mcbedvr::xr::SyncClockOnFrameBegin(fd);
    LOG_SWAPPY("preSwap fd=%d", fd);
}

void PostSwapCb(void* user, long timeout_ns) {
    (void)user;
    mcbedvr::xr::SyncClockOnFrameEnd(timeout_ns);
    LOG_SWAPPY("postSwap timeout_ns=%ld", timeout_ns);
}

SwappyTracer g_tracer{
    /*user=*/nullptr,
    /*preSwap=*/&PreSwapCb,
    /*postSwap=*/&PostSwapCb,
};

// ─── Resolución de símbolos + registro ─────────────────────────────────

bool TryCxxApi() {
    ::dlerror();  // limpia errno previo
    void* fn = ::dlsym(RTLD_DEFAULT, "SwappyGL_injectTracer");
    if (!fn) return false;
    auto inject = reinterpret_cast<SwappyGLInjectTracerCxxFn>(fn);
    inject(nullptr, nullptr, &g_tracer);
    g_resolved_symbol = "SwappyGL_injectTracer";
    return true;
}

bool TryCLegacyApi() {
    ::dlerror();
    void* fn = ::dlsym(RTLD_DEFAULT, "swappyGLInjectTracer");
    if (!fn) return false;
    auto inject = reinterpret_cast<SwappyGLInjectTracerCApiFn>(fn);
    inject(&g_tracer);
    g_resolved_symbol = "swappyGLInjectTracer";
    return true;
}

bool ResolveAndRegister() {
    if (TryCxxApi()) {
        LOG_SWAPPY("API C++ (AGDK 1.5+) resuelta — tracer registrado");
        return true;
    }
    if (TryCLegacyApi()) {
        LOG_SWAPPY("API C legacy (AGDK 1.4) resuelta — tracer registrado");
        return true;
    }
    LOG_SWAPPYW("ni SwappyGL_injectTracer ni swappyGLInjectTracer presentes "
                "en RTLD_DEFAULT — F1 fallback (Choreographer-only)");
    return false;
}

}  // namespace

const char* StatusName(SwappyBridgeStatus s) {
    switch (s) {
        case SwappyBridgeStatus::NotInitialized:           return "NotInitialized";
        case SwappyBridgeStatus::TracerActive:             return "TracerActive";
        case SwappyBridgeStatus::FallbackChoreographerOnly:
                                                          return "FallbackChoreographerOnly";
        case SwappyBridgeStatus::Failed:                   return "Failed";
    }
    return "?";
}

bool SwappyBridgeInit() {
    auto cur = g_status.load();
    if (cur == SwappyBridgeStatus::TracerActive) {
        LOG_SWAPPYW("Init() re-llamado cuando ya estaba TracerActive — no-op");
        return true;
    }
    if (cur != SwappyBridgeStatus::NotInitialized) {
        LOG_SWAPPYW("Init() en estado %s — ignorando", StatusName(cur));
        return false;
    }
    if (ResolveAndRegister()) {
        g_status.store(SwappyBridgeStatus::TracerActive);
        return true;
    }
    g_status.store(SwappyBridgeStatus::FallbackChoreographerOnly);
    return false;
}

void SwappyBridgeShutdown() {
    auto cur = g_status.load();
    if (cur == SwappyBridgeStatus::NotInitialized) return;
    // Swappy NO expone deregistro público del tracer → neutralizamos
    // los punteros para que cualquier callback tardío caiga en no-op.
    g_tracer.preSwap = nullptr;
    g_tracer.postSwap = nullptr;
    g_status.store(SwappyBridgeStatus::NotInitialized);
    LOG_SWAPPY("shutdown ok (callbacks neutralizados, sin deregistro formal)");
}

SwappyBridgeStatus SwappyBridgeStatus_() { return g_status.load(); }

void SwappyBridgeDumpDiagnostics() {
    LOG_SWAPPY("==== SwappyBridge diagnostics ====");
    LOG_SWAPPY("  status        : %s", StatusName(g_status.load()));
    LOG_SWAPPY("  resolved_api  : %s",
               g_resolved_symbol ? g_resolved_symbol : "<none>");
    LOG_SWAPPY("  preSwap cb    : %p",
               reinterpret_cast<void*>(g_tracer.preSwap));
    LOG_SWAPPY("  postSwap cb   : %p",
               reinterpret_cast<void*>(g_tracer.postSwap));
    LOG_SWAPPY("=================================");
}

}  // namespace mcbedvr::xr