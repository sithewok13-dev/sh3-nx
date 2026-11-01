#!/bin/sh
# Builds tests/touch_test.c with the touch controls (src/runtime/touch.c) on OpenGL ES and runs it headless on
# mesa; OUT/touch.ppm shows the overlay.   tests/run-touch-test.sh [out-dir]
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-$ROOT/build/touch-test}
mkdir -p "$OUT"
${CC:-cc} -std=gnu17 -O1 -g -DSH2_GLES -DSH2_TOUCH -I"$ROOT/third_party/xboxrecomp/templates/runtime" \
    "$ROOT/tests/touch_test.c" "$ROOT/src/runtime/touch.c" "$ROOT/src/runtime/touchUiDraw.c" "$ROOT/src/runtime/touchUiRender_gl.c" -o "$OUT/touch-test" \
    $(pkg-config --cflags --libs sdl2 glesv2) -lm -lpthread
SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-offscreen} "$OUT/touch-test" "$OUT"
