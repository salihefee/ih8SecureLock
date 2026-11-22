#include <android/log.h>
#include <android/api-level.h>
#include <jni.h>
#include <string.h>

#include "binder.hpp"
#include "zygisk.hpp"

#define LOGD(fmt, ...) \
    __android_log_print(ANDROID_LOG_DEBUG, "ih8SecureLock", "[%d] [%s] " fmt, __LINE__, PROC_NAME, ##__VA_ARGS__)

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define FLAG_SECURE 0x00002000

#define I_WINDOW_SESSION_DESC u"android.view.IWindowSession"
#define I_ACTIVITY_TASKMANAGER_DESC u"android.app.IActivityTaskManager"

static int sdk = 0;
static uint32_t relayout_code = 0;
static uint32_t relayoutAsync_code = 0;
static uint32_t relayout2_code = 0;
static uint32_t relayoutAsync2_code = 0;
static uint32_t registerScreenCaptureObserver_code = 0;

static char PROC_NAME[256] = "";
static jmethodID parcelSize, parcelPosition, parcelSetPosition;
static jmethodID parcelReadString, parcelReadBinder, parcelReadInt, parcelWriteInt;
static jmethodID parcelWriteNoException;
static jboolean (*transactOrig)(JNIEnv*, jobject, jint, jobject, jobject, jint);

static bool isRelayout(uint32_t code) {
    return code && (code == relayout_code || code == relayoutAsync_code ||
                    code == relayout2_code || code == relayoutAsync2_code);
}

static bool descriptorEquals(JNIEnv* env, jstring value, const char16_t* expected, size_t length) {
    if (!value || env->GetStringLength(value) != jsize(length)) return false;
    const jchar* chars = env->GetStringChars(value, nullptr);
    if (!chars) return false;
    const bool equal = memcmp(chars, expected, length * sizeof(char16_t)) == 0;
    env->ReleaseStringChars(value, chars);
    return equal;
}

static bool getTransactionCodes(JNIEnv* env) {
    relayout_code = getStaticIntFieldJni(env, BINDER_STUB("android/view/IWindowSession"), BINDER_TRSCTN("relayout"));
    relayoutAsync_code = getStaticIntFieldJni(env, BINDER_STUB("android/view/IWindowSession"), BINDER_TRSCTN("relayoutAsync"));
    registerScreenCaptureObserver_code =
        getStaticIntFieldJni(env, BINDER_STUB("android/app/IActivityTaskManager"), BINDER_TRSCTN("registerScreenCaptureObserver"));

    if (sdk >= 37) {
        // Optional, introduced since 37, maybe removed in the future.
        // https://android.googlesource.com/platform/frameworks/base/+/refs/tags/android-17.0.0_r1/core/java/android/view/IWindowSession.aidl#93
        relayout2_code = getStaticIntFieldJni(
            env, BINDER_STUB("android/view/IWindowSession"), BINDER_TRSCTN("relayout2"));
        relayoutAsync2_code = getStaticIntFieldJni(
            env, BINDER_STUB("android/view/IWindowSession"), BINDER_TRSCTN("relayoutAsync2"));
    }

    if (registerScreenCaptureObserver_code == 0 && relayoutAsync_code == 0 &&
        relayout_code == 0 && relayoutAsync2_code == 0 && relayout2_code == 0) {
        LOGD("ERROR getTransactionCodes: Could not get any transaction codes");
        return false;
    }
    return true;
}

// Window requests contain Binder objects: marshall/unmarshall would discard
// their object table. Read and overwrite only the flags integer in the original
// Parcel, and always restore its cursor before forwarding the transaction.
static jboolean transactHook(JNIEnv* env, jobject self, jint code, jobject data,
                             jobject reply, jint flags) {
    const bool relayout = isRelayout(uint32_t(code));
    const bool observer = code && uint32_t(code) == registerScreenCaptureObserver_code;
    if ((!relayout && !observer) || !data || env->ExceptionCheck())
        return transactOrig(env, self, code, data, reply, flags);

    const jint position = env->CallIntMethod(data, parcelPosition);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return transactOrig(env, self, code, data, reply, flags);
    }
    bool suppressObserver = false;
    do {
        const jint size = env->CallIntMethod(data, parcelSize);
        const jint header = getBinderHeadersLen(sdk);
        if (env->ExceptionCheck() || size < header + 4) break;
        env->CallVoidMethod(data, parcelSetPosition, header);
        if (env->ExceptionCheck()) break;
        auto descriptor = static_cast<jstring>(env->CallObjectMethod(data, parcelReadString));
        if (env->ExceptionCheck()) break;
        const bool matches = relayout
            ? descriptorEquals(env, descriptor, I_WINDOW_SESSION_DESC, STR_LEN(I_WINDOW_SESSION_DESC))
            : descriptorEquals(env, descriptor, I_ACTIVITY_TASKMANAGER_DESC, STR_LEN(I_ACTIVITY_TASKMANAGER_DESC));
        if (descriptor) env->DeleteLocalRef(descriptor);
        if (env->ExceptionCheck() || !matches) break;
        if (observer) {
            suppressObserver = true;
            break;
        }

        jobject window = env->CallObjectMethod(data, parcelReadBinder);
        if (window) env->DeleteLocalRef(window);
        if (env->ExceptionCheck()) break;
        jint cursor = env->CallIntMethod(data, parcelPosition);
        // Android 10/11 place a sequence integer before the typed LayoutParams.
        if (sdk <= 30) cursor += 4;
        // Presence marker followed by width, height, x, y, type, flags.
        if (env->ExceptionCheck() || cursor < 0 || cursor > size || size - cursor < 7 * 4) break;
        env->CallVoidMethod(data, parcelSetPosition, cursor);
        if (env->ExceptionCheck()) break;
        const jint present = env->CallIntMethod(data, parcelReadInt);
        if (env->ExceptionCheck() || present != 1) break;
        const jint flagsPosition = cursor + 6 * 4;
        env->CallVoidMethod(data, parcelSetPosition, flagsPosition);
        if (env->ExceptionCheck()) break;
        const jint windowFlags = env->CallIntMethod(data, parcelReadInt);
        if (env->ExceptionCheck()) break;
        if (windowFlags & FLAG_SECURE) {
            env->CallVoidMethod(data, parcelSetPosition, flagsPosition);
            if (env->ExceptionCheck()) break;
            env->CallVoidMethod(data, parcelWriteInt, windowFlags & ~FLAG_SECURE);
            if (!env->ExceptionCheck()) LOGD("Bypassed secure lock (JNI)");
        }
    } while (false);
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->CallVoidMethod(data, parcelSetPosition, position);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return transactOrig(env, self, code, data, reply, flags);
    }
    if (suppressObserver) {
        // A synchronous Java Binder call expects both true and a valid reply
        // exception header, rather than the native status_t used by the PLT hook.
        if (reply && !(flags & 1)) {
            env->CallVoidMethod(reply, parcelWriteNoException);
            if (!env->ExceptionCheck()) env->CallVoidMethod(reply, parcelSetPosition, 0);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                return transactOrig(env, self, code, data, reply, flags);
            }
        }
        LOGD("Bypassed screenshot listener (JNI)");
        return JNI_TRUE;
    }
    return transactOrig(env, self, code, data, reply, flags);
}

static bool hookBinder(zygisk::Api* api, JNIEnv* env) {
    jclass parcel = env->FindClass("android/os/Parcel");
    if (!parcel) { env->ExceptionClear(); return false; }
    // Resolve sequentially: do not make JNI calls with a pending exception.
    struct Method { jmethodID* id; const char* name; const char* signature; };
    Method methods[] = {
        {&parcelSize, "dataSize", "()I"},
        {&parcelPosition, "dataPosition", "()I"},
        {&parcelSetPosition, "setDataPosition", "(I)V"},
        {&parcelReadString, "readString", "()Ljava/lang/String;"},
        {&parcelReadBinder, "readStrongBinder", "()Landroid/os/IBinder;"},
        {&parcelReadInt, "readInt", "()I"},
        {&parcelWriteInt, "writeInt", "(I)V"},
        {&parcelWriteNoException, "writeNoException", "()V"},
    };
    for (auto& method : methods) {
        *method.id = env->GetMethodID(parcel, method.name, method.signature);
        if (!*method.id || env->ExceptionCheck()) {
            env->ExceptionClear();
            env->DeleteLocalRef(parcel);
            return false;
        }
    }
    env->DeleteLocalRef(parcel);
    JNINativeMethod hook[] = {{const_cast<char*>("transactNative"),
        const_cast<char*>("(ILandroid/os/Parcel;Landroid/os/Parcel;I)Z"),
        reinterpret_cast<void*>(transactHook)}};
    api->hookJniNativeMethods(env, "android/os/BinderProxy", hook, 1);
    transactOrig = reinterpret_cast<decltype(transactOrig)>(hook[0].fnPtr);
    if (!transactOrig || transactOrig == transactHook) {
        LOGD("ERROR: BinderProxy JNI hook unavailable");
        return false;
    }
    return true;
}

static bool run(zygisk::Api* api, JNIEnv* env) {
    sdk = android_get_device_api_level();
    if (sdk <= 0) {
        LOGD("ERROR android_get_device_api_level: %d", sdk);
        return false;
    }
    if (!getTransactionCodes(env)) return false;
    if (!hookBinder(api, env)) return false;
    return true;
}

class ih8SecureLock : public zygisk::ModuleBase {
   public:
    void onLoad(zygisk::Api* api, JNIEnv* env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs* args) override {
        if (api->getFlags() & zygisk::StateFlag::PROCESS_ON_DENYLIST) {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }
        const char* name = env->GetStringUTFChars(args->nice_name, nullptr);
        if (!name) {
            env->ExceptionClear();
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }
        strncpy(PROC_NAME, name, sizeof(PROC_NAME) - 1);
        env->ReleaseStringUTFChars(args->nice_name, name);
        // Zygisk's JNI hook API must be used before specialization completes.
        loaded = run(api, env);
        if (!loaded) api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

    void preServerSpecialize(zygisk::ServerSpecializeArgs* args) override {
        (void)args;
        api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs* args) override {
        (void)args;
        if (loaded) LOGD("Loaded (BinderProxy JNI hook)");
    }

   private:
    bool loaded = false;
    zygisk::Api* api;
    JNIEnv* env;
};

REGISTER_ZYGISK_MODULE(ih8SecureLock)
