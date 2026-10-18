#pragma once

#include <jni.h>
#include <cstdint>

namespace mcbedvr::xr {

class InputBridge {
public:
    static bool Init(JavaVM* vm, jobject activity);
    static void Shutdown();

    // Dispatches an Android KeyEvent to MainActivity.dispatchKeyEvent.
    // action: 0 = ACTION_DOWN, 1 = ACTION_UP
    // source: 0x401 = InputDevice.SOURCE_GAMEPAD (default)
    static void DispatchKey(int32_t keyCode, int32_t action, int32_t source = 0x401);

    // Dispatches an Android MotionEvent to MainActivity.dispatchTouchEvent.
    // action: 0 = ACTION_DOWN, 1 = ACTION_UP, 2 = ACTION_MOVE
    static void DispatchTouch(float x, float y, int32_t action);

    // Dispatches an Android MotionEvent with SOURCE_JOYSTICK to MainActivity.dispatchGenericMotionEvent.
    // lx, ly: Left stick axes [-1.0f, 1.0f] (lx: -Left/+Right, ly: -Forward/+Backward)
    // rx, ry: Right stick axes [-1.0f, 1.0f] (rx: -TurnLeft/+TurnRight, ry: -PitchUp/+PitchDown)
    // lt, rt: Triggers [0.0f, 1.0f] (lt: brake/interact/place, rt: gas/mine/attack)
    static void DispatchJoystick(float lx, float ly, float rx, float ry, float lt, float rt);

    // Dispatches mouse look deltas (relative motion) via SOURCE_MOUSE to MainActivity.dispatchGenericMotionEvent.
    static void DispatchLookDelta(float dx, float dy);

    // Requests pointer capture lock / release via MainActivity.lockCursor / unlockCursor.
    static void SetPointerCapture(bool locked);

    // Informs Bedrock's internal JellyBeanDeviceManager that an Xbox controller is active.
    static void SetXboxControllerConnected(bool connected);
};

}  // namespace mcbedvr::xr
