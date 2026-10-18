#include <android/log.h>
#include <cstdint>
#include <cstring>

#define LOG_TAG "mcbedvr_ovrfmod"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#define EXPORT __attribute__((visibility("default")))

typedef int FMOD_RESULT;
typedef struct FMOD_DSP_STATE FMOD_DSP_STATE;
typedef struct FMOD_DSP_BUFFER_ARRAY FMOD_DSP_BUFFER_ARRAY;

struct FMOD_DSP_DESCRIPTION {
    unsigned int pluginsdkversion;
    char name[32];
    unsigned int version;
    int numinputbuffers;
    int numoutputbuffers;
    FMOD_RESULT (*create)(FMOD_DSP_STATE* dsp_state);
    FMOD_RESULT (*release)(FMOD_DSP_STATE* dsp_state);
    FMOD_RESULT (*reset)(FMOD_DSP_STATE* dsp_state);
    FMOD_RESULT (*read)(FMOD_DSP_STATE* dsp_state, float* inbuffer, float* outbuffer, unsigned int length, int inchannels, int* outchannels);
    FMOD_RESULT (*process)(FMOD_DSP_STATE* dsp_state, unsigned int length, const FMOD_DSP_BUFFER_ARRAY* inbufferarray, FMOD_DSP_BUFFER_ARRAY* outbufferarray, int inputsidle, int op);
    FMOD_RESULT (*setposition)(FMOD_DSP_STATE* dsp_state, unsigned int pos);
    int numparameters;
    void* paramdesc;
    FMOD_RESULT (*setparameterfloat)(FMOD_DSP_STATE* dsp_state, int index, float value);
    FMOD_RESULT (*setparameterint)(FMOD_DSP_STATE* dsp_state, int index, int value);
    FMOD_RESULT (*setparameterbool)(FMOD_DSP_STATE* dsp_state, int index, int value);
    FMOD_RESULT (*setparameterdata)(FMOD_DSP_STATE* dsp_state, int index, void* data, unsigned int length);
    FMOD_RESULT (*getparameterfloat)(FMOD_DSP_STATE* dsp_state, int index, float* value, char* valuestr);
    FMOD_RESULT (*getparameterint)(FMOD_DSP_STATE* dsp_state, int index, int* value, char* valuestr);
    FMOD_RESULT (*getparameterbool)(FMOD_DSP_STATE* dsp_state, int index, int* value, char* valuestr);
    FMOD_RESULT (*getparameterdata)(FMOD_DSP_STATE* dsp_state, int index, void** data, unsigned int* length, char* valuestr);
    FMOD_RESULT (*shouldiprocess)(FMOD_DSP_STATE* dsp_state, int inputsidle, unsigned int length, int inmask, int inchannels, int speakermode);
    void* userdata;
    FMOD_RESULT (*sys_register)(FMOD_DSP_STATE* dsp_state);
    FMOD_RESULT (*sys_deregister)(FMOD_DSP_STATE* dsp_state);
    FMOD_RESULT (*sys_mix)(FMOD_DSP_STATE* dsp_state, int stage);
};

static FMOD_RESULT DummyCreate(FMOD_DSP_STATE*) { return 0; }
static FMOD_RESULT DummyRelease(FMOD_DSP_STATE*) { return 0; }
static FMOD_RESULT DummyReset(FMOD_DSP_STATE*) { return 0; }
static FMOD_RESULT DummyProcess(FMOD_DSP_STATE*, unsigned int, const FMOD_DSP_BUFFER_ARRAY*, FMOD_DSP_BUFFER_ARRAY*, int, int) { return 0; }

static FMOD_DSP_DESCRIPTION g_dummyDspDesc = {
    0x00010800, // SDK version
    "Oculus Spatializer",
    0x00010000,
    1, 1,
    DummyCreate,
    DummyRelease,
    DummyReset,
    nullptr,
    DummyProcess,
    nullptr,
    0, nullptr,
    nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr
};

extern "C" {

EXPORT FMOD_DSP_DESCRIPTION* FMODGetDSPDescription() {
    LOGI("[ovrfmod_stub] FMODGetDSPDescription called -> returning stub DSP description");
    return &g_dummyDspDesc;
}

EXPORT int OSP_FMOD_Initialize(int, int) {
    LOGI("[ovrfmod_stub] OSP_FMOD_Initialize called -> returning 0 (FMOD_OK)");
    return 0;
}

EXPORT int OSP_FMOD_SetGlobalScale(float) { return 0; }
EXPORT int SetOvrFMODGlobalScalef(float) { return 0; }
EXPORT int OSP_FMOD_SetEarlyReflectionsEnabled(int) { return 0; }
EXPORT int OSP_FMOD_SetLateReverberationEnabled(int) { return 0; }
EXPORT int OSP_FMOD_SetSimpleBoxRoomParameters(float, float, float, float, float, float) { return 0; }
EXPORT int OSP_FMOD_SetBypass(int) { return 0; }
EXPORT int OSP_FMOD_SetGain(float) { return 0; }

// ovrAudio functions
EXPORT int ovrAudio_Initialize() { return 0; }
EXPORT void ovrAudio_Shutdown() {}
EXPORT const char* ovrAudio_GetVersion(int* major, int* minor, int* patch) {
    if (major) *major = 1; if (minor) *minor = 0; if (patch) *patch = 0;
    return "1.0.0";
}
EXPORT int ovrAudio_CreateContext(void*, void*) { return 0; }
EXPORT void ovrAudio_DestroyContext(void*) {}

// Pure ARM assembly implementations of AEABI arithmetic helpers
// Using native ARMv7-A hardware division (udiv/sdiv) to guarantee zero recursion
__asm__(
    ".text\n"
    ".syntax unified\n"
    ".arm\n"
    ".align 2\n"

    ".global __aeabi_uidiv\n"
    ".type __aeabi_uidiv, %function\n"
    "__aeabi_uidiv:\n"
    "    cmp r1, #0\n"
    "    bxeq lr\n"
    "    udiv r0, r0, r1\n"
    "    bx lr\n"

    ".global __aeabi_idiv\n"
    ".type __aeabi_idiv, %function\n"
    "__aeabi_idiv:\n"
    "    cmp r1, #0\n"
    "    bxeq lr\n"
    "    sdiv r0, r0, r1\n"
    "    bx lr\n"

    ".global __aeabi_uidivmod\n"
    ".type __aeabi_uidivmod, %function\n"
    "__aeabi_uidivmod:\n"
    "    cmp r1, #0\n"
    "    bxeq lr\n"
    "    udiv r2, r0, r1\n"
    "    mul r3, r2, r1\n"
    "    sub r1, r0, r3\n"
    "    mov r0, r2\n"
    "    bx lr\n"

    ".global __aeabi_idivmod\n"
    ".type __aeabi_idivmod, %function\n"
    "__aeabi_idivmod:\n"
    "    cmp r1, #0\n"
    "    bxeq lr\n"
    "    sdiv r2, r0, r1\n"
    "    mul r3, r2, r1\n"
    "    sub r1, r0, r3\n"
    "    mov r0, r2\n"
    "    bx lr\n"

    ".global __aeabi_uldivmod\n"
    ".type __aeabi_uldivmod, %function\n"
    "__aeabi_uldivmod:\n"
    "    push {r6, lr}\n"
    "    sub sp, sp, #16\n"
    "    add r6, sp, #8\n"
    "    str r6, [sp]\n"
    "    bl __udivmoddi4\n"
    "    ldr r2, [sp, #8]\n"
    "    ldr r3, [sp, #12]\n"
    "    add sp, sp, #16\n"
    "    pop {r6, pc}\n"

    ".global __aeabi_ldiv0\n"
    ".type __aeabi_ldiv0, %function\n"
    "__aeabi_ldiv0:\n"
    "    mov r0, #0\n"
    "    mov r1, #0\n"
    "    bx lr\n"

    ".global __aeabi_idiv0\n"
    ".type __aeabi_idiv0, %function\n"
    "__aeabi_idiv0:\n"
    "    mov r0, #0\n"
    "    bx lr\n"
);

EXPORT double __aeabi_ui2d(unsigned int a) { return (double)a; }
EXPORT double __aeabi_i2d(int a) { return (double)a; }
EXPORT double __aeabi_ul2d(unsigned long long a) { return (double)a; }
EXPORT double __aeabi_l2d(long long a) { return (double)a; }
EXPORT double __aeabi_f2d(float a) { return (double)a; }
EXPORT unsigned int __aeabi_d2uiz(double a) { return (unsigned int)a; }
EXPORT unsigned long long __aeabi_d2ulz(double a) { return (unsigned long long)a; }
EXPORT double __aeabi_ddiv(double a, double b) { return b != 0.0 ? (a / b) : 0.0; }
EXPORT double __aeabi_dmul(double a, double b) { return a * b; }
EXPORT double __aeabi_dsub(double a, double b) { return a - b; }

typedef struct { long long quot; long long rem; } ldivmod_result;

EXPORT ldivmod_result __gnu_ldivmod_helper(long long a, long long b, long long *rem) {
    ldivmod_result r;
    r.quot = 0;
    r.rem = 0;
    if (rem) *rem = 0;
    return r;
}

} // extern "C"
