#!/bin/sh
# The game-file importer's zip reader (src/runtime/unzip.c) against zips made by Python's zipfile, as the build's
# packaging step makes them: stored and deflated files, folders, a corrupted file and an escaping path.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-$ROOT/build/unzip-test}
rm -rf "$OUT" && mkdir -p "$OUT/src" "$OUT/dest"
${CC:-cc} -std=gnu17 -O1 -DSH2_UNZIP "$ROOT/tests/unzip_test.c" "$ROOT/src/runtime/unzip.c" -lz -o "$OUT/unzip-test"
python3 - "$OUT" <<'PY'
import os, random, sys, zipfile
out = sys.argv[1]
random.seed(1)
files = {
    'sh3.exe': os.urandom(300000),                                  # incompressible
    'data/bg/ap/ap01.map': bytes(random.randrange(4)) * 2000000,      # very compressible
    'data/sound/adx/voice/voice.afs': os.urandom(3 * 1024 * 1024 + 7),
    'sh2e/pic/etc/title.tex': b'title' * 99999,
    'empty.txt': b'',
}
for name, data in files.items():
    p = os.path.join(out, 'src', name)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, 'wb').write(data)
with zipfile.ZipFile(f'{out}/part1.zip', 'w', zipfile.ZIP_DEFLATED, allowZip64=False) as z:
    z.writestr('data/', b'')  # an explicit folder entry
    for name in ['sh3.exe', 'data/bg/ap/ap01.map', 'empty.txt']:
        z.write(os.path.join(out, 'src', name), name)
with zipfile.ZipFile(f'{out}/part2.zip', 'w', zipfile.ZIP_STORED, allowZip64=False) as z:
    for name in ['data/sound/adx/voice/voice.afs', 'sh2e/pic/etc/title.tex']:
        z.write(os.path.join(out, 'src', name), name)
with zipfile.ZipFile(f'{out}/evil.zip', 'w') as z:
    z.writestr('../outside.txt', b'no')
data = bytearray(open(f'{out}/part2.zip', 'rb').read())
data[100] ^= 0xFF  # inside voice.afs's data
open(f'{out}/corrupt.zip', 'wb').write(data)
PY
"$OUT/unzip-test" "$OUT/part1.zip" "$OUT/dest" 0
"$OUT/unzip-test" "$OUT/part2.zip" "$OUT/dest" 0
diff -r "$OUT/src" "$OUT/dest" && echo "  unpacked files are identical"
mkdir -p "$OUT/bad"
"$OUT/unzip-test" "$OUT/corrupt.zip" "$OUT/bad" 1
[ ! -f "$OUT/bad/data/sound/adx/voice/voice.afs" ] && echo "  the corrupted file was not left behind"
"$OUT/unzip-test" "$OUT/evil.zip" "$OUT/bad" 1
[ ! -f "$OUT/outside.txt" ] && echo "  nothing was written outside the folder"
echo PASS
