// MCBedVr.h — Tipos y constantes centrales del inyector.
// Patrón: namespace raíz `mcbedvr`, submódulos `mcbedvr::xr`, `mcbedvr::sym`.
// Construido sobre el ejemplo de savr::xr / savr::GameSymbols.

#pragma once

#include <cstdint>
#include <jni.h>

namespace mcbedvr {

// Versión del inyector. Cambiar MAJOR/MINOR con cada release de Bedrock
// soportada (los hooks de RenderDragon son movedizos).
constexpr int kVersionMajor = 0;
constexpr int kVersionMinor = 1;
constexpr int kVersionPatch = 0;

constexpr const char* kProjectName = "mcbedvr";

// Target por defecto: nombre del módulo inyectable. Si Mojang renombra la
// .so en una futura build, sólo hay que cambiar esto.
constexpr const char* kTargetLibrary = "libminecraftpe.so";

// ---------------------------------------------------------------------------
// JNI-side wrappers (definidos en src/JNI_Hooks.cpp).
//
// DOS conjuntos conviven:
//
//   (A) Bedrock_native* — camino primario Fase 1, registrados vía
//       env->RegisterNatives (patrón Dubrovskiy gta-sa-vr-quest).
//       No requieren forward-decl aquí — su dirección se pasa vía
//       reinterpret_cast en el array JNINativeMethod.
//
//   (B) Hook_native* — fallback Fase 2, parcheados vía Dobby/And64 sobre
//       los prólogos de los exports Java_com_mojang_* cuando A1 falla.
//       Forward-decl acá para que Symbols.cpp::InstallNativeHooks()
//       pueda armar el reinterpret_cast.
// ---------------------------------------------------------------------------
extern "C" {
void Hook_nativeResize(JNIEnv* env, jobject thiz, jint w, jint h);
void Hook_nativeSuspend(JNIEnv* env, jobject thiz);
void Hook_nativeOnDestroy(JNIEnv* env, jobject thiz);
jboolean Bedrock_nativeKeyHandler(JNIEnv* env, jobject thiz, jint keyCode, jint metaState, jint action);
}

JavaVM* GetJavaVM();
void PatchBedrockOptions(const char* baseDir = nullptr);

}  // namespace mcbedvr
