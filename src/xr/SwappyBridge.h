// SwappyBridge.h — Bridge con Android Frame Pacing (Swappy).
//
// Patrón derivado de vendor/mcbedvr/xr_render_plan.md §5.3 (tracer callback
// de Google AGDK). Sin parchar memoria, sin W⊕X, sin SELinux drama. La API
// `SwappyGL_injectTracer` está documentada y estable desde AGDK 1.4 (Sep 2024).
//
// Fase 1 (este archivo): sólo resolución de símbolos + registro del tracer con
// callbacks stub. La lógica real (xrWaitFrame / blit / pose publish) se enchufa
// en Fase 3 sobre los mismos hooks `preSwap` / `postSwap`.

#pragma once

#include <cstdint>

namespace mcbedvr::xr {

enum class SwappyBridgeStatus : std::uint8_t {
    NotInitialized = 0,
    TracerActive,                 // SwappyGL_injectTracer/swappyGLInjectTracer OK
    FallbackChoreographerOnly,    // dlsym devolvió NULL → F1 (ver xr_render_plan §9.2)
    Failed,
};

const char*       StatusName(SwappyBridgeStatus s);

// Resuelve `SwappyGL_injectTracer` (API C++ AGDK 1.5+) o `swappyGLInjectTracer`
// (API C AGDK 1.4) vía dlsym(RTLD_DEFAULT) y registra el tracer. Si ninguno de
// los dos símbolos está presente, queda en FallbackChoreographerOnly y el
// frame loop depende sólo del callback JNI de Choreographer (Fase 1 limit).
// Retorna true si el tracer quedó activo.
bool              SwappyBridgeInit();

// Neutraliza los callbacks (no hay deregistro público en Swappy → pone los
// punteros a nullptr para que Swappy no invoque código muerto). Idempotente.
void              SwappyBridgeShutdown();

SwappyBridgeStatus SwappyBridgeStatus_();

// Imprime a logcat el estado actual + nombre del símbolo resuelto +
// direcciones de los callbacks. Llamado desde `JNI_OnLoad` y desde
// `adb shell setprop log.tag.mcbedvr VERBOSE` para diagnóstico en device.
void              SwappyBridgeDumpDiagnostics();

}  // namespace mcbedvr::xr