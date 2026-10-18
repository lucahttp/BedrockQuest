// Xr.h — API pública del subsistema OpenXR.
// Cubre: init/shutdown + un step de frame por iteración. Los hooks específicos
// contra Bedrock viven en módulos vecinos (VrCamera, RenderBridge — Fase 3).

#pragma once

#include <cstdint>

namespace mcbedvr::xr {

// Estado runtime del subsistema XR.
enum class State : int {
    Uninitialized = 0,
    InstanceCreated,
    SessionActive,
    StereoRendering,  // swapchain creada, frame loop corriendo
    ShuttingDown,
    Failed,
};

// Inicializa el subsistema XR. Carga runtime OpenXR de PICO via dlopen,
// crea instancia + sesión + swapchain (PICO 4 Ultra: 2 layers por ojo).
// Retorna true si quedó en StereoRendering. Si falla queda en Failed.
bool Init();

// Apaga todo en orden inverso. Idempotente.
void Shutdown();

// Devuelve el estado actual. Usado por las decisiones de logging + guards.
State CurrentState();

// Imprime a logcat el estado del runtime, instance, sesión, formatos
// soportados y FOV. Útil para `adb logcat -s mcbedvr` durante depuración.
void DumpDiagnostics();

// Step por frame. Devuelve true si el frame fue presentado; false si no había
// pose lista (typical cuando el visor está fuera de la cara).
//
// En Fase 1 esto es un loop sin headset pose real (sólo valida que el runtime
// carga). En Fase 3 se enchufa al hook del render loop de RenderDragon.
bool StepFrame();

// Sincroniza la swapchain OpenXR con las dimensiones publicadas por Bedrock
// (vía Java_com_mojang_minecraftpe_MainActivity_nativeResize). Idempotente:
// si las dims coinciden con la swapchain actual, no-op. Si XR no está en
// StereoRendering todavía, guarda las dims como pending y las aplica en el
// próximo StepFrame(). Retorna true si la swapchain quedó en (w, h).
bool ResizeSwapchain(int w, int h);

}  // namespace mcbedvr::xr
