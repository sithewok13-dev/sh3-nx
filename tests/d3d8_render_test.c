/* Drives the D3D8 layer the way the game does (COM calls on guest memory) with no game: four scenes and
 * two readbacks, each written to OUT/<name>.ppm or .bin. Built once against desktop GL and once against
 * OpenGL ES (tests/run-render-test.sh), so the ES port can be compared with the renderer it replaces.
 * Its own main(); src/game/ee.c and the generated game code are left out, their few entry points stubbed. */
#include "../src/runtime/rt.h"
#include "../src/runtime/host.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <stdarg.h>
#include <unistd.h>

/* ---- what the game file (ee.c / sh3.c) and the generated code would provide ----
 * Weak, so the one test builds against sh2-nx and sh3-nx alike: whatever the runtime defines itself wins. */
#define STUB __attribute__((weak))
STUB recomp_func_t recomp_lookup(uint32_t va) { (void)va; return NULL; }
STUB int recomp_dispatch_init(void) { return 0; }
STUB void xbe_entry_point(void) {}
STUB recomp_func_t ee_lookup_manual(uint32_t va) { (void)va; return NULL; }
STUB recomp_func_t recomp_lookup_manual(uint32_t va) { (void)va; return NULL; }
STUB void ee_frame(void) {}
STUB void ee_init(void) {}
STUB void game_init(void) {}
STUB void launcher_run(void) {}
STUB void ee_file_opened(const char *guest, FILE *f) { (void)guest; (void)f; }
STUB uint32_t ee_rumble_gain(uint32_t magnitude) { return magnitude; }
STUB void cheat_tick(void) {}
STUB void controls_tick(void) {}

/* ---- calls into the layer ---- */
static uint32_t call_bridge(const char *name, int n, const uint32_t *a) {
    for (const Bridge *b = rt_bridges_begin; b < rt_bridges_end; b++)
        if (!strcmp(b->name, name)) {
            for (int i = n - 1; i >= 0; i--) PUSH32(g_esp, a[i]);
            PUSH32(g_esp, 0xFFFFFFF0u);
            b->fn();
            return g_eax;
        }
    rt_fatal("no bridge %s", name);
}
/* obj->vtable[slot](obj, ...) */
static uint32_t com(uint32_t obj, int slot, int n, ...) {
    uint32_t a[16] = {obj};
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++) a[1 + i] = va_arg(ap, uint32_t);
    va_end(ap);
    uint32_t r = guest_call(MEM32(MEM32(obj) + 4 * slot), n + 1, a, 1);
    return r;
}
enum { Present = 15, CreateTexture = 20, CreateVertexBuffer = 23, CreateIndexBuffer = 24, CreateRenderTarget = 25,
       CreateImageSurface = 27, GetFrontBuffer = 30, SetRenderTarget = 31, Clear = 36, SetTransform = 37,
       SetRenderState = 50, SetTexture = 61, SetTextureStageState = 63, DrawIndexedPrimitive = 71,
       DrawPrimitiveUP = 72, CreateVertexShader = 75, SetVertexShader = 76, SetVertexShaderConstant = 79,
       SetStreamSource = 83, SetIndices = 85, CreatePixelShader = 87, SetPixelShader = 88,
       T_LockRect = 16, T_UnlockRect = 17, S_LockRect = 9, S_UnlockRect = 10, B_Lock = 11, B_Unlock = 12 };
static uint32_t u32f(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static uint32_t gcopy(const void *p, uint32_t n) { uint32_t va = galloc(n); memcpy(GPTR(va), p, n); return va; }
static uint32_t dev, W = 320, H = 240;
static const char *out;

static uint32_t texture(uint32_t w, uint32_t h, uint32_t fmt, const void *px, uint32_t bytes) {
    uint32_t pp = galloc(4), lr = galloc(8);
    if (com(dev, CreateTexture, 7, w, h, 1, 0, fmt, 1, pp)) rt_fatal("CreateTexture %u", fmt);
    uint32_t t = MEM32(pp);
    com(t, T_LockRect, 4, 0, lr, 0, 0);
    memcpy(GPTR(MEM32(lr + 4)), px, bytes);
    com(t, T_UnlockRect, 1, 0);
    return t;
}
/* a screen-space quad: XYZRHW | DIFFUSE | TEX1 */
static void quad(float x0, float y0, float x1, float y1, uint32_t color) {
    float v[4][7] = {{x0, y0, 0.5f, 1, 0, 0, 0}, {x1, y0, 0.5f, 1, 0, 1, 0}, {x0, y1, 0.5f, 1, 0, 0, 1}, {x1, y1, 0.5f, 1, 0, 1, 1}};
    for (int i = 0; i < 4; i++) memcpy(&v[i][4], &color, 4);
    uint32_t va = gcopy(v, sizeof v);
    com(dev, SetVertexShader, 1, 0x144);
    com(dev, DrawPrimitiveUP, 4, 5, 2, va, 28);  /* TRIANGLESTRIP */
    gfree(va);
}
static void rs(uint32_t s, uint32_t v) { com(dev, SetRenderState, 2, s, v); }
/* Presents, then saves what was presented the way the game reads it back (GetFrontBuffer: B,G,R,A rows) as
 * OUT/frameN.ppm. */
static void save(const char *name, const void *p, size_t n);
static void present(void) {
    static int n;
    com(dev, Present, 4, 0, 0, 0, 0);
    /* mesa's llvmpipe sometimes hands GetFrontBuffer the frame before (a sync gap between Present's snapshot
     * blit and the readback; the back buffer itself is right): wait for the GPU so every run reads the same. */
    ((void (*)(void))SDL_GL_GetProcAddress("glFinish"))();
    uint32_t pp = galloc(4), lr = galloc(8);
    com(dev, CreateImageSurface, 4, W, H, 21, pp);
    uint32_t img = MEM32(pp);
    com(dev, GetFrontBuffer, 1, img);
    com(img, S_LockRect, 3, lr, 0, 0);
    const uint8_t *bgra = GPTR(MEM32(lr + 4));
    char hdr[32], name[32];
    int k = snprintf(hdr, sizeof hdr, "P6 %u %u 255\n", W, H);
    uint8_t *ppm = malloc(k + W * H * 3);
    memcpy(ppm, hdr, k);
    for (uint32_t i = 0; i < W * H; i++) { ppm[k + 3 * i] = bgra[4 * i + 2]; ppm[k + 3 * i + 1] = bgra[4 * i + 1]; ppm[k + 3 * i + 2] = bgra[4 * i]; }
    snprintf(name, sizeof name, "frame%05d.ppm", n++);
    save(name, ppm, k + W * H * 3);
    free(ppm);
    com(img, S_UnlockRect, 0);
}
static void save(const char *name, const void *p, size_t n) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s", out, name);
    FILE *f = fopen(path, "wb");
    fwrite(p, 1, n, f);
    fclose(f);
}

/* ---- scenes ---- */
static void scene_texture(void) {  /* A8R8G8B8 2x2: red, green / blue, white-with-half-alpha */
    uint32_t px[4] = {0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0x80FFFFFF};
    uint32_t t = texture(2, 2, 21, px, sizeof px);
    com(dev, Clear, 6, 0, 0, 1 | 2, 0xFF203040, u32f(1), 0);
    com(dev, SetTexture, 2, 0, t);
    quad(40, 40, 280, 200, 0xFFFFFFFF);
    com(dev, SetTexture, 2, 0, 0);
    present();
}

static void scene_formats(void) {  /* 1555, 4444, 565, DXT1, DXT5 side by side, alpha-blended over a grey ramp */
    com(dev, Clear, 6, 0, 0, 1 | 2, 0xFF808080, u32f(1), 0);
    com(dev, SetTexture, 2, 0, 0);
    for (int i = 0; i < 8; i++) quad(i * 40, 0, i * 40 + 40, 240, 0xFF000000 | (i * 32) * 0x010101);
    uint16_t p1555[4] = {0xFC00, 0x83E0, 0x801F, 0x7FFF};   /* red, green, blue opaque; white transparent */
    uint16_t p4444[4] = {0xFF00, 0x80F0, 0xF00F, 0x4FFF};   /* red, half green, blue, quarter white */
    uint16_t p565[4] = {0xF800, 0x07E0, 0x001F, 0xFFFF};
    /* DXT1: c0 = red, c1 = blue, rows of indices 0,1,2,3 (4-color mode since c0 > c1) */
    uint8_t dxt1[8] = {0x00, 0xF8, 0x1F, 0x00, 0xE4, 0xE4, 0xE4, 0xE4};
    /* DXT5: alpha 255..0 ramp by index, color block white/black */
    uint8_t dxt5[16] = {255, 0, 0x88, 0xC6, 0xFA, 0x88, 0xC6, 0xFA, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x55, 0xAA, 0xFF};
    uint32_t t[5] = {texture(2, 2, 25, p1555, 8), texture(2, 2, 26, p4444, 8), texture(2, 2, 23, p565, 8),
                     texture(4, 4, 0x31545844, dxt1, 8), texture(4, 4, 0x35545844, dxt5, 16)};
    rs(27, 1); rs(19, 5); rs(20, 6);  /* ALPHABLEND, SRCALPHA, INVSRCALPHA */
    for (int i = 0; i < 5; i++) {
        com(dev, SetTexture, 2, 0, t[i]);
        quad(8 + i * 62, 40, 62 + i * 62, 200, 0xFFFFFFFF);
    }
    rs(27, 0);
    com(dev, SetTexture, 2, 0, 0);
    present();
}

static void ident(float *m) { memset(m, 0, 64); m[0] = m[5] = m[10] = m[15] = 1; }
static void scene_3d(void) {  /* depth test, culling, vertex colors and DrawIndexedPrimitive with a base vertex */
    com(dev, Clear, 6, 0, 0, 1 | 2, 0xFF000000, u32f(1), 0);
    float m[16];
    ident(m);
    uint32_t mva = gcopy(m, 64);
    com(dev, SetTransform, 2, 256, mva);  /* world */
    com(dev, SetTransform, 2, 2, mva);    /* view */
    /* perspective, D3D left-handed: near 1, far 10, 90 degrees */
    float zf = 10, zn = 1;
    float p[16] = {0.75f, 0, 0, 0, 0, 1, 0, 0, 0, 0, zf / (zf - zn), 1, 0, 0, -zn * zf / (zf - zn), 0};
    uint32_t pva = gcopy(p, 64);
    com(dev, SetTransform, 2, 3, pva);
    rs(137, 0);  /* LIGHTING off */
    rs(7, 1);    /* ZENABLE */
    /* two clockwise triangles (D3D's front faces): the near green one drawn after the far red one */
    struct { float x, y, z; uint32_t c; } tri[6] = {
        {-2, -1.5f, 4, 0xFFFF0000}, {0, 2, 4, 0xFFFF0000}, {2, -1.5f, 4, 0xFFFF0000},
        {-1, -1, 2, 0xFF00FF00}, {0, 1, 2, 0xFF00FF00}, {1, -1, 2, 0xFF00FF00}};
    uint32_t va = gcopy(tri, sizeof tri);
    com(dev, SetVertexShader, 1, 0x042);  /* XYZ | DIFFUSE */
    com(dev, DrawPrimitiveUP, 4, 4, 1, va + 48, 16);  /* near first, then far: depth must keep green on top */
    com(dev, DrawPrimitiveUP, 4, 4, 1, va, 16);
    /* a counter-clockwise (back-facing) magenta triangle across everything: culled */
    struct { float x, y, z; uint32_t c; } back[3] = {{-3, -3, 1.5f, 0xFFFF00FF}, {3, -3, 1.5f, 0xFFFF00FF}, {0, 3, 1.5f, 0xFFFF00FF}};
    uint32_t bva = gcopy(back, sizeof back);
    com(dev, DrawPrimitiveUP, 4, 4, 1, bva, 16);
    /* indexed: a yellow quad in the top-left from vertices 4..7 of a buffer, indices 0..3, base vertex 4 */
    struct { float x, y, z; uint32_t c; } vb[8] = {
        {0, 0, 9, 0xFFFF0000}, {0, 0, 9, 0xFFFF0000}, {0, 0, 9, 0xFFFF0000}, {0, 0, 9, 0xFFFF0000},
        {-1.4f, 1.0f, 1.2f, 0xFFFFFF00}, {-0.9f, 1.0f, 1.2f, 0xFFFFFF00}, {-1.4f, 0.6f, 1.2f, 0xFFFFFF00}, {-0.9f, 0.6f, 1.2f, 0xFF00FFFF}};
    uint16_t ib[6] = {0, 1, 2, 2, 1, 3};
    uint32_t pp = galloc(4), lp = galloc(4);
    com(dev, CreateVertexBuffer, 5, sizeof vb, 0, 0x042, 1, pp);
    uint32_t vbo = MEM32(pp);
    com(vbo, B_Lock, 4, 0, 0, lp, 0);
    memcpy(GPTR(MEM32(lp)), vb, sizeof vb);
    com(vbo, B_Unlock, 0);
    com(dev, CreateIndexBuffer, 5, sizeof ib, 0, 101, 1, pp);
    uint32_t ibo = MEM32(pp);
    com(ibo, B_Lock, 4, 0, 0, lp, 0);
    memcpy(GPTR(MEM32(lp)), ib, sizeof ib);
    com(ibo, B_Unlock, 0);
    com(dev, SetStreamSource, 3, 0, vbo, 16);
    com(dev, SetIndices, 2, ibo, 4);
    com(dev, DrawIndexedPrimitive, 5, 4, 0, 4, 0, 2);  /* TRIANGLELIST, min 0, 4 vertices, start 0, 2 triangles */
    rs(7, 0);
    present();
}

static void scene_shaders(void) {  /* vs_1_1 with a D3DCOLOR input, ps_1_1 tex * diffuse */
    com(dev, Clear, 6, 0, 0, 1, 0xFF000000, u32f(1), 0);
    uint32_t decl[] = {0x20000000, 0x40020000, 0x40040005, 0xFFFFFFFF};  /* stream 0: v0 FLOAT3, v5 D3DCOLOR */
    uint32_t vs[] = {0xFFFE0101,
                     20, 0xC00F0000, 0x90E40000, 0xA0E40000,  /* m4x4 oPos, v0, c0 */
                     1, 0xD00F0000, 0x90E40005,               /* mov oD0, v5 */
                     1, 0xE00F0000, 0x90E40000,               /* mov oT0, v0 */
                     0x0000FFFF};
    uint32_t ps[] = {0xFFFF0101,
                     66, 0xB00F0000,                          /* tex t0 */
                     5, 0x800F0000, 0xB0E40000, 0x90E40000,   /* mul r0, t0, v0 */
                     0x0000FFFF};
    uint32_t hp = galloc(4);
    com(dev, CreateVertexShader, 4, gcopy(decl, sizeof decl), gcopy(vs, sizeof vs), hp, 0);
    uint32_t vsh = MEM32(hp);
    com(dev, CreatePixelShader, 2, gcopy(ps, sizeof ps), hp);
    uint32_t psh = MEM32(hp);
    /* c0..c3: rows of a matrix mapping x, y in [0,1] to clip space [-0.8, 0.8] */
    float c[16] = {1.6f, 0, 0, -0.8f, 0, -1.6f, 0, 0.8f, 0, 0, 0, 0.5f, 0, 0, 0, 1};
    com(dev, SetVertexShaderConstant, 3, 0, gcopy(c, 64), 4);
    uint32_t px[4] = {0xFFFFFFFF, 0xFF808080, 0xFF808080, 0xFFFFFFFF};
    uint32_t t = texture(2, 2, 21, px, sizeof px);
    com(dev, SetTexture, 2, 0, t);
    com(dev, SetVertexShader, 1, vsh);
    com(dev, SetPixelShader, 1, psh);
    struct { float x, y, z; uint32_t c; } v[4] = {{0, 0, 0, 0xFFFF8000}, {1, 0, 0, 0xFF00FF80}, {0, 1, 0, 0xFF8000FF}, {1, 1, 0, 0xFFFFFFFF}};
    com(dev, DrawPrimitiveUP, 4, 5, 2, gcopy(v, sizeof v), 16);
    com(dev, SetPixelShader, 1, 0);
    com(dev, SetTexture, 2, 0, 0);
    present();
}

static void readbacks(void) {
    /* the last presented frame through GetFrontBuffer, as the game's save thumbnails get it */
    uint32_t pp = galloc(4), lr = galloc(8);
    com(dev, CreateImageSurface, 4, W, H, 21, pp);
    uint32_t img = MEM32(pp);
    com(dev, GetFrontBuffer, 1, img);
    com(img, S_LockRect, 3, lr, 0, 0);
    save("front.bin", GPTR(MEM32(lr + 4)), W * H * 4);
    com(img, S_UnlockRect, 0);
    /* a 16-bit render target cleared, drawn into, then locked: read back through GL */
    com(dev, CreateRenderTarget, 6, 64, 32, 23, 0, 1, pp);
    uint32_t rt = MEM32(pp);
    com(dev, SetRenderTarget, 2, rt, 0);
    com(dev, Clear, 6, 0, 0, 1, 0xFF00FF00, u32f(1), 0);
    quad(32, 0, 64, 32, 0xFFFF0000);
    com(rt, S_LockRect, 3, lr, 0, 0);
    save("rt565.bin", GPTR(MEM32(lr + 4)), 64 * 32 * 2);
    com(rt, S_UnlockRect, 0);
}

int main(int argc, char **argv) {
    out = argc > 1 ? argv[1] : ".";
    if (chdir(out)) rt_fatal("cannot enter %s", out);
    g_xbox_mem_offset = (ptrdiff_t)host_arena_reserve();
    rt_thread_init_main(1u << 20);
    rt_window_open();
    uint32_t d3d = call_bridge("Direct3DCreate8", 1, (uint32_t[]){220});
    /* D3DPRESENT_PARAMETERS: W, H, fmt, count, ms, swap, hwnd, windowed, autodepth, depth fmt, flags, rate, interval */
    uint32_t pp[13] = {W, H, 22, 1, 0, 1, 0x00010001, 1, 1, 75, 0, 0, 0};
    uint32_t ppva = gcopy(pp, sizeof pp), out_dev = galloc(4);
    if (com(d3d, 15, 6, 0, 1, 0x00010001, 0x40, ppva, out_dev)) rt_fatal("CreateDevice failed");
    dev = MEM32(out_dev);
    scene_texture();
    scene_formats();
    scene_3d();
    scene_shaders();
    readbacks();
    puts("render test: done");
    return 0;
}
