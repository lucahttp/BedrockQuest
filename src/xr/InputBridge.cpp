#include "xr/InputBridge.h"
#include "xr/XrContext.h"
#include "MCBedVr.h"
#include "Log.h"

#include <dlfcn.h>
#include <mutex>

namespace mcbedvr::xr {
namespace {

std::mutex   s_mtx;
JavaVM*      s_vm = nullptr;
jobject      s_activity = nullptr;

jclass       s_keyEventCls = nullptr;
jmethodID    s_keyEventComplexInit = nullptr;
jmethodID    s_keyEventSimpleInit = nullptr;

jclass       s_motionEventCls = nullptr;
jmethodID    s_motionEventObtain = nullptr;
jmethodID    s_motionEventObtainJoystick = nullptr;
jmethodID    s_motionEventRecycle = nullptr;

jclass       s_pointerPropertiesCls = nullptr;
jmethodID    s_ppCtor = nullptr;
jfieldID     s_ppIdField = nullptr;
jfieldID     s_ppToolTypeField = nullptr;

jclass       s_pointerCoordsCls = nullptr;
jmethodID    s_pcCtor = nullptr;
jmethodID    s_pcSetAxisValue = nullptr;
jfieldID     s_pcX = nullptr;
jfieldID     s_pcY = nullptr;

jobject      s_ppObj = nullptr;
jobject      s_pcObj = nullptr;
jobjectArray s_ppArray = nullptr;
jobjectArray s_pcArray = nullptr;

jobject      s_mousePpObj = nullptr;
jobject      s_mousePcObj = nullptr;
jobjectArray s_mousePpArray = nullptr;
jobjectArray s_mousePcArray = nullptr;

jclass       s_systemClockCls = nullptr;
jmethodID    s_systemClockUptimeMillis = nullptr;

jmethodID    s_dispatchKeyEvent = nullptr;
jmethodID    s_onKeyDown = nullptr;
jmethodID    s_onKeyUp = nullptr;
jmethodID    s_dispatchTouchEvent = nullptr;
jmethodID    s_dispatchGenericMotionEvent = nullptr;
jmethodID    s_processMotionEvent = nullptr;
static jint  s_validDeviceId = 6; // Default to PICO 4 Ultra virtual input device (Device 6: virtual-input-unScale-1)

JNIEnv* GetJNIEnv() {
    if (!s_vm) {
        s_vm = mcbedvr::GetJavaVM();
    }
    if (!s_vm) return nullptr;

    JNIEnv* env = nullptr;
    jint res = s_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (res == JNI_EDETACHED) {
        if (s_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            LOG_INPUTE("GetJNIEnv: AttachCurrentThread failed");
            return nullptr;
        }
    }
    return env;
}

}  // namespace

bool InputBridge::Init(JavaVM* vm, jobject activity) {
    std::lock_guard<std::mutex> lk(s_mtx);
    if (vm) s_vm = vm;
    if (!s_vm) s_vm = mcbedvr::GetJavaVM();

    JNIEnv* env = GetJNIEnv();
    if (!env) {
        LOG_INPUTE("InputBridge::Init: failed to acquire JNIEnv");
        return false;
    }

    if (activity && !s_activity) {
        s_activity = env->NewGlobalRef(activity);
        LOG_INPUT("InputBridge: captured Activity global ref %p", s_activity);
    }

    if (!s_keyEventCls) {
        jclass cls = env->FindClass("android/view/KeyEvent");
        if (cls) {
            s_keyEventCls = reinterpret_cast<jclass>(env->NewGlobalRef(cls));
            // public KeyEvent(long downTime, long eventTime, int action, int code, int repeat, int metaState, int deviceId, int scancode, int flags, int source)
            s_keyEventComplexInit = env->GetMethodID(s_keyEventCls, "<init>", "(JJIIIIIIII)V");
            if (!s_keyEventComplexInit) {
                env->ExceptionClear();
                // Fallback: public KeyEvent(int action, int code)
                s_keyEventSimpleInit = env->GetMethodID(s_keyEventCls, "<init>", "(II)V");
            }
            env->DeleteLocalRef(cls);
        } else {
            env->ExceptionClear();
            LOG_INPUTW("InputBridge: could not find android.view.KeyEvent class");
        }
    }

    if (!s_motionEventCls) {
        jclass cls = env->FindClass("android/view/MotionEvent");
        if (cls) {
            s_motionEventCls = reinterpret_cast<jclass>(env->NewGlobalRef(cls));
            s_motionEventObtain = env->GetStaticMethodID(s_motionEventCls, "obtain", "(JJIFFI)Landroid/view/MotionEvent;");
            s_motionEventObtainJoystick = env->GetStaticMethodID(
                s_motionEventCls, "obtain",
                "(JJII[Landroid/view/MotionEvent$PointerProperties;[Landroid/view/MotionEvent$PointerCoords;IIFFIIII)Landroid/view/MotionEvent;");
            s_motionEventRecycle = env->GetMethodID(s_motionEventCls, "recycle", "()V");
            env->DeleteLocalRef(cls);
        } else {
            env->ExceptionClear();
            LOG_INPUTW("InputBridge: could not find android.view.MotionEvent class");
        }
    }

    if (!s_pointerPropertiesCls) {
        jclass cls = env->FindClass("android/view/MotionEvent$PointerProperties");
        if (cls) {
            s_pointerPropertiesCls = reinterpret_cast<jclass>(env->NewGlobalRef(cls));
            s_ppCtor = env->GetMethodID(s_pointerPropertiesCls, "<init>", "()V");
            s_ppIdField = env->GetFieldID(s_pointerPropertiesCls, "id", "I");
            s_ppToolTypeField = env->GetFieldID(s_pointerPropertiesCls, "toolType", "I");
            env->DeleteLocalRef(cls);
        } else {
            env->ExceptionClear();
            LOG_INPUTW("InputBridge: could not find android.view.MotionEvent$PointerProperties class");
        }
    }

    if (!s_pointerCoordsCls) {
        jclass cls = env->FindClass("android/view/MotionEvent$PointerCoords");
        if (cls) {
            s_pointerCoordsCls = reinterpret_cast<jclass>(env->NewGlobalRef(cls));
            s_pcCtor = env->GetMethodID(s_pointerCoordsCls, "<init>", "()V");
            s_pcSetAxisValue = env->GetMethodID(s_pointerCoordsCls, "setAxisValue", "(IF)V");
            s_pcX = env->GetFieldID(s_pointerCoordsCls, "x", "F");
            s_pcY = env->GetFieldID(s_pointerCoordsCls, "y", "F");
            env->DeleteLocalRef(cls);
        } else {
            env->ExceptionClear();
            LOG_INPUTW("InputBridge: could not find android.view.MotionEvent$PointerCoords class");
        }
    }

    // Pre-allocate reusable single-pointer arrays for zero-allocation hot path
    if (s_pointerPropertiesCls && s_pointerCoordsCls && !s_ppArray) {
        jobject pp = env->NewObject(s_pointerPropertiesCls, s_ppCtor);
        if (pp) {
            env->SetIntField(pp, s_ppIdField, 0);
            env->SetIntField(pp, s_ppToolTypeField, 0 /* TOOL_TYPE_UNKNOWN */);
            s_ppObj = env->NewGlobalRef(pp);
            jobjectArray arr = env->NewObjectArray(1, s_pointerPropertiesCls, s_ppObj);
            s_ppArray = reinterpret_cast<jobjectArray>(env->NewGlobalRef(arr));
            env->DeleteLocalRef(arr);
            env->DeleteLocalRef(pp);
        }

        jobject pc = env->NewObject(s_pointerCoordsCls, s_pcCtor);
        if (pc) {
            s_pcObj = env->NewGlobalRef(pc);
            jobjectArray arr = env->NewObjectArray(1, s_pointerCoordsCls, s_pcObj);
            s_pcArray = reinterpret_cast<jobjectArray>(env->NewGlobalRef(arr));
            env->DeleteLocalRef(arr);
            env->DeleteLocalRef(pc);
        }

        // Reusable mouse look structures
        jobject mpp = env->NewObject(s_pointerPropertiesCls, s_ppCtor);
        if (mpp) {
            env->SetIntField(mpp, s_ppIdField, 0);
            env->SetIntField(mpp, s_ppToolTypeField, 3 /* TOOL_TYPE_MOUSE */);
            s_mousePpObj = env->NewGlobalRef(mpp);
            jobjectArray arr = env->NewObjectArray(1, s_pointerPropertiesCls, s_mousePpObj);
            s_mousePpArray = reinterpret_cast<jobjectArray>(env->NewGlobalRef(arr));
            env->DeleteLocalRef(arr);
            env->DeleteLocalRef(mpp);
        }

        jobject mpc = env->NewObject(s_pointerCoordsCls, s_pcCtor);
        if (mpc) {
            s_mousePcObj = env->NewGlobalRef(mpc);
            jobjectArray arr = env->NewObjectArray(1, s_pointerCoordsCls, s_mousePcObj);
            s_mousePcArray = reinterpret_cast<jobjectArray>(env->NewGlobalRef(arr));
            env->DeleteLocalRef(arr);
            env->DeleteLocalRef(mpc);
        }
    }

    if (!s_systemClockCls) {
        jclass cls = env->FindClass("android/os/SystemClock");
        if (cls) {
            s_systemClockCls = reinterpret_cast<jclass>(env->NewGlobalRef(cls));
            s_systemClockUptimeMillis = env->GetStaticMethodID(s_systemClockCls, "uptimeMillis", "()J");
            env->DeleteLocalRef(cls);
        } else {
            env->ExceptionClear();
            LOG_INPUTW("InputBridge: could not find android.os.SystemClock class");
        }
    }

    if (s_activity) {
        jclass actCls = env->GetObjectClass(s_activity);
        if (actCls) {
            s_dispatchKeyEvent = env->GetMethodID(actCls, "dispatchKeyEvent", "(Landroid/view/KeyEvent;)Z");
            s_onKeyDown = env->GetMethodID(actCls, "onKeyDown", "(ILandroid/view/KeyEvent;)Z");
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                s_onKeyDown = nullptr;
            }
            s_onKeyUp = env->GetMethodID(actCls, "onKeyUp", "(ILandroid/view/KeyEvent;)Z");
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                s_onKeyUp = nullptr;
            }
            s_dispatchTouchEvent = env->GetMethodID(actCls, "dispatchTouchEvent", "(Landroid/view/MotionEvent;)Z");
            s_dispatchGenericMotionEvent = env->GetMethodID(actCls, "dispatchGenericMotionEvent", "(Landroid/view/MotionEvent;)Z");
            s_processMotionEvent = env->GetMethodID(actCls, "processMotionEvent", "(Landroid/view/MotionEvent;)Z");
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                s_processMotionEvent = nullptr;
            }
            env->DeleteLocalRef(actCls);
        } else {
            env->ExceptionClear();
        }
    }

    LOG_INPUT("InputBridge initialized: act=%p, keyInit=%p, touchObtain=%p, joyObtain=%p, dispatchGen=%p, processMotion=%p",
              s_activity,
              (void*)(s_keyEventComplexInit ? s_keyEventComplexInit : s_keyEventSimpleInit),
              (void*)s_motionEventObtain,
              (void*)s_motionEventObtainJoystick,
              (void*)s_dispatchGenericMotionEvent,
              (void*)s_processMotionEvent);

    // Discover valid input device ID dynamically with JOYSTICK + MOUSE support (e.g. Device 6: virtual-input-unScale-1)
    jclass inputDeviceCls = env->FindClass("android/view/InputDevice");
    if (inputDeviceCls) {
        jmethodID getDeviceIds = env->GetStaticMethodID(inputDeviceCls, "getDeviceIds", "()[I");
        jmethodID getDevice = env->GetStaticMethodID(inputDeviceCls, "getDevice", "(I)Landroid/view/InputDevice;");
        jmethodID getSources = env->GetMethodID(inputDeviceCls, "getSources", "()I");
        if (getDeviceIds && getDevice && getSources) {
            jintArray idsArr = reinterpret_cast<jintArray>(env->CallStaticObjectMethod(inputDeviceCls, getDeviceIds));
            if (idsArr) {
                jsize len = env->GetArrayLength(idsArr);
                jint* ids = env->GetIntArrayElements(idsArr, nullptr);
                jint bestDevice = 6;
                for (jsize i = 0; i < len; i++) {
                    jobject dev = env->CallStaticObjectMethod(inputDeviceCls, getDevice, ids[i]);
                    if (dev) {
                        jint sources = env->CallIntMethod(dev, getSources);
                        // SOURCE_JOYSTICK = 0x01000010, SOURCE_MOUSE = 0x00002002, SOURCE_GAMEPAD = 0x00000401
                        bool hasJoy = (sources & 0x01000010) != 0;
                        bool hasMouse = (sources & 0x00002002) != 0;
                        bool hasGamepad = (sources & 0x00000401) != 0;
                        if (hasJoy && hasMouse) {
                            bestDevice = ids[i];
                            env->DeleteLocalRef(dev);
                            break;
                        } else if (hasJoy || hasGamepad) {
                            bestDevice = ids[i];
                        }
                        env->DeleteLocalRef(dev);
                    }
                }
                s_validDeviceId = bestDevice;
                env->ReleaseIntArrayElements(idsArr, ids, JNI_ABORT);
                env->DeleteLocalRef(idsArr);
            }
        } else {
            env->ExceptionClear();
            s_validDeviceId = 6;
        }
        env->DeleteLocalRef(inputDeviceCls);
    } else {
        env->ExceptionClear();
        s_validDeviceId = 6;
    }
    LOG_INPUT("InputBridge: using valid input deviceId=%d (JOYSTICK/MOUSE capable)", s_validDeviceId);

    // Enable all 48 GameActivity pointer axes natively
    void* peHandle = ::dlopen("libminecraftpe.so", RTLD_NOW | RTLD_NOLOAD);
    if (!peHandle) peHandle = ::dlopen("libminecraftpe.so", RTLD_NOW | RTLD_LOCAL);
    if (peHandle) {
        using EnableAxisFn = void (*)(int32_t);
        auto* fnEnable = reinterpret_cast<EnableAxisFn>(
            ::dlsym(peHandle, "GameActivityPointerAxes_enableAxis"));
        if (fnEnable) {
            for (int32_t i = 0; i < 48; i++) {
                fnEnable(i);
            }
            LOG_INPUT("InputBridge: Enabled all 48 GameActivity pointer axes natively");
        }
    }

    SetXboxControllerConnected(true);
    return true;
}

void InputBridge::Shutdown() {
    std::lock_guard<std::mutex> lk(s_mtx);
    JNIEnv* env = GetJNIEnv();
    if (env) {
        if (s_activity) {
            env->DeleteGlobalRef(s_activity);
            s_activity = nullptr;
        }
        if (s_keyEventCls) {
            env->DeleteGlobalRef(s_keyEventCls);
            s_keyEventCls = nullptr;
        }
        if (s_motionEventCls) {
            env->DeleteGlobalRef(s_motionEventCls);
            s_motionEventCls = nullptr;
        }
        if (s_pointerPropertiesCls) {
            env->DeleteGlobalRef(s_pointerPropertiesCls);
            s_pointerPropertiesCls = nullptr;
        }
        if (s_pointerCoordsCls) {
            env->DeleteGlobalRef(s_pointerCoordsCls);
            s_pointerCoordsCls = nullptr;
        }
        if (s_ppObj) { env->DeleteGlobalRef(s_ppObj); s_ppObj = nullptr; }
        if (s_pcObj) { env->DeleteGlobalRef(s_pcObj); s_pcObj = nullptr; }
        if (s_ppArray) { env->DeleteGlobalRef(s_ppArray); s_ppArray = nullptr; }
        if (s_pcArray) { env->DeleteGlobalRef(s_pcArray); s_pcArray = nullptr; }
        if (s_mousePpObj) { env->DeleteGlobalRef(s_mousePpObj); s_mousePpObj = nullptr; }
        if (s_mousePcObj) { env->DeleteGlobalRef(s_mousePcObj); s_mousePcObj = nullptr; }
        if (s_mousePpArray) { env->DeleteGlobalRef(s_mousePpArray); s_mousePpArray = nullptr; }
        if (s_mousePcArray) { env->DeleteGlobalRef(s_mousePcArray); s_mousePcArray = nullptr; }
        if (s_systemClockCls) {
            env->DeleteGlobalRef(s_systemClockCls);
            s_systemClockCls = nullptr;
        }
    }
    s_keyEventComplexInit = nullptr;
    s_keyEventSimpleInit = nullptr;
    s_motionEventObtain = nullptr;
    s_motionEventObtainJoystick = nullptr;
    s_motionEventRecycle = nullptr;
    s_systemClockUptimeMillis = nullptr;
    s_dispatchKeyEvent = nullptr;
    s_onKeyDown = nullptr;
    s_onKeyUp = nullptr;
    s_dispatchTouchEvent = nullptr;
    s_dispatchGenericMotionEvent = nullptr;
    s_processMotionEvent = nullptr;
}

void InputBridge::DispatchKey(int32_t keyCode, int32_t action, int32_t source) {
    JNIEnv* env = GetJNIEnv();
    if (!env) return;

    if (!s_activity) {
        jobject act = mcbedvr::xr::XrContextGetActivity();
        if (act) {
            Init(nullptr, act);
        }
    }
    if (!s_activity || !s_dispatchKeyEvent || !s_keyEventCls) return;

    jlong now = 0;
    if (s_systemClockCls && s_systemClockUptimeMillis) {
        now = env->CallStaticLongMethod(s_systemClockCls, s_systemClockUptimeMillis);
    }

    jobject keyEventObj = nullptr;
    if (s_keyEventComplexInit) {
        keyEventObj = env->NewObject(s_keyEventCls, s_keyEventComplexInit,
                                     now, now, (jint)action, (jint)keyCode,
                                     (jint)0, (jint)0, (jint)s_validDeviceId, (jint)0, (jint)0, (jint)source);
    } else if (s_keyEventSimpleInit) {
        keyEventObj = env->NewObject(s_keyEventCls, s_keyEventSimpleInit, (jint)action, (jint)keyCode);
    }

    if (keyEventObj) {
        jboolean handled = env->CallBooleanMethod(s_activity, s_dispatchKeyEvent, keyEventObj);
        if (!handled) {
            if (action == 0 && s_onKeyDown) {
                handled = env->CallBooleanMethod(s_activity, s_onKeyDown, (jint)keyCode, keyEventObj);
            } else if (action == 1 && s_onKeyUp) {
                handled = env->CallBooleanMethod(s_activity, s_onKeyUp, (jint)keyCode, keyEventObj);
            }
        }
        LOG_INPUT("DispatchKey(code=%d, action=%d, src=0x%x) -> handled=%d",
                  keyCode, action, source, (int)handled);
        env->DeleteLocalRef(keyEventObj);
    }
}

void InputBridge::DispatchTouch(float x, float y, int32_t action) {
    JNIEnv* env = GetJNIEnv();
    if (!env) return;

    if (!s_activity) {
        jobject act = mcbedvr::xr::XrContextGetActivity();
        if (act) {
            Init(nullptr, act);
        }
    }
    if (!s_activity || !s_dispatchTouchEvent || !s_motionEventCls || !s_motionEventObtain) return;

    static jlong s_downTime = 0;
    jlong now = 0;
    if (s_systemClockCls && s_systemClockUptimeMillis) {
        now = env->CallStaticLongMethod(s_systemClockCls, s_systemClockUptimeMillis);
    }
    if (action == 0 /* ACTION_DOWN */) {
        s_downTime = now;
    }

    jobject motionEventObj = env->CallStaticObjectMethod(
        s_motionEventCls, s_motionEventObtain,
        s_downTime, now, (jint)action, (jfloat)x, (jfloat)y, (jint)0);

    if (motionEventObj) {
        jboolean handled = env->CallBooleanMethod(s_activity, s_dispatchTouchEvent, motionEventObj);
        if (action != 2 /* don't spam MOVE in logs */) {
            LOG_INPUT("DispatchTouch(x=%.1f, y=%.1f, action=%d) -> handled=%d",
                      x, y, action, (int)handled);
        }
        if (s_motionEventRecycle) {
            env->CallVoidMethod(motionEventObj, s_motionEventRecycle);
        }
        env->DeleteLocalRef(motionEventObj);
    }
}

void InputBridge::DispatchJoystick(float lx, float ly, float rx, float ry, float lt, float rt) {
    JNIEnv* env = GetJNIEnv();
    if (!env) return;

    if (!s_activity) {
        jobject act = mcbedvr::xr::XrContextGetActivity();
        if (act) {
            Init(nullptr, act);
        }
    }
    if (!s_activity || !s_dispatchGenericMotionEvent || !s_motionEventObtainJoystick || !s_pcObj) return;

    // AXIS_X = 0 (Left stick X)
    // AXIS_Y = 1 (Left stick Y, negative forward in Android)
    // AXIS_Z = 11 (Right stick X)
    // AXIS_RZ = 14 (Right stick Y)
    // AXIS_BRAKE = 23 (Left Trigger)
    // AXIS_GAS = 22 (Right Trigger)
    // AXIS_LTRIGGER = 17
    // AXIS_RTRIGGER = 18
    env->CallVoidMethod(s_pcObj, s_pcSetAxisValue, 0 /* AXIS_X */, lx);
    env->CallVoidMethod(s_pcObj, s_pcSetAxisValue, 1 /* AXIS_Y */, ly);
    env->CallVoidMethod(s_pcObj, s_pcSetAxisValue, 11 /* AXIS_Z */, rx);
    env->CallVoidMethod(s_pcObj, s_pcSetAxisValue, 14 /* AXIS_RZ */, ry);
    env->CallVoidMethod(s_pcObj, s_pcSetAxisValue, 12 /* AXIS_RX */, rx);
    env->CallVoidMethod(s_pcObj, s_pcSetAxisValue, 13 /* AXIS_RY */, ry);
    env->CallVoidMethod(s_pcObj, s_pcSetAxisValue, 23 /* AXIS_BRAKE */, lt);
    env->CallVoidMethod(s_pcObj, s_pcSetAxisValue, 22 /* AXIS_GAS */, rt);
    env->CallVoidMethod(s_pcObj, s_pcSetAxisValue, 17 /* AXIS_LTRIGGER */, lt);
    env->CallVoidMethod(s_pcObj, s_pcSetAxisValue, 18 /* AXIS_RTRIGGER */, rt);

    env->SetFloatField(s_pcObj, s_pcX, lx);
    env->SetFloatField(s_pcObj, s_pcY, ly);

    jlong now = 0;
    if (s_systemClockCls && s_systemClockUptimeMillis) {
        now = env->CallStaticLongMethod(s_systemClockCls, s_systemClockUptimeMillis);
    }

    jobject motionEventObj = env->CallStaticObjectMethod(
        s_motionEventCls, s_motionEventObtainJoystick,
        now, now, 2 /* ACTION_MOVE */, 1 /* pointerCount */,
        s_ppArray, s_pcArray,
        0 /* metaState */, 0 /* buttonState */,
        1.0f /* xPrecision */, 1.0f /* yPrecision */,
        s_validDeviceId, 0 /* edgeFlags */,
        0x01000411 /* SOURCE_JOYSTICK | SOURCE_GAMEPAD | SOURCE_KEYBOARD */, 0 /* flags */);

    if (motionEventObj) {
        jboolean handled = false;
        if (s_processMotionEvent) {
            handled = env->CallBooleanMethod(s_activity, s_processMotionEvent, motionEventObj);
        }
        if (!handled && s_dispatchGenericMotionEvent) {
            handled = env->CallBooleanMethod(s_activity, s_dispatchGenericMotionEvent, motionEventObj);
        }
        if (s_motionEventRecycle) {
            env->CallVoidMethod(motionEventObj, s_motionEventRecycle);
        }
        env->DeleteLocalRef(motionEventObj);
    }
}

void InputBridge::DispatchLookDelta(float dx, float dy) {
    JNIEnv* env = GetJNIEnv();
    if (!env) return;

    if (!s_activity) {
        jobject act = mcbedvr::xr::XrContextGetActivity();
        if (act) {
            Init(nullptr, act);
        }
    }
    if (!s_activity || (!s_processMotionEvent && !s_dispatchGenericMotionEvent) || !s_motionEventObtainJoystick || !s_mousePcObj) return;

    // AXIS_RELATIVE_X = 27, AXIS_RELATIVE_Y = 28
    // AXIS_X = 0, AXIS_Y = 1
    env->CallVoidMethod(s_mousePcObj, s_pcSetAxisValue, 27 /* AXIS_RELATIVE_X */, dx);
    env->CallVoidMethod(s_mousePcObj, s_pcSetAxisValue, 28 /* AXIS_RELATIVE_Y */, dy);
    env->CallVoidMethod(s_mousePcObj, s_pcSetAxisValue, 0 /* AXIS_X */, dx);
    env->CallVoidMethod(s_mousePcObj, s_pcSetAxisValue, 1 /* AXIS_Y */, dy);
    env->SetFloatField(s_mousePcObj, s_pcX, dx);
    env->SetFloatField(s_mousePcObj, s_pcY, dy);

    jlong now = 0;
    if (s_systemClockCls && s_systemClockUptimeMillis) {
        now = env->CallStaticLongMethod(s_systemClockCls, s_systemClockUptimeMillis);
    }

    // Captured pointer events in GameActivity use ACTION_MOVE (2)
    jobject motionEventObj = env->CallStaticObjectMethod(
        s_motionEventCls, s_motionEventObtainJoystick,
        now, now, 2 /* ACTION_MOVE */, 1 /* pointerCount */,
        s_mousePpArray, s_mousePcArray,
        0 /* metaState */, 0 /* buttonState */,
        1.0f /* xPrecision */, 1.0f /* yPrecision */,
        s_validDeviceId, 0 /* edgeFlags */,
        0x00002002 /* SOURCE_MOUSE */, 0 /* flags */);

    if (motionEventObj) {
        if (s_processMotionEvent) {
            env->CallBooleanMethod(s_activity, s_processMotionEvent, motionEventObj);
        } else if (s_dispatchGenericMotionEvent) {
            env->CallBooleanMethod(s_activity, s_dispatchGenericMotionEvent, motionEventObj);
        }
        if (s_motionEventRecycle) {
            env->CallVoidMethod(motionEventObj, s_motionEventRecycle);
        }
        env->DeleteLocalRef(motionEventObj);
    }
}

void InputBridge::SetPointerCapture(bool locked) {
    JNIEnv* env = GetJNIEnv();
    if (!env || !s_activity) return;

    jclass actCls = env->GetObjectClass(s_activity);
    if (!actCls) return;

    // Set mCursorLocked field directly on MainActivity so Bedrock's window focus handler maintains capture
    jfieldID field = env->GetFieldID(actCls, "mCursorLocked", "Z");
    if (field) {
        env->SetBooleanField(s_activity, field, locked ? JNI_TRUE : JNI_FALSE);
        LOG_INPUT("SetPointerCapture(%s) updated MainActivity.mCursorLocked field", locked ? "true" : "false");
    } else {
        env->ExceptionClear();
    }

    const char* methodName = locked ? "lockCursor" : "unlockCursor";
    jmethodID method = env->GetMethodID(actCls, methodName, "()V");
    if (method) {
        env->CallVoidMethod(s_activity, method);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            LOG_INPUTW("SetPointerCapture(%s) direct invocation exception cleared", locked ? "true" : "false");
        } else {
            LOG_INPUT("SetPointerCapture(%s) applied to MainActivity", locked ? "true" : "false");
        }
    } else {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(actCls);
}

void InputBridge::SetXboxControllerConnected(bool connected) {
    static void* s_handle = nullptr;
    if (!s_handle) {
        s_handle = ::dlopen("libminecraftpe.so", RTLD_NOW | RTLD_NOLOAD);
        if (!s_handle) s_handle = ::dlopen("libminecraftpe.so", RTLD_NOW | RTLD_LOCAL);
    }
    if (!s_handle) return;

    using DeviceAddedFn = void (*)(JNIEnv*, jobject, jint);
    using SetDoubleTrigFn = void (*)(JNIEnv*, jobject, jboolean);
    using SetDetailsFn = void (*)(JNIEnv*, jobject, jint, jboolean, jboolean);
    using SetXboxFn = void (*)(JNIEnv*, jobject, jboolean);

    static auto* fnAdded = reinterpret_cast<DeviceAddedFn>(
        ::dlsym(s_handle, "Java_com_mojang_minecraftpe_input_JellyBeanDeviceManager_onInputDeviceAddedNative"));
    static auto* fnDouble = reinterpret_cast<SetDoubleTrigFn>(
        ::dlsym(s_handle, "Java_com_mojang_minecraftpe_input_JellyBeanDeviceManager_setDoubleTriggersSupportedNative"));
    static auto* fnDetails = reinterpret_cast<SetDetailsFn>(
        ::dlsym(s_handle, "Java_com_mojang_minecraftpe_input_JellyBeanDeviceManager_setControllerDetailsNative"));
    static auto* fnXbox = reinterpret_cast<SetXboxFn>(
        ::dlsym(s_handle, "Java_com_mojang_minecraftpe_input_JellyBeanDeviceManager_setFoundXboxControllerNative"));

    JNIEnv* env = GetJNIEnv();
    if (connected) {
        if (fnAdded) {
            fnAdded(env, nullptr, s_validDeviceId);
            fnAdded(env, nullptr, 7); // Device 7: pvr-virtual-input-0
        }
        if (fnDouble) fnDouble(env, nullptr, JNI_TRUE);
        if (fnDetails) {
            fnDetails(env, nullptr, s_validDeviceId, JNI_FALSE, JNI_TRUE);
            fnDetails(env, nullptr, 7, JNI_FALSE, JNI_TRUE);
        }
        if (fnXbox) fnXbox(env, nullptr, JNI_TRUE);
        LOG_INPUT("InputBridge: Registered Xbox controller (deviceId=%d & 7) in JellyBeanDeviceManager", s_validDeviceId);
    } else {
        if (fnXbox) fnXbox(env, nullptr, JNI_FALSE);
    }
}

}  // namespace mcbedvr::xr
