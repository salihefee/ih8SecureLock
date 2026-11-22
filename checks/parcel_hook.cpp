// Exercise the production hook with real Android Parcels and a fake transport.
#include "../zygisk/jni/module.cpp"
static int forwarded;
static jboolean forward(JNIEnv*, jobject, jint, jobject, jobject, jint) {
    ++forwarded;
    return JNI_TRUE;
}
class CheckModule : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api* api, JNIEnv* env) override {
        if (!run(api, env)) env->FatalError("Could not initialize production JNI hook");
    }
};
extern "C" JNIEXPORT void JNICALL Java_ParcelHookCheck_init(JNIEnv* env, jclass) {
    static zygisk::internal::api_table table{};
    table.registerModule = [](auto, auto) { return true; };
    table.hookJniNativeMethods = [](JNIEnv*, const char*, JNINativeMethod* methods, int) {
        methods[0].fnPtr = reinterpret_cast<void*>(forward);
    };
    zygisk::internal::entry_impl<CheckModule>(&table, env);
}
extern "C" JNIEXPORT jint JNICALL Java_ParcelHookCheck_code(JNIEnv*, jclass, jint kind) {
    uint32_t codes[] = {relayout_code, relayoutAsync_code, relayout2_code,
                        relayoutAsync2_code, registerScreenCaptureObserver_code};
    return kind >= 0 && kind < 5 ? codes[kind] : 0;
}
extern "C" JNIEXPORT jint JNICALL Java_ParcelHookCheck_invoke(
    JNIEnv* env, jclass, jint code, jobject data, jobject reply, jint flags) {
    forwarded = 0;
    const auto result = transactHook(env, nullptr, code, data, reply, flags);
    return result == JNI_TRUE ? forwarded : -1;
}
