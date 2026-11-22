#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
NDK=${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}
[[ -n "$NDK" ]] || { echo 'Set ANDROID_NDK_HOME to an installed Android NDK.' >&2; exit 1; }
BIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
mkdir -p "$ROOT/out"
for spec in arm64-v8a:aarch64-linux-android armeabi-v7a:armv7a-linux-androideabi x86:i686-linux-android x86_64:x86_64-linux-android; do
    ABI=${spec%%:*}
    TRIPLE=${spec#*:}
    # Include the C++ ABI runtime so loading does not depend on other modules
    # already exporting guard/static-initializer functions into the namespace.
    "$BIN/${TRIPLE}24-clang++" -std=c++17 -O2 -fPIC -fvisibility=hidden \
        -fno-exceptions -fno-rtti -shared -Wl,-z,relro,-z,now,--no-undefined \
        -Wall -Wextra -Wno-unused-parameter -static-libstdc++ \
        "$ROOT/zygisk/jni/module.cpp" "$ROOT/zygisk/jni/binder.cpp" \
        -llog -o "$ROOT/out/$ABI.so"
    echo "Built $ABI"
done
python3 - "$ROOT" <<'PY'
import pathlib, sys, zipfile
root = pathlib.Path(sys.argv[1])
archive = root / 'out/ih8SecureLock-v11-jni-fix.zip'
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
    for path in sorted((root / 'module').rglob('*')):
        if path.is_file():
            z.write(path, str(path.relative_to(root / 'module')))
    for path in sorted((root / 'out').glob('*.so')):
        if path.stem in ('arm64-v8a', 'armeabi-v7a', 'x86', 'x86_64'):
            z.write(path, 'zygisk/' + path.name)
print('Packaged', archive)
PY
