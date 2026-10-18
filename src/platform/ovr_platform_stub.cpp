#include <jni.h>
#include <android/log.h>
#include <cstdint>
#include <cstring>
#include <queue>
#include <string>

#define LOG_TAG "mcbedvr_ovrplatform"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define OVR_EXPORT __attribute__((visibility("default")))

typedef uint64_t ovrID;
typedef uint64_t ovrRequest;

typedef enum {
    ovrPlatformInitialize_Success = 0,
    ovrPlatformInitialize_Uninitialized = -1,
    ovrPlatformInitialize_PreAlreadyInitialized = -2,
    ovrPlatformInitialize_FileInvalid = -3,
    ovrPlatformInitialize_SignatureInvalid = -4,
    ovrPlatformInitialize_UnableToVerify = -5,
    ovrPlatformInitialize_VersionMismatch = -6,
    ovrPlatformInitialize_Unknown = -7
} ovrPlatformInitializeResult;

typedef enum {
    ovrMessage_Entitlement_GetIsViewerEntitled = 0x186B58B1,
    ovrMessage_User_GetAccessToken = 0x06A85ABE,
    ovrMessage_IAP_GetViewerPurchases = 0x14E56C2C,
    ovrMessage_IAP_GetProductsBySKU = 0x7E9AC463,
    ovrMessage_IAP_ConsumePurchase = 0x1FBB72D9,
    ovrMessage_IAP_LaunchCheckoutFlow = 0x3F9E0D0F,
    ovrMessage_Unknown = 0
} ovrMessageType;

struct ovrMessage {
    ovrMessageType type;
    ovrRequest requestID;
    bool isError;
    std::string stringPayload;
};

struct ovrProductArray {
    int count = 0;
};

struct ovrPurchaseArray {
    int count = 0;
};

static std::queue<ovrMessage*> g_msgQueue;
static ovrProductArray g_emptyProductArray;
static ovrPurchaseArray g_emptyPurchaseArray;

extern "C" {

OVR_EXPORT ovrPlatformInitializeResult ovr_PlatformInitializeAndroid(const char* appId, jobject activityObject, JNIEnv* jniEnv) {
    LOGI("[ovrplatform_stub] ovr_PlatformInitializeAndroid called for appId: %s -> SUCCESS", appId ? appId : "null");
    return ovrPlatformInitialize_Success;
}

OVR_EXPORT ovrPlatformInitializeResult ovr_PlatformInitializeAndroidHelper(const char* appId, jobject activityObject, JNIEnv* jniEnv) {
    LOGI("[ovrplatform_stub] ovr_PlatformInitializeAndroidHelper called -> SUCCESS");
    return ovrPlatformInitialize_Success;
}

OVR_EXPORT ovrRequest ovr_Entitlement_GetIsViewerEntitled() {
    LOGI("[ovrplatform_stub] ovr_Entitlement_GetIsViewerEntitled called -> queueing ENTITLED success!");
    ovrMessage* msg = new ovrMessage();
    msg->type = ovrMessage_Entitlement_GetIsViewerEntitled;
    msg->requestID = 1ULL;
    msg->isError = false;
    g_msgQueue.push(msg);
    return 1ULL;
}

OVR_EXPORT ovrMessage* ovr_PopMessage() {
    if (g_msgQueue.empty()) {
        return nullptr;
    }
    ovrMessage* msg = g_msgQueue.front();
    g_msgQueue.pop();
    LOGI("[ovrplatform_stub] ovr_PopMessage returning msg type=0x%08X", (unsigned int)msg->type);
    return msg;
}

OVR_EXPORT void ovr_FreeMessage(ovrMessage* message) {
    if (message) {
        delete message;
    }
}

OVR_EXPORT bool ovr_Message_IsError(const ovrMessage* message) {
    if (!message) return false;
    return message->isError;
}

OVR_EXPORT const void* ovr_Message_GetError(const ovrMessage* message) {
    return nullptr;
}

OVR_EXPORT const char* ovr_Error_GetMessage(const void* error) {
    return "";
}

OVR_EXPORT ovrMessageType ovr_Message_GetType(const ovrMessage* message) {
    if (!message) return ovrMessage_Unknown;
    return message->type;
}

OVR_EXPORT const char* ovr_Message_GetString(const ovrMessage* message) {
    if (!message) return "";
    return message->stringPayload.c_str();
}

OVR_EXPORT ovrRequest ovr_User_GetAccessToken() {
    LOGI("[ovrplatform_stub] ovr_User_GetAccessToken called");
    ovrMessage* msg = new ovrMessage();
    msg->type = ovrMessage_User_GetAccessToken;
    msg->requestID = 2ULL;
    msg->isError = false;
    msg->stringPayload = "mcbedvr_dummy_token";
    g_msgQueue.push(msg);
    return 2ULL;
}

OVR_EXPORT ovrRequest ovr_IAP_ConsumePurchase(const char* sku) {
    LOGI("[ovrplatform_stub] ovr_IAP_ConsumePurchase: %s", sku ? sku : "null");
    return 3ULL;
}

OVR_EXPORT ovrRequest ovr_IAP_GetProductsBySKU(const char** skus, int count) {
    LOGI("[ovrplatform_stub] ovr_IAP_GetProductsBySKU (count=%d)", count);
    return 4ULL;
}

OVR_EXPORT ovrRequest ovr_IAP_GetViewerPurchases() {
    LOGI("[ovrplatform_stub] ovr_IAP_GetViewerPurchases");
    return 5ULL;
}

OVR_EXPORT ovrRequest ovr_IAP_LaunchCheckoutFlow(const char* sku) {
    LOGI("[ovrplatform_stub] ovr_IAP_LaunchCheckoutFlow: %s", sku ? sku : "null");
    return 6ULL;
}

OVR_EXPORT const ovrProductArray* ovr_Message_GetProductArray(const ovrMessage* message) {
    return &g_emptyProductArray;
}

OVR_EXPORT const void* ovr_Message_GetPurchase(const ovrMessage* message) {
    return nullptr;
}

OVR_EXPORT const ovrPurchaseArray* ovr_Message_GetPurchaseArray(const ovrMessage* message) {
    return &g_emptyPurchaseArray;
}

OVR_EXPORT const void* ovr_ProductArray_GetElement(const ovrProductArray* array, size_t index) {
    return nullptr;
}

OVR_EXPORT size_t ovr_ProductArray_GetSize(const ovrProductArray* array) {
    return 0;
}

OVR_EXPORT const char* ovr_Product_GetFormattedPrice(const void* product) {
    return "$0.00";
}

OVR_EXPORT const char* ovr_Product_GetSKU(const void* product) {
    return "";
}

OVR_EXPORT const void* ovr_PurchaseArray_GetElement(const ovrPurchaseArray* array, size_t index) {
    return nullptr;
}

OVR_EXPORT size_t ovr_PurchaseArray_GetSize(const ovrPurchaseArray* array) {
    return 0;
}

OVR_EXPORT ovrID ovr_Purchase_GetPurchaseID(const void* purchase) {
    return 0ULL;
}

OVR_EXPORT const char* ovr_Purchase_GetSKU(const void* purchase) {
    return "";
}

} // extern "C"
