// JNI_OnLoad.cpp — Entry point del inyector.
//
// Pivot de Fase 1 (post-decisión Mario turno 8): el camino primario para
// ciclo de vida + input es A1 = `env->RegisterNatives`, NO And64/Dobby.
// Razón: Dubrovskiy lo hace así en gta-sa-vr-quest, los símbolos
// `Java_com_mojang_minecraftpe_MainActivity_*` están todos vivos en
// libminecraftpe.so 1.26.52 (auditados por Pablo), y la JVM los reemplaza
// sin que tengamos que tocar `.text`. Cero W⊕X, cero SELinux.
//
// Si A1 falla (algún día Mojang cambia visibilidad / ofusca nombres),
// caemos a B = `mcbedvr::sym::InstallNativeHooks()` que parchea prólogos
// con Dobby/And64. Esa rama queda en `src/Symbols.cpp` como fallback.
//
// El frame loop NO se intercepta acá — vive en `src/xr/SwappyBridge.cpp`
// que se inicializa después del RegisterNatives.

#include "MCBedVr.h"
#include "Log.h"
#include "Xr.h"
#include "Symbols.h"
#include "xr/SwappyBridge.h"
#include "xr/SyncClock.h"  // Fase 2.B

#include <jni.h>
#include <android/log.h>
#include <cstring>

namespace {

JavaVM* g_jvm = nullptr;  // capturado para callbacks JNI en Fase 3

}  // namespace

namespace mcbedvr {

// Forward decl — definida en JNI_Hooks.cpp. Devuelve true si al menos un
// método fue registrado con éxito vía A1 (RegisterNatives).
bool RegisterBedrockNatives(JavaVM* vm);

JavaVM* GetJavaVM() {
    return g_jvm;
}

}  // namespace mcbedvr

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    g_jvm = vm;

    LOG_JNI("%s v%d.%d.%d: JNI_OnLoad — inyector arrancando",
            mcbedvr::kProjectName,
            mcbedvr::kVersionMajor, mcbedvr::kVersionMinor, mcbedvr::kVersionPatch);

    // (1) Diagnóstico: resolver los símbolos Java_* en libminecraftpe.so
    //     para conocer offsets/direcciones. Se loggean siempre, incluso si
    //     A1 luego los "reemplaza" — son útiles para el admin tool de Emilio.
    int missing = mcbedvr::sym::ResolveGameSymbols();
    if (missing > 0) {
        LOG_SYMW("%d JNI exports REQUIRED faltantes — A1 va a fallar",
                 missing);
    }
    mcbedvr::sym::DumpGameSymbols();

    // (2) Camino primario A1: RegisterNatives sobre MainActivity + Choreographer.
    //     Cubre 5 lifecycle (Resize/Suspend/OnDestroy/StopThis/Shutdown) +
    //     6 input (BackPressed/Return/Shift/ClearA/BackSpace/KeyHandler*) +
    //     1 frame tick (ChoreographerCallback_nOnChoreographer).
    //     *KeyHandler queda gated hasta verificar firma exacta en device.
    if (!mcbedvr::RegisterBedrockNatives(vm)) {
        LOG_JNIW("RegisterNatives A1 falló — se usarán fallbacks para lifecycle");
    }

    // Instalar hooks nativos de gráficos / EGL (eglSwapBuffers) vía Dobby.
    int hooked = mcbedvr::sym::InstallNativeHooks();
    LOG_HOOK("InstallNativeHooks total: %d hooks activos", hooked);

    // (3) SwappyBridge — tracer API para frame loop. Si no se puede
    //     registrar (sin libswappy.so en el APK), queda en F1
    //     (Choreographer-only) que sigue siendo suficiente para diagnóstico.
    mcbedvr::xr::SwappyBridgeInit();
    mcbedvr::xr::SwappyBridgeDumpDiagnostics();

    // (4) SyncClock — Fase 2.B. Mapea SwappyBridgeStatus → ClockSource
    //     y queda listo para recibir ticks de ChoreographerCallback.
    mcbedvr::xr::SyncClockInit();
    mcbedvr::xr::SyncClockDumpDiagnostics();

    // (5) OpenXR se inicializa de forma diferida cuando MainActivity envía
    //     nativeResize (con Activity ref) o cuando eglSwapBuffers activa el EGLContext.
    LOG_JNI("%s: JNI_OnLoad OK — OpenXR init diferido a MainActivity / eglSwapBuffers",
            mcbedvr::kProjectName);
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* /*vm*/, void* /*reserved*/) {
    LOG_JNI("%s: JNI_OnUnload — cerrando subsistemas", mcbedvr::kProjectName);
    mcbedvr::xr::SyncClockShutdown();         // Fase 2.B: counts acumulados → logcat
    mcbedvr::xr::Shutdown();                  // Apaga OpenXR (instance/session/swapchain)
    mcbedvr::xr::SwappyBridgeShutdown();      // Apaga SwappyBridge tracer
    g_jvm = nullptr;
}