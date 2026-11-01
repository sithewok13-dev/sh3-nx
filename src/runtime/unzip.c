/* A minimal zip reader for the iOS app's game-file import (ios.c): the game folder arrives as a few .zip parts
 * (a download is limited to 2 GB) that the app unpacks into its Documents folder on first launch.
 *
 * Handles what Python's zipfile writes without Zip64: stored and deflated entries, directories, a CRC check per
 * file. Paths are kept inside the destination (no absolute paths, no ".."). Built for iOS, and with SH2_UNZIP
 * for tests/run-unzip-test.sh. */
#if defined(SH2_IOS) || defined(SH2_UNZIP)
#include "unzip.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

static uint32_t rd16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

typedef struct { FILE *f; uint8_t *cd; uint32_t cd_size, entries; } Zip;

static int fail(char *err, size_t n, const char *fmt, const char *arg) {
    if (err) snprintf(err, n, fmt, arg);
    return -1;
}

/* The central directory: found from the end-of-central-directory record in the last 64 KB. */
static int zip_open(Zip *z, const char *path, char *err, size_t n) {
    memset(z, 0, sizeof *z);
    if (!(z->f = fopen(path, "rb"))) return fail(err, n, "cannot open %s", path);
    fseeko(z->f, 0, SEEK_END);
    off_t size = ftello(z->f), tail = size < 65557 ? size : 65557;
    uint8_t *buf = malloc(tail);
    fseeko(z->f, size - tail, SEEK_SET);
    if (fread(buf, 1, tail, z->f) != (size_t)tail) { free(buf); return fail(err, n, "cannot read %s", path); }
    int at = -1;
    for (off_t i = tail - 22; i >= 0 && at < 0; i--)
        if (rd32(buf + i) == 0x06054b50) at = (int)i;
    if (at < 0) { free(buf); return fail(err, n, "%s is not a zip file", path); }
    z->entries = rd16(buf + at + 10);
    z->cd_size = rd32(buf + at + 12);
    uint32_t cd_off = rd32(buf + at + 16);
    free(buf);
    if (z->entries == 0xFFFF || cd_off == 0xFFFFFFFFu) return fail(err, n, "%s needs Zip64, which is not supported", path);
    z->cd = malloc(z->cd_size ? z->cd_size : 1);
    fseeko(z->f, cd_off, SEEK_SET);
    if (fread(z->cd, 1, z->cd_size, z->f) != z->cd_size) return fail(err, n, "cannot read %s's directory", path);
    return 0;
}

static void zip_close(Zip *z) {
    if (z->f) fclose(z->f);
    free(z->cd);
}

/* Walks the central directory: calls fn for each entry with its name, method, sizes, CRC and local header
 * offset. Returns -1 on a malformed directory. */
typedef int (*EntryFn)(Zip *z, const char *name, int method, uint32_t csize, uint32_t usize, uint32_t crc,
                       uint32_t local, void *ctx);
static int zip_walk(Zip *z, EntryFn fn, void *ctx) {
    const uint8_t *p = z->cd, *end = z->cd + z->cd_size;
    for (uint32_t i = 0; i < z->entries; i++) {
        if (p + 46 > end || rd32(p) != 0x02014b50) return -1;
        uint32_t nl = rd16(p + 28), el = rd16(p + 30), cl = rd16(p + 32);
        if (p + 46 + nl > end) return -1;
        char name[1024];
        if (nl >= sizeof name) return -1;
        memcpy(name, p + 46, nl);
        name[nl] = 0;
        int r = fn(z, name, rd16(p + 10), rd32(p + 20), rd32(p + 24), rd32(p + 16), rd32(p + 42), ctx);
        if (r) return r;
        p += 46 + nl + el + cl;
    }
    return 0;
}

static int sum_sizes(Zip *z, const char *name, int method, uint32_t csize, uint32_t usize, uint32_t crc,
                     uint32_t local, void *ctx) {
    (void)z; (void)name; (void)method; (void)csize; (void)crc; (void)local;
    *(uint64_t *)ctx += usize;
    return 0;
}

uint64_t unzip_total_size(const char *zip_path) {
    Zip z;
    uint64_t total = 0;
    if (zip_open(&z, zip_path, NULL, 0) == 0) zip_walk(&z, sum_sizes, &total);
    zip_close(&z);
    return total;
}

/* mkdir -p for the directories leading to path (the last component is a file unless dir_too). */
static int make_dirs(char *path, int dir_too) {
    for (char *s = path + 1; *s; s++) {
        if (*s != '/') continue;
        *s = 0;
        int r = mkdir(path, 0755);
        *s = '/';
        if (r && errno != EEXIST) return -1;
    }
    if (dir_too && mkdir(path, 0755) && errno != EEXIST) return -1;
    return 0;
}

static int safe_name(const char *name) {
    if (!*name || name[0] == '/' || strchr(name, '\\')) return 0;
    for (const char *s = name; *s; ) {  /* no ".." component */
        const char *e = strchr(s, '/');
        size_t len = e ? (size_t)(e - s) : strlen(s);
        if (len == 2 && s[0] == '.' && s[1] == '.') return 0;
        s += len + (e != NULL);
    }
    return 1;
}

typedef struct {
    const char *dest;
    UnzipProgress progress;
    void *progress_ctx;
    uint64_t done;
    char *err;
    size_t err_n;
} Extract;

static int extract_one(Zip *z, const char *name, int method, uint32_t csize, uint32_t usize, uint32_t crc,
                       uint32_t local, void *ctx) {
    Extract *x = ctx;
    if (!safe_name(name)) return fail(x->err, x->err_n, "unsafe path in the zip: %s", name);
    char path[2048];
    snprintf(path, sizeof path, "%s/%s", x->dest, name);
    size_t pl = strlen(path);
    if (path[pl - 1] == '/') {  /* a directory */
        path[pl - 1] = 0;
        return make_dirs(path, 1) ? fail(x->err, x->err_n, "cannot create the folder %s", name) : 0;
    }
    if (method != 0 && method != 8) return fail(x->err, x->err_n, "%s uses a compression this app cannot read", name);
    uint8_t lh[30];
    fseeko(z->f, local, SEEK_SET);
    if (fread(lh, 1, 30, z->f) != 30 || rd32(lh) != 0x04034b50) return fail(x->err, x->err_n, "damaged zip at %s", name);
    fseeko(z->f, (off_t)local + 30 + rd16(lh + 26) + rd16(lh + 28), SEEK_SET);
    if (make_dirs(path, 0)) return fail(x->err, x->err_n, "cannot create the folder for %s", name);
    FILE *out = fopen(path, "wb");
    if (!out) return fail(x->err, x->err_n, "cannot write %s (is the phone full?)", name);

    enum { CHUNK = 1 << 18 };
    static uint8_t in[CHUNK], buf[CHUNK];
    z_stream zs;
    memset(&zs, 0, sizeof zs);
    if (method == 8 && inflateInit2(&zs, -MAX_WBITS) != Z_OK) { fclose(out); return fail(x->err, x->err_n, "zlib: %s", name); }
    uint32_t left = csize, sum = crc32(0, NULL, 0), written = 0;
    int r = 0, zr = Z_OK;
    while (left && zr != Z_STREAM_END) {
        uint32_t want = left < CHUNK ? left : CHUNK;
        if (fread(in, 1, want, z->f) != want) { r = fail(x->err, x->err_n, "the zip ends early in %s", name); break; }
        left -= want;
        if (method == 0) {
            sum = crc32(sum, in, want);
            if (fwrite(in, 1, want, out) != want) { r = fail(x->err, x->err_n, "cannot write %s (is the phone full?)", name); break; }
            written += want;
            x->done += want;
        } else {
            zs.next_in = in;
            zs.avail_in = want;
            do {
                zs.next_out = buf;
                zs.avail_out = CHUNK;
                zr = inflate(&zs, Z_NO_FLUSH);
                if (zr != Z_OK && zr != Z_STREAM_END && zr != Z_BUF_ERROR) { r = fail(x->err, x->err_n, "damaged data in %s", name); break; }
                uint32_t got = CHUNK - zs.avail_out;
                sum = crc32(sum, buf, got);
                if (got && fwrite(buf, 1, got, out) != got) { r = fail(x->err, x->err_n, "cannot write %s (is the phone full?)", name); break; }
                written += got;
                x->done += got;
            } while (zs.avail_out == 0 && zr != Z_STREAM_END);
            if (r) break;
        }
        if (x->progress) x->progress(x->done, x->progress_ctx);
    }
    if (method == 8) inflateEnd(&zs);
    if (fclose(out) && !r) r = fail(x->err, x->err_n, "cannot write %s (is the phone full?)", name);
    if (!r && (written != usize || sum != crc)) r = fail(x->err, x->err_n, "%s did not unpack correctly (download it again)", name);
    if (r) remove(path);
    return r;
}

int unzip_to(const char *zip_path, const char *dest, UnzipProgress progress, void *ctx, uint64_t done_before,
             char *err, size_t err_n) {
    Zip z;
    if (zip_open(&z, zip_path, err, err_n)) { zip_close(&z); return -1; }
    Extract x = {dest, progress, ctx, done_before, err, err_n};
    int r = zip_walk(&z, extract_one, &x);
    if (r && err && !*err) snprintf(err, err_n, "%s has a damaged directory", zip_path);
    zip_close(&z);
    return r ? -1 : 0;
}
#endif
