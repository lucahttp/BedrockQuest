// Log.h — Macros de logging estilo savr (envuelve __android_log_print).
// Compila en host (macOS/Linux) redirigiendo a stderr; en Android va a logcat.
//
// Tags por subsistema — Emilio Ravenna pidió tags independientes para correlación
// `adb logcat -s` ↔ admin tool. Los nombres siguen el patrón `mcbedvr.<subsistema>`
// (filtro de logcat estándar con punto como separador de namespace).
//
//   mcbedvr.jni      ← ciclo de vida Bedrock (Resize, Suspend, OnDestroy, StopThis, Shutdown)
//                     vía JNI_OnLoad + RunOneBatch
//   mcbedvr.input    ← input nativo (BackPressed, Return, Shift, ClearA, BackSpace, KeyHandler)
//   mcbedvr.tick     ← frame tick (Choreographer nOnChoreographer)
//   mcbedvr.swappy   ← frame submission (Swappy tracer preSwap/postSwap)
//   mcbedvr.xr       ← OpenXR runtime (Xr.cpp, XrContext, SyncClock — Fase 2+)
//   mcbedvr.hook     ← Dobby/And64 hook engine (InstallNativeHooks)
//   mcbedvr.sym      ← resolución de símbolos JNI (ResolveGameSymbols)
//   mcbedvr          ← tag genérico (cae al global, sin subsistema)
//
// Uso host-side: las macros redirigen a stderr con prefijo `[mcbedvr.<tag>]`
// para que la salida sea trivialmente grepeable en CI sin device.

#pragma once

#include <cstdio>

#ifdef __ANDROID__
#include <android/log.h>

// Macros internas — no usar directamente, preferí las categorizadas.
#define MCBEDVR_LOG_AT_(level, tag, ...) \
    __android_log_print((level), (tag), __VA_ARGS__)

// Tag genérico (subsistema indefinido) — compat hacia atrás.
#define BEDROCKQUEST_LOG_TAG "bedrockquest"
#define LOGI(...) MCBEDVR_LOG_AT_(ANDROID_LOG_INFO,  BEDROCKQUEST_LOG_TAG, __VA_ARGS__)
#define LOGW(...) MCBEDVR_LOG_AT_(ANDROID_LOG_WARN,  BEDROCKQUEST_LOG_TAG, __VA_ARGS__)
#define LOGE(...) MCBEDVR_LOG_AT_(ANDROID_LOG_ERROR, BEDROCKQUEST_LOG_TAG, __VA_ARGS__)

// Macros por subsistema — preferir estas para que `adb logcat -s bedrockquest.jni:I`
// funcione desde el primer boot.
#define LOG_JNI(...)    MCBEDVR_LOG_AT_(ANDROID_LOG_INFO,  "bedrockquest.jni",    __VA_ARGS__)
#define LOG_JNIW(...)   MCBEDVR_LOG_AT_(ANDROID_LOG_WARN,  "bedrockquest.jni",    __VA_ARGS__)
#define LOG_JNIE(...)   MCBEDVR_LOG_AT_(ANDROID_LOG_ERROR, "bedrockquest.jni",    __VA_ARGS__)

#define LOG_INPUT(...)  MCBEDVR_LOG_AT_(ANDROID_LOG_INFO,  "bedrockquest.input",  __VA_ARGS__)
#define LOG_INPUTW(...) MCBEDVR_LOG_AT_(ANDROID_LOG_WARN,  "bedrockquest.input",  __VA_ARGS__)
#define LOG_INPUTE(...) MCBEDVR_LOG_AT_(ANDROID_LOG_ERROR, "bedrockquest.input",  __VA_ARGS__)

#define LOG_TICK(...)   MCBEDVR_LOG_AT_(ANDROID_LOG_INFO,  "bedrockquest.tick",   __VA_ARGS__)
#define LOG_TICKW(...)  MCBEDVR_LOG_AT_(ANDROID_LOG_WARN,  "bedrockquest.tick",   __VA_ARGS__)
#define LOG_TICKE(...)  MCBEDVR_LOG_AT_(ANDROID_LOG_ERROR, "bedrockquest.tick",   __VA_ARGS__)

#define LOG_SWAPPY(...)  MCBEDVR_LOG_AT_(ANDROID_LOG_INFO,  "bedrockquest.swappy", __VA_ARGS__)
#define LOG_SWAPPYW(...) MCBEDVR_LOG_AT_(ANDROID_LOG_WARN,  "bedrockquest.swappy", __VA_ARGS__)
#define LOG_SWAPPYE(...) MCBEDVR_LOG_AT_(ANDROID_LOG_ERROR, "bedrockquest.swappy", __VA_ARGS__)

#define LOG_XR(...)     MCBEDVR_LOG_AT_(ANDROID_LOG_INFO,  "bedrockquest.xr",     __VA_ARGS__)
#define LOG_XRW(...)    MCBEDVR_LOG_AT_(ANDROID_LOG_WARN,  "bedrockquest.xr",     __VA_ARGS__)
#define LOG_XRE(...)    MCBEDVR_LOG_AT_(ANDROID_LOG_ERROR, "bedrockquest.xr",     __VA_ARGS__)

#define LOG_HOOK(...)   MCBEDVR_LOG_AT_(ANDROID_LOG_INFO,  "bedrockquest.hook",   __VA_ARGS__)
#define LOG_HOOKW(...)  MCBEDVR_LOG_AT_(ANDROID_LOG_WARN,  "bedrockquest.hook",   __VA_ARGS__)
#define LOG_HOOKE(...)  MCBEDVR_LOG_AT_(ANDROID_LOG_ERROR, "bedrockquest.hook",   __VA_ARGS__)

#define LOG_SYM(...)    MCBEDVR_LOG_AT_(ANDROID_LOG_INFO,  "bedrockquest.sym",    __VA_ARGS__)
#define LOG_SYMW(...)   MCBEDVR_LOG_AT_(ANDROID_LOG_WARN,  "bedrockquest.sym",    __VA_ARGS__)
#define LOG_SYME(...)   MCBEDVR_LOG_AT_(ANDROID_LOG_ERROR, "bedrockquest.sym",    __VA_ARGS__)

#else  // !__ANDROID__ (host/macOS para CI)

#define MCBEDVR_LOG_AT_HOST_(level, tag, ...)                                  \
    do {                                                                      \
        const char* lvl = "";                                                 \
        switch (level) {                                                      \
            case ANDROID_LOG_INFO:  lvl = "I"; break;                         \
            case ANDROID_LOG_WARN:  lvl = "W"; break;                         \
            case ANDROID_LOG_ERROR: lvl = "E"; break;                         \
        }                                                                     \
        std::fprintf(stderr, "[%s %s] ", tag, lvl);                           \
        std::fprintf(stderr, __VA_ARGS__);                                    \
        std::fputc('\n', stderr);                                             \
    } while (0)

// Define ANDROID_LOG_* locally since we don't include <android/log.h>.
#ifndef ANDROID_LOG_INFO
#define ANDROID_LOG_INFO  4
#endif
#ifndef ANDROID_LOG_WARN
#define ANDROID_LOG_WARN  5
#endif
#ifndef ANDROID_LOG_ERROR
#define ANDROID_LOG_ERROR 6
#endif

#define LOGI(...) MCBEDVR_LOG_AT_HOST_(ANDROID_LOG_INFO,  "bedrockquest", __VA_ARGS__)
#define LOGW(...) MCBEDVR_LOG_AT_HOST_(ANDROID_LOG_WARN,  "bedrockquest", __VA_ARGS__)
#define LOGE(...) MCBEDVR_LOG_AT_HOST_(ANDROID_LOG_ERROR, "bedrockquest", __VA_ARGS__)

#define LOG_JNI(...)    MCBEDVR_LOG_AT_HOST_(ANDROID_LOG_INFO,  "bedrockquest.jni",    __VA_ARGS__)
#define LOG_JNIW(...)   MCBEDVR_LOG_AT_HOST_(ANDROID_LOG_WARN,  "bedrockquest.jni",    __VA_ARGS__)
#define LOG_JNIE(...)   MCBEDVR_LOG_AT_HOST_(ANDROID_LOG_ERROR, "bedrockquest.jni",    __VA_ARGS__)

#define LOG_INPUT(...)  MCBEDVR_LOG_AT_HOST_(ANDROID_LOG_INFO,  "bedrockquest.input",  __VA_ARGS__)
#define LOG_INPUTW(...) MCBEDVR_LOG_AT_HOST_(ANDROID_LOG_WARN,  "bedrockquest.input",  __VA_ARGS__)
#define LOG_INPUTE(...) MCBEDVR_LOG_AT_HOST_(ANDROID_LOG_ERROR, "bedrockquest.input",  __VA_ARGS__)

#define LOG_TICK(...)   MCBEDVR_LOG_AT_HOST_(ANDROID_LOG_INFO,  "bedrockquest.tick",   __VA_ARGS__)
#define LOG_TICKW(...)  MCBEDVR_LOG_AT_HOST_(ANDROID_LOG_WARN,  "bedrockquest.tick",   __VA_ARGS__)
#define LOG_TICKE(...)  MCBEDVR_LOG_AT_HOST_(ANDROID_LOG_ERROR, "bedrockquest.tick",   __VA_ARGS__)

#define LOG_SWAPPY(...)  MCBEDVR_LOG_AT_HOST_(ANDROID_LOG_INFO,  "bedrockquest.swappy", __VA_ARGS__)
#define LOG_SWAPPYW(...) MCBEDVR_LOG_AT_HOST_(ANDROID_LOG_WARN,  "bedrockquest.swappy", __VA_ARGS__)
#define LOG_SWAPPYE(...) MCBEDVR_LOG_AT_HOST_(ANDROID_LOG_ERROR, "bedrockquest.swappy", __VA_ARGS__)

#define LOG_XR(...)     MCBEDVR_LOG_AT_HOST_(ANDROID_LOG_INFO,  "bedrockquest.xr",     __VA_ARGS__)
#define LOG_XRW(...)    MCBEDVR_LOG_AT_HOST_(ANDROID_LOG_WARN,  "bedrockquest.xr",     __VA_ARGS__)
#define LOG_XRE(...)    MCBEDVR_LOG_AT_HOST_(ANDROID_LOG_ERROR, "bedrockquest.xr",     __VA_ARGS__)

#define LOG_HOOK(...)   MCBEDVR_LOG_AT_HOST_(ANDROID_LOG_INFO,  "bedrockquest.hook",   __VA_ARGS__)
#define LOG_HOOKW(...)  MCBEDVR_LOG_AT_HOST_(ANDROID_LOG_WARN,  "bedrockquest.hook",   __VA_ARGS__)
#define LOG_HOOKE(...)  MCBEDVR_LOG_AT_HOST_(ANDROID_LOG_ERROR, "bedrockquest.hook",   __VA_ARGS__)

#define LOG_SYM(...)    MCBEDVR_LOG_AT_HOST_(ANDROID_LOG_INFO,  "bedrockquest.sym",    __VA_ARGS__)
#define LOG_SYMW(...)   MCBEDVR_LOG_AT_HOST_(ANDROID_LOG_WARN,  "bedrockquest.sym",    __VA_ARGS__)
#define LOG_SYME(...)   MCBEDVR_LOG_AT_HOST_(ANDROID_LOG_ERROR, "bedrockquest.sym",    __VA_ARGS__)

#endif  // __ANDROID__