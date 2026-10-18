#include "xr/PicoInput.h"
#include "xr/XrContext.h"
#include "xr/InputBridge.h"
#include "xr/BlitEye.h"
#include "MCBedVr.h"
#include "Log.h"

#include <openxr/openxr.h>
#include <dlfcn.h>
#include <cstring>
#include <cmath>
#include <vector>
#include <mutex>

#ifdef __ANDROID__
#include <android/keycodes.h>
#else
constexpr int32_t AKEYCODE_BACK = 4;
constexpr int32_t AKEYCODE_DPAD_UP = 19;
constexpr int32_t AKEYCODE_DPAD_DOWN = 20;
constexpr int32_t AKEYCODE_DPAD_LEFT = 21;
constexpr int32_t AKEYCODE_DPAD_RIGHT = 22;
constexpr int32_t AKEYCODE_DPAD_CENTER = 23;
constexpr int32_t AKEYCODE_BUTTON_A = 96;
constexpr int32_t AKEYCODE_BUTTON_B = 97;
constexpr int32_t AKEYCODE_BUTTON_X = 99;
constexpr int32_t AKEYCODE_BUTTON_Y = 100;
constexpr int32_t AKEYCODE_BUTTON_L1 = 102;
constexpr int32_t AKEYCODE_BUTTON_R1 = 103;
constexpr int32_t AKEYCODE_BUTTON_L2 = 104;
constexpr int32_t AKEYCODE_BUTTON_R2 = 105;
constexpr int32_t AKEYCODE_BUTTON_THUMBL = 106;
constexpr int32_t AKEYCODE_BUTTON_THUMBR = 107;
#endif

namespace mcbedvr::xr {
namespace {

bool              s_initialized = false;
XrInstance        s_instance = XR_NULL_HANDLE;
XrSession         s_session = XR_NULL_HANDLE;
XrActionSet       s_actionSet = XR_NULL_HANDLE;

XrPath            s_leftHandPath = XR_NULL_PATH;
XrPath            s_rightHandPath = XR_NULL_PATH;

XrSpace           s_leftHandSpace = XR_NULL_HANDLE;
XrSpace           s_rightHandSpace = XR_NULL_HANDLE;

XrAction          s_actionAimPose = XR_NULL_HANDLE;
XrAction          s_actionThumbstick = XR_NULL_HANDLE;
XrAction          s_actionThumbstickClick = XR_NULL_HANDLE;
XrAction          s_actionTriggerVal = XR_NULL_HANDLE;
XrAction          s_actionTriggerClick = XR_NULL_HANDLE;
XrAction          s_actionSqueezeVal = XR_NULL_HANDLE;
XrAction          s_actionSqueezeClick = XR_NULL_HANDLE;
XrAction          s_actionButtonPrimary = XR_NULL_HANDLE;
XrAction          s_actionButtonSecondary = XR_NULL_HANDLE;
XrAction          s_actionMenuClick = XR_NULL_HANDLE;
XrAction          s_actionHaptic = XR_NULL_HANDLE;

std::mutex        s_snapshotMutex;
PicoInputSnapshot s_prevSnapshot{};
PicoInputSnapshot s_currentSnapshot{};

LaserHit          s_laserHit{};
bool              s_prevTouchDown = false;
XrPosef           s_quadPose = {
    .orientation = {0.0f, 0.0f, 0.0f, 1.0f},
    .position = {0.0f, 0.0f, -1.8f}
};

struct DPadState {
    bool up{false};
    bool down{false};
    bool left{false};
    bool right{false};
};
DPadState         s_leftDPadState{};

bool CreateAction(XrActionSet set, const char* name, const char* locName,
                  XrActionType type, const XrPath* subactions, uint32_t subactionCount,
                  XrAction& outAction) {
    XrActionCreateInfo aci{XR_TYPE_ACTION_CREATE_INFO};
    aci.next = nullptr;
    std::strncpy(aci.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
    aci.actionType = type;
    aci.countSubactionPaths = subactionCount;
    aci.subactionPaths = subactions;
    std::strncpy(aci.localizedActionName, locName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    XrResult r = xrCreateAction(set, &aci, &outAction);
    if (XR_FAILED(r)) {
        LOG_INPUTE("xrCreateAction('%s') failed: %d", name, r);
        return false;
    }
    return true;
}

bool MakeSuggestedBinding(XrInstance instance, const char* pathStr, XrAction action,
                          XrActionSuggestedBinding& outBinding) {
    XrPath p = XR_NULL_PATH;
    XrResult r = xrStringToPath(instance, pathStr, &p);
    if (XR_FAILED(r) || p == XR_NULL_PATH) {
        LOG_INPUTW("xrStringToPath('%s') failed: %d", pathStr, r);
        return false;
    }
    outBinding.action = action;
    outBinding.binding = p;
    return true;
}

bool SuggestProfileBindings(XrInstance instance, const char* profileStr,
                            const std::vector<XrActionSuggestedBinding>& bindings) {
    XrPath profilePath = XR_NULL_PATH;
    XrResult r = xrStringToPath(instance, profileStr, &profilePath);
    if (XR_FAILED(r) || profilePath == XR_NULL_PATH) {
        LOG_INPUTW("xrStringToPath('%s') failed: %d", profileStr, r);
        return false;
    }
    XrInteractionProfileSuggestedBinding ipsb{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    ipsb.next = nullptr;
    ipsb.interactionProfile = profilePath;
    ipsb.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
    ipsb.suggestedBindings = bindings.data();
    r = xrSuggestInteractionProfileBindings(instance, &ipsb);
    if (XR_FAILED(r)) {
        LOG_INPUTW("xrSuggestInteractionProfileBindings('%s') failed: %d", profileStr, r);
        return false;
    }
    LOG_INPUT("Suggested %zu bindings for %s", bindings.size(), profileStr);
    return true;
}

void DispatchKey(int32_t keyCode, int32_t action) {
    LOG_INPUT("PicoInput dispatch keyCode=%d action=%d (%s)",
              keyCode, action, (action == 0 ? "DOWN" : "UP"));
    InputBridge::DispatchKey(keyCode, action);
}

void CheckButton(bool cur, bool prev, int32_t keyCode) {
    if (cur != prev) {
        DispatchKey(keyCode, cur ? 0 : 1);
    }
}

void DispatchButtonTransitions(const ControllerState& cur, const ControllerState& prev, bool isRightHand) {
    bool isCinema = mcbedvr::xr::GetQuadLayerMode();

    if (isRightHand) {
        if (isCinema) {
            // Cinema Menu Mode: A = Select / Confirm, B = Cancel / Back
            CheckButton(cur.button_primary, prev.button_primary, AKEYCODE_BUTTON_A);
            CheckButton(cur.button_secondary, prev.button_secondary, AKEYCODE_BUTTON_B);
            CheckButton(cur.thumbstick_click, prev.thumbstick_click, AKEYCODE_BUTTON_THUMBR);
        } else {
            // QuestCraft Controls in Immersive Gameplay:
            // B = Jump (Bedrock A button)
            CheckButton(cur.button_secondary, prev.button_secondary, AKEYCODE_BUTTON_A);
            // A = Action / Radial Menu (Bedrock X button)
            CheckButton(cur.button_primary, prev.button_primary, AKEYCODE_BUTTON_X);
            // Right Thumbstick Click = Crouch / Sneak (Bedrock R3: AKEYCODE_BUTTON_THUMBR)
            CheckButton(cur.thumbstick_click, prev.thumbstick_click, AKEYCODE_BUTTON_THUMBR);
            // Right Grab = Hotbar Right (Scroll Right, Bedrock R1)
            CheckButton(cur.grip > 0.5f, prev.grip > 0.5f, AKEYCODE_BUTTON_R1);
            // Right Trigger = Break / Grab (Bedrock R2)
            CheckButton(cur.trigger > 0.5f, prev.trigger > 0.5f, AKEYCODE_BUTTON_R2);
        }
    } else {
        // Left Hand
        // Y button: hold > 0.35s to toggle between 360 Immersive VR and Cinema Quad; short tap triggers action
        static int s_yHoldFrames = 0;
        static bool s_yToggled = false;
        if (cur.button_secondary) {
            s_yHoldFrames++;
            if (s_yHoldFrames >= 32 && !s_yToggled) { // ~0.35s hold
                s_yToggled = true;
                mcbedvr::xr::ToggleLayerMode();
                PicoInput::TriggerHaptic(0, 0.12f, 180.0f, 0.7f);
                LOG_INPUT("Y button held (>0.35s): toggled layer mode -> %s",
                          GetQuadLayerMode() ? "CINEMA_QUAD" : "IMMERSIVE_VR");
            }
        } else {
            if (prev.button_secondary && !s_yToggled && s_yHoldFrames > 0 && s_yHoldFrames < 32) {
                // Short tap on Y
                PicoInput::TriggerHaptic(0, 0.04f, 200.0f, 0.4f);
                LOG_INPUT("Y button short tap");
            }
            s_yHoldFrames = 0;
            s_yToggled = false;
        }

        if (isCinema) {
            CheckButton(cur.button_primary, prev.button_primary, AKEYCODE_BUTTON_X);
            CheckButton(cur.thumbstick_click, prev.thumbstick_click, AKEYCODE_BUTTON_THUMBL);
            CheckButton(cur.menu_click, prev.menu_click, AKEYCODE_BACK);
        } else {
            // QuestCraft Controls in Immersive Gameplay:
            // X = Inventory (Bedrock Y button)
            CheckButton(cur.button_primary, prev.button_primary, AKEYCODE_BUTTON_Y);
            // Left Thumbstick Click = Sprint (Bedrock ThumbL)
            CheckButton(cur.thumbstick_click, prev.thumbstick_click, AKEYCODE_BUTTON_THUMBL);
            // Left Grab = Hotbar Left (Scroll Left, Bedrock L1)
            CheckButton(cur.grip > 0.5f, prev.grip > 0.5f, AKEYCODE_BUTTON_L1);
            // Left Trigger = Place / Use (Bedrock L2)
            CheckButton(cur.trigger > 0.5f, prev.trigger > 0.5f, AKEYCODE_BUTTON_L2);
            // Menu Button = Pause / Esc (Bedrock Gamepad Start button)
            CheckButton(cur.menu_click, prev.menu_click, 108 /* AKEYCODE_BUTTON_START */);
        }
    }
}

void UpdateStickDPad(float stickX, float stickY, DPadState& curState) {
    constexpr float kPressThreshold = 0.50f;
    constexpr float kReleaseThreshold = 0.30f;

    bool newUp = curState.up ? (stickY > kReleaseThreshold) : (stickY > kPressThreshold);
    bool newDown = curState.down ? (stickY < -kReleaseThreshold) : (stickY < -kPressThreshold);
    bool newLeft = curState.left ? (stickX < -kReleaseThreshold) : (stickX < -kPressThreshold);
    bool newRight = curState.right ? (stickX > kReleaseThreshold) : (stickX > kPressThreshold);

    if (newUp != curState.up) {
        InputBridge::DispatchKey(AKEYCODE_DPAD_UP, newUp ? 0 : 1);
        curState.up = newUp;
    }
    if (newDown != curState.down) {
        InputBridge::DispatchKey(AKEYCODE_DPAD_DOWN, newDown ? 0 : 1);
        curState.down = newDown;
    }
    if (newLeft != curState.left) {
        InputBridge::DispatchKey(AKEYCODE_DPAD_LEFT, newLeft ? 0 : 1);
        curState.left = newLeft;
    }
    if (newRight != curState.right) {
        InputBridge::DispatchKey(AKEYCODE_DPAD_RIGHT, newRight ? 0 : 1);
        curState.right = newRight;
    }
}

void UpdateLaserAndTouch(const ControllerState& rightHand, const ControllerState& leftHand) {
    // Prefer right hand aim pose, fallback to left hand
    const ControllerState* pointerHand = rightHand.pose_valid ? &rightHand : (leftHand.pose_valid ? &leftHand : nullptr);
    if (!pointerHand) {
        if (s_laserHit.is_hit && s_prevTouchDown) {
            InputBridge::DispatchTouch(s_laserHit.pixel_x, s_laserHit.pixel_y, 1 /* ACTION_UP */);
            s_prevTouchDown = false;
        }
        s_laserHit.is_hit = false;
        return;
    }

    const XrPosef& pose = pointerHand->pose;
    // Rotate forward -Z vector by orientation quaternion:
    float qx = pose.orientation.x;
    float qy = pose.orientation.y;
    float qz = pose.orientation.z;
    float qw = pose.orientation.w;

    float dx = -2.0f * (qx * qz + qy * qw);
    float dy = -2.0f * (qy * qz - qx * qw);
    float dz = -(1.0f - 2.0f * (qx * qx + qy * qy));

    float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len > 1e-5f) {
        dx /= len; dy /= len; dz /= len;
    }

    // Ray-plane intersection with arbitrary Quad pose (s_quadPose)
    float qqx = s_quadPose.orientation.x;
    float qqy = s_quadPose.orientation.y;
    float qqz = s_quadPose.orientation.z;
    float qqw = s_quadPose.orientation.w;

    // Quad normal (+Z rotated, points toward player front face):
    float nx = 2.0f * (qqx * qqz + qqy * qqw);
    float ny = 2.0f * (qqy * qqz - qqx * qqw);
    float nz = 1.0f - 2.0f * (qqx * qqx + qqy * qqy);

    // Quad right vector (+X rotated):
    float ux = 1.0f - 2.0f * (qqy * qqy + qqz * qqz);
    float uy = 2.0f * (qqx * qqy + qqz * qqw);
    float uz = 2.0f * (qqx * qqz - qqy * qqw);

    // Quad up vector (+Y rotated):
    float vx = 2.0f * (qqx * qqy - qqz * qqw);
    float vy = 1.0f - 2.0f * (qqx * qqx + qqz * qqz);
    float vz = 2.0f * (qqy * qqz + qqx * qqw);

    float denom = dx * nx + dy * ny + dz * nz;
    if (denom < -0.01f) {
        float cx = s_quadPose.position.x;
        float cy = s_quadPose.position.y;
        float cz = s_quadPose.position.z;

        float numer = (cx - pose.position.x) * nx +
                      (cy - pose.position.y) * ny +
                      (cz - pose.position.z) * nz;
        float t = numer / denom;
        if (t > 0.0f) {
            float hx = pose.position.x + t * dx;
            float hy = pose.position.y + t * dy;
            float hz = pose.position.z + t * dz;

            float relX = (hx - cx) * ux + (hy - cy) * uy + (hz - cz) * uz;
            float relY = (hx - cx) * vx + (hy - cy) * vy + (hz - cz) * vz;

            constexpr float kQuadWidth = 2.4f;
            constexpr float kQuadHeight = 1.35f;

            float u = (relX + kQuadWidth * 0.5f) / kQuadWidth;
            float v = (kQuadHeight * 0.5f - relY) / kQuadHeight;

            if (u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f) {
                s_laserHit.is_hit = true;
                s_laserHit.u = u;
                s_laserHit.v = v;
                s_laserHit.pixel_x = u * static_cast<float>(BlitEye::GetWindowWidth());
                s_laserHit.pixel_y = v * static_cast<float>(BlitEye::GetWindowHeight());
            } else {
                s_laserHit.is_hit = false;
            }
        } else {
            s_laserHit.is_hit = false;
        }
    } else {
        s_laserHit.is_hit = false;
    }

    bool triggerClick = (pointerHand->trigger > 0.5f);
    s_laserHit.clicking = triggerClick;

    if (s_laserHit.is_hit && GetQuadLayerMode()) {
        if (triggerClick && !s_prevTouchDown) {
            InputBridge::DispatchTouch(s_laserHit.pixel_x, s_laserHit.pixel_y, 0 /* ACTION_DOWN */);
            s_prevTouchDown = true;
        } else if (triggerClick && s_prevTouchDown) {
            InputBridge::DispatchTouch(s_laserHit.pixel_x, s_laserHit.pixel_y, 2 /* ACTION_MOVE */);
        }
    }

    if (!triggerClick && s_prevTouchDown) {
        InputBridge::DispatchTouch(s_laserHit.pixel_x, s_laserHit.pixel_y, 1 /* ACTION_UP */);
        s_prevTouchDown = false;
    }
}

void ReadControllerState(XrSession session, XrPath handPath, XrSpace handSpace,
                         XrSpace appSpace, XrTime displayTime, ControllerState& state) {
    // 1. Thumbstick Vector2f
    XrActionStateGetInfo stickInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, s_actionThumbstick, handPath};
    XrActionStateVector2f stickState{XR_TYPE_ACTION_STATE_VECTOR2F};
    if (XR_SUCCEEDED(xrGetActionStateVector2f(session, &stickInfo, &stickState)) && stickState.isActive) {
        state.thumbstick_x = stickState.currentState.x;
        state.thumbstick_y = stickState.currentState.y;
    } else {
        state.thumbstick_x = 0.0f;
        state.thumbstick_y = 0.0f;
    }

    // 2. Thumbstick Click
    XrActionStateGetInfo stickClickInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, s_actionThumbstickClick, handPath};
    XrActionStateBoolean stickClickState{XR_TYPE_ACTION_STATE_BOOLEAN};
    if (XR_SUCCEEDED(xrGetActionStateBoolean(session, &stickClickInfo, &stickClickState)) && stickClickState.isActive) {
        state.thumbstick_click = (stickClickState.currentState == XR_TRUE);
    } else {
        state.thumbstick_click = false;
    }

    // 3. Trigger value
    XrActionStateGetInfo triggerInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, s_actionTriggerVal, handPath};
    XrActionStateFloat triggerState{XR_TYPE_ACTION_STATE_FLOAT};
    if (XR_SUCCEEDED(xrGetActionStateFloat(session, &triggerInfo, &triggerState)) && triggerState.isActive) {
        state.trigger = triggerState.currentState;
    } else {
        state.trigger = 0.0f;
    }

    // 4. Trigger click
    XrActionStateGetInfo triggerClickInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, s_actionTriggerClick, handPath};
    XrActionStateBoolean triggerClickState{XR_TYPE_ACTION_STATE_BOOLEAN};
    if (XR_SUCCEEDED(xrGetActionStateBoolean(session, &triggerClickInfo, &triggerClickState)) && triggerClickState.isActive) {
        state.trigger_click = (triggerClickState.currentState == XR_TRUE);
    } else {
        state.trigger_click = (state.trigger > 0.75f);
    }

    // 5. Grip / Squeeze value
    XrActionStateGetInfo squeezeInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, s_actionSqueezeVal, handPath};
    XrActionStateFloat squeezeState{XR_TYPE_ACTION_STATE_FLOAT};
    if (XR_SUCCEEDED(xrGetActionStateFloat(session, &squeezeInfo, &squeezeState)) && squeezeState.isActive) {
        state.grip = squeezeState.currentState;
    } else {
        state.grip = 0.0f;
    }

    // 6. Grip / Squeeze click
    XrActionStateGetInfo squeezeClickInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, s_actionSqueezeClick, handPath};
    XrActionStateBoolean squeezeClickState{XR_TYPE_ACTION_STATE_BOOLEAN};
    if (XR_SUCCEEDED(xrGetActionStateBoolean(session, &squeezeClickInfo, &squeezeClickState)) && squeezeClickState.isActive) {
        state.grip_click = (squeezeClickState.currentState == XR_TRUE);
    } else {
        state.grip_click = (state.grip > 0.75f);
    }

    // 7. Primary button (A or X)
    XrActionStateGetInfo primaryInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, s_actionButtonPrimary, handPath};
    XrActionStateBoolean primaryState{XR_TYPE_ACTION_STATE_BOOLEAN};
    if (XR_SUCCEEDED(xrGetActionStateBoolean(session, &primaryInfo, &primaryState)) && primaryState.isActive) {
        state.button_primary = (primaryState.currentState == XR_TRUE);
    } else {
        state.button_primary = false;
    }

    // 8. Secondary button (B or Y)
    XrActionStateGetInfo secondaryInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, s_actionButtonSecondary, handPath};
    XrActionStateBoolean secondaryState{XR_TYPE_ACTION_STATE_BOOLEAN};
    if (XR_SUCCEEDED(xrGetActionStateBoolean(session, &secondaryInfo, &secondaryState)) && secondaryState.isActive) {
        state.button_secondary = (secondaryState.currentState == XR_TRUE);
    } else {
        state.button_secondary = false;
    }

    // 9. Menu click
    XrActionStateGetInfo menuInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, s_actionMenuClick, handPath};
    XrActionStateBoolean menuState{XR_TYPE_ACTION_STATE_BOOLEAN};
    if (XR_SUCCEEDED(xrGetActionStateBoolean(session, &menuInfo, &menuState)) && menuState.isActive) {
        state.menu_click = (menuState.currentState == XR_TRUE);
    } else {
        state.menu_click = false;
    }

    // 10. Hand Pose in app space
    if (handSpace != XR_NULL_HANDLE && appSpace != XR_NULL_HANDLE) {
        XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
        loc.next = nullptr;
        if (XR_SUCCEEDED(xrLocateSpace(handSpace, appSpace, displayTime, &loc)) &&
            (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
            (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
            state.pose = loc.pose;
            state.pose_valid = true;
        } else {
            state.pose = {{0, 0, 0, 1}, {0, 0, 0}};
            state.pose_valid = false;
        }
    } else {
        state.pose = {{0, 0, 0, 1}, {0, 0, 0}};
        state.pose_valid = false;
    }
}

}  // namespace

bool PicoInput::Init(XrInstance instance, XrSession session) {
    if (s_initialized) {
        LOG_INPUTW("PicoInput::Init already initialized — skipping");
        return true;
    }

    if (instance == XR_NULL_HANDLE || session == XR_NULL_HANDLE) {
        LOG_INPUTW("PicoInput::Init called with null instance or session — skipping");
        return false;
    }

    s_instance = instance;
    s_session = session;

    if (XR_FAILED(xrStringToPath(instance, "/user/hand/left", &s_leftHandPath)) ||
        XR_FAILED(xrStringToPath(instance, "/user/hand/right", &s_rightHandPath))) {
        LOG_INPUTE("Failed to get hand subaction paths");
        return false;
    }

    XrPath subactions[2] = { s_leftHandPath, s_rightHandPath };

    XrActionSetCreateInfo asci{XR_TYPE_ACTION_SET_CREATE_INFO};
    asci.next = nullptr;
    std::strncpy(asci.actionSetName, "mcbedvr_input", XR_MAX_ACTION_SET_NAME_SIZE - 1);
    std::strncpy(asci.localizedActionSetName, "MCBedVR Input", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
    asci.priority = 0;
    XrResult r = xrCreateActionSet(instance, &asci, &s_actionSet);
    if (XR_FAILED(r)) {
        LOG_INPUTE("xrCreateActionSet failed: %d", r);
        return false;
    }

    bool ok = true;
    ok &= CreateAction(s_actionSet, "aim_pose", "Aim Pose", XR_ACTION_TYPE_POSE_INPUT, subactions, 2, s_actionAimPose);
    ok &= CreateAction(s_actionSet, "thumbstick", "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT, subactions, 2, s_actionThumbstick);
    ok &= CreateAction(s_actionSet, "thumbstick_click", "Thumbstick Click", XR_ACTION_TYPE_BOOLEAN_INPUT, subactions, 2, s_actionThumbstickClick);
    ok &= CreateAction(s_actionSet, "trigger_val", "Trigger Value", XR_ACTION_TYPE_FLOAT_INPUT, subactions, 2, s_actionTriggerVal);
    ok &= CreateAction(s_actionSet, "trigger_click", "Trigger Click", XR_ACTION_TYPE_BOOLEAN_INPUT, subactions, 2, s_actionTriggerClick);
    ok &= CreateAction(s_actionSet, "squeeze_val", "Squeeze Value", XR_ACTION_TYPE_FLOAT_INPUT, subactions, 2, s_actionSqueezeVal);
    ok &= CreateAction(s_actionSet, "squeeze_click", "Squeeze Click", XR_ACTION_TYPE_BOOLEAN_INPUT, subactions, 2, s_actionSqueezeClick);
    ok &= CreateAction(s_actionSet, "button_primary", "Primary Button", XR_ACTION_TYPE_BOOLEAN_INPUT, subactions, 2, s_actionButtonPrimary);
    ok &= CreateAction(s_actionSet, "button_secondary", "Secondary Button", XR_ACTION_TYPE_BOOLEAN_INPUT, subactions, 2, s_actionButtonSecondary);
    ok &= CreateAction(s_actionSet, "menu_click", "Menu Click", XR_ACTION_TYPE_BOOLEAN_INPUT, subactions, 2, s_actionMenuClick);
    ok &= CreateAction(s_actionSet, "haptic", "Haptic", XR_ACTION_TYPE_VIBRATION_OUTPUT, subactions, 2, s_actionHaptic);

    if (!ok) {
        LOG_INPUTE("Failed to create all OpenXR input actions");
        Shutdown();
        return false;
    }

    std::vector<XrActionSuggestedBinding> pico4sBindings;
    auto addBinding = [&](const char* pathStr, XrAction act) {
        XrActionSuggestedBinding b{};
        if (MakeSuggestedBinding(instance, pathStr, act, b)) {
            pico4sBindings.push_back(b);
        }
    };

    addBinding("/user/hand/left/input/aim/pose", s_actionAimPose);
    addBinding("/user/hand/right/input/aim/pose", s_actionAimPose);
    addBinding("/user/hand/left/input/thumbstick", s_actionThumbstick);
    addBinding("/user/hand/right/input/thumbstick", s_actionThumbstick);
    addBinding("/user/hand/left/input/thumbstick/click", s_actionThumbstickClick);
    addBinding("/user/hand/right/input/thumbstick/click", s_actionThumbstickClick);
    addBinding("/user/hand/left/input/trigger/value", s_actionTriggerVal);
    addBinding("/user/hand/right/input/trigger/value", s_actionTriggerVal);
    addBinding("/user/hand/left/input/trigger/click", s_actionTriggerClick);
    addBinding("/user/hand/right/input/trigger/click", s_actionTriggerClick);
    addBinding("/user/hand/left/input/squeeze/value", s_actionSqueezeVal);
    addBinding("/user/hand/right/input/squeeze/value", s_actionSqueezeVal);
    addBinding("/user/hand/left/input/squeeze/click", s_actionSqueezeClick);
    addBinding("/user/hand/right/input/squeeze/click", s_actionSqueezeClick);
    addBinding("/user/hand/left/input/x/click", s_actionButtonPrimary);
    addBinding("/user/hand/left/input/y/click", s_actionButtonSecondary);
    addBinding("/user/hand/right/input/a/click", s_actionButtonPrimary);
    addBinding("/user/hand/right/input/b/click", s_actionButtonSecondary);
    addBinding("/user/hand/left/input/menu/click", s_actionMenuClick);
    addBinding("/user/hand/left/output/haptic", s_actionHaptic);
    addBinding("/user/hand/right/output/haptic", s_actionHaptic);

    // Primary: bytedance/pico4s_controller (PICO 4 Ultra / Hawk controller)
    SuggestProfileBindings(instance, "/interaction_profiles/bytedance/pico4s_controller", pico4sBindings);

    // Fallback: bytedance/pico4_controller
    SuggestProfileBindings(instance, "/interaction_profiles/bytedance/pico4_controller", pico4sBindings);

    // Fallback: oculus/touch_controller
    std::vector<XrActionSuggestedBinding> touchBindings;
    auto addTouchBinding = [&](const char* pathStr, XrAction act) {
        XrActionSuggestedBinding b{};
        if (MakeSuggestedBinding(instance, pathStr, act, b)) {
            touchBindings.push_back(b);
        }
    };
    addTouchBinding("/user/hand/left/input/aim/pose", s_actionAimPose);
    addTouchBinding("/user/hand/right/input/aim/pose", s_actionAimPose);
    addTouchBinding("/user/hand/left/input/thumbstick", s_actionThumbstick);
    addTouchBinding("/user/hand/right/input/thumbstick", s_actionThumbstick);
    addTouchBinding("/user/hand/left/input/thumbstick/click", s_actionThumbstickClick);
    addTouchBinding("/user/hand/right/input/thumbstick/click", s_actionThumbstickClick);
    addTouchBinding("/user/hand/left/input/trigger/value", s_actionTriggerVal);
    addTouchBinding("/user/hand/right/input/trigger/value", s_actionTriggerVal);
    addTouchBinding("/user/hand/left/input/squeeze/value", s_actionSqueezeVal);
    addTouchBinding("/user/hand/right/input/squeeze/value", s_actionSqueezeVal);
    addTouchBinding("/user/hand/left/input/x/click", s_actionButtonPrimary);
    addTouchBinding("/user/hand/left/input/y/click", s_actionButtonSecondary);
    addTouchBinding("/user/hand/right/input/a/click", s_actionButtonPrimary);
    addTouchBinding("/user/hand/right/input/b/click", s_actionButtonSecondary);
    addTouchBinding("/user/hand/left/input/menu/click", s_actionMenuClick);
    addTouchBinding("/user/hand/left/output/haptic", s_actionHaptic);
    addTouchBinding("/user/hand/right/output/haptic", s_actionHaptic);

    SuggestProfileBindings(instance, "/interaction_profiles/oculus/touch_controller", touchBindings);

    // Attach ActionSet to session
    XrSessionActionSetsAttachInfo asai{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    asai.next = nullptr;
    asai.countActionSets = 1;
    asai.actionSets = &s_actionSet;
    r = xrAttachSessionActionSets(session, &asai);
    if (XR_FAILED(r)) {
        LOG_INPUTE("xrAttachSessionActionSets failed: %d", r);
        Shutdown();
        return false;
    }

    // Create action spaces for hand poses
    XrActionSpaceCreateInfo asciLeft{XR_TYPE_ACTION_SPACE_CREATE_INFO};
    asciLeft.next = nullptr;
    asciLeft.action = s_actionAimPose;
    asciLeft.subactionPath = s_leftHandPath;
    asciLeft.poseInActionSpace = {{0, 0, 0, 1}, {0, 0, 0}};
    r = xrCreateActionSpace(session, &asciLeft, &s_leftHandSpace);
    if (XR_FAILED(r)) {
        LOG_INPUTW("xrCreateActionSpace left failed: %d", r);
    }

    XrActionSpaceCreateInfo asciRight{XR_TYPE_ACTION_SPACE_CREATE_INFO};
    asciRight.next = nullptr;
    asciRight.action = s_actionAimPose;
    asciRight.subactionPath = s_rightHandPath;
    asciRight.poseInActionSpace = {{0, 0, 0, 1}, {0, 0, 0}};
    r = xrCreateActionSpace(session, &asciRight, &s_rightHandSpace);
    if (XR_FAILED(r)) {
        LOG_INPUTW("xrCreateActionSpace right failed: %d", r);
    }

    s_initialized = true;
    LOG_INPUT("PicoInput initialized successfully");
    return true;
}

void PicoInput::Shutdown() {
    if (!s_initialized && s_actionSet == XR_NULL_HANDLE) {
        return;
    }
    if (s_leftHandSpace != XR_NULL_HANDLE) {
        xrDestroySpace(s_leftHandSpace);
        s_leftHandSpace = XR_NULL_HANDLE;
    }
    if (s_rightHandSpace != XR_NULL_HANDLE) {
        xrDestroySpace(s_rightHandSpace);
        s_rightHandSpace = XR_NULL_HANDLE;
    }
    if (s_actionSet != XR_NULL_HANDLE) {
        xrDestroyActionSet(s_actionSet);
        s_actionSet = XR_NULL_HANDLE;
    }
    s_actionAimPose = XR_NULL_HANDLE;
    s_actionThumbstick = XR_NULL_HANDLE;
    s_actionThumbstickClick = XR_NULL_HANDLE;
    s_actionTriggerVal = XR_NULL_HANDLE;
    s_actionTriggerClick = XR_NULL_HANDLE;
    s_actionSqueezeVal = XR_NULL_HANDLE;
    s_actionSqueezeClick = XR_NULL_HANDLE;
    s_actionButtonPrimary = XR_NULL_HANDLE;
    s_actionButtonSecondary = XR_NULL_HANDLE;
    s_actionMenuClick = XR_NULL_HANDLE;
    s_actionHaptic = XR_NULL_HANDLE;

    s_leftHandPath = XR_NULL_PATH;
    s_rightHandPath = XR_NULL_PATH;
    s_instance = XR_NULL_HANDLE;
    s_session = XR_NULL_HANDLE;

    s_prevSnapshot = {};
    {
        std::lock_guard<std::mutex> lk(s_snapshotMutex);
        s_currentSnapshot = {};
    }
    s_laserHit = {};
    s_prevTouchDown = false;
    s_initialized = false;
    LOG_INPUT("PicoInput shut down");
}

void PicoInput::SyncActions(XrSession session, XrSpace appSpace, XrTime predictedDisplayTime,
                            float headYawRate, float headPitchRate) {
    if (!s_initialized || s_actionSet == XR_NULL_HANDLE) {
        return;
    }

    XrActiveActionSet activeSet{
        .actionSet = s_actionSet,
        .subactionPath = XR_NULL_PATH
    };
    XrActionsSyncInfo syncInfo{
        .type = XR_TYPE_ACTIONS_SYNC_INFO,
        .next = nullptr,
        .countActiveActionSets = 1,
        .activeActionSets = &activeSet
    };

    XrResult r = xrSyncActions(session, &syncInfo);
    if (XR_FAILED(r)) {
        return;
    }

    PicoInputSnapshot newSnapshot{};
    ReadControllerState(session, s_leftHandPath, s_leftHandSpace, appSpace, predictedDisplayTime, newSnapshot.left);
    ReadControllerState(session, s_rightHandPath, s_rightHandSpace, appSpace, predictedDisplayTime, newSnapshot.right);

    // Button transitions (QuestCraft scheme)
    DispatchButtonTransitions(newSnapshot.left, s_prevSnapshot.left, /*isRightHand*/ false);
    DispatchButtonTransitions(newSnapshot.right, s_prevSnapshot.right, /*isRightHand*/ true);

    // Dual-grip chord or L3+R3 chord to toggle between Cinema Quad and Immersive VR
    bool dualGrip = (newSnapshot.left.grip > 0.75f && newSnapshot.right.grip > 0.75f);
    bool dualStickClick = (newSnapshot.left.thumbstick_click && newSnapshot.right.thumbstick_click);
    static int s_chordFrames = 0;
    if (dualGrip || dualStickClick) {
        s_chordFrames++;
        if (s_chordFrames == 20) { // ~0.22s hold threshold
            mcbedvr::xr::ToggleLayerMode();
            TriggerHaptic(0, 0.12f, 150.0f, 0.6f);
            TriggerHaptic(1, 0.12f, 150.0f, 0.6f);
            LOG_INPUT("VR Mode Chord: toggled layer mode -> %s",
                      GetQuadLayerMode() ? "CINEMA_QUAD" : "IMMERSIVE_VR");
        }
    } else {
        s_chordFrames = 0;
    }

    bool isCinema = GetQuadLayerMode();

    // QuestCraft Diegetic Wrist Gesture: Look at left wrist to toggle Inventory
    if (newSnapshot.left.pose_valid && !isCinema) {
        float hx = -newSnapshot.left.pose.position.x;
        float hy = 0.15f - newSnapshot.left.pose.position.y;
        float hz = -newSnapshot.left.pose.position.z;
        float dist = std::sqrt(hx * hx + hy * hy + hz * hz);
        if (dist > 0.18f && dist < 0.60f) {
            float qx = newSnapshot.left.pose.orientation.x;
            float qy = newSnapshot.left.pose.orientation.y;
            float qz = newSnapshot.left.pose.orientation.z;
            float qw = newSnapshot.left.pose.orientation.w;
            // Local +Y (top of wrist) rotated to world:
            float upX = 2.0f * (qx * qy - qz * qw);
            float upY = 1.0f - 2.0f * (qx * qx + qz * qz);
            float upZ = 2.0f * (qy * qz + qx * qw);
            float dotWrist = (upX * hx + upY * hy + upZ * hz) / dist;
            static bool s_wristLooking = false;
            if (dotWrist > 0.70f && !s_wristLooking) {
                s_wristLooking = true;
                InputBridge::DispatchKey(AKEYCODE_BUTTON_Y, 0); // Bedrock Inventory button
                InputBridge::DispatchKey(AKEYCODE_BUTTON_Y, 1);
                TriggerHaptic(0, 0.08f, 180.0f, 0.7f);
                LOG_INPUT("Wrist Gesture: Looked at left wrist (dot=%.2f, dist=%.2f) -> toggled inventory", dotWrist, dist);
            } else if (dotWrist < 0.45f && s_wristLooking) {
                s_wristLooking = false;
            }
        }
    }

    if (isCinema) {
        // Left stick D-Pad navigation for menus
        UpdateStickDPad(newSnapshot.left.thumbstick_x, newSnapshot.left.thumbstick_y, s_leftDPadState);

        // Virtual cinema screen laser pointer & touch dispatch
        UpdateLaserAndTouch(newSnapshot.right, newSnapshot.left);

        // Mute joystick locomotion while in cinema menus
        static bool s_wasCinemaJoyActive = false;
        if (s_wasCinemaJoyActive) {
            InputBridge::DispatchJoystick(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
            s_wasCinemaJoyActive = false;
        }
    } else {
        // IMMERSIVE VR MODE
        // Ensure laser pointer and touch are completely disengaged
        if (s_laserHit.is_hit && s_prevTouchDown) {
            InputBridge::DispatchTouch(s_laserHit.pixel_x, s_laserHit.pixel_y, 1 /* ACTION_UP */);
            s_prevTouchDown = false;
        }
        s_laserHit.is_hit = false;

        // Analog Locomotion (Left Stick: Strafe/Walk)
        float lx = newSnapshot.left.thumbstick_x;
        float ly = -newSnapshot.left.thumbstick_y; // In Android joystick: negative Y is forward
        constexpr float kDeadzone = 0.08f;
        if (std::abs(lx) < kDeadzone) lx = 0.0f;
        if (std::abs(ly) < kDeadzone) ly = 0.0f;

        // Right Stick Turn (Yaw/Pitch)
        float rx = newSnapshot.right.thumbstick_x;
        float ry = -newSnapshot.right.thumbstick_y;
        if (std::abs(rx) < kDeadzone) rx = 0.0f;
        if (std::abs(ry) < kDeadzone) ry = 0.0f;

        // Triggers (Left Trigger: Place/Use, Right Trigger: Break/Mine)
        float lt = newSnapshot.left.trigger;
        float rt = newSnapshot.right.trigger;
        if (lt < 0.05f) lt = 0.0f;
        if (rt < 0.05f) rt = 0.0f;

        // Physical Swing Mining (QuestCraft motion controls: swing right hand forward/down to mine)
        static XrVector3f s_prevRightPos{0.0f, 0.0f, 0.0f};
        static XrTime s_prevRightTime = 0;
        static bool s_swingMining = false;

        if (newSnapshot.right.pose_valid && predictedDisplayTime > s_prevRightTime && s_prevRightTime != 0) {
            float dt = static_cast<float>(predictedDisplayTime - s_prevRightTime) * 1e-9f;
            if (dt > 0.002f && dt < 0.1f) {
                float vx = (newSnapshot.right.pose.position.x - s_prevRightPos.x) / dt;
                float vy = (newSnapshot.right.pose.position.y - s_prevRightPos.y) / dt;
                float vz = (newSnapshot.right.pose.position.z - s_prevRightPos.z) / dt;
                float speed = std::sqrt(vx * vx + vy * vy + vz * vz);

                // Forward/downward swing gesture: moving forward (-Z) or down (-Y) fast
                bool isSwinging = (speed > 1.3f && (vz < -0.6f || vy < -0.6f));
                if (isSwinging) {
                    rt = 1.0f; // Force mine/attack on physical swing
                    if (!s_swingMining) {
                        s_swingMining = true;
                        TriggerHaptic(1, 0.08f, 220.0f, 0.85f); // Haptic impact feedback
                        LOG_INPUT("Physical Swing Mine: speed=%.2f m/s (vy=%.2f, vz=%.2f)", speed, vy, vz);
                    }
                } else {
                    s_swingMining = false;
                }
            }
        }
        if (newSnapshot.right.pose_valid) {
            s_prevRightPos = newSnapshot.right.pose.position;
            s_prevRightTime = predictedDisplayTime;
        }

        // Right stick turn: manual character turning (QuestCraft) + Head tracking look
        float totalRx = std::clamp(rx + headYawRate, -1.0f, 1.0f);
        float totalRy = std::clamp(ry + headPitchRate, -1.0f, 1.0f);

        static bool s_wasImmersiveJoyActive = false;
        bool joyActive = (lx != 0.0f || ly != 0.0f || totalRx != 0.0f || totalRy != 0.0f || lt != 0.0f || rt != 0.0f);

        if (joyActive) {
            InputBridge::DispatchJoystick(lx, ly, totalRx, totalRy, lt, rt);
            s_wasImmersiveJoyActive = true;
        } else if (s_wasImmersiveJoyActive) {
            InputBridge::DispatchJoystick(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
            s_wasImmersiveJoyActive = false;
        }
    }

    s_prevSnapshot = newSnapshot;
    {
        std::lock_guard<std::mutex> lk(s_snapshotMutex);
        s_currentSnapshot = newSnapshot;
    }
}

const PicoInputSnapshot& PicoInput::GetSnapshot() {
    return s_currentSnapshot;
}

const LaserHit& PicoInput::GetLaserHit() {
    return s_laserHit;
}

void PicoInput::SetQuadPose(const XrPosef& pose) {
    s_quadPose = pose;
}

void PicoInput::TriggerHaptic(uint32_t hand, float durationSeconds, float frequency, float amplitude) {
    if (!s_initialized || s_session == XR_NULL_HANDLE || s_actionHaptic == XR_NULL_HANDLE) {
        return;
    }

    XrPath subaction = (hand == 0) ? s_leftHandPath : s_rightHandPath;
    if (subaction == XR_NULL_PATH) {
        return;
    }

    XrHapticActionInfo hai{XR_TYPE_HAPTIC_ACTION_INFO};
    hai.next = nullptr;
    hai.action = s_actionHaptic;
    hai.subactionPath = subaction;

    if (amplitude <= 0.0f || durationSeconds <= 0.0f) {
        xrStopHapticFeedback(s_session, &hai);
        return;
    }

    XrHapticVibration vib{XR_TYPE_HAPTIC_VIBRATION};
    vib.next = nullptr;
    vib.duration = static_cast<XrDuration>(durationSeconds * 1e9f);
    vib.frequency = frequency;
    vib.amplitude = amplitude;

    xrApplyHapticFeedback(s_session, &hai, reinterpret_cast<const XrHapticBaseHeader*>(&vib));
}

}  // namespace mcbedvr::xr
