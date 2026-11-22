#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
SDK=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
NDK=${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}
: "${SDK:?Set ANDROID_HOME to an Android SDK with platform android-36}"
: "${NDK:?Set ANDROID_NDK_HOME to an Android NDK}"
BUILD_TOOLS=${ANDROID_BUILD_TOOLS:-$SDK/build-tools/35.0.0}
[[ $(adb shell getprop ro.product.cpu.abi | tr -d '\r') == arm64-v8a ]] || {
    echo 'This device test runner requires an arm64 Android device.' >&2; exit 1;
}
mkdir -p "$ROOT/out/check-classes" "$ROOT/out/check-dex"
"$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android24-clang++" \
    -std=c++17 -O2 -fPIC -shared -static-libstdc++ -Wl,--no-undefined \
    "$ROOT/checks/parcel_hook.cpp" "$ROOT/zygisk/jni/binder.cpp" -llog \
    -o "$ROOT/out/libparcel-hook-check.so"
javac --release 8 -Xlint:-options -cp "$SDK/platforms/android-36/android.jar" \
    -d "$ROOT/out/check-classes" "$ROOT/checks/ParcelHookCheck.java"
"$BUILD_TOOLS/d8" --output "$ROOT/out/check-dex" "$ROOT/out/check-classes/ParcelHookCheck.class"
adb push "$ROOT/out/libparcel-hook-check.so" /data/local/tmp/
adb push "$ROOT/out/check-dex/classes.dex" /data/local/tmp/parcel-hook-check.dex
adb shell 'CLASSPATH=/data/local/tmp/parcel-hook-check.dex app_process /system/bin ParcelHookCheck /data/local/tmp/libparcel-hook-check.so'
adb shell rm /data/local/tmp/libparcel-hook-check.so /data/local/tmp/parcel-hook-check.dex
