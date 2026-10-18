// Symbols.cpp — Resolver de entradas JNI de libminecraftpe.so + parchador de hooks.
//
// Estrategia Fase 2 (post-REA 1.26.52): los mangled names del motor están
// ausentes de .dynsym (visibilidad oculta desde RenderDragon / 1.16+). Resolvemos
// los exports JNI oficiales (`Java_com_mojang_minecraftpe_MainActivity_native*`,
// `Java_com_mojang_minecraftpe_input_JellyBeanDeviceManager_*`) que SÍ están en
// .dynsym porque Android los necesita para el bridge Java<->C++. Ver
// tools/offset_map_consolidated.json §jni_surface para el catálogo completo.
//
// Resolver un JNI export requiere dlopen() del binario Bedrock. En runtime, cuando
// mcbedvr.so corre desde JNI_OnLoad, libminecraftpe.so YA está mapeado en el
// proceso (Mojang carga primero la .so de Bedrock vía System.loadLibrary desde
// su Application.onCreate()). Por eso usamos dlopen(RTLD_NOLOAD): conseguimos
// handle sin re-mapear. Si NOLOAD falla (caso edge: mcbedvr cargado vía un
// mecanismo que llega antes que libminecraftpe.so), caemos a RTLD_NOW|LOCAL.

#include "Symbols.h"
#include "MCBedVr.h"
#include "Log.h"
#include "xr/XrContext.h"

#include <EGL/egl.h>
#include <dlfcn.h>
#include <link.h>

#include <array>
#include <cstring>
#include <string>

#ifdef MCBEDVR_HAS_DOBBY
// Dobby expone un hook engine minimalista: DobbyHook(addr, replacement, &original).
// Internamente hace mprotect + branch island + flush icache. ARM64-safe.
#include <dobby.h>

typedef EGLBoolean (*eglSwapBuffers_fn)(EGLDisplay dpy, EGLSurface surf);
static eglSwapBuffers_fn g_orig_eglSwapBuffers = nullptr;

static EGLBoolean Hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surf) {
    if (mcbedvr::xr::XrContextState() == mcbedvr::xr::XrState::Uninitialized) {
        mcbedvr::xr::XrContextInit();
    }
    if (mcbedvr::xr::XrContextState() == mcbedvr::xr::XrState::InstanceCreated) {
        mcbedvr::xr::XrContextEnsureSession();
    }
    if (mcbedvr::xr::XrContextReady()) {
        mcbedvr::xr::XrContextSubmitFrame({});
    }
    if (g_orig_eglSwapBuffers) {
        return g_orig_eglSwapBuffers(dpy, surf);
    }
    return EGL_TRUE;
}
#endif

namespace mcbedvr::sym {
namespace {

GameSymbols g_table{};

// Una entrada del catálogo. El nombre completo del JNI export es el contrato;
// `required` decide si un fallo aborta el inyector o sólo degrada.
struct Entry {
    const char* jni_name;   // "Java_com_mojang_minecraftpe_MainActivity_nativeResize"
    JniEntry*   slot;
    bool        required;
    const char* kind;       // sólo para logging ("lifecycle" / "input" / ...)
};

// Catálogo de los 8 JNI exports que necesitamos resolver. Los offsets confirmados
// vienen de tools/offset_map_consolidated.json §jni_surface.lifecycle_jnis y
// §jni_surface.groups. Los nombres de los symbols coinciden EXACTAMENTE con lo
// que reporta `nm -D --defined-only libminecraftpe.so` (verificado por Gabriel).
constexpr std::array<Entry, 9> kJniEntries{{
    // ---- MainActivity lifecycle (5) ----
    {"Java_com_mojang_minecraftpe_MainActivity_nativeResize",
        &g_table.nativeResize,                 /*required*/true,  "lifecycle"},
    {"Java_com_mojang_minecraftpe_MainActivity_nativeSuspend",
        &g_table.nativeSuspend,                /*required*/false, "lifecycle"},
    {"Java_com_mojang_minecraftpe_MainActivity_nativeOnDestroy",
        &g_table.nativeOnDestroy,              /*required*/false, "lifecycle"},
    {"Java_com_mojang_minecraftpe_MainActivity_nativeStopThis",
        &g_table.nativeStopThis,               /*required*/false, "lifecycle"},
    {"Java_com_mojang_minecraftpe_MainActivity_nativeSuspendGameplayUpdates",
        &g_table.nativeSuspendGameplayUpdates, /*required*/false, "lifecycle"},
    {"Java_com_mojang_minecraftpe_MainActivity_nativeSetHeadphonesConnected",
        &g_table.nativeSetHeadphonesConnected, /*required*/false, "lifecycle"},
    // ---- JellyBeanDeviceManager input (3) ----
    {"Java_com_mojang_minecraftpe_input_JellyBeanDeviceManager_onInputDeviceAddedNative",
        &g_table.onInputDeviceAddedNative,     /*required*/false, "input"},
    {"Java_com_mojang_minecraftpe_input_JellyBeanDeviceManager_setControllerDetailsNative",
        &g_table.setControllerDetailsNative,   /*required*/false, "input"},
    {"Java_com_mojang_minecraftpe_input_JellyBeanDeviceManager_setFoundXboxControllerNative",
        &g_table.setFoundXboxControllerNative, /*required*/false, "input"},
}};

bool ResolveOne(void* handle, const Entry& e) {
    ::dlerror();
    void* addr = ::dlsym(handle, e.jni_name);
    if (addr != nullptr) {
        e.slot->fn     = addr;
        e.slot->orig   = nullptr;  // se completa en InstallNativeHooks
        e.slot->status = SymbolStatus::Resolved;
        LOGI("  [%-9s] %-78s %p", e.kind, e.jni_name, addr);
        return true;
    }
    const char* err = ::dlerror();
    e.slot->status = SymbolStatus::Missing;
    if (e.required) {
        LOGE("  [%-9s] %-78s MISSING REQUIRED (%s)",
             e.kind, e.jni_name, err ? err : "no error text");
    } else {
        LOGW("  [%-9s] %-78s MISSING optional (%s)",
             e.kind, e.jni_name, err ? err : "no error text");
    }
    return !e.required;
}

// Igual que savr: dl_iterate_phdr en vez de dlinfo (más portable entre API levels).
int MatchLibMinecraft(dl_phdr_info* info, std::size_t, void* out) {
    if (info->dlpi_name == nullptr) return 0;
    const char* slash = std::strrchr(info->dlpi_name, '/');
    const char* name  = slash ? slash + 1 : info->dlpi_name;
    if (std::strcmp(name, kTargetLibrary) != 0) return 0;
    *static_cast<std::uintptr_t*>(out) =
        static_cast<std::uintptr_t>(info->dlpi_addr);
    return 1;  // stop iterating
}

// Dobby is used strictly for eglSwapBuffers to intercept frames before present.
// JNI methods are intercepted via RegisterNatives A1 in JNI_Hooks.cpp.

}  // namespace

GameSymbols& g() { return g_table; }

uintptr_t FindLoadBase() {
    std::uintptr_t base = 0;
    ::dl_iterate_phdr(MatchLibMinecraft, &base);
    return base;
}

int ResolveGameSymbols() {
    // (a) Intentar agarrar el handle del binario YA cargado (camino normal).
    //     NOLOAD evita re-map; si libminecraftpe.so no está en memoria, falla.
    void* handle = ::dlopen(kTargetLibrary, RTLD_NOW | RTLD_NOLOAD);
    if (!handle) {
        // (b) Fallback: cargar normalmente. Esto puede pasar si mcbedvr.so es
        //     inyectado antes de que Bedrock se cargue (vía ld_preload por ej).
        LOGW("sym: dlopen(NOLOAD) falló (%s) — fallback a RTLD_NOW",
             ::dlerror());
        handle = ::dlopen(kTargetLibrary, RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            LOGE("sym: dlopen(\"%s\") failed: %s — inyector no se puede montar",
                 kTargetLibrary, ::dlerror());
            return -1;
        }
    }
    LOGI("sym: %s handle=%p, base=%p", kTargetLibrary, handle,
         reinterpret_cast<void*>(FindLoadBase()));

    int required_missing = 0;
    int resolved = 0;
    for (const auto& e : kJniEntries) {
        bool ok = ResolveOne(handle, e);
        if (ok) ++resolved;
        else if (e.required) ++required_missing;
    }
    LOGI("sym: %d/%d JNI exports resolved, %d required missing",
         resolved, static_cast<int>(kJniEntries.size()), required_missing);

    // No cerramos el handle: Bedrock puede seguir usándolo. savr tampoco lo hace.
    return required_missing;
}

int InstallNativeHooks() {
#ifdef MCBEDVR_HAS_DOBBY
    LOGI("hook: instalando trampolines JNI-side vía Dobby");
    int installed = 0;

    void* egl_sym = ::dlsym(RTLD_DEFAULT, "eglSwapBuffers");
    if (!egl_sym) {
        void* libegl = ::dlopen("libEGL.so", RTLD_NOW | RTLD_GLOBAL);
        if (libegl) {
            egl_sym = ::dlsym(libegl, "eglSwapBuffers");
        }
    }
    if (egl_sym) {
        if (DobbyHook(egl_sym, reinterpret_cast<void*>(&Hook_eglSwapBuffers),
                      reinterpret_cast<void**>(&g_orig_eglSwapBuffers)) == 0) {
            LOGI("hook: eglSwapBuffers hooked successfully (%p -> %p)",
                 egl_sym, reinterpret_cast<void*>(&Hook_eglSwapBuffers));
            ++installed;
        } else {
            LOGW("hook: DobbyHook failed for eglSwapBuffers");
        }
    }

    // Lifecycle methods (nativeResize, nativeSuspend, nativeOnDestroy, nativeStopThis)
    // are cleanly intercepted via RegisterNatives A1 in JNI_Hooks.cpp without memory patching.
    // Dobby MUST NOT hook them because these functions are 4-byte stubs situated right next
    // to each other (e.g. 0x33c, 0x340, 0x344, 0x348) and a 20-byte Dobby trampoline would
    // corrupt adjacent function bytes.
    LOGI("hook: %d hooks instalados (eglSwapBuffers)", installed);
    return installed;
#else
    LOGW("hook: Dobby no disponible — inyector queda en observer mode "
         "(symbols resueltos, hooks no parchados)");
    return 0;
#endif
}

void DumpGameSymbols() {
    auto row = [](const char* name, const JniEntry& e) {
        const char* st = "?";
        switch (e.status) {
            case SymbolStatus::Missing:  st = "MISS"; break;
            case SymbolStatus::Resolved: st = "RES "; break;
            case SymbolStatus::Hooked:   st = "HOOK"; break;
        }
        LOGI("  %-32s %s fn=%-18p orig=%-18p", name, st, e.fn, e.orig);
    };
    LOGI("sym: === libminecraftpe.so JNI exports ===");
    row("nativeResize",                  g_table.nativeResize);
    row("nativeSuspend",                 g_table.nativeSuspend);
    row("nativeOnDestroy",               g_table.nativeOnDestroy);
    row("nativeStopThis",                g_table.nativeStopThis);
    row("nativeSuspendGameplayUpdates",  g_table.nativeSuspendGameplayUpdates);
    row("nativeSetHeadphonesConnected",  g_table.nativeSetHeadphonesConnected);
    row("onInputDeviceAddedNative",      g_table.onInputDeviceAddedNative);
    row("setControllerDetailsNative",    g_table.setControllerDetailsNative);
    row("setFoundXboxControllerNative",  g_table.setFoundXboxControllerNative);
    LOGI("  %-32s %s", "MinecraftGame_ctor",
         g_table.MinecraftGame_ctor ? "RES " : "    ");
    LOGI("  %-32s %s", "LevelRendererPlayer_render",
         g_table.LevelRendererPlayer_render ? "RES " : "    ");
    LOGI("sym: ====================");
}

}  // namespace mcbedvr::sym