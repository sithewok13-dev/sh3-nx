/* Textures, surfaces and buffers. The game locks a guest memory copy; GL work happens lazily on the
 * render thread (at bind or draw time), so a loader thread may create and fill resources. */
#include "d3d8.h"

static Res *table[1 << 16];
static uint32_t next_slot = 1;

/* NULL for NULL, and for anything that is not a live resource object (a released texture the game
 * still has bound, say), which is logged once. */
Res *res_of(uint32_t obj) {
    if (!obj) return NULL;
    uint32_t slot = MEM32(obj + 4);
    Res *r = slot < (1u << 16) ? table[slot] : NULL;
    if (r && r->obj == obj) return r;
    static uint32_t warned;
    if (warned != obj) rt_log("D3D: %08X is not a live resource (slot %08X)", obj, slot), warned = obj;
    return NULL;
}

/* ---- formats ---- */
/* conv: how ES needs the guest's pixels rearranged first (gles_pixels); always CV_NONE on desktop GL. */
enum { CV_NONE, CV_SWAP_RB, CV_1555, CV_4444 };
typedef struct { uint32_t fmt; int bpp, block; GLenum ifmt, gfmt, type; GLint swz[4]; int conv; } Fmt;
#define RGBA {GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA}
#define RGB1 {GL_RED, GL_GREEN, GL_BLUE, GL_ONE}
#ifdef SH2_GLES
/* ES 3.0 takes no BGRA and no reversed 16-bit packings: those formats are rearranged into R,G,B,A order on
 * the way in (and back on readback), and DXT is decoded to RGBA8 where the driver has no S3TC. */
static const Fmt fmts[] = {
    {FMT_A8R8G8B8, 4, 0, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, RGBA, CV_SWAP_RB},
    {FMT_X8R8G8B8, 4, 0, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, RGB1, CV_SWAP_RB},
    {FMT_R5G6B5, 2, 0, GL_RGB565, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, RGB1, CV_NONE},
    {FMT_X1R5G5B5, 2, 0, GL_RGB5_A1, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, RGB1, CV_1555},
    {FMT_A1R5G5B5, 2, 0, GL_RGB5_A1, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, RGBA, CV_1555},
    {FMT_A4R4G4B4, 2, 0, GL_RGBA4, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, RGBA, CV_4444},
    {FMT_X4R4G4B4, 2, 0, GL_RGBA4, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, RGB1, CV_4444},
#else
static const Fmt fmts[] = {
    {FMT_A8R8G8B8, 4, 0, GL_RGBA8, GL_BGRA, GL_UNSIGNED_BYTE, RGBA},
    {FMT_X8R8G8B8, 4, 0, GL_RGBA8, GL_BGRA, GL_UNSIGNED_BYTE, RGB1},
    {FMT_R5G6B5, 2, 0, GL_RGB565, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, RGB1},
    {FMT_X1R5G5B5, 2, 0, GL_RGB5_A1, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV, RGB1},
    {FMT_A1R5G5B5, 2, 0, GL_RGB5_A1, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV, RGBA},
    {FMT_A4R4G4B4, 2, 0, GL_RGBA4, GL_BGRA, GL_UNSIGNED_SHORT_4_4_4_4_REV, RGBA},
    {FMT_X4R4G4B4, 2, 0, GL_RGBA4, GL_BGRA, GL_UNSIGNED_SHORT_4_4_4_4_REV, RGB1},
#endif
    {FMT_A8, 1, 0, GL_R8, GL_RED, GL_UNSIGNED_BYTE, {GL_ZERO, GL_ZERO, GL_ZERO, GL_RED}},
    {FMT_L8, 1, 0, GL_R8, GL_RED, GL_UNSIGNED_BYTE, {GL_RED, GL_RED, GL_RED, GL_ONE}},
    {FMT_A8L8, 2, 0, GL_RG8, GL_RG, GL_UNSIGNED_BYTE, {GL_RED, GL_RED, GL_RED, GL_GREEN}},
    {FMT_V8U8, 2, 0, GL_RG8_SNORM, GL_RG, GL_BYTE, {GL_RED, GL_GREEN, GL_ONE, GL_ONE}},
    {FMT_P8, 1, 0, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, RGBA},  /* expanded through the palette */
    {FMT_DXT1, 0, 8, 0x83F1, 0, 0, RGBA},
    {FMT_DXT2, 0, 16, 0x83F2, 0, 0, RGBA},
    {FMT_DXT3, 0, 16, 0x83F2, 0, 0, RGBA},
    {FMT_DXT4, 0, 16, 0x83F3, 0, 0, RGBA},
    {FMT_DXT5, 0, 16, 0x83F3, 0, 0, RGBA},
    {FMT_D16, 2, 0, GL_DEPTH24_STENCIL8, 0, 0, RGBA}, {FMT_D16_LOCKABLE, 2, 0, GL_DEPTH24_STENCIL8, 0, 0, RGBA},
    {FMT_D24S8, 4, 0, GL_DEPTH24_STENCIL8, 0, 0, RGBA}, {FMT_D24X8, 4, 0, GL_DEPTH24_STENCIL8, 0, 0, RGBA},
    {FMT_D24X4S4, 4, 0, GL_DEPTH24_STENCIL8, 0, 0, RGBA}, {FMT_D32, 4, 0, GL_DEPTH24_STENCIL8, 0, 0, RGBA},
    {FMT_D15S1, 2, 0, GL_DEPTH24_STENCIL8, 0, 0, RGBA},
};
static const Fmt *fmt_info(uint32_t f) {
    for (size_t i = 0; i < sizeof fmts / sizeof *fmts; i++)
        if (fmts[i].fmt == f) return &fmts[i];
    rt_fatal("unsupported D3D format %u (0x%08X)", f, f);
}
static int is_depth_fmt(uint32_t f) { return f >= FMT_D16_LOCKABLE && f <= FMT_D16; }

static void level_size(const Fmt *f, uint32_t w, uint32_t h, uint32_t *pitch, uint32_t *size) {
    if (f->block) {
        *pitch = (w + 3) / 4 * f->block;
        *size = *pitch * ((h + 3) / 4);
    } else {
        *pitch = w * f->bpp;
        *size = *pitch * h;
    }
}

/* ---- ES pixel conversions ---- */
#ifdef SH2_GLES
/* One 4x4 DXT1/3/5 block to RGBA8 (R,G,B,A bytes), clipped to the w x h image at x, y. */
static void dxt_block(const uint8_t *b, int kind, uint8_t *out, uint32_t w, uint32_t h, uint32_t x, uint32_t y) {
    uint8_t alpha[16];
    const uint8_t *cb = b;
    if (kind == 3) {         /* explicit 4-bit alpha */
        for (int i = 0; i < 16; i++) { int a = (b[i / 2] >> (4 * (i & 1))) & 15; alpha[i] = a * 17; }
        cb = b + 8;
    } else if (kind == 5) {  /* two endpoints and 3-bit indices */
        uint8_t a[8] = {b[0], b[1]};
        if (a[0] > a[1]) for (int i = 1; i < 7; i++) a[1 + i] = ((7 - i) * a[0] + i * a[1]) / 7;
        else { for (int i = 1; i < 5; i++) a[1 + i] = ((5 - i) * a[0] + i * a[1]) / 5; a[6] = 0; a[7] = 255; }
        uint64_t bits = 0;
        for (int i = 0; i < 6; i++) bits |= (uint64_t)b[2 + i] << (8 * i);
        for (int i = 0; i < 16; i++) alpha[i] = a[(bits >> (3 * i)) & 7];
        cb = b + 8;
    } else memset(alpha, 255, sizeof alpha);
    uint16_t c0 = cb[0] | cb[1] << 8, c1 = cb[2] | cb[3] << 8;
    uint8_t c[4][4];
    for (int i = 0; i < 2; i++) {
        uint16_t v = i ? c1 : c0;
        c[i][0] = (v >> 11) * 255 / 31; c[i][1] = ((v >> 5) & 63) * 255 / 63; c[i][2] = (v & 31) * 255 / 31; c[i][3] = 255;
    }
    int four = kind != 1 || c0 > c1;  /* DXT1 with c0 <= c1: three colors and transparent black */
    for (int k = 0; k < 3; k++) {
        c[2][k] = four ? (2 * c[0][k] + c[1][k]) / 3 : (c[0][k] + c[1][k]) / 2;
        c[3][k] = four ? (c[0][k] + 2 * c[1][k]) / 3 : 0;
    }
    c[2][3] = 255;
    c[3][3] = four ? 255 : 0;
    uint32_t idx = cb[4] | cb[5] << 8 | cb[6] << 16 | (uint32_t)cb[7] << 24;
    for (int i = 0; i < 16; i++) {
        uint32_t px = x + (i & 3), py = y + (i >> 2);
        if (px >= w || py >= h) continue;
        uint8_t *o = out + 4 * (py * w + px);
        memcpy(o, c[(idx >> (2 * i)) & 3], 4);
        if (kind != 1) o[3] = alpha[i];
    }
}

/* The guest's pixels (rows pitch bytes apart) as ES takes them for f, tightly packed; NULL when ES takes them
 * as they are. DXT comes out as RGBA8 when the driver has no S3TC (gles_dxt_decoded). The caller frees it. */
static int gles_dxt_decoded(const Fmt *f) { return f->block && !gl_has_s3tc; }
static void *gles_pixels(const Fmt *f, const void *src, uint32_t w, uint32_t h, uint32_t pitch) {
    if (gles_dxt_decoded(f)) {
        int kind = f->fmt == FMT_DXT1 ? 1 : f->fmt <= FMT_DXT3 ? 3 : 5;
        uint8_t *out = malloc(4u * w * h);
        for (uint32_t by = 0; by < (h + 3) / 4; by++)
            for (uint32_t bx = 0; bx < (w + 3) / 4; bx++)
                dxt_block((const uint8_t *)src + by * pitch + bx * f->block, kind, out, w, h, bx * 4, by * 4);
        return out;
    }
    if (f->conv == CV_NONE) return NULL;
    uint8_t *out = malloc((size_t)w * h * f->bpp);
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *r = (const uint8_t *)src + (size_t)y * pitch;
        if (f->conv == CV_SWAP_RB) {  /* B,G,R,A bytes -> R,G,B,A */
            uint8_t *o = out + (size_t)y * w * 4;
            for (uint32_t x = 0; x < w; x++) { o[4 * x] = r[4 * x + 2]; o[4 * x + 1] = r[4 * x + 1]; o[4 * x + 2] = r[4 * x]; o[4 * x + 3] = r[4 * x + 3]; }
        } else {  /* ARGB 1555 / 4444 -> RGBA 5551 / 4444: the alpha bits move from the top to the bottom */
            uint16_t *o = (uint16_t *)(out + (size_t)y * w * 2);
            int sh = f->conv == CV_1555 ? 1 : 4;
            for (uint32_t x = 0; x < w; x++) {
                uint16_t v = r[2 * x] | r[2 * x + 1] << 8;
                o[x] = (uint16_t)(v << sh | v >> (16 - sh));
            }
        }
    }
    return out;
}

/* RGBA8 rows read back from GL into the guest's layout for format fmt, rows pitch bytes apart. */
static void gles_from_rgba(uint32_t fmt, const uint8_t *rgba, uint8_t *dst, uint32_t w, uint32_t h, uint32_t pitch) {
    static int warned;
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *s = rgba + (size_t)y * w * 4;
        uint8_t *d = dst + (size_t)y * pitch;
        for (uint32_t x = 0; x < w; x++, s += 4) {
            uint32_t r = s[0], g = s[1], b = s[2], a = s[3];
            uint16_t v;
            switch (fmt) {
            case FMT_A8R8G8B8: case FMT_X8R8G8B8: d[4 * x] = b; d[4 * x + 1] = g; d[4 * x + 2] = r; d[4 * x + 3] = a; continue;
            case FMT_R5G6B5: v = (r >> 3) << 11 | (g >> 2) << 5 | b >> 3; break;
            case FMT_X1R5G5B5: case FMT_A1R5G5B5: v = (a >> 7) << 15 | (r >> 3) << 10 | (g >> 3) << 5 | b >> 3; break;
            case FMT_A4R4G4B4: case FMT_X4R4G4B4: v = (a >> 4) << 12 | (r >> 4) << 8 | (g >> 4) << 4 | b >> 4; break;
            default:
                if (!warned++) rt_log("GL: readback of format %u not converted", fmt);
                return;
            }
            d[2 * x] = v & 255;
            d[2 * x + 1] = v >> 8;
        }
    }
}
#endif

/* glTexSubImage2D of guest pixels (rows pitch bytes apart), converted first where ES needs it. */
static void tex_sub_image(GLenum target, int level, int x, int y, uint32_t w, uint32_t h, const Fmt *f,
                          const void *px, uint32_t pitch) {
#ifdef SH2_GLES
    void *conv = gles_pixels(f, px, w, h, pitch);
    if (conv) {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        if (gles_dxt_decoded(f)) glTexSubImage2D(target, level, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, conv);
        else glTexSubImage2D(target, level, x, y, w, h, f->gfmt, f->type, conv);
        free(conv);
        return;
    }
#endif
    glPixelStorei(GL_UNPACK_ROW_LENGTH, f->bpp && pitch != w * f->bpp ? pitch / f->bpp : 0);
    glTexSubImage2D(target, level, x, y, w, h, f->gfmt, f->type, px);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

/* ---- objects ---- */
static uint32_t vt_tex, vt_cube, vt_surf, vt_vb, vt_ib;

static Res *new_res(int kind, uint32_t vtable) {
    uint32_t slot = 0;
    for (uint32_t i = 0; i < (1u << 16) && !slot; i++) {
        uint32_t s = (next_slot + i) & 0xFFFF;
        if (s && !table[s]) slot = s;
    }
    if (!slot) rt_fatal("too many D3D resources");
    next_slot = slot + 1;
    Res *r = calloc(1, sizeof *r);
    r->kind = kind;
    r->refs = 1;
    r->obj = com_new(vtable, 16);
    MEM32(r->obj + 4) = slot;
    table[slot] = r;
    return r;
}

static void free_res(Res *r) {
    if (r->tex && !r->parent) glDeleteTextures(1, &r->tex);
    if (r->buf) glDeleteBuffers(1, &r->buf);
    if (r->mem) gfree(r->mem);
    for (int f = 0; f < 6; f++)
        for (int l = 0; l < 14; l++)
            if (r->surf[f][l]) free_res(res_of(r->surf[f][l]));
    table[MEM32(r->obj + 4)] = NULL;
    gfree(r->obj);
    free(r);
}

void res_addref(uint32_t obj) {
    Res *r = res_of(obj);
    if (!r) return;
    if (r->parent) r = r->parent;  /* a texture level shares its texture's count */
    r->refs++;
}
void res_release(uint32_t obj) {
    Res *r = res_of(obj);
    if (!r) return;
    if (r->parent) r = r->parent;
    /* ponytail: objects are freed at zero, but GL names deleted off the render thread would leak;
     * all game releases seen so far come from the render thread. */
    if (--r->refs == 0) free_res(r);
}

uint32_t res_surface(uint32_t w, uint32_t h, uint32_t fmt, int is_rt, int is_depth) {
    Res *r = new_res(RES_SURFACE, vt_surf);
    r->fmt = fmt;
    r->w = w;
    r->h = h;
    r->is_rt = is_rt;
    r->is_depth = is_depth || is_depth_fmt(fmt);
    level_size(fmt_info(fmt), w, h, &r->pitch, &r->size);
    if (!r->is_rt && !r->is_depth) r->mem = galloc(r->size);  /* image surfaces live in guest memory only */
    return r->obj;
}

uint32_t res_texture(uint32_t w, uint32_t h, uint32_t levels, uint32_t usage, uint32_t fmt, uint32_t pool, int cube) {
    Res *t = new_res(cube ? RES_CUBE : RES_TEXTURE, cube ? vt_cube : vt_tex);
    uint32_t max = 1;
    while ((w >> max) || (h >> max)) max++;
    if (!levels || levels > max) levels = max;
    if (levels > 14) levels = 14;
    *t = (Res){t->kind, t->obj, 1, fmt, usage, pool, w, h, levels};
    t->is_rt = usage & 1;
    t->is_depth = (usage & 2) || is_depth_fmt(fmt);
    const Fmt *f = fmt_info(fmt);
    for (int face = 0; face < (cube ? 6 : 1); face++)
        for (uint32_t l = 0; l < levels; l++) {
            Res *s = new_res(RES_SURFACE, vt_surf);
            s->fmt = fmt;
            s->w = w >> l ? w >> l : 1;
            s->h = h >> l ? h >> l : 1;
            s->parent = t;
            s->level = l;
            s->face = face;
            s->is_rt = t->is_rt;
            s->is_depth = t->is_depth;
            level_size(f, s->w, s->h, &s->pitch, &s->size);
            if (!s->is_rt && !s->is_depth) s->mem = galloc(s->size);
            t->surf[face][l] = s->obj;
        }
    return t->obj;
}

uint32_t res_buffer(int kind, uint32_t size, uint32_t usage, uint32_t fmt_or_fvf, uint32_t pool) {
    Res *b = new_res(kind, kind == RES_VB ? vt_vb : vt_ib);
    b->size = size;
    b->usage = usage;
    b->fmt = fmt_or_fvf;
    b->pool = pool;
    b->mem = galloc(size);
    b->dirty = 1;
    return b->obj;
}

/* ---- GL side ---- */

static GLenum face_target(Res *t, int face) {
    return t->kind == RES_CUBE ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + face : GL_TEXTURE_2D;
}

/* Creates the GL texture of a texture or standalone surface, with every level allocated. */
static void gl_ensure(Res *t) {
    if (t->tex) return;
    const Fmt *f = fmt_info(t->fmt);
    GLenum target = t->kind == RES_CUBE ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
    glGenTextures(1, &t->tex);
    glBindTexture(target, t->tex);
    uint32_t levels = t->kind == RES_SURFACE ? 1 : t->levels;
    glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, levels - 1);
#ifdef SH2_GLES
    glTexParameteri(target, GL_TEXTURE_SWIZZLE_R, f->swz[0]);
    glTexParameteri(target, GL_TEXTURE_SWIZZLE_G, f->swz[1]);
    glTexParameteri(target, GL_TEXTURE_SWIZZLE_B, f->swz[2]);
    glTexParameteri(target, GL_TEXTURE_SWIZZLE_A, f->swz[3]);
#else
    glTexParameteriv(target, GL_TEXTURE_SWIZZLE_RGBA, f->swz);
#endif
    for (int face = 0; face < (t->kind == RES_CUBE ? 6 : 1); face++)
        for (uint32_t l = 0; l < levels; l++) {
            uint32_t w = t->w >> l ? t->w >> l : 1, h = t->h >> l ? t->h >> l : 1, pitch, size;
            if (t->is_depth)
                glTexImage2D(face_target(t, face), l, GL_DEPTH24_STENCIL8, w, h, 0, GL_DEPTH_STENCIL,
                             GL_UNSIGNED_INT_24_8, NULL);
#ifdef SH2_GLES
            else if (gles_dxt_decoded(f))
                glTexImage2D(face_target(t, face), l, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
#endif
            else if (f->block) {
                level_size(f, w, h, &pitch, &size);
                glCompressedTexImage2D(face_target(t, face), l, f->ifmt, w, h, 0, size, NULL);
            } else glTexImage2D(face_target(t, face), l, f->ifmt, w, h, 0, f->gfmt, f->type, NULL);
        }
}

static GLuint surface_tex(Res *s) { if (s->parent) { gl_ensure(s->parent); return s->parent->tex; } gl_ensure(s); return s->tex; }

/* Uploads one level from its guest copy. */
static void upload_level(Res *t, Res *s) {
    const Fmt *f = fmt_info(s->fmt);
    GLenum target = face_target(t, s->face);
    glBindTexture(t->kind == RES_CUBE ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D, t->tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (f->block && gl_has_s3tc) glCompressedTexSubImage2D(target, s->level, 0, 0, s->w, s->h, f->ifmt, s->size, GPTR(s->mem));
    else if (f->block) tex_sub_image(target, s->level, 0, 0, s->w, s->h, f, GPTR(s->mem), s->pitch);
    else if (s->fmt == FMT_P8) {
        /* The indices through the current palette. PALETTEENTRY is R,G,B,flags in memory, and flags
         * is the alpha (D3DPTEXTURECAPS_ALPHAPALETTE, as on the GeForce-class cards the game was made
         * for); it used to be forced opaque, which turns any alpha-masked P8 texture into solid blocks
         * (issue #1, Heather's hair in the real-time cutscenes). A palette with no alpha set at all is
         * opaque, not invisible. */
        uint32_t *rgba = malloc(4u * s->w * s->h);
        const uint8_t *src = GPTR(s->mem);
        static const uint32_t none[256];
        const uint32_t *pal = dev->palette[dev->cur_palette] ? dev->palette[dev->cur_palette] : none;
        uint32_t has_alpha = 0;
        for (int i = 0; i < 256; i++) has_alpha |= pal[i] & 0xFF000000u;
        uint32_t opaque = has_alpha ? 0 : 0xFF000000u;
        for (uint32_t i = 0; i < s->w * s->h; i++) rgba[i] = pal[src[i]] | opaque;
        glTexSubImage2D(target, s->level, 0, 0, s->w, s->h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        free(rgba);
    } else tex_sub_image(target, s->level, 0, 0, s->w, s->h, f, GPTR(s->mem), s->pitch);
    s->dirty = 0;
}

void res_upload_dirty(Res *t) {
    gl_ensure(t);
    if (t->fmt == FMT_P8) {
        /* D3D resolves a paletted texture at draw time, with the palette then current: a GL copy
         * expanded under another palette, or one SetPaletteEntries has changed since, is redone. */
        uint32_t ver = dev->pal_version[dev->cur_palette];
        if (t->pal_idx != dev->cur_palette || t->pal_ver != ver) {
            t->pal_idx = dev->cur_palette;
            t->pal_ver = ver;
            t->dirty = 1;
            for (int face = 0; face < (t->kind == RES_CUBE ? 6 : 1); face++)
                for (uint32_t l = 0; l < t->levels; l++) res_of(t->surf[face][l])->dirty = 1;
        }
    }
    if (!t->dirty) return;
    t->dirty = 0;
    for (int face = 0; face < (t->kind == RES_CUBE ? 6 : 1); face++)
        for (uint32_t l = 0; l < t->levels; l++) {
            Res *s = res_of(t->surf[face][l]);
            if (s->dirty) upload_level(t, s);
        }
}

static void mark_dirty(Res *s) {
    s->dirty = 1;
    if (s->parent) s->parent->dirty = 1;
}

/* A depth surface smaller than the render target clips a GL framebuffer to their intersection, where D3D8
 * drivers drew the whole target: SH3 renders its scene into 512x512 with the 640x480 auto depth, which left
 * the bottom 32 rows black. Such a pass gets a depth texture as big as both instead.
 * ponytail: one spare, re-made when the size changes. */
static GLuint spare_depth(uint32_t w, uint32_t h) {
    static GLuint tex;
    static uint32_t sw, sh;
    if (!tex || sw != w || sh != h) {
        if (!tex) glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, w, h, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
        sw = w;
        sh = h;
    }
    return tex;
}

void res_bind_targets(void) {
    Res *rt = res_of(dev->rt), *ds = res_of(dev->ds);
    GLuint depth = ds ? surface_tex(ds) : 0;
    int depth_level = ds ? ds->level : 0;
    if (ds && (ds->w < rt->w || ds->h < rt->h)) {
        depth = spare_depth(ds->w > rt->w ? ds->w : rt->w, ds->h > rt->h ? ds->h : rt->h);
        depth_level = 0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, dev->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, rt->parent ? face_target(rt->parent, rt->face) : GL_TEXTURE_2D,
                           surface_tex(rt), rt->level);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, depth, depth_level);
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) rt_log("framebuffer incomplete: %04X (rt %ux%u fmt %u)", st, rt->w, rt->h, rt->fmt);
}

/* Reads a render target back into the surface's guest copy, for locks of rendered images. */
static void readback(Res *s) {
    const Fmt *f = fmt_info(s->fmt);
    if (!s->mem) s->mem = galloc(s->size);
    GLuint fb;
    glGenFramebuffers(1, &fb);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fb);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, surface_tex(s), s->level);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
#ifdef SH2_GLES  /* ES reads RGBA8, converted to the surface's format */
    uint8_t *rgba = malloc(4u * s->w * s->h);
    glReadPixels(0, 0, s->w, s->h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    gles_from_rgba(s->fmt, rgba, GPTR(s->mem), s->w, s->h, s->pitch);
    free(rgba);
    (void)f;
#else
    glReadPixels(0, 0, s->w, s->h, f->gfmt, f->type, GPTR(s->mem));
#endif
    glDeleteFramebuffers(1, &fb);
    glBindFramebuffer(GL_FRAMEBUFFER, dev->fbo);
}

/* The last presented frame. Direct3D's front buffer is what was last shown, not the back buffer the game has
 * already cleared for the next frame; the game uses it for the transition cross-fades and the save thumbnail. */
static GLuint front_tex, front_fbo;
static uint32_t front_w, front_h;

void res_snapshot_front(void) {  /* called by Present with the back buffer bound for reading */
    Res *bb = res_of(dev->backbuffer);
    if (!bb) return;
    if (!front_tex || front_w != bb->w || front_h != bb->h) {
        if (front_tex) { glDeleteTextures(1, &front_tex); glDeleteFramebuffers(1, &front_fbo); }
        front_w = bb->w;
        front_h = bb->h;
        glGenTextures(1, &front_tex);
        glBindTexture(GL_TEXTURE_2D, front_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, front_w, front_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glGenFramebuffers(1, &front_fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, front_fbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, front_tex, 0);
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, front_fbo);
    glBlitFramebuffer(0, 0, front_w, front_h, 0, 0, front_w, front_h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl_screen_fbo());  /* back to the window (not 0 on iOS) */
}

/* GetFrontBuffer: the last presented image into an A8R8G8B8 image surface. */
uint32_t res_read_front(uint32_t dst_o) {
    Res *src = res_of(dev->backbuffer), *dst = res_of(dst_o);
    if (!dst || !dst->mem || dst->fmt != FMT_A8R8G8B8) return D3DERR_INVALIDCALL;
    GLuint fb;
    glGenFramebuffers(1, &fb);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fb);
    /* the presented frame when there is one, else the back buffer */
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           front_tex && front_w == src->w && front_h == src->h ? front_tex : surface_tex(src), 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    uint32_t rw = src->w < dst->w ? src->w : dst->w, rh = src->h < dst->h ? src->h : dst->h;
#ifdef SH2_GLES
    uint8_t *rgba = malloc(4u * rw * rh);
    glReadPixels(0, 0, rw, rh, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    gles_from_rgba(FMT_A8R8G8B8, rgba, GPTR(dst->mem), rw, rh, dst->pitch);
    free(rgba);
#else
    glPixelStorei(GL_PACK_ROW_LENGTH, dst->pitch / 4);
    glReadPixels(0, 0, rw, rh, GL_BGRA, GL_UNSIGNED_BYTE, GPTR(dst->mem));
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
#endif
    glDeleteFramebuffers(1, &fb);
    for (uint32_t i = 0; i < dst->size; i += 4) *(uint8_t *)GPTR(dst->mem + i + 3) = 0xFF;
    mark_dirty(dst);
    res_bind_targets();
    return 0;
}

uint32_t res_copy_rects(uint32_t src_o, uint32_t rects, uint32_t n, uint32_t dst_o, uint32_t points) {
    Res *src = res_of(src_o), *dst = res_of(dst_o);
    if (src->fmt != dst->fmt && !(src->is_rt && dst->is_rt)) return D3DERR_INVALIDCALL;
    int32_t whole[4] = {0, 0, (int32_t)src->w, (int32_t)src->h};
    if (!n) n = 1;
    for (uint32_t i = 0; i < n; i++) {
        const int32_t *r = rects ? (const int32_t *)GPTR(rects + 16 * i) : whole;  /* RECT l t r b */
        int32_t dx = points ? ((int32_t *)GPTR(points + 8 * i))[0] : r[0];
        int32_t dy = points ? ((int32_t *)GPTR(points + 8 * i))[1] : r[1];
        int w = r[2] - r[0], h = r[3] - r[1];
        if (src->is_rt && dst->is_rt) {  /* GPU to GPU */
            GLuint fb[2];
            glGenFramebuffers(2, fb);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, fb[0]);
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, surface_tex(src), src->level);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb[1]);
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, surface_tex(dst), dst->level);
            glDisable(GL_SCISSOR_TEST);
            glBlitFramebuffer(r[0], r[1], r[2], r[3], dx, dy, dx + w, dy + h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glDeleteFramebuffers(2, fb);
            res_bind_targets();
            continue;
        }
        if (src->is_rt) readback(src);
        const Fmt *f = fmt_info(src->fmt);
        if (dst->mem) {  /* row copies between guest copies */
            int bpp = f->block ? f->block : f->bpp, div = f->block ? 4 : 1;
            for (int y = 0; y < h / div; y++)
                memcpy(GPTR(dst->mem + (dy / div + y) * dst->pitch + dx / div * bpp),
                       GPTR(src->mem + (r[1] / div + y) * src->pitch + r[0] / div * bpp), w / div * bpp);
            mark_dirty(dst);
        } else {         /* guest copy into a render target */
            glBindTexture(GL_TEXTURE_2D, surface_tex(dst));
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            tex_sub_image(GL_TEXTURE_2D, dst->level, dx, dy, w, h, f,
                          GPTR(src->mem + r[1] * src->pitch + r[0] * f->bpp), src->pitch);
        }
    }
    return 0;
}

/* ---- COM methods ---- */

#define RES ((Res *)res_of(THIS))
METHOD(r_QueryInterface, 3) { return E_NOINTERFACE; }
METHOD(r_AddRef, 1) { res_addref(THIS); return RES->parent ? RES->parent->refs : RES->refs; }
METHOD(r_Release, 1) {
    Res *r = RES->parent ? RES->parent : RES;
    uint32_t left = r->refs - 1;
    res_release(THIS);
    return left;
}
METHOD(r_GetDevice, 2) { MEM32(ARG(1)) = dev->obj; return 0; }
METHOD(r_SetPrivateData, 5) { return 0; }
METHOD(r_GetPrivateData, 4) { return D3DERR_NOTAVAILABLE; }
METHOD(r_FreePrivateData, 2) { return 0; }
METHOD(r_SetPriority, 2) { return 0; }
METHOD(r_GetPriority, 1) { return 0; }
METHOD(r_PreLoad, 1) { return 0; }
METHOD(r_GetType, 1) {
    static const uint32_t type[] = {0, 3, 5, 1, 6, 7};
    return type[RES->kind];
}
METHOD(t_SetLOD, 2) { return 0; }
METHOD(t_GetLOD, 1) { return 0; }
METHOD(t_GetLevelCount, 1) { return RES->levels; }

static void surface_desc(Res *s, uint32_t d) {
    uint32_t usage = s->is_rt ? 1 : s->is_depth ? 2 : 0;
    uint32_t v[8] = {s->fmt, 1, usage, s->parent ? s->parent->pool : 0, s->size, 0, s->w, s->h};
    memcpy(GPTR(d), v, 32);
}
static Res *level_of(Res *t, int face, uint32_t l) { return l < t->levels ? res_of(t->surf[face][l]) : NULL; }

static uint32_t lock_surface(Res *s, uint32_t locked, uint32_t rect) {
    if (!s) return D3DERR_INVALIDCALL;
    if (!s->mem) readback(s);
    uint32_t off = 0;
    if (rect) {
        const int32_t *r = GPTR(rect);
        const Fmt *f = fmt_info(s->fmt);
        off = f->block ? r[1] / 4 * s->pitch + r[0] / 4 * f->block : r[1] * s->pitch + r[0] * f->bpp;
    }
    MEM32(locked) = s->pitch;
    MEM32(locked + 4) = s->mem + off;
    return 0;
}
static uint32_t unlock_surface(Res *s) {
    if (!s) return D3DERR_INVALIDCALL;
    if (s->parent || !s->is_rt) mark_dirty(s);
    else {  /* a locked render target: push the guest copy back */
        const Fmt *f = fmt_info(s->fmt);
        glBindTexture(GL_TEXTURE_2D, surface_tex(s));
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        tex_sub_image(GL_TEXTURE_2D, 0, 0, 0, s->w, s->h, f, GPTR(s->mem), s->pitch);
    }
    return 0;
}

METHOD(t_GetLevelDesc, 3) { Res *s = level_of(RES, 0, ARG(1)); if (!s) return D3DERR_INVALIDCALL; surface_desc(s, ARG(2)); return 0; }
METHOD(t_GetSurfaceLevel, 3) {
    Res *s = level_of(RES, 0, ARG(1));
    if (!s) return D3DERR_INVALIDCALL;
    res_addref(s->obj);
    MEM32(ARG(2)) = s->obj;
    return 0;
}
METHOD(t_LockRect, 5) { return lock_surface(level_of(RES, 0, ARG(1)), ARG(2), ARG(3)); }
METHOD(t_UnlockRect, 2) { return unlock_surface(level_of(RES, 0, ARG(1))); }
METHOD(t_AddDirtyRect, 2) { RES->dirty = 1; return 0; }

METHOD(c_GetCubeMapSurface, 4) {
    Res *s = level_of(RES, ARG(1) % 6, ARG(2));
    if (!s) return D3DERR_INVALIDCALL;
    res_addref(s->obj);
    MEM32(ARG(3)) = s->obj;
    return 0;
}
METHOD(c_LockRect, 6) { return lock_surface(level_of(RES, ARG(1) % 6, ARG(2)), ARG(3), ARG(4)); }
METHOD(c_UnlockRect, 3) { return unlock_surface(level_of(RES, ARG(1) % 6, ARG(2))); }
METHOD(c_AddDirtyRect, 3) { RES->dirty = 1; return 0; }

METHOD(s_GetContainer, 3) {
    Res *p = RES->parent;
    if (!p) return E_NOINTERFACE;
    res_addref(p->obj);
    MEM32(ARG(2)) = p->obj;
    return 0;
}
METHOD(s_GetDesc, 2) { surface_desc(RES, ARG(1)); return 0; }
METHOD(s_LockRect, 4) { return lock_surface(RES, ARG(1), ARG(2)); }
METHOD(s_UnlockRect, 1) { return unlock_surface(RES); }

METHOD(b_Lock, 5) {
    Res *b = RES;
    MEM32(ARG(3)) = b->mem + (ARG(1) < b->size ? ARG(1) : 0);
    return 0;
}
METHOD(b_Unlock, 1) { RES->dirty = 1; return 0; }
METHOD(vb_GetDesc, 2) {
    Res *b = RES;
    uint32_t v[6] = {FMT_VERTEXDATA, 6, b->usage, b->pool, b->size, b->fmt};
    memcpy(GPTR(ARG(1)), v, 24);
    return 0;
}
METHOD(ib_GetDesc, 2) {
    Res *b = RES;
    uint32_t v[5] = {b->fmt, 7, b->usage, b->pool, b->size};
    memcpy(GPTR(ARG(1)), v, 20);
    return 0;
}

#define RESOURCE(i) \
    {i "::QueryInterface", r_QueryInterface}, {i "::AddRef", r_AddRef}, {i "::Release", r_Release}, \
    {i "::GetDevice", r_GetDevice}, {i "::SetPrivateData", r_SetPrivateData}, {i "::GetPrivateData", r_GetPrivateData}, \
    {i "::FreePrivateData", r_FreePrivateData}, {i "::SetPriority", r_SetPriority}, {i "::GetPriority", r_GetPriority}, \
    {i "::PreLoad", r_PreLoad}, {i "::GetType", r_GetType}
static const ComEntry tex_vt[] = {
    RESOURCE("IDirect3DTexture8"), {"IDirect3DTexture8::SetLOD", t_SetLOD}, {"IDirect3DTexture8::GetLOD", t_GetLOD},
    {"IDirect3DTexture8::GetLevelCount", t_GetLevelCount}, {"IDirect3DTexture8::GetLevelDesc", t_GetLevelDesc},
    {"IDirect3DTexture8::GetSurfaceLevel", t_GetSurfaceLevel}, {"IDirect3DTexture8::LockRect", t_LockRect},
    {"IDirect3DTexture8::UnlockRect", t_UnlockRect}, {"IDirect3DTexture8::AddDirtyRect", t_AddDirtyRect},
};
static const ComEntry cube_vt[] = {
    RESOURCE("IDirect3DCubeTexture8"), {"IDirect3DCubeTexture8::SetLOD", t_SetLOD},
    {"IDirect3DCubeTexture8::GetLOD", t_GetLOD}, {"IDirect3DCubeTexture8::GetLevelCount", t_GetLevelCount},
    {"IDirect3DCubeTexture8::GetLevelDesc", t_GetLevelDesc},
    {"IDirect3DCubeTexture8::GetCubeMapSurface", c_GetCubeMapSurface},
    {"IDirect3DCubeTexture8::LockRect", c_LockRect}, {"IDirect3DCubeTexture8::UnlockRect", c_UnlockRect},
    {"IDirect3DCubeTexture8::AddDirtyRect", c_AddDirtyRect},
};
static const ComEntry surf_vt[] = {
    {"IDirect3DSurface8::QueryInterface", r_QueryInterface}, {"IDirect3DSurface8::AddRef", r_AddRef},
    {"IDirect3DSurface8::Release", r_Release}, {"IDirect3DSurface8::GetDevice", r_GetDevice},
    {"IDirect3DSurface8::SetPrivateData", r_SetPrivateData}, {"IDirect3DSurface8::GetPrivateData", r_GetPrivateData},
    {"IDirect3DSurface8::FreePrivateData", r_FreePrivateData}, {"IDirect3DSurface8::GetContainer", s_GetContainer},
    {"IDirect3DSurface8::GetDesc", s_GetDesc}, {"IDirect3DSurface8::LockRect", s_LockRect},
    {"IDirect3DSurface8::UnlockRect", s_UnlockRect},
};
static const ComEntry vb_vt[] = {
    RESOURCE("IDirect3DVertexBuffer8"), {"IDirect3DVertexBuffer8::Lock", b_Lock},
    {"IDirect3DVertexBuffer8::Unlock", b_Unlock}, {"IDirect3DVertexBuffer8::GetDesc", vb_GetDesc},
};
static const ComEntry ib_vt[] = {
    RESOURCE("IDirect3DIndexBuffer8"), {"IDirect3DIndexBuffer8::Lock", b_Lock},
    {"IDirect3DIndexBuffer8::Unlock", b_Unlock}, {"IDirect3DIndexBuffer8::GetDesc", ib_GetDesc},
};

void res_init_vtables(void) {
    vt_tex = com_vtable(tex_vt, sizeof tex_vt / sizeof *tex_vt);
    vt_cube = com_vtable(cube_vt, sizeof cube_vt / sizeof *cube_vt);
    vt_surf = com_vtable(surf_vt, sizeof surf_vt / sizeof *surf_vt);
    vt_vb = com_vtable(vb_vt, sizeof vb_vt / sizeof *vb_vt);
    vt_ib = com_vtable(ib_vt, sizeof ib_vt / sizeof *ib_vt);
}
