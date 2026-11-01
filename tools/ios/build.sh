#!/bin/sh
# Builds the iOS app from your own game: build/ios/SH3.ipa, unsigned, for SideStore, AltStore or Sideloadly.
#
#   1. Put your PC Silent Hill 3 install in game/ (see docs/IOS.md).
#   2. tools/ios/build.sh
#
# Generates the game's C from game/sh3.exe the first time (tools/regen.sh, a few minutes), builds SDL2 for iOS the first time (tools/ios/build-deps.sh), then the app. Needs a Mac with Xcode, CMake,
# Ninja and Python 3.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
BUILD=$ROOT/build/ios

[ -f game/sh3.exe ] || { echo "game/sh3.exe not found: copy or link your game folder to game/ (docs/IOS.md)"; exit 1; }
# The port is made for one build of the exe.
WANT=aa2aca6dba19bf510c9c2e92eefdc15e98266581
HAVE=$(shasum -a 1 game/sh3.exe | cut -d' ' -f1)
if [ "$HAVE" != "$WANT" ]; then
    echo "game/sh3.exe is not the one this port is built for (SHA-1 $HAVE, want $WANT): see docs/IOS.md."
    exit 1
fi
if [ ! -x .venv/bin/python3 ]; then
    python3 -m venv .venv
    .venv/bin/pip install -q -r requirements.txt
fi
if ! ls src/recomp/gen/*.c >/dev/null 2>&1; then
    echo "== generating the game's C from game/sh3.exe"
    tools/regen.sh
fi
tools/ios/build-deps.sh

echo "== the app"
cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
    -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO ${SH2_IOS_BUNDLE_ID:+-DSH2_IOS_BUNDLE_ID=$SH2_IOS_BUNDLE_ID}
cmake --build "$BUILD"

rm -rf "$BUILD/Payload" "$BUILD/SH3.ipa"
mkdir -p "$BUILD/Payload"
cp -R "$BUILD/SH3.app" "$BUILD/Payload/"
(cd "$BUILD" && zip -qr SH3.ipa Payload)
rm -rf "$BUILD/Payload"
echo "built $BUILD/SH3.ipa: install it with SideStore, AltStore or Sideloadly, then copy the game files (docs/IOS.md)"
