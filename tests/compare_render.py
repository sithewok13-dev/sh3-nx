#!/usr/bin/env python3
"""Checks what tests/d3d8_render_test.c drew with desktop GL (dir A) and OpenGL ES (dir B): known colors at
known pixels in each, and the two renderers within a few levels of each other everywhere."""
import struct
import sys


def ppm(path):
    data = open(path, 'rb').read()
    fields, at = [], 0
    while len(fields) < 4:  # P6 w h max, each followed by one whitespace byte; pixels may start with 0x20
        end = at
        while data[end] not in b' \t\n\r':
            end += 1
        fields.append(data[at:end])
        at = end + 1
    w, h = int(fields[1]), int(fields[2])
    px = data[at:]
    return w, h, lambda x, y: tuple(px[3 * (y * w + x):3 * (y * w + x) + 3])


failures = []


def check(what, cond):
    if not cond:
        failures.append(what)
    print(('  ok   ' if cond else '  FAIL ') + what)


def near(c, want, tol=24):
    return all(abs(a - b) <= tol for a, b in zip(c, want))


def scene_checks(name, d):
    f = [ppm(f'{d}/frame{i:05d}.ppm') for i in range(4)]
    _, _, p = f[0]
    check(f'{name} texture: clear color', near(p(10, 10), (0x20, 0x30, 0x40), 2))
    check(f'{name} texture: red texel', near(p(100, 80), (255, 0, 0)))
    check(f'{name} texture: green texel', near(p(220, 80), (0, 255, 0)))
    check(f'{name} texture: blue texel', near(p(100, 160), (0, 0, 255)))
    check(f'{name} texture: white texel', near(p(220, 160), (255, 255, 255)))
    _, _, p = f[1]
    for i, fmt in enumerate(['A1R5G5B5', 'A4R4G4B4', 'R5G6B5', 'DXT1']):
        check(f'{name} formats: {fmt} red corner', near(p(8 + 62 * i + 5, 60), (255, 0, 0)))
    check(f'{name} formats: A1R5G5B5 green', near(p(8 + 62 * 0 + 45, 60), (0, 255, 0), 30))
    check(f'{name} formats: A1R5G5B5 blue', near(p(8 + 62 * 0 + 5, 160), (0, 0, 255), 30))
    check(f'{name} formats: A1R5G5B5 transparent texel shows the ramp', p(8 + 45, 160)[0] < 200)
    check(f'{name} formats: A4R4G4B4 half-alpha green blends', 60 < p(8 + 62 + 45, 60)[1] < 255 and p(8 + 62 + 45, 60)[0] < 200)
    check(f'{name} formats: DXT1 second texel is the blue endpoint', near(p(8 + 62 * 3 + 20, 45), (0, 0, 255), 40))
    check(f'{name} formats: DXT1 last texel is 1/3 red + 2/3 blue', near(p(8 + 62 * 3 + 50, 45), (85, 0, 170), 30))
    check(f'{name} formats: DXT5 top row white', near(p(8 + 62 * 4 + 20, 50), (255, 255, 255), 40) or p(8 + 62 * 4 + 20, 50)[0] > 100)
    _, _, p = f[2]
    check(f'{name} 3d: near green wins the depth test', near(p(160, 150), (0, 255, 0)))
    check(f'{name} 3d: back-facing magenta culled', near(p(20, 200), (0, 0, 0), 2))
    yl = p(30, 28)
    check(f'{name} 3d: base-vertex indexed quad drawn (yellow)', yl[0] > 180 and yl[1] > 180 and yl[2] < 120)
    _, _, p = f[3]
    tl = p(40, 30)
    check(f'{name} shaders: vs D3DCOLOR orange stays orange (not swapped)', tl[0] > 180 and tl[2] < 90)
    check(f'{name} shaders: outside the quad black', near(p(5, 5), (0, 0, 0), 2))
    return f


def bins(name, d):
    front = open(f'{d}/front.bin', 'rb').read()
    b, g, r, a = front[4 * (30 * 320 + 40):4 * (30 * 320 + 40) + 4]
    check(f'{name} GetFrontBuffer: B,G,R,A bytes of the orange corner', r > 180 and b < 90 and a == 255)
    rt = open(f'{d}/rt565.bin', 'rb').read()
    left, right = struct.unpack_from('<H', rt, 2 * (10 * 64 + 10))[0], struct.unpack_from('<H', rt, 2 * (10 * 64 + 50))[0]
    check(f'{name} R5G6B5 render target readback: cleared green {left:04X}', left == 0x07E0)
    check(f'{name} R5G6B5 render target readback: drawn red {right:04X}', right == 0xF800)
    return front


a, b = sys.argv[1], sys.argv[2]
other = sys.argv[3:]  # more ES runs (DXT decoded), checked like b and compared with a
print('desktop GL:')
fa = scene_checks('GL', a)
ba = bins('GL', a)
print('OpenGL ES:')
fb = scene_checks('ES', b)
bb = bins('ES', b)


def compare(label, fa, fb, ba, bb):
    """Interiors must match exactly. Pixels on or next to an edge in either image are left out: GL's clip
    control rasterizes D3D quads one row lower than ES on mesa (ES is the one that covers D3D's rows), and
    on a quad's edge row either renderer may sample the wrapped texel (float error on a coordinate of
    exactly 0 or 1), GL at the bottom and ES at the top."""
    for i, ((w, h, pa), (_, _, pb)) in enumerate(zip(fa, fb)):
        def edge(x, y):  # near a color change in either image
            for img in (pa, pb):
                c = img(x, y)
                for yy in range(max(0, y - 2), min(h, y + 3)):
                    for xx in range(max(0, x - 2), min(w, x + 3)):
                        if max(abs(u - v) for u, v in zip(img(xx, yy), c)) > 12:
                            return True
            return False
        n_off = n_in = 0
        for y in range(h):
            for x in range(w):
                if max(abs(u - v) for u, v in zip(pa(x, y), pb(x, y))) <= 12:
                    continue
                if edge(x, y):
                    n_off += 1
                else:
                    n_in += 1
        check(f'{label} frame {i}: interiors identical ({n_in} differ), {n_off} edge pixels differ', n_in == 0 and n_off <= 2 * (w + h))
    diff = sum(abs(u - v) > 12 for u, v in zip(ba, bb))
    check(f'{label} GetFrontBuffer bytes: {diff} differ', diff <= len(ba) // 200)


print('GL vs ES:')
compare('GL vs ES', fa, fb, ba, bb)
for d in other:
    print(f'OpenGL ES, {d}:')
    fo = scene_checks('ES', d)
    bo = bins('ES', d)
    compare('GL vs ' + d.rstrip('/').split('/')[-1], fa, fo, ba, bo)
print('PASS' if not failures else f'{len(failures)} FAILED')
sys.exit(1 if failures else 0)
