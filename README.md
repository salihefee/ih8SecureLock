# ih8SecureLock

Prevent apps from blocking and listening to your screenshots with Zygisk.

- Works with KernelSU or Magisk
- Works on Android versions 10 - 17
- Do not put the apps for which you want to take an ss in any kind of denylist and disable 'Unmount modules'" option for them

### JNI hook build

This branch hooks `BinderProxy.transactNative` before app specialization instead
of registering a libbinder PLT hook. Window Parcels are read and edited in place
through Java's Parcel methods, preserving their Binder object table. It clears
`LayoutParams.flags` (rather than the following `privateFlags` integer) and
returns a valid synchronous Binder reply when suppressing screenshot observers.
Denylisted processes and the system server remain excluded.

Set `ANDROID_NDK_HOME` to a Linux Android NDK, then run:

```sh
bash zygisk/build.sh
```

The four ABI libraries and installable `ih8SecureLock-v11-jni-fix.zip` are in
`out/`. The build uses a static C++ runtime and rejects unresolved symbols.

With an arm64 Android device connected, set `ANDROID_HOME` to an SDK containing
platform `android-36` and build tools `35.0.0`, then run:

```sh
bash checks/run-android.sh
```

These checks exercise the production hook with real Parcels and a fake Binder
transport: relayout variants, null attributes, unrelated interfaces, malformed
requests, Binder-object and cursor preservation, and screenshot-observer replies.
They do not install a Zygisk module. The optional activity in `checks/smoke/`
requests `FLAG_SECURE` and registers a screenshot callback for an installation
smoke test; compare its WindowManager flags before and after enabling the module.
