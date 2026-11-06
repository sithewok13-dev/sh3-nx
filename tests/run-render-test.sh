#!/bin/sh
# Builds tests/d3d8_render_test.c against desktop GL and against OpenGL ES (SH2_GLES), runs both headless on
# mesa and compares what they drew (tests/compare_render.py). Needs SDL2, FFmpeg, GLES and mesa's llvmpipe.
#   tests/run-render-test.sh [out-dir]
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-$ROOT/build/render-test}
CC=${CC:-cc}
SRC="$ROOT/src/runtime/rt_core.c $ROOT/src/runtime/host.c $ROOT/src/win32/kernel32.c $ROOT/src/win32/user32.c
     $ROOT/src/win32/dinput.c $ROOT/src/win32/dsound.c
     $ROOT/src/runtime/camera.c $ROOT/src/d3d8/d3d8.c $ROOT/src/d3d8/d3d8_res.c $ROOT/src/d3d8/d3d8_shader.c $ROOT/src/d3d8/gl.c
     $ROOT/tests/d3d8_render_test.c"
CFLAGS="-std=gnu17 -O1 -g -fwrapv -fno-strict-aliasing -w -I$ROOT/third_party/xboxrecomp/templates/runtime"
LIBS="$(pkg-config --cflags --libs sdl2 libavformat libavcodec libswscale libswresample libavutil) -lm -lpthread"
mkdir -p "$OUT/gl" "$OUT/gles"
# rt_core.c's main() starts the game; the test has its own.
for mode in gl gles; do
    extra=""
    [ $mode = gles ] && extra="-DSH2_GLES $(pkg-config --cflags --libs glesv2)"
    objs=""
    for f in $SRC; do
        o="$OUT/$mode/$(basename "$f").o"
        def=""
        [ "$(basename "$f")" = rt_core.c ] && def="-Dmain=rt_core_main"
        $CC $CFLAGS $def $extra $(pkg-config --cflags sdl2 libavformat) -c "$f" -o "$o"
        objs="$objs $o"
    done
    $CC -o "$OUT/$mode/render-test" $objs $LIBS $extra
    rm -f "$OUT/$mode"/*.ppm
    (cd "$OUT/$mode" && SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-offscreen} ./render-test "$OUT/$mode" > log.txt 2>&1) \
        || { echo "$mode run failed:"; tail -20 "$OUT/$mode/log.txt"; exit 1; }
    grep -E "^GL|GL:" "$OUT/$mode/log.txt" | head -3
done
# The ES build again with DXT decoded in software, as on Apple's ES (no S3TC there).
mkdir -p "$OUT/gles-dxt-decoded"
rm -f "$OUT/gles-dxt-decoded"/*.ppm
(cd "$OUT/gles-dxt-decoded" && SH2_NO_S3TC=1 SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-offscreen} \
    "$OUT/gles/render-test" "$OUT/gles-dxt-decoded" > log.txt 2>&1) || { echo "decoded-DXT run failed"; exit 1; }
grep -E "GL:" "$OUT/gles-dxt-decoded/log.txt" | head -1
python3 "$ROOT/tests/compare_render.py" "$OUT/gl" "$OUT/gles" "$OUT/gles-dxt-decoded"
