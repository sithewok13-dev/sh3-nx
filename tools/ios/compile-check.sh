#!/bin/sh
# Compiles every hand-written source for iOS arm64 (the generated game code needs your sh3.exe, so it is
# left out): catches what only Apple's compiler and SDK reject. Used by .github/workflows/ios.yml.
# Needs a Mac with Xcode, tools/ios/build-deps.sh run first, and ANGLE's headers (tools/ios/build-angle.sh --headers).
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
DEPS=${SH2_IOS_DEPS:-$ROOT/build/ios-deps}
ANGLE=${SH2_IOS_ANGLE:-$ROOT/build/angle}
OUT=$ROOT/build/ios-compile-check
mkdir -p "$OUT"
CC="xcrun --sdk iphoneos clang"
FLAGS="-arch arm64 -mios-version-min=15.0 -std=gnu17 -O1 -fwrapv -fno-strict-aliasing -Wall -Wno-unused-function
       -DSH2_IOS -DSH2_GLES -I$ROOT/third_party/xboxrecomp/templates/runtime -I$DEPS/include"
fail=0
for f in "$ROOT"/src/runtime/*.c "$ROOT"/src/win32/*.c "$ROOT"/src/d3d8/*.c "$ROOT"/src/game/*.c; do
    if ! $CC $FLAGS -DSRC_DIR="\"$ROOT\"" -DSPLASH_SHA1="\"0\"" -c "$f" -o "$OUT/$(basename "$f").o"; then
        echo "FAILED: $f"
        fail=1
    fi
done
# the Objective-C: the photos, and the GL context (ANGLE, with Apple's OpenGL ES as the fallback)
for f in "$ROOT"/src/runtime/*.m; do
    if ! $CC $FLAGS -x objective-c -I"$ANGLE/include" -c "$f" -o "$OUT/$(basename "$f").o"; then
        echo "FAILED: $f"
        fail=1
    fi
done
# Every GL call goes through the table (src/d3d8/gl.h, GLES_FUNCS): one that does not would reach Apple's OpenGL ES
# even while ANGLE draws.
if nm -u "$OUT"/*.o | grep -E ' _gl[A-Z]'; then
    echo "FAILED: the GL functions above are called directly: add them to GLES_FUNCS in src/d3d8/gl.h"
    fail=1
fi
[ $fail = 0 ] && echo "every source compiles for iOS"
exit $fail
