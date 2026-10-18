#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <cmath>
#include <cstring>
#include <vector>
#include <chrono>
#include <string>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

#define XR_USE_PLATFORM_ANDROID 1
#define XR_USE_GRAPHICS_API_OPENGL_ES 1
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "VrApi.h"
#include "VrApi_Types.h"
#include "VrApi_Helpers.h"
#include "VrApi_Input.h"
#include "VrApi_SystemUtils.h"

#define LOG_TAG "mcbedvr_vrapi"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

//---------------------------------------------------------------------------
// OpenXR State & Definitions
//---------------------------------------------------------------------------
struct TextureSwapChainImpl {
    ovrTextureType type;
    ovrTextureFormat format;
    int width;
    int height;
    int levels;
    int bufferCount;
    std::vector<GLuint> textures;
};

struct ControllerInputState {
    bool connected = false;
    float thumbstick[2] = {0.f, 0.f};
    bool thumbstickClick = false;
    float trigger = 0.f;
    bool triggerClick = false;
    float grip = 0.f;
    bool gripClick = false;
    bool buttonPrimary = false;   // A (right) / X (left)
    bool buttonSecondary = false; // B (right) / Y (left)
    bool menu = false;
    XrPosef pose{{0, 0, 0, 1}, {0, 0, 0}};
    bool poseValid = false;
};

struct BridgeState {
    JavaVM* vm = nullptr;
    jobject activity = nullptr;
    jclass activityCls = nullptr;
    jmethodID dispatchKeyEventMethod = nullptr;
    jmethodID dispatchGenericMotionEventMethod = nullptr;

    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId systemId = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace appSpace = XR_NULL_HANDLE;
    XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
    bool sessionRunning = false;

    // OpenXR Stereo Swapchain (Texture 2D Array: layer 0 = Left, layer 1 = Right)
    XrSwapchain xrSwapchain = XR_NULL_HANDLE;
    uint32_t scWidth = 1920;
    uint32_t scHeight = 1920;
    std::vector<XrSwapchainImageOpenGLESKHR> scImages;

    // Frame timing & views
    XrFrameState frameState{XR_TYPE_FRAME_STATE};
    bool frameBegun = false;
    XrView views[2]{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
    XrViewState viewState{XR_TYPE_VIEW_STATE};

    // Actions & Input
    XrActionSet actionSet = XR_NULL_HANDLE;
    XrAction aimPoseAction = XR_NULL_HANDLE;
    XrAction thumbstickAction = XR_NULL_HANDLE;
    XrAction thumbstickClickAction = XR_NULL_HANDLE;
    XrAction triggerValAction = XR_NULL_HANDLE;
    XrAction triggerClickAction = XR_NULL_HANDLE;
    XrAction gripValAction = XR_NULL_HANDLE;
    XrAction gripClickAction = XR_NULL_HANDLE;
    XrAction buttonPrimaryAction = XR_NULL_HANDLE;
    XrAction buttonSecondaryAction = XR_NULL_HANDLE;
    XrAction menuAction = XR_NULL_HANDLE;
    XrAction hapticAction = XR_NULL_HANDLE;

    XrSpace handSpaces[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE}; // 0 = Left, 1 = Right
    XrPath handSubactionPaths[2] = {XR_NULL_PATH, XR_NULL_PATH};
    ControllerInputState controllers[2]; // 0 = Left, 1 = Right

    // Previous button states for edge detection
    ControllerInputState prevControllers[2];

    // Helper FBOs for blitting (separate read and draw to prevent driver attachment conflicts)
    GLuint readFbo = 0;
    GLuint drawFbo = 0;

    // Configurable properties
    int foveationLevel = 0;
    ovrTrackingSpace trackingSpace = VRAPI_TRACKING_SPACE_LOCAL;
};

} // namespace

struct ovrMobile {
    int dummy = 1;
};

namespace {
BridgeState g_bridge;
ovrMobile g_dummyMobile;

// OpenXR function pointers
PFN_xrGetOpenGLESGraphicsRequirementsKHR pfn_xrGetOpenGLESGraphicsRequirementsKHR = nullptr;

double GetTimeNowSeconds() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration<double>(now).count();
}

//---------------------------------------------------------------------------
// JNI Helpers for Android Input Dispatch
//---------------------------------------------------------------------------
JNIEnv* GetJniEnv() {
    if (!g_bridge.vm) return nullptr;
    JNIEnv* env = nullptr;
    jint res = g_bridge.vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (res == JNI_EDETACHED) {
        if (g_bridge.vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            return nullptr;
        }
    }
    return env;
}

void DispatchKeyEvent(int action, int keyCode) {
    JNIEnv* env = GetJniEnv();
    if (!env || !g_bridge.activity) return;

    jclass keyEventCls = env->FindClass("android/view/KeyEvent");
    if (!keyEventCls) return;
    jmethodID ctor = env->GetMethodID(keyEventCls, "<init>", "(II)V");
    if (!ctor) { env->DeleteLocalRef(keyEventCls); return; }

    jobject ev = env->NewObject(keyEventCls, ctor, action, keyCode);
    if (ev) {
        if (!g_bridge.dispatchKeyEventMethod) {
            jclass actCls = env->GetObjectClass(g_bridge.activity);
            g_bridge.dispatchKeyEventMethod = env->GetMethodID(actCls, "dispatchKeyEvent", "(Landroid/view/KeyEvent;)Z");
            env->DeleteLocalRef(actCls);
        }
        if (g_bridge.dispatchKeyEventMethod) {
            env->CallBooleanMethod(g_bridge.activity, g_bridge.dispatchKeyEventMethod, ev);
        }
        env->DeleteLocalRef(ev);
    }
    env->DeleteLocalRef(keyEventCls);
}

//---------------------------------------------------------------------------
// OpenXR Initialization & Setup
//---------------------------------------------------------------------------
bool InitOpenXRRuntime() {
    if (g_bridge.instance != XR_NULL_HANDLE) {
        return true;
    }

    LOGI("[vrapi] Initializing OpenXR Runtime on PICO 4 Ultra...");

    // 1. Initialize Loader via xrGetInstanceProcAddr
    PFN_xrInitializeLoaderKHR pfn_xrInitializeLoaderKHR = nullptr;
    xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction*)&pfn_xrInitializeLoaderKHR);
    if (pfn_xrInitializeLoaderKHR && g_bridge.vm && g_bridge.activity) {
        XrLoaderInitInfoAndroidKHR loaderInit{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
        loaderInit.applicationVM = g_bridge.vm;
        loaderInit.applicationContext = g_bridge.activity;
        XrResult lres = pfn_xrInitializeLoaderKHR(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&loaderInit));
        LOGI("[vrapi] xrInitializeLoaderKHR returned: %d (vm=%p, act=%p)", lres, g_bridge.vm, g_bridge.activity);
    } else {
        LOGE("[vrapi] xrInitializeLoaderKHR pointer=%p, vm=%p, act=%p", pfn_xrInitializeLoaderKHR, g_bridge.vm, g_bridge.activity);
    }

    // Enumerate supported instance extensions
    uint32_t extCount = 0;
    xrEnumerateInstanceExtensionProperties(nullptr, 0, &extCount, nullptr);
    LOGI("[vrapi] OpenXR supported instance extensions count: %u", extCount);
    if (extCount > 0) {
        std::vector<XrExtensionProperties> extProps(extCount, {XR_TYPE_EXTENSION_PROPERTIES});
        xrEnumerateInstanceExtensionProperties(nullptr, extCount, &extCount, extProps.data());
        for (const auto& ep : extProps) {
            LOGI("[vrapi]   Extension: %s (v%u)", ep.extensionName, ep.extensionVersion);
        }
    }

    // 2. Create OpenXR Instance with Android and GLES extensions
    const char* extensions[] = {
        XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
        XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME
    };

    XrInstanceCreateInfoAndroidKHR androidCreateInfo{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    androidCreateInfo.applicationVM = g_bridge.vm;
    androidCreateInfo.applicationActivity = g_bridge.activity;

    XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
    createInfo.next = &androidCreateInfo;
    strcpy(createInfo.applicationInfo.applicationName, "MinecraftVR");
    createInfo.applicationInfo.applicationVersion = 1;
    strcpy(createInfo.applicationInfo.engineName, "BedrockVR");
    createInfo.applicationInfo.engineVersion = 1;
    createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    createInfo.enabledExtensionCount = 2;
    createInfo.enabledExtensionNames = extensions;

    XrResult res = xrCreateInstance(&createInfo, &g_bridge.instance);
    if (res != XR_SUCCESS || g_bridge.instance == XR_NULL_HANDLE) {
        LOGE("[vrapi] FATAL: xrCreateInstance failed (XrResult=%d)", res);
        return false;
    }
    LOGI("[vrapi] OpenXR Instance created successfully (handle=%llu)", (unsigned long long)g_bridge.instance);

    // Resolve extension function
    xrGetInstanceProcAddr(g_bridge.instance, "xrGetOpenGLESGraphicsRequirementsKHR",
                          (PFN_xrVoidFunction*)&pfn_xrGetOpenGLESGraphicsRequirementsKHR);

    // 3. Get System ID
    XrSystemGetInfo sysGetInfo{XR_TYPE_SYSTEM_GET_INFO};
    sysGetInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    res = xrGetSystem(g_bridge.instance, &sysGetInfo, &g_bridge.systemId);
    if (res != XR_SUCCESS) {
        LOGE("[vrapi] xrGetSystem failed (XrResult=%d)", res);
        return false;
    }
    LOGI("[vrapi] Got OpenXR SystemId=%llu", (unsigned long long)g_bridge.systemId);

    // Query recommended view configuration
    uint32_t viewConfigCount = 0;
    xrEnumerateViewConfigurationViews(g_bridge.instance, g_bridge.systemId,
                                      XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                      0, &viewConfigCount, nullptr);
    if (viewConfigCount > 0) {
        std::vector<XrViewConfigurationView> configViews(viewConfigCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
        xrEnumerateViewConfigurationViews(g_bridge.instance, g_bridge.systemId,
                                          XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                          viewConfigCount, &viewConfigCount, configViews.data());
        g_bridge.scWidth = configViews[0].recommendedImageRectWidth;
        g_bridge.scHeight = configViews[0].recommendedImageRectHeight;
        LOGI("[vrapi] Recommended OpenXR eye resolution: %ux%u", g_bridge.scWidth, g_bridge.scHeight);
    }

    return true;
}

bool InitOpenXRActions() {
    if (g_bridge.actionSet != XR_NULL_HANDLE) return true;

    // Create Action Set
    XrActionSetCreateInfo setInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
    strcpy(setInfo.actionSetName, "minecraft_vr_actions");
    strcpy(setInfo.localizedActionSetName, "Minecraft VR Actions");
    setInfo.priority = 0;
    xrCreateActionSet(g_bridge.instance, &setInfo, &g_bridge.actionSet);

    xrStringToPath(g_bridge.instance, "/user/hand/left", &g_bridge.handSubactionPaths[0]);
    xrStringToPath(g_bridge.instance, "/user/hand/right", &g_bridge.handSubactionPaths[1]);

    auto CreateAction = [&](const char* name, const char* locName, XrActionType type,
                            XrAction& outAction, bool subaction) {
        XrActionCreateInfo actInfo{XR_TYPE_ACTION_CREATE_INFO};
        strcpy(actInfo.actionName, name);
        strcpy(actInfo.localizedActionName, locName);
        actInfo.actionType = type;
        if (subaction) {
            actInfo.countSubactionPaths = 2;
            actInfo.subactionPaths = g_bridge.handSubactionPaths;
        }
        xrCreateAction(g_bridge.actionSet, &actInfo, &outAction);
    };

    CreateAction("aim_pose", "Aim Pose", XR_ACTION_TYPE_POSE_INPUT, g_bridge.aimPoseAction, true);
    CreateAction("thumbstick", "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT, g_bridge.thumbstickAction, true);
    CreateAction("thumbstick_click", "Thumbstick Click", XR_ACTION_TYPE_BOOLEAN_INPUT, g_bridge.thumbstickClickAction, true);
    CreateAction("trigger_value", "Trigger Value", XR_ACTION_TYPE_FLOAT_INPUT, g_bridge.triggerValAction, true);
    CreateAction("trigger_click", "Trigger Click", XR_ACTION_TYPE_BOOLEAN_INPUT, g_bridge.triggerClickAction, true);
    CreateAction("grip_value", "Grip Value", XR_ACTION_TYPE_FLOAT_INPUT, g_bridge.gripValAction, true);
    CreateAction("grip_click", "Grip Click", XR_ACTION_TYPE_BOOLEAN_INPUT, g_bridge.gripClickAction, true);
    CreateAction("button_primary", "Button Primary (A/X)", XR_ACTION_TYPE_BOOLEAN_INPUT, g_bridge.buttonPrimaryAction, true);
    CreateAction("button_secondary", "Button Secondary (B/Y)", XR_ACTION_TYPE_BOOLEAN_INPUT, g_bridge.buttonSecondaryAction, true);
    CreateAction("menu_button", "Menu Button", XR_ACTION_TYPE_BOOLEAN_INPUT, g_bridge.menuAction, true);
    CreateAction("haptic", "Haptic", XR_ACTION_TYPE_VIBRATION_OUTPUT, g_bridge.hapticAction, true);

    // Bindings for PICO 4 controllers
    auto Bind = [&](const char* pathStr, XrAction action) -> XrActionSuggestedBinding {
        XrActionSuggestedBinding b;
        b.action = action;
        xrStringToPath(g_bridge.instance, pathStr, &b.binding);
        return b;
    };

    std::vector<XrActionSuggestedBinding> bindings = {
        Bind("/user/hand/left/input/aim/pose", g_bridge.aimPoseAction),
        Bind("/user/hand/right/input/aim/pose", g_bridge.aimPoseAction),
        Bind("/user/hand/left/input/thumbstick", g_bridge.thumbstickAction),
        Bind("/user/hand/right/input/thumbstick", g_bridge.thumbstickAction),
        Bind("/user/hand/left/input/thumbstick/click", g_bridge.thumbstickClickAction),
        Bind("/user/hand/right/input/thumbstick/click", g_bridge.thumbstickClickAction),
        Bind("/user/hand/left/input/trigger/value", g_bridge.triggerValAction),
        Bind("/user/hand/right/input/trigger/value", g_bridge.triggerValAction),
        Bind("/user/hand/left/input/trigger/click", g_bridge.triggerClickAction),
        Bind("/user/hand/right/input/trigger/click", g_bridge.triggerClickAction),
        Bind("/user/hand/left/input/squeeze/value", g_bridge.gripValAction),
        Bind("/user/hand/right/input/squeeze/value", g_bridge.gripValAction),
        Bind("/user/hand/left/input/squeeze/click", g_bridge.gripClickAction),
        Bind("/user/hand/right/input/squeeze/click", g_bridge.gripClickAction),
        Bind("/user/hand/left/input/x/click", g_bridge.buttonPrimaryAction),
        Bind("/user/hand/right/input/a/click", g_bridge.buttonPrimaryAction),
        Bind("/user/hand/left/input/y/click", g_bridge.buttonSecondaryAction),
        Bind("/user/hand/right/input/b/click", g_bridge.buttonSecondaryAction),
        Bind("/user/hand/left/input/menu/click", g_bridge.menuAction),
        Bind("/user/hand/left/output/haptic", g_bridge.hapticAction),
        Bind("/user/hand/right/output/haptic", g_bridge.hapticAction),
    };

    const char* profiles[] = {
        "/interaction_profiles/bytedance/pico4s_controller",
        "/interaction_profiles/bytedance/pico4_controller",
        "/interaction_profiles/oculus/touch_controller"
    };

    for (const char* p : profiles) {
        XrPath profilePath = XR_NULL_PATH;
        if (xrStringToPath(g_bridge.instance, p, &profilePath) == XR_SUCCESS) {
            XrInteractionProfileSuggestedBinding sug{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
            sug.interactionProfile = profilePath;
            sug.suggestedBindings = bindings.data();
            sug.countSuggestedBindings = bindings.size();
            xrSuggestInteractionProfileBindings(g_bridge.instance, &sug);
        }
    }

    return true;
}

bool CreateOpenXRSession(EGLDisplay display, EGLContext context) {
    if (g_bridge.session != XR_NULL_HANDLE) return true;

    if (pfn_xrGetOpenGLESGraphicsRequirementsKHR) {
        XrGraphicsRequirementsOpenGLESKHR reqs{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
        pfn_xrGetOpenGLESGraphicsRequirementsKHR(g_bridge.instance, g_bridge.systemId, &reqs);
    }

    XrGraphicsBindingOpenGLESAndroidKHR graphicsBinding{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    graphicsBinding.display = display;
    graphicsBinding.config = nullptr;
    graphicsBinding.context = context;

    XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
    sessionInfo.next = &graphicsBinding;
    sessionInfo.systemId = g_bridge.systemId;

    XrResult res = xrCreateSession(g_bridge.instance, &sessionInfo, &g_bridge.session);
    if (res != XR_SUCCESS || g_bridge.session == XR_NULL_HANDLE) {
        LOGE("[vrapi] xrCreateSession failed (XrResult=%d)", res);
        return false;
    }
    LOGI("[vrapi] OpenXR Session created successfully (handle=%llu)", (unsigned long long)g_bridge.session);

    // Reference Space (STAGE or LOCAL)
    XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;
    xrCreateReferenceSpace(g_bridge.session, &spaceInfo, &g_bridge.appSpace);

    // Attach Action Set to Session
    InitOpenXRActions();
    XrSessionActionSetsAttachInfo attachInfo{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attachInfo.countActionSets = 1;
    attachInfo.actionSets = &g_bridge.actionSet;
    xrAttachSessionActionSets(g_bridge.session, &attachInfo);

    // Create Action Spaces for controller poses
    for (int i = 0; i < 2; ++i) {
        XrActionSpaceCreateInfo actionSpaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        actionSpaceInfo.action = g_bridge.aimPoseAction;
        actionSpaceInfo.subactionPath = g_bridge.handSubactionPaths[i];
        actionSpaceInfo.poseInActionSpace.orientation.w = 1.0f;
        xrCreateActionSpace(g_bridge.session, &actionSpaceInfo, &g_bridge.handSpaces[i]);
    }

    // Create Stereo Swapchain (Texture 2D Array: arraySize = 2)
    XrSwapchainCreateInfo scInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    scInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    scInfo.format = GL_RGBA8;
    scInfo.sampleCount = 1;
    scInfo.width = g_bridge.scWidth;
    scInfo.height = g_bridge.scHeight;
    scInfo.faceCount = 1;
    scInfo.arraySize = 2; // Stereo
    scInfo.mipCount = 1;

    res = xrCreateSwapchain(g_bridge.session, &scInfo, &g_bridge.xrSwapchain);
    if (res != XR_SUCCESS) {
        LOGE("[vrapi] xrCreateSwapchain failed (XrResult=%d)", res);
        return false;
    }

    uint32_t scImgCount = 0;
    xrEnumerateSwapchainImages(g_bridge.xrSwapchain, 0, &scImgCount, nullptr);
    g_bridge.scImages.assign(scImgCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
    xrEnumerateSwapchainImages(g_bridge.xrSwapchain, scImgCount, &scImgCount,
                               reinterpret_cast<XrSwapchainImageBaseHeader*>(g_bridge.scImages.data()));
    LOGI("[vrapi] Created OpenXR Swapchain with %u images", scImgCount);

    // Begin Session
    XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
    beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    res = xrBeginSession(g_bridge.session, &beginInfo);
    if (res == XR_SUCCESS) {
        g_bridge.sessionRunning = true;
        LOGI("[vrapi] OpenXR Session started successfully!");
    } else {
        LOGE("[vrapi] xrBeginSession failed (XrResult=%d)", res);
    }

    return true;
}

void SyncOpenXRInput(XrTime predictedTime) {
    if (!g_bridge.sessionRunning || g_bridge.actionSet == XR_NULL_HANDLE) return;

    XrActiveActionSet activeSet{g_bridge.actionSet, XR_NULL_PATH};
    XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &activeSet;
    if (xrSyncActions(g_bridge.session, &syncInfo) != XR_SUCCESS) return;

    for (int i = 0; i < 2; ++i) {
        auto& ctrl = g_bridge.controllers[i];
        XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
        getInfo.subactionPath = g_bridge.handSubactionPaths[i];

        // Thumbstick
        getInfo.action = g_bridge.thumbstickAction;
        XrActionStateVector2f stickState{XR_TYPE_ACTION_STATE_VECTOR2F};
        if (xrGetActionStateVector2f(g_bridge.session, &getInfo, &stickState) == XR_SUCCESS && stickState.isActive) {
            ctrl.thumbstick[0] = stickState.currentState.x;
            ctrl.thumbstick[1] = stickState.currentState.y;
            ctrl.connected = true;
        }

        // Thumbstick Click (L3 / R3)
        getInfo.action = g_bridge.thumbstickClickAction;
        XrActionStateBoolean stickClickState{XR_TYPE_ACTION_STATE_BOOLEAN};
        if (xrGetActionStateBoolean(g_bridge.session, &getInfo, &stickClickState) == XR_SUCCESS && stickClickState.isActive) {
            ctrl.thumbstickClick = stickClickState.currentState;
        }

        // Trigger
        getInfo.action = g_bridge.triggerValAction;
        XrActionStateFloat trigState{XR_TYPE_ACTION_STATE_FLOAT};
        if (xrGetActionStateFloat(g_bridge.session, &getInfo, &trigState) == XR_SUCCESS && trigState.isActive) {
            ctrl.trigger = trigState.currentState;
        }
        getInfo.action = g_bridge.triggerClickAction;
        XrActionStateBoolean trigClickState{XR_TYPE_ACTION_STATE_BOOLEAN};
        if (xrGetActionStateBoolean(g_bridge.session, &getInfo, &trigClickState) == XR_SUCCESS && trigClickState.isActive) {
            ctrl.triggerClick = trigClickState.currentState;
        }

        // Grip
        getInfo.action = g_bridge.gripValAction;
        XrActionStateFloat gripState{XR_TYPE_ACTION_STATE_FLOAT};
        if (xrGetActionStateFloat(g_bridge.session, &getInfo, &gripState) == XR_SUCCESS && gripState.isActive) {
            ctrl.grip = gripState.currentState;
            ctrl.gripClick = (ctrl.grip > 0.65f);
        }

        // Buttons
        getInfo.action = g_bridge.buttonPrimaryAction;
        XrActionStateBoolean priState{XR_TYPE_ACTION_STATE_BOOLEAN};
        if (xrGetActionStateBoolean(g_bridge.session, &getInfo, &priState) == XR_SUCCESS && priState.isActive) {
            ctrl.buttonPrimary = priState.currentState;
        }
        getInfo.action = g_bridge.buttonSecondaryAction;
        XrActionStateBoolean secState{XR_TYPE_ACTION_STATE_BOOLEAN};
        if (xrGetActionStateBoolean(g_bridge.session, &getInfo, &secState) == XR_SUCCESS && secState.isActive) {
            ctrl.buttonSecondary = secState.currentState;
        }
        getInfo.action = g_bridge.menuAction;
        XrActionStateBoolean menuState{XR_TYPE_ACTION_STATE_BOOLEAN};
        if (xrGetActionStateBoolean(g_bridge.session, &getInfo, &menuState) == XR_SUCCESS && menuState.isActive) {
            ctrl.menu = menuState.currentState;
        }

        // Pose
        if (g_bridge.handSpaces[i] != XR_NULL_HANDLE) {
            XrSpaceLocation spaceLoc{XR_TYPE_SPACE_LOCATION};
            if (xrLocateSpace(g_bridge.handSpaces[i], g_bridge.appSpace, predictedTime, &spaceLoc) == XR_SUCCESS) {
                if ((spaceLoc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
                    (spaceLoc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
                    ctrl.pose = spaceLoc.pose;
                    ctrl.poseValid = true;
                } else {
                    ctrl.poseValid = false;
                }
            }
        }
    }

    // QuestCraft Controls Table Key Dispatches
    // Left Stick Click -> L3 Sprint (AKEYCODE_BUTTON_THUMBL = 106)
    if (g_bridge.controllers[0].thumbstickClick != g_bridge.prevControllers[0].thumbstickClick) {
        DispatchKeyEvent(g_bridge.controllers[0].thumbstickClick ? 0 : 1, 106);
    }
    // Right Stick Click -> R3 Crouch (AKEYCODE_BUTTON_THUMBR = 107)
    if (g_bridge.controllers[1].thumbstickClick != g_bridge.prevControllers[1].thumbstickClick) {
        DispatchKeyEvent(g_bridge.controllers[1].thumbstickClick ? 0 : 1, 107);
    }
    // Left Grip -> Hotbar Left (L1 / AKEYCODE_BUTTON_L1 = 102)
    if (g_bridge.controllers[0].gripClick != g_bridge.prevControllers[0].gripClick) {
        DispatchKeyEvent(g_bridge.controllers[0].gripClick ? 0 : 1, 102);
    }
    // Right Grip -> Hotbar Right (R1 / AKEYCODE_BUTTON_R1 = 103)
    if (g_bridge.controllers[1].gripClick != g_bridge.prevControllers[1].gripClick) {
        DispatchKeyEvent(g_bridge.controllers[1].gripClick ? 0 : 1, 103);
    }
    // Left Trigger Click -> Place / Use (AKEYCODE_BUTTON_L2 = 104)
    if (g_bridge.controllers[0].triggerClick != g_bridge.prevControllers[0].triggerClick) {
        DispatchKeyEvent(g_bridge.controllers[0].triggerClick ? 0 : 1, 104);
    }
    // Right Trigger Click -> Break / Grab (AKEYCODE_BUTTON_R2 = 105)
    if (g_bridge.controllers[1].triggerClick != g_bridge.prevControllers[1].triggerClick) {
        DispatchKeyEvent(g_bridge.controllers[1].triggerClick ? 0 : 1, 105);
    }
    // X Button -> Inventory (AKEYCODE_BUTTON_X = 99)
    if (g_bridge.controllers[0].buttonPrimary != g_bridge.prevControllers[0].buttonPrimary) {
        DispatchKeyEvent(g_bridge.controllers[0].buttonPrimary ? 0 : 1, 99);
    }
    // Y Button -> VR Mode Toggle (AKEYCODE_BUTTON_Y = 100)
    if (g_bridge.controllers[0].buttonSecondary != g_bridge.prevControllers[0].buttonSecondary) {
        DispatchKeyEvent(g_bridge.controllers[0].buttonSecondary ? 0 : 1, 100);
    }
    // A Button -> Radial Menu / Action (AKEYCODE_BUTTON_A = 96)
    if (g_bridge.controllers[1].buttonPrimary != g_bridge.prevControllers[1].buttonPrimary) {
        DispatchKeyEvent(g_bridge.controllers[1].buttonPrimary ? 0 : 1, 96);
    }
    // B Button -> Jump (AKEYCODE_BUTTON_B = 97)
    if (g_bridge.controllers[1].buttonSecondary != g_bridge.prevControllers[1].buttonSecondary) {
        DispatchKeyEvent(g_bridge.controllers[1].buttonSecondary ? 0 : 1, 97);
    }
    // Left Menu Button -> Pause / Esc (AKEYCODE_BACK = 4)
    if (g_bridge.controllers[0].menu != g_bridge.prevControllers[0].menu) {
        DispatchKeyEvent(g_bridge.controllers[0].menu ? 0 : 1, 4);
    }

    g_bridge.prevControllers[0] = g_bridge.controllers[0];
    g_bridge.prevControllers[1] = g_bridge.controllers[1];
}

} // namespace

//===========================================================================
// The 22 VrApi Functions Imported by Mojang's libminecraftpe.so
//===========================================================================
extern "C" {

OVR_VRAPI_EXPORT ovrInitializeStatus vrapi_Initialize(const ovrInitParms* initParms) {
    LOGI("[vrapi] >>> vrapi_Initialize called");
    if (!initParms) {
        LOGE("[vrapi] initParms is null!");
        return VRAPI_INITIALIZE_UNKNOWN_ERROR;
    }

    g_bridge.vm = initParms->Java.Vm;
    if (g_bridge.vm && initParms->Java.ActivityObject) {
        JNIEnv* env = nullptr;
        if (g_bridge.vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK && env) {
            g_bridge.activity = env->NewGlobalRef(initParms->Java.ActivityObject);
        } else {
            g_bridge.activity = initParms->Java.ActivityObject;
        }
    } else {
        g_bridge.activity = initParms->Java.ActivityObject;
    }

    if (!InitOpenXRRuntime()) {
        LOGE("[vrapi] Failed to initialize OpenXR runtime!");
        return VRAPI_INITIALIZE_UNKNOWN_ERROR;
    }

    LOGI("[vrapi] <<< vrapi_Initialize succeeded (VRAPI_INITIALIZE_SUCCESS)");
    return VRAPI_INITIALIZE_SUCCESS;
}

OVR_VRAPI_EXPORT void vrapi_Shutdown() {
    LOGI("[vrapi] >>> vrapi_Shutdown called");
    if (g_bridge.session != XR_NULL_HANDLE) {
        xrDestroySession(g_bridge.session);
        g_bridge.session = XR_NULL_HANDLE;
    }
    if (g_bridge.instance != XR_NULL_HANDLE) {
        xrDestroyInstance(g_bridge.instance);
        g_bridge.instance = XR_NULL_HANDLE;
    }
    if (g_bridge.activity && g_bridge.vm) {
        JNIEnv* env = nullptr;
        if (g_bridge.vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK && env) {
            env->DeleteGlobalRef(g_bridge.activity);
            g_bridge.activity = nullptr;
        }
    }
    if (g_bridge.readFbo != 0) {
        glDeleteFramebuffers(1, &g_bridge.readFbo);
        g_bridge.readFbo = 0;
    }
    if (g_bridge.drawFbo != 0) {
        glDeleteFramebuffers(1, &g_bridge.drawFbo);
        g_bridge.drawFbo = 0;
    }
    LOGI("[vrapi] <<< vrapi_Shutdown completed");
}

OVR_VRAPI_EXPORT void vrapi_SetPropertyInt(const ovrJava* java, const ovrProperty propType, const int intVal) {
    if (propType == VRAPI_FOVEATION_LEVEL) {
        g_bridge.foveationLevel = intVal;
    }
}

OVR_VRAPI_EXPORT int vrapi_GetSystemPropertyInt(const ovrJava* java, const ovrSystemProperty propType) {
    switch (propType) {
        case VRAPI_SYS_PROP_SUGGESTED_EYE_TEXTURE_WIDTH:
            return static_cast<int>(g_bridge.scWidth);
        case VRAPI_SYS_PROP_SUGGESTED_EYE_TEXTURE_HEIGHT:
            return static_cast<int>(g_bridge.scHeight);
        case VRAPI_SYS_PROP_DISPLAY_PIXELS_WIDE:
            return 2160;
        case VRAPI_SYS_PROP_DISPLAY_PIXELS_HIGH:
            return 2160;
        case VRAPI_SYS_PROP_NUM_SUPPORTED_DISPLAY_REFRESH_RATES:
            return 1;
        case VRAPI_SYS_PROP_HAS_ORIENTATION_TRACKING:
        case VRAPI_SYS_PROP_HAS_POSITION_TRACKING:
            return 1;
        case VRAPI_SYS_PROP_DEVICE_TYPE:
            return VRAPI_DEVICE_TYPE_OCULUSQUEST; // Quest 6DoF hardware profile
        default:
            return 1;
    }
}

OVR_VRAPI_EXPORT float vrapi_GetSystemPropertyFloat(const ovrJava* java, const ovrSystemProperty propType) {
    switch (propType) {
        case VRAPI_SYS_PROP_SUGGESTED_EYE_FOV_DEGREES_X:
        case VRAPI_SYS_PROP_SUGGESTED_EYE_FOV_DEGREES_Y:
            return 105.0f;
        case VRAPI_SYS_PROP_DISPLAY_REFRESH_RATE:
            return 90.0f;
        default:
            return 90.0f;
    }
}

OVR_VRAPI_EXPORT int vrapi_GetSystemStatusInt(const ovrJava* java, const ovrSystemStatus statusType) {
    switch (statusType) {
        case VRAPI_SYS_STATUS_MOUNTED:
            return 1;
        case VRAPI_SYS_STATUS_APP_FRAMES_PER_SECOND:
            return 90;
        default:
            return 0;
    }
}

OVR_VRAPI_EXPORT ovrResult vrapi_SetTrackingSpace(ovrMobile* ovr, ovrTrackingSpace whichSpace) {
    g_bridge.trackingSpace = whichSpace;
    return ovrSuccess;
}

OVR_VRAPI_EXPORT double vrapi_GetTimeInSeconds() {
    return GetTimeNowSeconds();
}

OVR_VRAPI_EXPORT ovrMobile* vrapi_EnterVrMode(const ovrModeParms* parms) {
    LOGI("[vrapi] >>> vrapi_EnterVrMode called (Display=%llu, Context=%llu)",
         parms ? parms->Display : 0, parms ? parms->ShareContext : 0);

    if (!parms) {
        return &g_dummyMobile;
    }

    EGLDisplay dpy = reinterpret_cast<EGLDisplay>(parms->Display);
    EGLContext ctx = reinterpret_cast<EGLContext>(parms->ShareContext);

    if (dpy == EGL_NO_DISPLAY) dpy = eglGetCurrentDisplay();
    if (ctx == EGL_NO_CONTEXT) ctx = eglGetCurrentContext();

    if (!CreateOpenXRSession(dpy, ctx)) {
        LOGE("[vrapi] Failed to create OpenXR session in vrapi_EnterVrMode!");
    }

    LOGI("[vrapi] <<< vrapi_EnterVrMode completed");
    return &g_dummyMobile;
}

OVR_VRAPI_EXPORT void vrapi_LeaveVrMode(ovrMobile* ovr) {
    LOGI("[vrapi] >>> vrapi_LeaveVrMode called");
    if (g_bridge.sessionRunning) {
        xrRequestExitSession(g_bridge.session);
        xrEndSession(g_bridge.session);
        g_bridge.sessionRunning = false;
    }
    LOGI("[vrapi] <<< vrapi_LeaveVrMode completed");
}

OVR_VRAPI_EXPORT double vrapi_GetPredictedDisplayTime(ovrMobile* ovr, long long frameIndex) {
    if (!g_bridge.sessionRunning) {
        return GetTimeNowSeconds() + 0.011;
    }

    XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
    g_bridge.frameState = {XR_TYPE_FRAME_STATE};
    xrWaitFrame(g_bridge.session, &waitInfo, &g_bridge.frameState);

    // Call xrBeginFrame right after xrWaitFrame
    XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    xrBeginFrame(g_bridge.session, &beginInfo);
    g_bridge.frameBegun = true;

    // Sync input actions for this frame
    SyncOpenXRInput(g_bridge.frameState.predictedDisplayTime);

    double predSec = static_cast<double>(g_bridge.frameState.predictedDisplayTime) * 1e-9;
    return predSec;
}

OVR_VRAPI_EXPORT ovrTracking vrapi_GetPredictedTracking(ovrMobile* ovr, double absTimeInSeconds) {
    ovrTracking tracking{};
    tracking.Status = VRAPI_TRACKING_STATUS_ORIENTATION_TRACKED |
                      VRAPI_TRACKING_STATUS_POSITION_TRACKED |
                      VRAPI_TRACKING_STATUS_ORIENTATION_VALID |
                      VRAPI_TRACKING_STATUS_POSITION_VALID |
                      VRAPI_TRACKING_STATUS_HMD_CONNECTED;

    if (!g_bridge.sessionRunning || g_bridge.appSpace == XR_NULL_HANDLE) {
        tracking.HeadPose.Pose.Orientation.w = 1.0f;
        return tracking;
    }

    XrTime queryTime = g_bridge.frameState.predictedDisplayTime;
    if (queryTime == 0) {
        queryTime = static_cast<XrTime>(absTimeInSeconds * 1e9);
    }

    XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
    locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locateInfo.displayTime = queryTime;
    locateInfo.space = g_bridge.appSpace;

    uint32_t viewCount = 2;
    xrLocateViews(g_bridge.session, &locateInfo, &g_bridge.viewState, 2, &viewCount, g_bridge.views);

    // Compute center-eye position and orientation
    tracking.HeadPose.Pose.Position.x = (g_bridge.views[0].pose.position.x + g_bridge.views[1].pose.position.x) * 0.5f;
    tracking.HeadPose.Pose.Position.y = (g_bridge.views[0].pose.position.y + g_bridge.views[1].pose.position.y) * 0.5f;
    tracking.HeadPose.Pose.Position.z = (g_bridge.views[0].pose.position.z + g_bridge.views[1].pose.position.z) * 0.5f;

    tracking.HeadPose.Pose.Orientation.x = g_bridge.views[0].pose.orientation.x;
    tracking.HeadPose.Pose.Orientation.y = g_bridge.views[0].pose.orientation.y;
    tracking.HeadPose.Pose.Orientation.z = g_bridge.views[0].pose.orientation.z;
    tracking.HeadPose.Pose.Orientation.w = g_bridge.views[0].pose.orientation.w;
    tracking.HeadPose.TimeInSeconds = absTimeInSeconds;

    return tracking;
}

OVR_VRAPI_EXPORT ovrTextureSwapChain* vrapi_CreateTextureSwapChain(
    ovrTextureType type,
    ovrTextureFormat format,
    int width,
    int height,
    int levels,
    bool buffered) {

    auto* chain = new TextureSwapChainImpl();
    chain->type = type;
    chain->format = format;
    chain->width = width;
    chain->height = height;
    chain->levels = levels;
    chain->bufferCount = buffered ? 3 : 1;
    chain->textures.resize(chain->bufferCount);

    glGenTextures(chain->bufferCount, chain->textures.data());
    for (int i = 0; i < chain->bufferCount; ++i) {
        glBindTexture(GL_TEXTURE_2D, chain->textures[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    LOGI("[vrapi] Created SwapChain %p (%dx%d, %d buffers)", chain, width, height, chain->bufferCount);
    return reinterpret_cast<ovrTextureSwapChain*>(chain);
}

OVR_VRAPI_EXPORT void vrapi_DestroyTextureSwapChain(ovrTextureSwapChain* chain) {
    if (!chain) return;
    auto* impl = reinterpret_cast<TextureSwapChainImpl*>(chain);
    if (!impl->textures.empty()) {
        glDeleteTextures(impl->textures.size(), impl->textures.data());
    }
    delete impl;
}

OVR_VRAPI_EXPORT int vrapi_GetTextureSwapChainLength(ovrTextureSwapChain* chain) {
    if (!chain) return 1;
    return reinterpret_cast<TextureSwapChainImpl*>(chain)->bufferCount;
}

OVR_VRAPI_EXPORT unsigned int vrapi_GetTextureSwapChainHandle(ovrTextureSwapChain* chain, int index) {
    if (!chain) return 0;
    auto* impl = reinterpret_cast<TextureSwapChainImpl*>(chain);
    int safeIdx = index % impl->bufferCount;
    return impl->textures[safeIdx];
}

OVR_VRAPI_EXPORT void vrapi_SubmitFrame(ovrMobile* ovr, const ovrFrameParms* parms) {
    if (!g_bridge.sessionRunning || g_bridge.xrSwapchain == XR_NULL_HANDLE || !parms) {
        return;
    }

    if (!g_bridge.frameBegun) {
        XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
        xrBeginFrame(g_bridge.session, &beginInfo);
        g_bridge.frameBegun = true;
    }

    // Acquire OpenXR swapchain image
    uint32_t imgIndex = 0;
    XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    xrAcquireSwapchainImage(g_bridge.xrSwapchain, &acquireInfo, &imgIndex);

    XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = 1000000000;
    xrWaitSwapchainImage(g_bridge.xrSwapchain, &waitInfo);

    // Save previous framebuffer & viewport
    GLint prevReadFbo = 0, prevDrawFbo = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);
    GLint prevVp[4];
    glGetIntegerv(GL_VIEWPORT, prevVp);

    if (g_bridge.readFbo == 0) {
        glGenFramebuffers(1, &g_bridge.readFbo);
    }
    if (g_bridge.drawFbo == 0) {
        glGenFramebuffers(1, &g_bridge.drawFbo);
    }
    GLuint dstArrayTex = g_bridge.scImages[imgIndex].image;

    // Blit Eye 0 (Left Eye) -> Texture 2D Array layer 0
    if (parms->Layers[0].Textures[0].ColorTextureSwapChain != nullptr) {
        auto* leftChain = reinterpret_cast<TextureSwapChainImpl*>(parms->Layers[0].Textures[0].ColorTextureSwapChain);
        int idx = parms->Layers[0].Textures[0].TextureSwapChainIndex % leftChain->bufferCount;
        GLuint srcTex = leftChain->textures[idx];

        glBindFramebuffer(GL_READ_FRAMEBUFFER, g_bridge.readFbo);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, srcTex, 0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_bridge.drawFbo);
        glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, dstArrayTex, 0, 0);
        GLenum drawBufs[1] = {GL_COLOR_ATTACHMENT0};
        glDrawBuffers(1, drawBufs);

        glBlitFramebuffer(0, 0, leftChain->width, leftChain->height,
                          0, 0, g_bridge.scWidth, g_bridge.scHeight,
                          GL_COLOR_BUFFER_BIT, GL_LINEAR);
    }

    // Blit Eye 1 (Right Eye) -> Texture 2D Array layer 1
    if (parms->Layers[0].Textures[1].ColorTextureSwapChain != nullptr) {
        auto* rightChain = reinterpret_cast<TextureSwapChainImpl*>(parms->Layers[0].Textures[1].ColorTextureSwapChain);
        int idx = parms->Layers[0].Textures[1].TextureSwapChainIndex % rightChain->bufferCount;
        GLuint srcTex = rightChain->textures[idx];

        glBindFramebuffer(GL_READ_FRAMEBUFFER, g_bridge.readFbo);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, srcTex, 0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_bridge.drawFbo);
        glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, dstArrayTex, 0, 1);
        GLenum drawBufs[1] = {GL_COLOR_ATTACHMENT0};
        glDrawBuffers(1, drawBufs);

        glBlitFramebuffer(0, 0, rightChain->width, rightChain->height,
                          0, 0, g_bridge.scWidth, g_bridge.scHeight,
                          GL_COLOR_BUFFER_BIT, GL_LINEAR);
    }

    // Detach textures from helper FBOs
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_bridge.readFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_bridge.drawFbo);
    glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 0, 0, 0);

    // Restore GL State
    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFbo);
    glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);

    // Release OpenXR Swapchain image
    XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(g_bridge.xrSwapchain, &releaseInfo);

    // End Frame with Stereo Projection Layer
    XrCompositionLayerProjectionView projViews[2];
    for (int eye = 0; eye < 2; ++eye) {
        projViews[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
        projViews[eye].next = nullptr;
        projViews[eye].pose = g_bridge.views[eye].pose;
        projViews[eye].fov = g_bridge.views[eye].fov;
        projViews[eye].subImage.swapchain = g_bridge.xrSwapchain;
        projViews[eye].subImage.imageRect.offset = {0, 0};
        projViews[eye].subImage.imageRect.extent = {
            static_cast<int32_t>(g_bridge.scWidth),
            static_cast<int32_t>(g_bridge.scHeight)
        };
        projViews[eye].subImage.imageArrayIndex = eye;
    }

    XrCompositionLayerProjection projLayer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    projLayer.space = g_bridge.appSpace;
    projLayer.viewCount = 2;
    projLayer.views = projViews;

    const XrCompositionLayerBaseHeader* layers[1] = {
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projLayer)
    };

    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = g_bridge.frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = 1;
    endInfo.layers = layers;

    xrEndFrame(g_bridge.session, &endInfo);
    g_bridge.frameBegun = false;
}

OVR_VRAPI_EXPORT bool vrapi_ShowSystemUIWithExtra(const ovrJava* java, const ovrSystemUIType type, const char* extraText) {
    LOGI("[vrapi] vrapi_ShowSystemUIWithExtra requested (type=%d, extra=%s)", type, extraText ? extraText : "");
    return true;
}

OVR_VRAPI_EXPORT ovrResult vrapi_EnumerateInputDevices(
    ovrMobile* ovr,
    const uint32_t index,
    ovrInputCapabilityHeader* capsHeader) {

    if (!capsHeader || index >= 3) {
        return ovrError_InvalidParameter;
    }

    if (index == 0) {
        capsHeader->Type = ovrControllerType_TrackedRemote;
        capsHeader->DeviceID = 1; // 1 = Right Touch
    } else if (index == 1) {
        capsHeader->Type = ovrControllerType_TrackedRemote;
        capsHeader->DeviceID = 2; // 2 = Left Touch
    } else if (index == 2) {
        capsHeader->Type = ovrControllerType_Gamepad;
        capsHeader->DeviceID = 3; // 3 = Gamepad
    }
    return ovrSuccess;
}

OVR_VRAPI_EXPORT ovrResult vrapi_GetInputDeviceCapabilities(ovrMobile* ovr, ovrInputCapabilityHeader* capsHeader) {
    if (!capsHeader) return ovrError_InvalidParameter;

    if (capsHeader->Type == ovrControllerType_TrackedRemote) {
        auto* caps = reinterpret_cast<ovrInputTrackedRemoteCapabilities*>(capsHeader);
        caps->ControllerCapabilities = ovrControllerCaps_HasOrientationTracking |
                                       ovrControllerCaps_HasPositionTracking |
                                       ovrControllerCaps_HasAnalogGripTrigger |
                                       ovrControllerCaps_HasAnalogIndexTrigger |
                                       ovrControllerCaps_HasJoystick |
                                       ovrControllerCaps_ModelOculusTouch;
        if (capsHeader->DeviceID == 1) {
            caps->ControllerCapabilities |= ovrControllerCaps_RightHand;
        } else if (capsHeader->DeviceID == 2) {
            caps->ControllerCapabilities |= ovrControllerCaps_LeftHand;
        }

        caps->ButtonCapabilities = ovrButton_A | ovrButton_B | ovrButton_X | ovrButton_Y |
                                   ovrButton_Trigger | ovrButton_GripTrigger | ovrButton_Enter |
                                   ovrButton_LThumb | ovrButton_RThumb |
                                   ovrButton_LShoulder | ovrButton_RShoulder;
        caps->TouchCapabilities = ovrTouch_A | ovrTouch_B | ovrTouch_X | ovrTouch_Y |
                                  ovrTouch_IndexTrigger | ovrTouch_Joystick |
                                  ovrTouch_LThumb | ovrTouch_RThumb;
        return ovrSuccess;
    } else if (capsHeader->Type == ovrControllerType_Gamepad) {
        auto* caps = reinterpret_cast<ovrInputGamepadCapabilities*>(capsHeader);
        caps->ControllerCapabilities = ovrControllerCaps_HasJoystick;
        caps->ButtonCapabilities = ovrButton_A | ovrButton_B | ovrButton_X | ovrButton_Y |
                                   ovrButton_RThumb | ovrButton_LThumb |
                                   ovrButton_RShoulder | ovrButton_LShoulder |
                                   ovrButton_Up | ovrButton_Down | ovrButton_Left | ovrButton_Right |
                                   ovrButton_Enter | ovrButton_Back;
        return ovrSuccess;
    }
    return ovrError_InvalidParameter;
}

OVR_VRAPI_EXPORT ovrResult vrapi_GetInputTrackingState(
    ovrMobile* ovr,
    const ovrDeviceID deviceID,
    const double absTimeInSeconds,
    ovrTracking* tracking) {

    if (!tracking) return ovrError_InvalidParameter;

    tracking->Status = VRAPI_TRACKING_STATUS_ORIENTATION_TRACKED |
                      VRAPI_TRACKING_STATUS_POSITION_TRACKED |
                      VRAPI_TRACKING_STATUS_ORIENTATION_VALID |
                      VRAPI_TRACKING_STATUS_POSITION_VALID;

    if (deviceID > 2) {
        tracking->HeadPose.Pose.Orientation.w = 1.0f;
        tracking->HeadPose.TimeInSeconds = absTimeInSeconds;
        return ovrSuccess;
    }

    int idx = (deviceID == 1) ? 1 : 0; // 1 = Right (index 1), 2 = Left (index 0)
    const auto& ctrl = g_bridge.controllers[idx];

    tracking->HeadPose.Pose.Orientation.x = ctrl.pose.orientation.x;
    tracking->HeadPose.Pose.Orientation.y = ctrl.pose.orientation.y;
    tracking->HeadPose.Pose.Orientation.z = ctrl.pose.orientation.z;
    tracking->HeadPose.Pose.Orientation.w = ctrl.pose.orientation.w;
    tracking->HeadPose.Pose.Position.x = ctrl.pose.position.x;
    tracking->HeadPose.Pose.Position.y = ctrl.pose.position.y;
    tracking->HeadPose.Pose.Position.z = ctrl.pose.position.z;
    tracking->HeadPose.TimeInSeconds = absTimeInSeconds;

    return ovrSuccess;
}

OVR_VRAPI_EXPORT ovrResult vrapi_GetCurrentInputState(
    ovrMobile* ovr,
    const ovrDeviceID deviceID,
    ovrInputStateHeader* inputState) {

    if (!inputState) return ovrError_InvalidParameter;

    inputState->TimeInSeconds = GetTimeNowSeconds();

    if (inputState->ControllerType == ovrControllerType_TrackedRemote) {
        auto* state = reinterpret_cast<ovrInputStateTrackedRemote*>(inputState);
        int idx = (deviceID == 1) ? 1 : 0; // 1 = Right, 2 = Left
        const auto& ctrl = g_bridge.controllers[idx];

        state->Buttons = 0;
        state->Touches = 0;

        if (idx == 1) { // Right Controller
            if (ctrl.buttonPrimary)   { state->Buttons |= ovrButton_A; state->Touches |= ovrTouch_A; }
            if (ctrl.buttonSecondary) { state->Buttons |= ovrButton_B; state->Touches |= ovrTouch_B; }
            if (ctrl.thumbstickClick) state->Buttons |= ovrButton_RThumb;
            if (ctrl.triggerClick)    state->Buttons |= ovrButton_Trigger;
            if (ctrl.gripClick)       state->Buttons |= (ovrButton_GripTrigger | ovrButton_RShoulder);
            if (ctrl.trigger > 0.05f) state->Touches |= ovrTouch_IndexTrigger;
            if (std::abs(ctrl.thumbstick[0]) > 0.1f || std::abs(ctrl.thumbstick[1]) > 0.1f) {
                state->Touches |= (ovrTouch_RThumb | ovrTouch_Joystick);
            }
        } else { // Left Controller
            if (ctrl.buttonPrimary)   { state->Buttons |= ovrButton_X; state->Touches |= ovrTouch_X; }
            if (ctrl.buttonSecondary) { state->Buttons |= ovrButton_Y; state->Touches |= ovrTouch_Y; }
            if (ctrl.thumbstickClick) state->Buttons |= ovrButton_LThumb;
            if (ctrl.menu)            state->Buttons |= ovrButton_Enter;
            if (ctrl.triggerClick)    state->Buttons |= ovrButton_Trigger;
            if (ctrl.gripClick)       state->Buttons |= (ovrButton_GripTrigger | ovrButton_LShoulder);
            if (ctrl.trigger > 0.05f) state->Touches |= ovrTouch_IndexTrigger;
            if (std::abs(ctrl.thumbstick[0]) > 0.1f || std::abs(ctrl.thumbstick[1]) > 0.1f) {
                state->Touches |= (ovrTouch_LThumb | ovrTouch_Joystick);
            }
        }

        state->Joystick.x = ctrl.thumbstick[0];
        state->Joystick.y = ctrl.thumbstick[1];
        state->JoystickNoDeadZone.x = ctrl.thumbstick[0];
        state->JoystickNoDeadZone.y = ctrl.thumbstick[1];
        state->IndexTrigger = ctrl.trigger;
        state->GripTrigger = ctrl.grip;
        state->BatteryPercentRemaining = 100;
        state->RecenterCount = 0;

        return ovrSuccess;
    } else if (inputState->ControllerType == ovrControllerType_Gamepad) {
        auto* state = reinterpret_cast<ovrInputStateGamepad*>(inputState);
        state->LeftJoystick.x = g_bridge.controllers[0].thumbstick[0];
        state->LeftJoystick.y = g_bridge.controllers[0].thumbstick[1];
        state->RightJoystick.x = g_bridge.controllers[1].thumbstick[0];
        state->RightJoystick.y = g_bridge.controllers[1].thumbstick[1];
        state->LeftTrigger = g_bridge.controllers[0].trigger;
        state->RightTrigger = g_bridge.controllers[1].trigger;

        state->Buttons = 0;
        if (g_bridge.controllers[1].buttonPrimary)   state->Buttons |= ovrButton_A;
        if (g_bridge.controllers[1].buttonSecondary) state->Buttons |= ovrButton_B;
        if (g_bridge.controllers[0].buttonPrimary)   state->Buttons |= ovrButton_X;
        if (g_bridge.controllers[0].buttonSecondary) state->Buttons |= ovrButton_Y;
        if (g_bridge.controllers[0].thumbstickClick) state->Buttons |= ovrButton_LThumb;
        if (g_bridge.controllers[1].thumbstickClick) state->Buttons |= ovrButton_RThumb;
        if (g_bridge.controllers[0].gripClick)       state->Buttons |= ovrButton_LShoulder;
        if (g_bridge.controllers[1].gripClick)       state->Buttons |= ovrButton_RShoulder;
        if (g_bridge.controllers[0].menu)            state->Buttons |= ovrButton_Enter;

        return ovrSuccess;
    }
    return ovrError_InvalidParameter;
}

} // extern "C"
