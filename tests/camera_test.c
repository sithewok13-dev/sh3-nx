/* The camera's math (src/runtime/camera.c, included whole so its own functions can be checked): with nothing
 * moved the frozen frame's matrix is the identity; a selfie's turn puts the camera in front of Heather's face; a
 * zoom brings it nearer along its line to her. No GL: the recorded frame's camera and Heather are made up here.
 *   cc -std=gnu17 -I third_party/xboxrecomp/templates/runtime $(pkg-config --cflags sdl2) tests/camera_test.c -lm */
#include "../src/runtime/camera.c"
#include <stdio.h>

static float V[16], P[16], HEAD[3], FACE[3];
int frame_rec_camera(float view[16], float proj[16]) { memcpy(view, V, 64), memcpy(proj, P, 64); return 1; }
int game_player_head(float head[3], float facing[3]) { memcpy(head, HEAD, 12), memcpy(facing, FACE, 12); return 1; }
void frame_rec_begin(void) {}
int frame_rec_ready(void) { return 0; }
void frame_rec_drop(void) {}
void frame_replay(const float *clip) { (void)clip; }
void frame_show(void) {}
void dsound_pause(int on) { (void)on; }
void rt_log(const char *fmt, ...) { (void)fmt; }

static int fails;
static void check(int ok, const char *what) { printf("  %s   camera: %s\n", ok ? "ok" : "FAIL", what); fails += !ok; }

/* The camera's position in the frame's view space after the move: Mv^-1 (0, 0, 0), Mv = P^-1 M P. */
static void moved_camera(const double *clip, double *pos) {
    double mv[16], inv[16], o[3] = {0, 0, 0};
    mat_mul(mv, F.proj_inv, clip);
    mat_mul(mv, mv, F.proj);
    mat_inv(inv, mv);
    xform(pos, inv, o, 1);
}

int main(void) {
    /* a projection like the game's (D3D, left-handed, near 50, far 50000), and a camera at world (0, -800, -2000)
     * looking along +z, its view y up = the world's -y (Silent Hill's world is y down) */
    float zn = 50, zf = 50000, q = zf / (zf - zn);
    float p[16] = {1.3f, 0, 0, 0, 0, 1.73f, 0, 0, 0, 0, q, 1, 0, 0, -zn * q, 0};
    memcpy(P, p, 64);
    float v[16] = {1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1, 0, 0, -800, 2000, 1};  /* world y down -> view y up */
    memcpy(V, v, 64);
    HEAD[0] = 100, HEAD[1] = -800, HEAD[2] = 0;   /* 2000 ahead of the camera, at its height */
    FACE[0] = 0, FACE[1] = 0, FACE[2] = -1;       /* facing the camera */
    frozen_setup();
    check(F.ok && F.heather, "Heather on screen: she is the pivot");
    double m[16], id = 0;
    frozen_clip(m, 0, 0, 1);
    for (int i = 0; i < 16; i++) id = fmax(id, fabs(m[i] - (i % 5 == 0)));
    check(id < 1e-3f, "nothing moved: the identity");

    /* she faces away: the selfie turns the camera half way round, to in front of her face */
    FACE[2] = 1;
    frozen_setup();
    double yaw = selfie_yaw(), pos[3];
    frozen_clip(m, yaw, 0, 1);
    moved_camera(m, pos);
    double to[3] = {pos[0] - F.pivot[0], pos[1] - F.pivot[1], pos[2] - F.pivot[2]};
    norm3(to);
    double f[3];
    memcpy(f, F.facing, sizeof f);
    char what[128];
    snprintf(what, sizeof what, "selfie: the camera ends up where she faces (%.3f, turned %.0f degrees)", dot3(to, f), yaw * 57.3f);
    check(dot3(to, f) > 0.98f, what);
    double d0 = sqrt(dot3(F.pivot, F.pivot)), d1 = sqrt((pos[0] - F.pivot[0]) * (pos[0] - F.pivot[0]) +
        (pos[1] - F.pivot[1]) * (pos[1] - F.pivot[1]) + (pos[2] - F.pivot[2]) * (pos[2] - F.pivot[2]));
    snprintf(what, sizeof what, "selfie: as far from her as before (%.1f, was %.1f)", d1, d0);
    check(fabs(d0 - d1) < d0 * 1e-4, what);
    frozen_clip(m, yaw, 0, 0.5f);
    moved_camera(m, pos);
    d1 = sqrt((pos[0] - F.pivot[0]) * (pos[0] - F.pivot[0]) + (pos[1] - F.pivot[1]) * (pos[1] - F.pivot[1]) +
               (pos[2] - F.pivot[2]) * (pos[2] - F.pivot[2]));
    check(fabs(d1 - d0 * 0.5) < 0.5, "zoom 0.5: half as far");
    /* the pivot stays on the same screen spot when only turning */
    frozen_clip(m, 0.7f, 0.3f, 1);
    double c[4], c2[4];
    for (int i = 0; i < 4; i++) c[i] = P[i] * F.pivot[0] + P[4 + i] * F.pivot[1] + P[8 + i] * F.pivot[2] + P[12 + i];
    for (int i = 0; i < 4; i++) c2[i] = m[i] * c[0] + m[4 + i] * c[1] + m[8 + i] * c[2] + m[12 + i] * c[3];
    check(fabs(c[0] / c[3] - c2[0] / c2[3]) < 1e-3f && fabs(c[1] / c[3] - c2[1] / c2[3]) < 1e-3f,
          "turning: she stays where she was on screen");
    puts(fails ? "FAIL" : "PASS");
    return fails != 0;
}
