#!/bin/sh
# Builds tests/camera_test.c (the camera's math, src/runtime/camera.c) and runs it.   tests/run-camera-test.sh [out-dir]
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-$ROOT/build/camera-test}
mkdir -p "$OUT"
${CC:-cc} -std=gnu17 -O1 -g -Wall -Wno-unused-function -I"$ROOT/third_party/xboxrecomp/templates/runtime" \
    "$ROOT/tests/camera_test.c" -o "$OUT/camera-test" $(pkg-config --cflags --libs sdl2) -lm
"$OUT/camera-test"
