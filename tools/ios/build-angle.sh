#!/bin/sh
# Builds ANGLE (OpenGL ES 3.0 on Metal, the iOS app's renderer: src/runtime/ios_angle.m) into build/angle:
#   include/                                  EGL and GLES headers
#   out/ios/libEGL.framework, libGLESv2.framework   arm64 iPhone/iPad, bundled into the app by CMakeLists.txt
# With Chromium's own tools (depot_tools, gclient, gn): about an hour and 10+ GB of checkout the first time, which is
# why the cloud builds keep the result in their cache (its key is this file's hash: change ANGLE_REV or the gn
# arguments here and it is rebuilt).
#   tools/ios/build-angle.sh            the frameworks and headers (needs a Mac with Xcode, git and Python 3)
#   tools/ios/build-angle.sh --headers  only include/ (enough for tools/ios/compile-check.sh), in seconds
# The same ANGLE revision and arguments as the owner's other ports (Dark Forces, DOOM 3, Jedi Knight).
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${SH2_IOS_ANGLE:-$ROOT/build/angle}
WORK=${ANGLE_WORK:-$ROOT/build/angle-src}
# Branch chromium/8060 (September 2026), from the github.com/google/angle mirror.
ANGLE_REV=4b7308a36376199c095ccb0ab69b773daa8b9fbe
# dyld will not load a framework made for a newer iOS than the phone's: on iOS 15-17 the app finds no ANGLE and
# draws with Apple's OpenGL ES instead (ios_angle.m). 18.0 is what the other ports' ANGLE is proven with.
ANGLE_MIN_IOS=18.0

if [ "$1" = --headers ]; then
    [ -f "$OUT/include/EGL/egl.h" ] && { echo "ANGLE headers are in $OUT/include"; exit 0; }
    rm -rf "$WORK-headers" && mkdir -p "$WORK-headers" && cd "$WORK-headers"
    git init -q
    git remote add origin https://github.com/google/angle
    git sparse-checkout set --no-cone /include
    git fetch -q --depth 1 --filter=blob:none origin "$ANGLE_REV"
    git checkout -q FETCH_HEAD
    mkdir -p "$OUT"
    rm -rf "$OUT/include" && cp -R include "$OUT/include"
    echo "ANGLE headers are in $OUT/include"
    exit 0
fi

if [ -f "$OUT/out/ios/libEGL.framework/libEGL" ] && [ -f "$OUT/out/ios/libGLESv2.framework/libGLESv2" ]; then
    echo "ANGLE is in $OUT"
    exit 0
fi
command -v xcrun >/dev/null || { echo "xcrun not found: this needs a Mac with Xcode"; exit 1; }

mkdir -p "$WORK"
[ -d "$WORK/depot_tools" ] || git clone -q --depth 1 https://chromium.googlesource.com/chromium/tools/depot_tools.git "$WORK/depot_tools"
export PATH="$WORK/depot_tools:$PATH"
export DEPOT_TOOLS_UPDATE=0

mkdir -p "$WORK/angle" && cd "$WORK/angle"
if [ "$(git rev-parse HEAD 2>/dev/null)" != "$ANGLE_REV" ]; then
    git init -q
    git fetch -q --depth 1 https://github.com/google/angle "$ANGLE_REV"
    git checkout -q FETCH_HEAD
fi
# An unmanaged solution rooted at this checkout (what scripts/bootstrap.py writes), for iOS, without the Dawn
# (WebGPU) dependencies, which are large and not needed.
cat > .gclient <<'EOF'
solutions = [
  {
    "name": ".",
    "url": "https://chromium.googlesource.com/angle/angle.git",
    "deps_file": "DEPS",
    "managed": False,
    "custom_deps": {},
    "custom_vars": {
      "checkout_angle_dawn_deps": False,
    },
  },
]
target_os = ["ios"]
EOF
gclient sync --no-history --shallow -j8
gn gen out/ios --args="
  is_debug=false
  is_component_build=false
  symbol_level=0
  target_os=\"ios\"
  target_cpu=\"arm64\"
  target_environment=\"device\"
  ios_deployment_target=\"$ANGLE_MIN_IOS\"
  ios_enable_code_signing=false
  angle_enable_vulkan=false
  angle_enable_swiftshader=false
  angle_enable_wgpu=false
  angle_build_tests=false
"
autoninja -C out/ios libEGL libGLESv2

mkdir -p "$OUT/out/ios"
rm -rf "$OUT/include" "$OUT/out/ios/libEGL.framework" "$OUT/out/ios/libGLESv2.framework"
cp -R include "$OUT/include"
cp -R out/ios/libEGL.framework out/ios/libGLESv2.framework "$OUT/out/ios/"
for f in libEGL libGLESv2; do
    file "$OUT/out/ios/$f.framework/$f"
    otool -l "$OUT/out/ios/$f.framework/$f" | grep -A4 LC_BUILD_VERSION | grep -E 'platform|minos' || true
done
echo "ANGLE is in $OUT"
