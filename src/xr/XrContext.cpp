#include "xr/XrContext.h"
#include "xr/BlitEye.h"
#include "xr/PicoInput.h"
#include "xr/InputBridge.h"
#include "MCBedVr.h"
#include "Log.h"

#include <cmath>
#include <jni.h>
#include <dlfcn.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <vector>

namespace mcbedvr::xr {
namespace {

// ─── Estado global ──────────────────────────────────────────────────────

std::atomic<XrState> g_state{XrState::Uninitialized};
std::mutex           g_mtx;
XrContextInfo        g_info;
XrSwapchain          g_sc_handles[2]{XR_NULL_HANDLE, XR_NULL_HANDLE};
std::vector<XrSwapchainImageOpenGLESKHR> g_sc_images[2];
jobject              g_app_context{nullptr};
jobject              g_activity{nullptr};
std::atomic<XrSessionState> g_session_state{XR_SESSION_STATE_UNKNOWN};
std::atomic<bool>           g_session_running{false};
std::atomic<bool>           g_use_quad_layer{true};
std::atomic<bool>           s_resetTracking{true};
XrPosef                     s_quadPose = {
    .orientation = {0.0f, 0.0f, 0.0f, 1.0f},
    .position = {0.0f, 0.0f, -1.8f}
};

void RecenterQuadLayer(const XrPosef& hmdPose) {
    float qx = hmdPose.orientation.x;
    float qy = hmdPose.orientation.y;
    float qz = hmdPose.orientation.z;
    float qw = hmdPose.orientation.w;

    float siny = 2.0f * (qw * qy + qx * qz);
    float cosy = 1.0f - 2.0f * (qy * qy + qz * qz);
    float yaw = std::atan2(siny, cosy);

    float fx = -std::sin(yaw);
    float fz = -std::cos(yaw);

    s_quadPose.position.x = hmdPose.position.x + fx * 1.8f;
    s_quadPose.position.y = hmdPose.position.y;
    s_quadPose.position.z = hmdPose.position.z + fz * 1.8f;

    s_quadPose.orientation.x = 0.0f;
    s_quadPose.orientation.y = std::sin(yaw * 0.5f);
    s_quadPose.orientation.z = 0.0f;
    s_quadPose.orientation.w = std::cos(yaw * 0.5f);

    PicoInput::SetQuadPose(s_quadPose);
}

void PollEvents() {
    if (g_info.instance == XR_NULL_HANDLE) {
        return;
    }
    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER, nullptr};
    while (xrPollEvent(g_info.instance, &event) == XR_SUCCESS) {
        switch (event.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                auto* stateChanged = reinterpret_cast<XrEventDataSessionStateChanged*>(&event);
                if (stateChanged->session == g_info.session) {
                    LOG_XR("XrEvent: session state changed: %d -> %d",
                           (int)g_session_state.load(), (int)stateChanged->state);
                    g_session_state.store(stateChanged->state);

                    if (stateChanged->state == XR_SESSION_STATE_READY) {
                        XrSessionBeginInfo sbi{
                            .type = XR_TYPE_SESSION_BEGIN_INFO,
                            .next = nullptr,
                            .primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                        };
                        XrResult r = xrBeginSession(g_info.session, &sbi);
                        if (XR_SUCCEEDED(r)) {
                            g_session_running.store(true);
                            s_resetTracking.store(true);
                            InputBridge::SetPointerCapture(!g_use_quad_layer.load());
                            LOG_XR("xrBeginSession OK — session is now RUNNING");
                        } else {
                            LOG_XRE("xrBeginSession failed: %d", r);
                        }
                    } else if (stateChanged->state == XR_SESSION_STATE_STOPPING) {
                        xrEndSession(g_info.session);
                        g_session_running.store(false);
                        LOG_XR("xrEndSession OK — session STOPPED");
                    } else if (stateChanged->state == XR_SESSION_STATE_LOSS_PENDING ||
                               stateChanged->state == XR_SESSION_STATE_EXITING) {
                        g_session_running.store(false);
                        LOG_XRW("Session loss/exiting: %d", (int)stateChanged->state);
                    }
                }
                break;
            }
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
                LOG_XRW("XrEvent: INSTANCE_LOSS_PENDING");
                break;
            }
            default:
                break;
        }
        event = {XR_TYPE_EVENT_DATA_BUFFER, nullptr};
    }
}

jobject GetApplicationContext(JavaVM* vm) {
    if (!vm) return nullptr;
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK || !env) {
        return nullptr;
    }
    jclass activityThreadClass = env->FindClass("android/app/ActivityThread");
    if (!activityThreadClass) {
        env->ExceptionClear();
        return nullptr;
    }
    jmethodID currentAppMethod = env->GetStaticMethodID(
        activityThreadClass, "currentApplication", "()Landroid/app/Application;");
    if (!currentAppMethod) {
        env->ExceptionClear();
        return nullptr;
    }
    jobject app = env->CallStaticObjectMethod(activityThreadClass, currentAppMethod);
    if (!app) {
        env->ExceptionClear();
        return nullptr;
    }
    return env->NewGlobalRef(app);
}

bool InitializeLoader(JavaVM* vm, jobject context) {
#ifdef __ANDROID__
    if (!vm || !context) {
        LOG_XRW("InitializeLoader: vm (%p) or context (%p) is null, skipping", vm, context);
        return false;
    }
    PFN_xrInitializeLoaderKHR pfnInitializeLoaderKHR = nullptr;
    if (XR_SUCCEEDED(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
                                           reinterpret_cast<PFN_xrVoidFunction*>(&pfnInitializeLoaderKHR))) &&
        pfnInitializeLoaderKHR != nullptr) {
        XrLoaderInitInfoAndroidKHR loaderInit{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
        loaderInit.applicationVM = vm;
        loaderInit.applicationContext = context;
        XrResult r = pfnInitializeLoaderKHR(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&loaderInit));
        if (XR_FAILED(r)) {
            LOG_XRW("xrInitializeLoaderKHR returned %d", r);
            return false;
        } else {
            LOG_XR("xrInitializeLoaderKHR OK (vm=%p, context=%p)", vm, context);
            return true;
        }
    } else {
        LOG_XRW("xrInitializeLoaderKHR entrypoint not found in loader");
    }
#endif
    return true;
}

bool CreateInstance(JavaVM* vm, jobject context) {
    if (!vm || !context) {
        LOG_XRW("CreateInstance: vm (%p) or context (%p) is null, skipping", vm, context);
        return false;
    }
    uint32_t ext_count = 0;
    xrEnumerateInstanceExtensionProperties(nullptr, 0, &ext_count, nullptr);
    std::vector<XrExtensionProperties> ext_props(ext_count, {XR_TYPE_EXTENSION_PROPERTIES});
    if (ext_count > 0) {
        xrEnumerateInstanceExtensionProperties(nullptr, ext_count, &ext_count, ext_props.data());
    }

    auto is_supported = [&](const char* name) {
        for (const auto& e : ext_props) {
            if (std::strcmp(e.extensionName, name) == 0) return true;
        }
        return false;
    };

    std::vector<const char*> enabled_exts;
    if (is_supported(XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME)) {
        enabled_exts.push_back(XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME);
    }
    if (is_supported(XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME)) {
        enabled_exts.push_back(XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME);
    }
    if (is_supported(XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME)) {
        enabled_exts.push_back(XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME);
    }
    if (is_supported("XR_BD_controller_interaction")) {
        enabled_exts.push_back("XR_BD_controller_interaction");
    }

    LOG_XR("Enabling %zu OpenXR instance extensions", enabled_exts.size());

    XrInstanceCreateInfoAndroidKHR android_info{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    android_info.applicationVM = vm;
    android_info.applicationActivity = context;

    XrInstanceCreateInfo ici{
        .type = XR_TYPE_INSTANCE_CREATE_INFO,
        .next = (vm != nullptr) ? &android_info : nullptr,
        .createFlags = 0,
        .applicationInfo = {
            .applicationName = "mcbedvr",
            .applicationVersion = 1,
            .engineName = "mcbedvr",
            .engineVersion = 1,
            .apiVersion = XR_CURRENT_API_VERSION,
        },
        .enabledApiLayerCount = 0,
        .enabledApiLayerNames = nullptr,
        .enabledExtensionCount = static_cast<uint32_t>(enabled_exts.size()),
        .enabledExtensionNames = enabled_exts.data(),
    };

    XrResult r = xrCreateInstance(&ici, &g_info.instance);
    if (XR_FAILED(r)) {
        LOG_XRE("xrCreateInstance failed: %d", r);
        return false;
    }
    LOG_XR("instance %p created", (void*)g_info.instance);

    XrSystemGetInfo sgi{XR_TYPE_SYSTEM_GET_INFO};
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    r = xrGetSystem(g_info.instance, &sgi, &g_info.system_id);
    if (XR_FAILED(r)) {
        LOG_XRE("xrGetSystem failed: %d", r);
        return false;
    }
    LOG_XR("system_id %lu acquired", (unsigned long)g_info.system_id);
    return true;
}

EGLConfig ConfigOfContext(EGLDisplay display, EGLContext context) {
    EGLint configId = 0;
    if (eglQueryContext(display, context, EGL_CONFIG_ID, &configId) != EGL_TRUE) {
        LOG_XRE("eglQueryContext(EGL_CONFIG_ID) failed: 0x%x", eglGetError());
        return nullptr;
    }
    const EGLint attribs[] = {EGL_CONFIG_ID, configId, EGL_NONE};
    EGLConfig config = nullptr;
    EGLint count = 0;
    if (eglChooseConfig(display, attribs, &config, 1, &count) != EGL_TRUE || count != 1) {
        LOG_XRE("no EGLConfig with id %d", configId);
        return nullptr;
    }
    return config;
}

bool CreateColorSwapchain(int w, int h);

bool CreateSession() {
    if (g_info.session != XR_NULL_HANDLE) {
        return true;
    }
    if (g_info.instance == XR_NULL_HANDLE) {
        LOG_XRE("CreateSession called before instance created");
        return false;
    }

    PFN_xrGetOpenGLESGraphicsRequirementsKHR pfn_gles = nullptr;
    xrGetInstanceProcAddr(g_info.instance, "xrGetOpenGLESGraphicsRequirementsKHR",
                          reinterpret_cast<PFN_xrVoidFunction*>(&pfn_gles));
    if (pfn_gles) {
        XrGraphicsRequirementsOpenGLESKHR reqs{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
        pfn_gles(g_info.instance, g_info.system_id, &reqs);
    }

    EGLDisplay display = eglGetCurrentDisplay();
    EGLContext context = eglGetCurrentContext();
    if (display == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT) {
        LOG_XRW("CreateSession: sin EGLContext activo en este thread — difiriendo al render thread");
        return false;
    }

    EGLConfig config = ConfigOfContext(display, context);
    if (!config) {
        LOG_XRE("CreateSession: no se pudo obtener EGLConfig del contexto actual");
        return false;
    }

    g_info.egl_display = display;
    g_info.egl_context = context;

    XrGraphicsBindingOpenGLESAndroidKHR bind{
        .type = XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR,
        .next = nullptr,
        .display = display,
        .config  = config,
        .context = context,
    };

    XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
    sci.next = &bind;
    sci.systemId = g_info.system_id;
    XrResult r = xrCreateSession(g_info.instance, &sci, &g_info.session);
    if (XR_FAILED(r)) {
        LOG_XRE("xrCreateSession failed: %d", r);
        return false;
    }
    LOG_XR("session %p created on system %lu", (void*)g_info.session,
           (unsigned long)g_info.system_id);

    XrReferenceSpaceCreateInfo rsci{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    rsci.poseInReferenceSpace = {{0,0,0,1}, {0,0,0}};
    r = xrCreateReferenceSpace(g_info.session, &rsci, &g_info.app_space);
    if (XR_FAILED(r)) {
        LOG_XRE("xrCreateReferenceSpace failed: %d", r);
        return false;
    }

    PicoInput::Init(g_info.instance, g_info.session);
    BlitEye::Init();

    return CreateColorSwapchain(0, 0);
}


bool CreateColorSwapchain(int w, int h) {
    if (w <= 0 || h <= 0) {
        w = 2160;  // PICO 4 Ultra default per-eye
        h = 2160;
        LOG_XRW("CreateColorSwapchain dims inválidas — usando default %dx%d", w, h);
    }

    XrViewConfigurationProperties vcp{XR_TYPE_VIEW_CONFIGURATION_PROPERTIES};
    if (XR_FAILED(xrGetViewConfigurationProperties(
            g_info.instance, g_info.system_id,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, &vcp))) {
        LOG_XRE("no se pudo leer view configuration properties");
        return false;
    }

    uint32_t view_count = 0;
    if (XR_FAILED(xrEnumerateViewConfigurationViews(
            g_info.instance, g_info.system_id,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &view_count, nullptr)) ||
        view_count == 0) {
        LOG_XRE("enumerate view configurations devolvió 0");
        return false;
    }

    std::vector<XrViewConfigurationView> views(view_count,
        XrViewConfigurationView{XR_TYPE_VIEW_CONFIGURATION_VIEW});
    if (XR_FAILED(xrEnumerateViewConfigurationViews(
            g_info.instance, g_info.system_id,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, view_count,
            &view_count, views.data()))) {
        LOG_XRE("enumerate view configurations failed");
        return false;
    }

    // Tomamos dims del primer view (todos los views del PICO 4 Ultra son
    // idénticos en este perfil). Si el caller pasó (w,h) explícitos, los
    // respetamos — útil cuando Bedrock publicó dims antes del Init.
    if (g_info.sc_width == 0 || g_info.sc_height == 0) {
        g_info.sc_width  = static_cast<std::int32_t>(views[0].recommendedImageRectWidth);
        g_info.sc_height = static_cast<std::int32_t>(views[0].recommendedImageRectHeight);
    }
    w = g_info.sc_width;
    h = g_info.sc_height;

    for (uint32_t eye = 0; eye < 2; eye++) {
        XrSwapchainCreateInfo sci{
            .type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
            .next = nullptr,
            .createFlags = 0,
            .usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                          XR_SWAPCHAIN_USAGE_SAMPLED_BIT,
            .format = GL_RGBA8,
            .sampleCount = 1,
            .width  = static_cast<uint32_t>(w),
            .height = static_cast<uint32_t>(h),
            .faceCount = 1,
            .arraySize = 1,  // PICO: 1 layer per swapchain (texture arrays not supported in GLES)
            .mipCount = 1,
        };
        XrResult r = xrCreateSwapchain(g_info.session, &sci, &g_sc_handles[eye]);
        if (XR_FAILED(r)) {
            LOG_XRE("xrCreateSwapchain[%u](%dx%d) failed: %d", eye, w, h, r);
            return false;
        }

        uint32_t count = 0;
        xrEnumerateSwapchainImages(g_sc_handles[eye], 0, &count, nullptr);
        g_sc_images[eye].assign(count, XrSwapchainImageOpenGLESKHR{
            .type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR,
            .next = nullptr,
            .image = 0
        });
        if (count > 0) {
            xrEnumerateSwapchainImages(
                g_sc_handles[eye], count, &count,
                reinterpret_cast<XrSwapchainImageBaseHeader*>(g_sc_images[eye].data()));
        }
        LOG_XR("swapchain[%u] %p created (%dx%d, %u images)",
               eye, (void*)g_sc_handles[eye], w, h, count);
    }

    g_info.sc_handle = g_sc_handles[0];
    g_info.sc_image_count = static_cast<std::int32_t>(g_sc_images[0].size());
    return true;
}

void DestroyColorSwapchain() {
    for (uint32_t eye = 0; eye < 2; eye++) {
        if (g_sc_handles[eye] != XR_NULL_HANDLE) {
            xrDestroySwapchain(g_sc_handles[eye]);
            g_sc_handles[eye] = XR_NULL_HANDLE;
        }
        g_sc_images[eye].clear();
    }
    g_info.sc_handle = XR_NULL_HANDLE;
    g_info.sc_width = 0;
    g_info.sc_height = 0;
    g_info.sc_image_count = 0;
}

}  // namespace

// ─── API pública ────────────────────────────────────────────────────────

const char* XrStateName(XrState s) {
    switch (s) {
        case XrState::Uninitialized:    return "Uninitialized";
        case XrState::InstanceCreated:  return "InstanceCreated";
        case XrState::SessionActive:    return "SessionActive";
        case XrState::StereoRendering:  return "StereoRendering";
        case XrState::ShuttingDown:     return "ShuttingDown";
        case XrState::Failed:           return "Failed";
    }
    return "?";
}

XrState XrContextState() { return g_state.load(); }

bool    XrContextReady() { return g_state.load() == XrState::StereoRendering; }

const   XrContextInfo& GetXrContextInfo() { return g_info; }

XrSwapchain  XrContextColorSwapchain()             { return g_info.sc_handle; }
std::int32_t XrContextColorSwapchainWidth()        { return g_info.sc_width; }
std::int32_t XrContextColorSwapchainHeight()       { return g_info.sc_height; }
std::int32_t XrContextColorSwapchainImageCount()   { return g_info.sc_image_count; }

void XrContextSetActivity(JNIEnv* env, jobject activity) {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (!g_activity && activity && env) {
        g_activity = env->NewGlobalRef(activity);
        LOG_XR("XrContextSetActivity() — captured MainActivity global ref %p", g_activity);
    }
}

jobject XrContextGetActivity() {
    return g_activity;
}

XrState XrContextInit() {
    std::lock_guard<std::mutex> lk(g_mtx);
    auto cur = g_state.load();
    if (cur != XrState::Uninitialized) {
        LOG_XRW("XrContextInit() llamado con state=%s — noop",
                XrStateName(cur));
        return cur;
    }

    JavaVM* vm = mcbedvr::GetJavaVM();
    jobject ctx = g_activity;
    if (!ctx && vm) {
        g_app_context = GetApplicationContext(vm);
        ctx = g_app_context;
    }

    if (!vm || !ctx) {
        LOG_XRW("XrContextInit() — no JavaVM (%p) or Activity/Context (%p) available yet, deferring init", vm, ctx);
        return XrState::Uninitialized;
    }

    if (!InitializeLoader(vm, ctx)) {
        LOG_XRW("XrContextInit() — InitializeLoader failed, deferring");
        return XrState::Uninitialized;
    }

    if (!CreateInstance(vm, ctx)) {
        LOG_XRW("XrContextInit() — CreateInstance failed, deferring");
        return XrState::Uninitialized;
    }
    g_state.store(XrState::InstanceCreated);
    LOG_XR("XrContextInit() OK — state=InstanceCreated");

    // Intenta crear sesión si ya existe un EGLContext en este thread
    if (CreateSession()) {
        g_state.store(XrState::StereoRendering);
        LOG_XR("XrContextInit() — sesión y swapchain estéreo creadas de inmediato");
    } else {
        LOG_XR("XrContextInit() — sesión diferida hasta que el render thread active EGL");
    }

    return g_state.load();
}

bool XrContextEnsureSession() {
    std::lock_guard<std::mutex> lk(g_mtx);
    auto cur = g_state.load();
    if (cur == XrState::StereoRendering) {
        return true;
    }
    if (cur != XrState::InstanceCreated) {
        return false;
    }

    if (!CreateSession()) {
        return false;
    }

    g_state.store(XrState::StereoRendering);
    LOG_XR("XrContextEnsureSession() OK — state=StereoRendering");
    return true;
}

void XrContextShutdown() {
    std::lock_guard<std::mutex> lk(g_mtx);
    auto cur = g_state.load();
    if (cur == XrState::Uninitialized || cur == XrState::Failed) {
        return;
    }
    g_state.store(XrState::ShuttingDown);
    if (g_session_running.load() && g_info.session != XR_NULL_HANDLE) {
        xrEndSession(g_info.session);
    }
    g_session_running.store(false);
    g_session_state.store(XR_SESSION_STATE_UNKNOWN);
    mcbedvr::xr::PicoInput::Shutdown();
    mcbedvr::xr::BlitEye::Shutdown();
    DestroyColorSwapchain();
    if (g_info.app_space) xrDestroySpace(g_info.app_space);
    if (g_info.session)   xrDestroySession(g_info.session);
    if (g_info.instance)  xrDestroyInstance(g_info.instance);
    JavaVM* vm = mcbedvr::GetJavaVM();
    if (vm) {
        JNIEnv* env = nullptr;
        if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK && env) {
            if (g_app_context) {
                env->DeleteGlobalRef(g_app_context);
            }
            if (g_activity) {
                env->DeleteGlobalRef(g_activity);
            }
        }
    }
    g_app_context = nullptr;
    g_activity = nullptr;
    g_info = {};
    g_state.store(XrState::Uninitialized);
    LOG_XR("XrContextShutdown() OK");
}

bool XrContextApplySwapchainDims(int w, int h) {
    if (w <= 0 || h <= 0) {
        LOG_XRW("XrContextApplySwapchainDims(%dx%d) dims inválidas", w, h);
        return false;
    }
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_state.load() != XrState::StereoRendering) {
        LOG_XRW("XrContextApplySwapchainDims sin StereoRendering (state=%s)",
                XrStateName(g_state.load()));
        return false;
    }
    if (g_info.sc_width == w && g_info.sc_height == h &&
        g_info.sc_handle != XR_NULL_HANDLE) {
        LOG_XR("XrContextApplySwapchainDims(%dx%d) — noop", w, h);
        return true;
    }
    DestroyColorSwapchain();
    if (!CreateColorSwapchain(w, h)) {
        return false;
    }
    LOG_XR("swapchain reemplazada %dx%d (%d images)",
           w, h, g_info.sc_image_count);
    return true;
}

void SetQuadLayerMode(bool enable) {
    g_use_quad_layer.store(enable);
    s_resetTracking.store(true);
    InputBridge::SetPointerCapture(!enable);
    LOG_XR("Layer mode set to: %s", enable ? "QUAD (Cinema Screen)" : "PROJECTION (360 Immersive)");
}

bool GetQuadLayerMode() {
    return g_use_quad_layer.load();
}

void ToggleLayerMode() {
    bool next = !g_use_quad_layer.load();
    g_use_quad_layer.store(next);
    s_resetTracking.store(true);
    InputBridge::SetPointerCapture(!next);
    LOG_XR("Layer mode toggled to: %s", next ? "QUAD (Cinema Screen)" : "PROJECTION (360 Immersive)");
}

FrameResult XrContextSubmitFrame(const FrameRequest& /*req*/) {
    FrameResult out{};
    if (g_state.load() != XrState::StereoRendering) {
        return out;
    }

    PollEvents();

    if (!g_session_running.load()) {
        return out;
    }

    auto t0 = std::chrono::steady_clock::now();
    XrFrameWaitInfo fwi{XR_TYPE_FRAME_WAIT_INFO, nullptr};
    XrFrameState fs{XR_TYPE_FRAME_STATE, nullptr};
    XrResult r = xrWaitFrame(g_info.session, &fwi, &fs);
    if (XR_FAILED(r)) {
        LOG_XRE("xrWaitFrame failed: %d", r);
        out.xr_result = r;
        return out;
    }
    XrFrameBeginInfo fbi{XR_TYPE_FRAME_BEGIN_INFO, nullptr};
    r = xrBeginFrame(g_info.session, &fbi);
    if (XR_FAILED(r)) {
        LOG_XRE("xrBeginFrame failed: %d", r);
        out.xr_result = r;
        return out;
    }

    if (!fs.shouldRender) {
        XrFrameEndInfo fei{
            .type = XR_TYPE_FRAME_END_INFO,
            .next = nullptr,
            .displayTime = fs.predictedDisplayTime,
            .environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
            .layerCount = 0,
            .layers = nullptr
        };
        r = xrEndFrame(g_info.session, &fei);
        out.submitted = !XR_FAILED(r);
        out.xr_result = r;
        return out;
    }

    XrViewLocateInfo vli{
        .type = XR_TYPE_VIEW_LOCATE_INFO,
        .next = nullptr,
        .viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
        .displayTime = fs.predictedDisplayTime,
        .space = g_info.app_space
    };
    XrViewState vs{
        .type = XR_TYPE_VIEW_STATE,
        .next = nullptr,
        .viewStateFlags = 0
    };
    uint32_t viewCount = 2;
    XrView views[2]{
        {.type = XR_TYPE_VIEW, .next = nullptr, .pose = {{0,0,0,1}, {0,0,0}}, .fov = {0,0,0,0}},
        {.type = XR_TYPE_VIEW, .next = nullptr, .pose = {{0,0,0,1}, {0,0,0}}, .fov = {0,0,0,0}}
    };
    r = xrLocateViews(g_info.session, &vli, &vs, 2, &viewCount, views);
    if (XR_FAILED(r)) {
        LOG_XRW("xrLocateViews failed: %d", r);
    }
    static bool s_views_diag = false;
    if (!s_views_diag) {
        s_views_diag = true;
        LOG_XR("xrLocateViews count=%u, v0.fov=[%.2f,%.2f,%.2f,%.2f], v1.fov=[%.2f,%.2f,%.2f,%.2f]",
               viewCount,
               views[0].fov.angleLeft, views[0].fov.angleRight, views[0].fov.angleUp, views[0].fov.angleDown,
               views[1].fov.angleLeft, views[1].fov.angleRight, views[1].fov.angleUp, views[1].fov.angleDown);
    }

    // Auto-detect menu vs world gameplay from Bedrock's MainActivity.mCursorLocked
    bool inMenu = true;
    if (g_activity) {
        JNIEnv* env = nullptr;
        JavaVM* vm = mcbedvr::GetJavaVM();
        if (vm && vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK && env) {
            static jfieldID s_cursorLockedFid = nullptr;
            if (!s_cursorLockedFid) {
                jclass actCls = env->GetObjectClass(g_activity);
                if (actCls) {
                    s_cursorLockedFid = env->GetFieldID(actCls, "mCursorLocked", "Z");
                    env->DeleteLocalRef(actCls);
                }
            }
            if (s_cursorLockedFid) {
                jboolean isLocked = env->GetBooleanField(g_activity, s_cursorLockedFid);
                inMenu = (isLocked == JNI_FALSE);
            }
        }
    }

    static bool s_wasInMenu = true;
    if (inMenu != s_wasInMenu) {
        s_wasInMenu = inMenu;
        g_use_quad_layer.store(inMenu);
        LOG_XR("VR Layer Mode auto-switched by mCursorLocked -> %s",
               inMenu ? "CINEMA_QUAD (Menu)" : "IMMERSIVE_VR (World)");
        if (inMenu) {
            RecenterQuadLayer(views[0].pose);
        } else {
            s_resetTracking.store(true);
        }
    }

    // 6DoF In-Game Head Tracking (QuestCraft Parity)
    // Extract yaw and pitch angular rate from OpenXR HMD pose (views[0].pose.orientation) and feed
    // directly into Bedrock's Gamepad Right Stick (AXIS_Z / AXIS_RZ) via PicoInput::SyncActions.
    static float s_lastYaw = 0.0f;
    static float s_lastPitch = 0.0f;
    static bool s_hasLastAngles = false;
    static auto s_lastTime = std::chrono::steady_clock::now();

    float headYawRate = 0.0f;
    float headPitchRate = 0.0f;

    auto curTime = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(curTime - s_lastTime).count();
    s_lastTime = curTime;

    if (s_resetTracking.exchange(false) || g_use_quad_layer.load()) {
        s_hasLastAngles = false;
    } else if (views[0].pose.orientation.w != 0.0f || views[0].pose.orientation.x != 0.0f ||
               views[0].pose.orientation.y != 0.0f || views[0].pose.orientation.z != 0.0f) {
        float qx = views[0].pose.orientation.x;
        float qy = views[0].pose.orientation.y;
        float qz = views[0].pose.orientation.z;
        float qw = views[0].pose.orientation.w;

        // Yaw (around Y axis)
        float siny_cosp = 2.0f * (qw * qy + qx * qz);
        float cosy_cosp = 1.0f - 2.0f * (qy * qy + qz * qz);
        float curYaw = std::atan2(siny_cosp, cosy_cosp);

        // Pitch (around X axis)
        float sinp = 2.0f * (qw * qx - qy * qz);
        float curPitch = (std::abs(sinp) >= 1.0f)
            ? std::copysign(static_cast<float>(M_PI) / 2.0f, sinp)
            : std::asin(sinp);

        if (!s_hasLastAngles || dt > 0.1f || dt < 0.001f) {
            s_lastYaw = curYaw;
            s_lastPitch = curPitch;
            s_hasLastAngles = true;
        } else {
            float dyaw = curYaw - s_lastYaw;
            constexpr float kPi = 3.14159265358979323846f;
            while (dyaw > kPi) dyaw -= 2.0f * kPi;
            while (dyaw < -kPi) dyaw += 2.0f * kPi;

            float dpitch = curPitch - s_lastPitch;

            s_lastYaw = curYaw;
            s_lastPitch = curPitch;

            // In Bedrock Gamepad:
            // AXIS_Z (right stick X): positive turns right.
            // When turning head to the right: curYaw decreases (dyaw < 0).
            // So positive rate: -dyaw / dt.
            // Full stick deflection in Bedrock rotates at approx 3.2 rad/s (~183 deg/s).
            constexpr float kTurnSpeedMax = 3.2f;
            headYawRate = (-dyaw / dt) / kTurnSpeedMax;
            headPitchRate = (-dpitch / dt) / kTurnSpeedMax;

            headYawRate = std::clamp(headYawRate, -1.0f, 1.0f);
            headPitchRate = std::clamp(headPitchRate, -1.0f, 1.0f);

            // Also dispatch mouse look delta as secondary fallback
            constexpr float kLookSensitivity = 580.0f;
            float dx = -dyaw * kLookSensitivity;
            float dy = -dpitch * kLookSensitivity;

            if (std::abs(dx) > 0.05f || std::abs(dy) > 0.05f) {
                InputBridge::DispatchLookDelta(dx, dy);
            }
        }
    }

    // Sync PICO 4 Ultra actions with head rotation rates
    PicoInput::SyncActions(g_info.session, g_info.app_space, fs.predictedDisplayTime, headYawRate, headPitchRate);

    uint32_t imgIndex[2] = {0, 0};
    bool ok[2] = {false, false};

    for (uint32_t eye = 0; eye < 2; eye++) {
        r = xrAcquireSwapchainImage(g_sc_handles[eye], nullptr, &imgIndex[eye]);
        if (XR_FAILED(r)) {
            LOG_XRE("xrAcquireSwapchainImage[%u] failed: %d", eye, r);
        } else {
            XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, nullptr, 1000000000};
            r = xrWaitSwapchainImage(g_sc_handles[eye], &waitInfo);
            if (XR_FAILED(r)) {
                LOG_XRE("xrWaitSwapchainImage[%u] failed: %d", eye, r);
            } else {
                ok[eye] = true;
            }
        }
    }

    if (ok[0] && ok[1] && imgIndex[0] < g_sc_images[0].size() && imgIndex[1] < g_sc_images[1].size()) {
        mcbedvr::xr::BlitEye::BlitStereo(g_sc_images[0][imgIndex[0]].image,
                                         g_sc_images[1][imgIndex[1]].image,
                                         g_info.sc_width, g_info.sc_height,
                                         views);
    }

    for (uint32_t eye = 0; eye < 2; eye++) {
        if (ok[eye]) {
            XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO, nullptr};
            r = xrReleaseSwapchainImage(g_sc_handles[eye], &releaseInfo);
            if (XR_FAILED(r)) {
                LOG_XRE("xrReleaseSwapchainImage[%u] failed: %d", eye, r);
            }
        }
    }

    XrCompositionLayerProjectionView proj_views[2]{};
    proj_views[0].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
    proj_views[0].next = nullptr;
    proj_views[0].pose = views[0].pose;
    proj_views[0].fov = views[0].fov;
    proj_views[0].subImage.swapchain = g_sc_handles[0];
    proj_views[0].subImage.imageRect.offset = {0, 0};
    proj_views[0].subImage.imageRect.extent = {g_info.sc_width, g_info.sc_height};
    proj_views[0].subImage.imageArrayIndex = 0;

    proj_views[1].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
    proj_views[1].next = nullptr;
    proj_views[1].pose = views[1].pose;
    proj_views[1].fov = views[1].fov;
    proj_views[1].subImage.swapchain = g_sc_handles[1];
    proj_views[1].subImage.imageRect.offset = {0, 0};
    proj_views[1].subImage.imageRect.extent = {g_info.sc_width, g_info.sc_height};
    proj_views[1].subImage.imageArrayIndex = 0;

    XrCompositionLayerProjection proj_layer{
        .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
        .next = nullptr,
        .layerFlags = 0,
        .space = g_info.app_space,
        .viewCount = 2,
        .views = proj_views
    };

    XrCompositionLayerQuad quad_layer{
        .type = XR_TYPE_COMPOSITION_LAYER_QUAD,
        .next = nullptr,
        .layerFlags = 0,
        .space = g_info.app_space,
        .eyeVisibility = XR_EYE_VISIBILITY_BOTH,
        .subImage = {
            .swapchain = g_sc_handles[0],
            .imageRect = {
                .offset = {0, 0},
                .extent = {g_info.sc_width, g_info.sc_height}
            },
            .imageArrayIndex = 0
        },
        .pose = s_quadPose,
        .size = {2.4f, 1.35f} // 16:9 aspect ratio matching 1920x1080 / 4320x2160
    };

    const XrCompositionLayerBaseHeader* layers[1] = {
        g_use_quad_layer.load()
            ? reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad_layer)
            : reinterpret_cast<const XrCompositionLayerBaseHeader*>(&proj_layer)
    };

    XrFrameEndInfo fei{
        .type = XR_TYPE_FRAME_END_INFO,
        .next = nullptr,
        .displayTime = fs.predictedDisplayTime,
        .environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
        .layerCount = 1,
        .layers = layers
    };

    r = xrEndFrame(g_info.session, &fei);
    auto t1 = std::chrono::steady_clock::now();
    out.submitted = !XR_FAILED(r);
    out.xr_result = r;
    out.duration_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    static uint32_t s_frame_log_counter = 0;
    if (out.submitted) {
        if (++s_frame_log_counter % 90 == 1) {
            LOG_XR("stereo frame #%u OK in %lldus (period=%llu ns)",
                   s_frame_log_counter,
                   (long long)out.duration_us,
                   (unsigned long long)fs.predictedDisplayPeriod);
        }
    } else {
        LOG_XRE("xrEndFrame failed: %d", r);
    }
    return out;
}

void XrContextDumpDiagnostics() {
    LOG_XR("==== XrContext diagnostics ====");
    LOG_XR("  state        : %s", XrStateName(g_state.load()));
    LOG_XR("  instance     : %p", (void*)g_info.instance);
    LOG_XR("  session      : %p", (void*)g_info.session);
    LOG_XR("  app_space    : %p", (void*)g_info.app_space);
    LOG_XR("  system_id    : %lu", (unsigned long)g_info.system_id);
    LOG_XR("  swapchain    : %p (%dx%d, %d images)",
           (void*)g_info.sc_handle, g_info.sc_width, g_info.sc_height,
           g_info.sc_image_count);
    LOG_XR("  EGL display  : %p", g_info.egl_display);
    LOG_XR("  EGL context  : %p", g_info.egl_context);
    LOG_XR("================================");
}

}  // namespace mcbedvr::xr
