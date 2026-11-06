/* CAMERA's freeze, selfies and sweeps (the touch controls' camera button, touch.c), the way the Jedi Knight port's
 * camera works (jk-files docs/HOUSE_STYLE.md, the v0.5 camera):
 *   freeze  the game stops (sound too) and the camera moves round the frozen moment: a drag turns it round what it
 *           looks at (Heather, when she is on screen), a pinch moves it in and out; CAMERA takes clean photos;
 *           leaving carries on from exactly the same moment
 *   selfie  the game freezes, the camera swings round in front of Heather, and 1, 3 or 5 photos are taken with a
 *           flash; then the camera swings back and the game carries on, changed in nothing
 *   sweep   the game freezes and five photos are taken as the camera sweeps round the moment
 * The game is held inside its Present (d3d8.c calls camera_after_present): one whole frame is recorded as the game
 * draws it, then drawn again each screen frame with the 3D moved (d3d8.c frame_replay). The camera moves in the
 * frame's view space, round a pivot, so where nothing is moved yet the picture is exactly the game's.
 *
 * Matrices here are 4x4 column-major arrays for column vectors (m[c * 4 + r]); a D3D row-major matrix (row vectors)
 * is the same array. Silent Hill 3's world is y down, in units of 2 mm (500 to a metre: SilentHill3.FusionFix). */
#include "rt.h"
#include "../d3d8/d3d8.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <pthread.h>

#define METRE 500.0f
#define PI 3.14159265f

static pthread_mutex_t mx = PTHREAD_MUTEX_INITIALIZER;
static struct {
    int req, req_shots;  /* asked for (main thread): CAM_*, and the selfie's photos */
    int mode, shots;     /* running (game thread) */
    int stop;            /* the freeze is to end */
    float yaw, pitch, zoom;  /* the freeze's: radians round the pivot, and the distance's factor */
} C = {.zoom = 1};

/* ---- the main thread's side (touch.c) ---- */
void camera_start(int what, int shots) {
    pthread_mutex_lock(&mx);
    if (!C.mode && !C.req) C.req = what, C.req_shots = shots, C.stop = 0, C.yaw = C.pitch = 0, C.zoom = 1;
    pthread_mutex_unlock(&mx);
}
void camera_stop(void) {
    pthread_mutex_lock(&mx);
    C.stop = 1, C.req = 0;
    pthread_mutex_unlock(&mx);
}
int camera_state(void) {
    pthread_mutex_lock(&mx);
    int s = C.mode ? C.mode : C.req;
    pthread_mutex_unlock(&mx);
    return s;
}
/* A drag of (dx, dy) points across a screen w points wide: half a turn for a drag across it. */
void camera_orbit(float dx, float dy, float w) {
    pthread_mutex_lock(&mx);
    C.yaw += dx / w * PI;
    C.pitch += dy / w * PI;
    if (C.pitch > 1.2f) C.pitch = 1.2f;
    if (C.pitch < -1.2f) C.pitch = -1.2f;
    pthread_mutex_unlock(&mx);
}
void camera_zoom(float factor) {
    pthread_mutex_lock(&mx);
    C.zoom /= factor > 0.2f ? factor : 0.2f;  /* fingers apart: closer */
    if (C.zoom < 0.25f) C.zoom = 0.25f;
    if (C.zoom > 3.0f) C.zoom = 3.0f;
    pthread_mutex_unlock(&mx);
}

/* ---- matrices ---- */
static void mat_mul(double *r, const double *a, const double *b) {  /* r = a b */
    double t[16];
    for (int c = 0; c < 4; c++)
        for (int i = 0; i < 4; i++)
            t[c * 4 + i] = a[i] * b[c * 4] + a[4 + i] * b[c * 4 + 1] + a[8 + i] * b[c * 4 + 2] + a[12 + i] * b[c * 4 + 3];
    memcpy(r, t, sizeof t);
}
static int mat_inv(double *r, const double *m) {
    double inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    double det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (fabs(det) < 1e-300) return 0;
    for (int i = 0; i < 16; i++) r[i] = inv[i] / det;
    return 1;
}
static void xform(double *out, const double *m, const double *v, double w) {  /* out = m (v, w), xyz only */
    for (int i = 0; i < 3; i++) out[i] = m[i] * v[0] + m[4 + i] * v[1] + m[8 + i] * v[2] + m[12 + i] * w;
}
static double dot3(const double *a, const double *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void cross3(double *r, const double *a, const double *b) {
    double t[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    memcpy(r, t, sizeof t);
}
static void norm3(double *v) {
    double l = sqrt(dot3(v, v));
    if (l > 0) v[0] /= l, v[1] /= l, v[2] /= l;
}
static void ident(double *m) { memset(m, 0, 16 * sizeof *m); m[0] = m[5] = m[10] = m[15] = 1; }
static void translate(double *m, const double *t) { ident(m); m[12] = t[0], m[13] = t[1], m[14] = t[2]; }
/* Turns points by a radians round the unit axis u through point c (Rodrigues). */
static void rotate_about(double *m, const double *u, double a, const double *c) {
    double s = sin(a), k = cos(a), t = 1 - k, x = u[0], y = u[1], z = u[2], r[16], tr[16], back[16];
    ident(r);
    r[0] = t * x * x + k, r[4] = t * x * y - s * z, r[8] = t * x * z + s * y;
    r[1] = t * x * y + s * z, r[5] = t * y * y + k, r[9] = t * y * z - s * x;
    r[2] = t * x * z - s * y, r[6] = t * y * z + s * x, r[10] = t * z * z + k;
    double nc[3] = {-c[0], -c[1], -c[2]};
    translate(tr, nc), translate(back, c);
    mat_mul(m, r, tr);
    mat_mul(m, back, m);
}

/* ---- the frozen frame's camera ---- */
static struct {
    int ok;              /* the recorded frame had a fixed-function 3D pass to take the projection from */
    double proj[16], proj_inv[16];
    double pivot[3];      /* in the frame's view space */
    double up[3];         /* the world's up, in view space */
    double facing[3];     /* Heather's facing, in view space (selfies) */
    int heather;         /* she is on screen: the pivot is her head */
} F;

static void frozen_setup(void) {
    double view[16];
    float fv[16], fp[16];
    memset(&F, 0, sizeof F);
    if (!frame_rec_camera(fv, fp)) return;
    for (int i = 0; i < 16; i++) view[i] = fv[i], F.proj[i] = fp[i];
    if (!mat_inv(F.proj_inv, F.proj)) return;
    F.ok = 1;
    double wup[3] = {0, -1, 0};  /* y down */
    xform(F.up, view, wup, 0);
    norm3(F.up);
    float fh[3], ff[3];
    if (game_player_head(fh, ff)) {
        double head[3] = {fh[0], fh[1], fh[2]}, face[3] = {ff[0], ff[1], ff[2]};
        double c[3], clip[4];
        xform(c, view, head, 1);
        for (int i = 0; i < 4; i++) clip[i] = F.proj[i] * c[0] + F.proj[4 + i] * c[1] + F.proj[8 + i] * c[2] + F.proj[12 + i];
        if (clip[3] > 0 && fabs(clip[0]) < clip[3] * 1.1 && fabs(clip[1]) < clip[3] * 1.1) {
            memcpy(F.pivot, c, sizeof c);
            xform(F.facing, view, face, 0);
            F.heather = 1;
        }
    }
    if (!F.heather) F.pivot[0] = F.pivot[1] = 0, F.pivot[2] = 3.0 * METRE;  /* what the camera looks at, 3 m away */
    rt_log("camera: frozen, pivot %s at %.0f %.0f %.0f", F.heather ? "Heather" : "ahead", F.pivot[0], F.pivot[1], F.pivot[2]);
}

/* The clip-space matrix for a camera turned yaw round the world's up through the pivot, tilted pitch round its
 * own right, and zoom times as far from the pivot. */
static void frozen_clip(double *out, double yaw, double pitch, double zoom) {
    double m[16], r[16], right[3] = {1, 0, 0}, toward[3];
    /* the camera turns one way round the pivot, so the frozen world turns the other */
    rotate_about(m, F.up, -yaw, F.pivot);
    cross3(right, F.up, F.pivot);  /* level, across the line of sight */
    norm3(right);
    if (dot3(right, right) > 0.5) { rotate_about(r, right, -pitch, F.pivot); mat_mul(m, r, m); }
    /* zoom: the camera moves along its line to the pivot, so the world moves the other way */
    for (int i = 0; i < 3; i++) toward[i] = -F.pivot[i] * (1 - zoom);
    translate(r, toward);
    mat_mul(m, r, m);
    mat_mul(out, F.proj, m);
    mat_mul(out, out, F.proj_inv);
}

/* The turn round the world's up that puts the camera in front of Heather's face. */
static double selfie_yaw(void) {
    double o[3] = {-F.pivot[0], -F.pivot[1], -F.pivot[2]}, f[3];  /* from her head to the camera */
    memcpy(f, F.facing, sizeof f);
    double ou = dot3(o, F.up), fu = dot3(f, F.up);
    for (int i = 0; i < 3; i++) o[i] -= ou * F.up[i], f[i] -= fu * F.up[i];
    double x[3];
    cross3(x, o, f);
    return atan2(dot3(x, F.up), dot3(o, f));
}

static float ease(float t) { t = t < 0 ? 0 : t > 1 ? 1 : t; return t * t * (3 - 2 * t); }

/* One screen frame of the frozen moment, drawn from the camera; a photo of it if asked. */
static void frozen_frame(float yaw, float pitch, float zoom, int photo) {
    double clip[16];
    float c[16];
    if (F.ok) frozen_clip(clip, yaw, pitch, zoom);
    for (int i = 0; i < 16; i++) c[i] = (float)clip[i];
    frame_replay(F.ok ? c : NULL);
#if defined(SH2_IOS) || defined(SH2_TOUCH)
    if (photo) touch_take_photo(photo == 2);
#endif
    frame_show();
}

static void run_frozen(int mode, int shots) {
    dsound_pause(1);
    frozen_setup();
    Uint32 t0 = SDL_GetTicks();
    if (mode == CAM_FREEZE) {
        for (;;) {
            pthread_mutex_lock(&mx);
            float yaw = C.yaw, pitch = C.pitch, zoom = C.zoom;
            int stop = C.stop;
            pthread_mutex_unlock(&mx);
            if (stop) break;
            frozen_frame(yaw, pitch, zoom, 0);
        }
    } else if (mode == CAM_SELFIE) {
        /* swing round (0.45 s), the photos 0.35 s apart, each a little to the side of the last, then back (0.3 s) */
        float to = F.heather ? (float)selfie_yaw() : 0, zoom = 1;
        if (F.heather) {
            float d = (float)sqrt(dot3(F.pivot, F.pivot));
            zoom = d > 0 ? 1.4f * METRE / d : 1;  /* 1.4 m from her: arm's length and a bit */
            if (zoom > 1.2f) zoom = 1.2f;
            if (zoom < 0.15f) zoom = 0.15f;
        }
        static const float side[] = {0, -0.14f, 0.14f, -0.26f, 0.26f};
        float swing = 450, gap = 350, back = 300, end = swing + 150 + gap * shots + back;
        int taken = 0;
        for (;;) {
            float t = SDL_GetTicks() - t0, a, z, p = -0.12f;
            if (t < swing) a = ease(t / swing);
            else if (t < end - back) a = 1;
            else if (t < end) a = 1 - ease((t - (end - back)) / back);
            else break;
            int k = t < swing + 150 ? -1 : (int)((t - swing - 150) / gap);
            int shoot = k >= 0 && k < shots && k == taken;
            float off = k >= 0 && k < shots ? side[k % 5] : side[(shots - 1) % 5];
            z = 1 + (zoom - 1) * a;
            frozen_frame(to * a + off * a, p * a, z, shoot ? 2 : 0);
            if (shoot) taken++;
        }
    } else {  /* CAM_SWEEP: five photos as the camera sweeps from 35 degrees one side to 35 the other (1.6 s) */
        float span = 0.61f, dur = 1600;
        int taken = 0;
        for (;;) {
            float t = SDL_GetTicks() - t0;
            if (t > dur + 250) break;
            float yaw = -span + 2 * span * ease(t / dur);
            int shoot = taken < 5 && t >= taken * dur / 4;  /* at 0, 1/4... of the way, and the end */
            frozen_frame(yaw, 0, 1, shoot ? 1 : 0);
            if (shoot) taken++;
        }
    }
    /* the picture just as the game left it, before it carries on */
    frame_replay(NULL);
    frame_rec_drop();
    dsound_pause(0);
}

/* Game thread, after every Present (d3d8.c). */
void camera_after_present(void) {
    pthread_mutex_lock(&mx);
    int req = C.req, mode = C.mode, shots = C.req_shots;
    pthread_mutex_unlock(&mx);
    if (!mode && req) {  /* record the game's next frame */
        frame_rec_begin();
        pthread_mutex_lock(&mx);
        C.mode = req, C.req = 0, C.shots = shots;
        pthread_mutex_unlock(&mx);
        return;
    }
    if (!mode || !frame_rec_ready()) return;
    rt_log("camera: %s", mode == CAM_FREEZE ? "freeze" : mode == CAM_SELFIE ? "selfie" : "sweep");
    run_frozen(mode, C.shots);
    pthread_mutex_lock(&mx);
    C.mode = 0, C.stop = 0;
    pthread_mutex_unlock(&mx);
    rt_log("camera: the game carries on");
}
