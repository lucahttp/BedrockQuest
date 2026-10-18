// JNI_Hooks.cpp — Natives Bedrock + función de registro batch (A1).
//
// DOS conjuntos de wrappers conviven en este archivo:
//
//   (A) `Bedrock_native*` — registro A1 (RegisterNatives). Son los natives
//       que la JVM invoca cuando Mojang llama desde Java. Reemplazan
//       limpiamente a las funciones internas de libminecraftpe.so sin
//       tocar .text. Patrón Dubrovskiy (gta-sa-vr-quest Symbols.cpp:51-60
//       + main.cpp:2419-2429).
//
//   (B) `Hook_native*` — fallback Dobby/And64 (sigue funcionando si A1
//       falla). Son los wrappers viejos que viven acá desde Fase 2; los
//       conserva `mcbedvr::sym::InstallNativeHooks()` y se llaman vía
//       patch de prólogo. Sólo se activan si RegisterNatives devuelve JNI_ERR.
//
// Estrategia actual (turno 8, decisión Mario): A1 primero, B como fallback.
//
// Todas las firmas JNI siguen el contrato `Java_com_mojang_minecraftpe_*`
// declarado en libminecraftpe.so 1.26.52.3 — ver memory/apk-bedrock-126-jni-symbols.md.

#include "Symbols.h"
#include "Log.h"
#include "Xr.h"
#include "xr/XrContext.h"
#include "xr/BlitEye.h"
#include "xr/InputBridge.h"
#include "xr/SwappyBridge.h"
#include "xr/SyncClock.h"  // Fase 2.C: SyncClockOnChoreographerTick
#include "MCBedVr.h"

#include <jni.h>
#include <android/log.h>
#include <atomic>

#include <cstdint>
#include <cstring>
#include <dlfcn.h>

namespace mcbedvr {

using NativeKeyHandlerFn = jboolean (*)(JNIEnv*, jobject, jint, jint, jint);
static NativeKeyHandlerFn g_origKeyHandler = nullptr;
static std::atomic<bool> g_origKeyHandlerResolved{false};

// KeyHandler — firma verificada desde classes.dex de Bedrock 1.26.52.3:
// MainActivity.dispatchKeyEvent invokes:
//   this.nativeKeyHandler(keyCode, action, source);
// Argumentos:
//   keyCode: KEYCODE_* Android
//   action:  ACTION_DOWN=0, ACTION_UP=1
//   source:  0x401 (SOURCE_GAMEPAD), 0x101 (SOURCE_KEYBOARD), 0x1000010 (SOURCE_JOYSTICK)
// Mojang maneja KEYCODE_BACK (4) y retorna true (1); para el resto retorna false (0)
// para que MainActivity llame a super.dispatchKeyEvent() -> GameActivity.
extern "C" jboolean Bedrock_nativeKeyHandler(JNIEnv* env, jobject thiz,
                                             jint keyCode, jint action, jint source) {
    LOG_INPUT("nativeKeyHandler(keyCode=0x%x, action=%d, source=0x%x)",
              static_cast<unsigned>(keyCode),
              static_cast<int>(action),
              static_cast<unsigned>(source));

    if (!g_origKeyHandlerResolved.load(std::memory_order_relaxed)) {
        void* handle = ::dlopen("libminecraftpe.so", RTLD_NOW | RTLD_NOLOAD);
        if (!handle) handle = ::dlopen("libminecraftpe.so", RTLD_NOW | RTLD_LOCAL);
        if (handle) {
            g_origKeyHandler = reinterpret_cast<NativeKeyHandlerFn>(
                ::dlsym(handle, "Java_com_mojang_minecraftpe_MainActivity_nativeKeyHandler"));
            if (g_origKeyHandler) {
                LOG_INPUT("Resolved original Java_com_mojang_minecraftpe_MainActivity_nativeKeyHandler: %p",
                          reinterpret_cast<void*>(g_origKeyHandler));
            }
        }
        g_origKeyHandlerResolved.store(true, std::memory_order_relaxed);
    }

    if (g_origKeyHandler) {
        if (!env) {
            JavaVM* vm = GetJavaVM();
            if (vm) {
                vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
            }
        }
        return g_origKeyHandler(env, thiz, keyCode, action, source);
    }
    return JNI_FALSE;
}

namespace {

// ───────────────────────────────────────────────────────────────────────
// (A) Natives registrados vía RegisterNatives (camino primario A1)
// ───────────────────────────────────────────────────────────────────────

// ---- MainActivity lifecycle ----

extern "C" void Bedrock_nativeResize(JNIEnv* env, jobject thiz,
                                     jint w, jint h) {
    LOG_JNI("nativeResize(%d, %d) — capturing activity, syncing XR swapchain & forwarding to Bedrock",
            static_cast<int>(w), static_cast<int>(h));
    if (env && thiz) {
        mcbedvr::xr::XrContextSetActivity(env, thiz);
        mcbedvr::xr::InputBridge::Init(mcbedvr::GetJavaVM(), thiz);
        if (mcbedvr::xr::XrContextState() == mcbedvr::xr::XrState::Uninitialized) {
            mcbedvr::xr::Init();
        }
    }
    mcbedvr::xr::BlitEye::SetWindowSize(static_cast<int>(w), static_cast<int>(h));
    xr::ResizeSwapchain(static_cast<int>(w), static_cast<int>(h));
    void* target = sym::g().nativeResize.orig ? sym::g().nativeResize.orig : sym::g().nativeResize.fn;
    if (target) {
        using Fn = void (*)(JNIEnv*, jobject, jint, jint);
        reinterpret_cast<Fn>(target)(env, thiz, w, h);
    }
}

extern "C" void Bedrock_nativeSuspend(JNIEnv* env, jobject thiz) {
    LOG_JNI("nativeSuspend — forwarding to Bedrock");
    if (env && thiz) {
        mcbedvr::xr::XrContextSetActivity(env, thiz);
    }
    void* target = sym::g().nativeSuspend.orig ? sym::g().nativeSuspend.orig : sym::g().nativeSuspend.fn;
    if (target) {
        using Fn = void (*)(JNIEnv*, jobject);
        reinterpret_cast<Fn>(target)(env, thiz);
    }
}

extern "C" void Bedrock_nativeOnDestroy(JNIEnv* env, jobject thiz) {
    LOG_JNI("nativeOnDestroy — forwarding to Bedrock");
    void* target = sym::g().nativeOnDestroy.orig ? sym::g().nativeOnDestroy.orig : sym::g().nativeOnDestroy.fn;
    if (target) {
        using Fn = void (*)(JNIEnv*, jobject);
        reinterpret_cast<Fn>(target)(env, thiz);
    }
}

extern "C" void Bedrock_nativeStopThis(JNIEnv* env, jobject thiz) {
    LOG_JNI("nativeStopThis — forwarding to Bedrock");
    void* target = sym::g().nativeStopThis.orig ? sym::g().nativeStopThis.orig : sym::g().nativeStopThis.fn;
    if (target) {
        using Fn = void (*)(JNIEnv*, jobject);
        reinterpret_cast<Fn>(target)(env, thiz);
    }
}

// ---- MainActivity input ----
//
// nativeBackPressed / ReturnKeyPressed / ShiftKeyPressed / ClearAButtonState /
// BackSpacePressed: argumentos `(...)V` (void) según inspección del APK.
// nativeKeyHandler / KeyHandler: firma exacta aún por verificar en device;
// lo dejamos registrado con un descriptor conservador "(Ljava/lang/String;)Z"
// (recibe String → boolean consumido) que es el patrón GameActivity estándar
// para text input. Si Mojang usa otra firma, RegisterNatives devuelve JNI_ERR
// para ESA entrada y la saltamos sin abortar el resto.

extern "C" void Bedrock_nativeBackPressed(JNIEnv* /*env*/, jobject /*thiz*/) {
    LOG_INPUT("nativeBackPressed — ruteo a PICO B button");
}

extern "C" void Bedrock_nativeReturnKeyPressed(JNIEnv* /*env*/, jobject /*thiz*/) {
    LOG_INPUT("nativeReturnKeyPressed");
}

extern "C" void Bedrock_nativeShiftKeyPressed(JNIEnv* /*env*/, jobject /*thiz*/, jint shift) {
    LOG_INPUT("nativeShiftKeyPressed(shift=0x%x)", static_cast<unsigned>(shift));
}

extern "C" void Bedrock_nativeClearAButtonState(JNIEnv* /*env*/, jobject /*thiz*/) {
    LOG_INPUT("nativeClearAButtonState");
}

extern "C" void Bedrock_nativeBackSpacePressed(JNIEnv* /*env*/, jobject /*thiz*/) {
    LOG_INPUT("nativeBackSpacePressed");
}

// ---- GameActivity Choreographer (frame tick) ----

extern "C" void Bedrock_nOnChoreographer(JNIEnv* /*env*/, jclass /*clazz*/,
                                         jlong frameTimeNanos, jfloat dt) {
    // frameTimeNanos = System.nanoTime() al momento del vsync.
    // dt = delta desde el último tick (segundos).
    //
    // Fase 2.C: enrutamos al SyncClock (lock-free, hot path). El log se
    // throttea a 1 cada 64 ticks (~1Hz a 60fps) para no inundar logcat.
    mcbedvr::xr::SyncClockOnChoreographerTick(
        static_cast<std::int64_t>(frameTimeNanos),
        static_cast<double>(dt));

    static std::atomic<int> tick_log_mod{0};
    int n = tick_log_mod.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n % 64 == 0) {
        LOG_TICK("choreo tick #%d t=%lld ns dt=%.4f s (fase 2 — clock source=%s)",
                 n, (long long)frameTimeNanos, (double)dt,
                 mcbedvr::xr::ClockSourceName(mcbedvr::xr::SyncClockSource_()));
    }
}

// ───────────────────────────────────────────────────────────────────────
// Tabla de registro A1 — el contrato con la JVM
// ───────────────────────────────────────────────────────────────────────

struct RegisterBatch {
    const char* jni_class;                  // FQN de la clase Java
    const JNINativeMethod* methods;         // array de métodos
    std::size_t             count;
    const char*             tag;            // para logging
};

// ---- Lote 1: MainActivity ----
constexpr JNINativeMethod kMainActivityMethods[] = {
    // ---- lifecycle (intercept window size & capture activity for OpenXR & InputBridge) ----
    { "nativeResize",     "(II)V",  (void*)Bedrock_nativeResize     },
};

// ---- Lote 2: GameActivity$ChoreographerCallback ----
// FQN verificado por la presencia del símbolo JNI export
// `Java_com_google_androidgamesdk_ChoreographerCallback_nOnChoreographer`
// en libminecraftpe.so 1.26.52.3 @ 0x7fa0694. Firma estándar de GameActivity:
//   private static void nOnChoreographer(long frameTimeNanos, float dt)
// → descriptor "(JF)V".
constexpr JNINativeMethod kChoreographerMethods[] = {
    { "nOnChoreographer", "(JF)V", (void*)Bedrock_nOnChoreographer },
};

// ---- Lote 3: JellyBeanDeviceManager ----
extern "C" void Bedrock_setFoundXboxControllerNative(JNIEnv* env, jobject thiz, jboolean found) {
    LOG_INPUT("setFoundXboxControllerNative(orig=%d) -> forcing true", (int)found);
    void* target = sym::g().setFoundXboxControllerNative.orig 
        ? sym::g().setFoundXboxControllerNative.orig 
        : sym::g().setFoundXboxControllerNative.fn;
    if (target) {
        using Fn = void (*)(JNIEnv*, jobject, jboolean);
        reinterpret_cast<Fn>(target)(env, thiz, JNI_TRUE);
    }
}

constexpr JNINativeMethod kJellyBeanMethods[] = {
    { "setFoundXboxControllerNative", "(Z)V", (void*)Bedrock_setFoundXboxControllerNative },
};

constexpr RegisterBatch kBatches[] = {
    { "com/mojang/minecraftpe/MainActivity",
        kMainActivityMethods,
        sizeof(kMainActivityMethods) / sizeof(kMainActivityMethods[0]),
        "MainActivity" },
    { "com/google/androidgamesdk/ChoreographerCallback",
        kChoreographerMethods,
        sizeof(kChoreographerMethods) / sizeof(kChoreographerMethods[0]),
        "ChoreographerCallback" },
    { "com/mojang/minecraftpe/input/JellyBeanDeviceManager",
        kJellyBeanMethods,
        sizeof(kJellyBeanMethods) / sizeof(kJellyBeanMethods[0]),
        "JellyBeanDeviceManager" },
};

// Run one batch — returns count of methods actually registered (>=0) or -1 on hard failure.
int RunOneBatch(JNIEnv* env, const RegisterBatch& batch) {
    jclass cls = env->FindClass(batch.jni_class);
    if (cls == nullptr) {
        LOG_JNIW("FindClass(\"%s\") → NULL (clase no cargada todavía; "
                 "RegisterNatives A1 se saltará este lote)",
                 batch.jni_class);
        env->ExceptionClear();  // FindClass side-effect
        return -1;
    }
    jint rv = env->RegisterNatives(cls, batch.methods, batch.count);
    if (rv != JNI_OK) {
        LOG_JNIE("RegisterNatives(%s, %zu métodos) → JNI_ERR (rv=%d). "
                 "Posible mismatch de descriptor — revisar firmas en classes.dex.",
                 batch.tag, batch.count, static_cast<int>(rv));
        env->DeleteLocalRef(cls);
        return -1;
    }
    LOG_JNI("RegisterNatives(%s, %zu métodos) OK",
            batch.tag, batch.count);
    env->DeleteLocalRef(cls);
    return static_cast<int>(batch.count);
}

}  // namespace

// ─── API pública llamada desde JNI_OnLoad ─────────────────────────────

bool RegisterBedrockNatives(JavaVM* vm) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        LOG_JNIE("GetEnv(JNI_VERSION_1_6) falló — A1 no ejecutable");
        return false;
    }
    int total_registered = 0;
    int batches_ok = 0;
    for (const auto& batch : kBatches) {
        int n = RunOneBatch(env, batch);
        if (n > 0) {
            total_registered += n;
            ++batches_ok;
        }
    }
    LOG_JNI("A1 total — %d métodos en %d clases",
            total_registered, batches_ok);
    return batches_ok > 0;
}

// ───────────────────────────────────────────────────────────────────────
// (B) Wrappers fallback vía Dobby/And64 — los llama InstallNativeHooks()
//     sólo si A1 falla. Conservados por compatibilidad.
// ───────────────────────────────────────────────────────────────────────

extern "C" void Hook_nativeResize(JNIEnv* env, jobject thiz, jint w, jint h) {
    LOG_HOOK("[fallback-Dobby] Hook_nativeResize(%d, %d)",
             static_cast<int>(w), static_cast<int>(h));
    xr::ResizeSwapchain(static_cast<int>(w), static_cast<int>(h));
    using Fn = void (*)(JNIEnv*, jobject, jint, jint);
    auto* orig = reinterpret_cast<Fn>(sym::g().nativeResize.orig);
    if (orig != nullptr) orig(env, thiz, w, h);
    else LOG_HOOKW("Hook_nativeResize sin orig — observer mode");
}

extern "C" void Hook_nativeSuspend(JNIEnv* env, jobject thiz) {
    LOG_HOOK("[fallback-Dobby] Hook_nativeSuspend");
    using Fn = void (*)(JNIEnv*, jobject);
    auto* orig = reinterpret_cast<Fn>(sym::g().nativeSuspend.orig);
    if (orig) orig(env, thiz);
}

extern "C" void Hook_nativeOnDestroy(JNIEnv* env, jobject thiz) {
    LOG_HOOK("[fallback-Dobby] Hook_nativeOnDestroy");
    using Fn = void (*)(JNIEnv*, jobject);
    auto* orig = reinterpret_cast<Fn>(sym::g().nativeOnDestroy.orig);
    if (orig) orig(env, thiz);
}

}  // namespace mcbedvr