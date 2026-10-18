// Symbols.h — Tabla de entradas a resolver/interceptar en libminecraftpe.so.
//
// CAMBIO DE ESTRATEGIA en Fase 2 (post-REA 1.26.52):
//   Antes:  dlsym de mangled names del motor (_ZN3App16setRenderingSizeEii, _ZN13MinecraftGame...).
//   Ahora:  dlsym de exports JNI oficiales (Java_com_mojang_minecraftpe_MainActivity_native*).
//
// Motivo (de tools/offset_map_consolidated.json): Mojang compila Bedrock 1.21+ con
// visibilidad oculta. Los mangled names del motor están AUSENTES en .dynsym. Pero los
// exports JNI (`extern "C" Java_...`) SÍ están, porque Android los necesita para
// resolver desde la capa Java. Son el contrato oficial entre la capa Android y el
// motor. Hookeamos ahí.
//
// Esto convierte el inyector en un observador/patch JNI-side: recibimos el contexto
// (JNIEnv*, jobject, jint w, jint h) cuando Bedrock pide un resize, sincronizamos
// el swapchain OpenXR con esas dimensiones, y delegamos al original.

#pragma once

#include <cstdint>
#include <jni.h>

namespace mcbedvr::sym {

// Estado de cada entrada tras ResolveGameSymbols() + InstallNativeHooks().
//   Missing   : dlsym no encontró el símbolo en libminecraftpe.so.
//   Resolved  : dlsym encontró el símbolo; no se instaló hook (no hay Dobby, o
//               el hook no fue requerido todavía — Fase 3 los enchufa).
//   Hooked    : el prólogo a JNI-export fue parchado y `orig` guarda la entrada
//               original para que el wrapper delegue.
enum class SymbolStatus : std::uint8_t {
    Missing   = 0,
    Resolved  = 1,
    Hooked    = 2,
};

struct JniEntry {
    void*         fn     = nullptr;  // dirección actual de la entry en el binario
    void*         orig   = nullptr;  // dirección original pre-hook (para delegar)
    SymbolStatus  status = SymbolStatus::Missing;
};

struct GameSymbols {
    // ---- JNI MainActivity lifecycle (REEMPLAZAN a App::setRenderingSize) ----
    // nativeResize es el hook crítico: Bedrock publica las dimensiones del
    // surface cada vez que el visor cambia FOV / resolution. Lo sincronizamos
    // con la swapchain OpenXR antes de delegar.
    JniEntry nativeResize;                  // void(JNIEnv*, jobject, jint, jint)
    JniEntry nativeSuspend;                 // void(JNIEnv*, jobject)
    JniEntry nativeOnDestroy;               // void(JNIEnv*, jobject)
    JniEntry nativeStopThis;                // void(JNIEnv*, jobject)
    JniEntry nativeSuspendGameplayUpdates;  // void(JNIEnv*, jobject)
    JniEntry nativeSetHeadphonesConnected;  // void(JNIEnv*, jobject, jboolean)

    // ---- JNI input (Fase 3 enchufa la lógica de gamepad PICO -> Bedrock) ----
    JniEntry onInputDeviceAddedNative;      // jboolean(JNIEnv*, jobject, jint)
    JniEntry setControllerDetailsNative;    // void(JNIEnv*, jobject, jint, jint, jint, jint, jstring)
    JniEntry setFoundXboxControllerNative;  // void(JNIEnv*, jobject, jboolean)

    // ---- Engine: chain-walking runtime (Fase 3) ----
    // Estos siguen siendo punteros void* porque se obtendrán via runtime walk
    // sobre las jump tables (0xe496620 etc) en Fase 3, no via dlsym.
    void* MinecraftGame_ctor          = nullptr;
    void* LevelRendererPlayer_render  = nullptr;
};

// Acceso único. Thread-safe reads después de Init (writes sólo durante
// JNI_OnLoad antes de que Bedrock arranque game thread).
GameSymbols& g();

// Resuelve los símbolos JNI de libminecraftpe.so (los que están en .dynsym) y
// guarda la dirección de cada entry en g(). Retorna el número de required que no
// se encontraron; 0 = éxito. NO instala hooks todavía — ver InstallNativeHooks.
int  ResolveGameSymbols();

// Parchea los prólogos de los JNI exports para redirigir a nuestros wrappers.
// Sólo funciona si Dobby está enlazado (MCBEDVR_HAS_DOBBY); sin Dobby, no-op
// y deja los slots en Resolved. Retorna cuántos hooks se instalaron OK.
int  InstallNativeHooks();

// Imprime tabla completa con direcciones y estado.
void DumpGameSymbols();

// Buscar la base de carga de libminecraftpe.so en el proceso. Útil para
// calcular file offset = addr - base en herramientas offline.
uintptr_t FindLoadBase();

}  // namespace mcbedvr::sym