#!/bin/sh
# Compiles every hand-written source for iOS arm64 (the generated game code needs your sh3.exe, so it is
# left out): catches what only Apple's compiler and SDK reject. Used by .github/workflows/ios.yml.
# Needs a Mac with Xcode and tools/ios/build-deps.sh run first.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
DEPS=${SH2_IOS_DEPS:-$ROOT/build/ios-deps}
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
[ $fail = 0 ] && echo "every source compiles for iOS"
exit $fail
