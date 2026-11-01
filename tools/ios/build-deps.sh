#!/bin/sh
# Builds what the iOS app links, static and for arm64 iPhones/iPads, into build/ios-deps:
#   SDL2    window, GL context, input, audio (Silent Hill 3 plays its movies without FFmpeg)
# Needs a Mac with Xcode (or its command line tools), CMake and curl. Run once; tools/ios/build.sh calls it.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${SH2_IOS_DEPS:-$ROOT/build/ios-deps}
WORK=$ROOT/build/ios-deps-src
MIN_IOS=15.0
SDL_VERSION=2.30.9
JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)

command -v xcrun >/dev/null || { echo "xcrun not found: this needs a Mac with Xcode"; exit 1; }
SDK=$(xcrun --sdk iphoneos --show-sdk-path)
mkdir -p "$OUT" "$WORK"

fetch() {  # url, archive name
    [ -f "$WORK/$2" ] || curl -fL --retry 3 -o "$WORK/$2" "$1"
}

if [ ! -f "$OUT/lib/libSDL2.a" ]; then
    echo "== SDL2 $SDL_VERSION"
    fetch "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL2-$SDL_VERSION.tar.gz" sdl2.tar.gz
    rm -rf "$WORK/SDL2-$SDL_VERSION" && tar -xzf "$WORK/sdl2.tar.gz" -C "$WORK"
    cmake -S "$WORK/SDL2-$SDL_VERSION" -B "$WORK/sdl2-build" -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=$MIN_IOS \
        -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF -DCMAKE_INSTALL_PREFIX="$OUT"
    cmake --build "$WORK/sdl2-build" --config Release -j "$JOBS"
    cmake --install "$WORK/sdl2-build" --config Release
fi


echo "iOS dependencies are in $OUT"
