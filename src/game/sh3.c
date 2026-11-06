/* Hand-written replacements of sh3.exe functions, by address: tools/regen.sh passes this file to the
 * recompiler (--exclude-manual), which then leaves these functions out of src/recomp/gen. */
#include "../runtime/rt.h"
#include "../runtime/host.h"
#include <math.h>

/* The static CRT's memcpy (memmove semantics) and its second copy: their jump tables sit past the end
 * the disassembler finds and are indexed backwards, so the lifted copies cannot follow them. */
static void crt_memmove(void) {
    uint32_t d = ARG(0), s = ARG(1), n = ARG(2);
    memmove((void *)XBOX_PTR(d), (const void *)XBOX_PTR(s), n);
    g_eax = d;
    g_esp += 4;  /* ret: cdecl */
}
void sub_00661D20(void) { crt_memmove(); }
void sub_0066F9C0(void) { crt_memmove(); }

/* ---- widescreen (ThirteenAG's SilentHill3.WidescreenFix, ported) ----
 * The display mode is always the screen's (resolution= in sh3.ini): the device is created and reset with it
 * whatever disp.ini or the options ask for. 0x72C780/84 is the mode the game wants, 0x72C788/8C the one it
 * lays the 2D out in; both feed D3DPRESENT_PARAMETERS at 0x72C664. */
extern uint32_t rt_res_w, rt_res_h;
static void force_mode(void) {
    MEM32(0x72C780) = MEM32(0x72C788) = rt_res_w;
    MEM32(0x72C784) = MEM32(0x72C78C) = rt_res_h;
}
extern void sub_004193A0_gen(void);
void sub_004193A0(void) { force_mode(); sub_004193A0_gen(); }  /* creates the device */
extern void sub_00419760_gen(void);
void sub_00419760(void) { force_mode(); sub_00419760_gen(); }  /* resets it after a mode change */
extern void sub_00416AE0_gen(void);
void sub_00416AE0(void) { ARG(0) = rt_res_w; ARG(1) = rt_res_h; sub_00416AE0_gen(); }  /* set mode (w, h) */
extern void sub_00416B70_gen(void);
void sub_00416B70(void) { ARG(0) = rt_res_w; ARG(1) = rt_res_h; sub_00416B70_gen(); }  /* set 2D size (w, h) */

/* The camera's projection scales, returned in ST(0): horizontal (0x43AB80) and vertical (0x43ABC0). Gameplay
 * widens the view (hor+); a cutscene (0x81FAB5) keeps its 4:3 width and loses some height instead. The
 * picked-up item and the mouse cursor (calls returning to 0x599C29, 0x4DDB98) keep the 4:3 width. */
#define ST0 g_fp_stack[g_fp_top & 7]
static double ws_aspect(void) { return (double)rt_res_w / rt_res_h; }
static double ws_fov(void) { return (4.0 / 3.0) / ws_aspect(); }  /* 1 at 4:3 */
extern void sub_0043AB80_gen(void);
void sub_0043AB80(void) {
    uint32_t from = MEM32(g_esp);
    sub_0043AB80_gen();
    if (!MEM8(0x81FAB5) && from != 0x599C29u && from != 0x4DDB98u) ST0 = (float)(ST0 * ws_fov());
}
extern void sub_0043ABC0_gen(void);
void sub_0043ABC0(void) { sub_0043ABC0_gen(); if (MEM8(0x81FAB5)) ST0 = (float)(ST0 / ws_fov()); }

/* float screen_x(int x) (0x682BA0): the 2D's x, in 1/16 units around 2048, to pixels. The 2D keeps a centered
 * 4:3 area; the elements meant to span the screen (ThirteenAG's list, by the call's return address) keep the
 * original mapping. */
static const uint32_t full_width_calls[] = {
    0x468B8A, 0x468BDF, 0x468C25, 0x468C71, 0x468E31, 0x468E78, 0x468EB7, 0x468F0C,
    0x47A684, 0x47A6D8, 0x47A726, 0x47A771, 0x47A7B9, 0x49FAF0, 0x5F6D27, 0x5F80F4,
};
void sub_00682BA0(void) {
    uint32_t from = MEM32(g_esp);
    int32_t x = (int32_t)ARG(0);
    int full = 0;
    for (size_t i = 0; i < sizeof full_width_calls / sizeof *full_width_calls; i++) full |= from == full_width_calls[i];
    double w = (int32_t)guest_call(0x402BA0, 0, NULL, 0);  /* the target's width */
    double left = full ? 256.0 : 192.0 * ws_aspect(), scale = full ? 1.0 / 512 : 1.0 / (384.0 * ws_aspect());
    rt_fpush(w * (x * MEMF(0x68E6E0) - MEMF(0x68E6DC) + left) * scale);
    g_esp += 4;  /* ret: cdecl */
}

/* Cave 0x401ED8, called from 0x5F1D15 in place of `fstp [ecx-0x18]; fld st(0)`: the health indicator's
 * vertices keep their place in the 4:3 area at the right of the screen. */
void sub_00401ED8(void) {
    double x = rt_fpop();
    MEMF(g_ecx - 0x18) = (float)(x * ws_fov() + (rt_res_w - rt_res_h * 4.0 / 3.0));
    rt_fpush(ST0);
    g_esp += 4;
}

/* The pad's default bindings, (button << 8 | action) shorts at 0x7214E4 that the game copies when there is no
 * savedata/key.ini: laid out as in sh2-nx. Buttons are 0x0C + dinput.c's numbering (X A B Y LB RB LT RT Back
 * Start LS RS); actions 0 search view, 1 ready weapon, 2/3 strafe left/right, 4 map, 5 action, 6 dash, 7 light,
 * 8 pause, 9 zoom, 0x0A supplies, 0x0B inventory. The other 13 entries (stick, camera, menus) stay. */
static const uint16_t pad_defaults[12] = {
    0x0C06, 0x0D05, 0x0E07, 0x0F04,  /* Switch Y dash, A action, B light (and cancel), X map */
    0x1002, 0x1103, 0x1200, 0x1301,  /* L / R strafe, ZL search view, ZR ready weapon */
    0x150B, 0x1408, 0x160A, 0x1709,  /* + inventory, − pause (and skip), stick clicks supplies / zoom */
};

/* int can_do(what) (0x495B60): the pause menu (calls returning to 0x5E88E2 / 0x5E88F7) asks it whether Save (0x0D)
 * and Load (0x0E) are allowed, and it refuses while the screen fade (type 0x897E38, done flag 0x897E3C) runs. The
 * pause freezes the fade, so pausing during a fade-in (types 5, 9, 13: back from the save screen, a load or a door)
 * left Save / Load greyed out until the menu was closed and opened again. For the pause menu a fade-in counts as
 * done; fade-outs still refuse. */
extern void sub_00495B60_gen(void);
void sub_00495B60(void) {
    uint32_t from = MEM32(g_esp);
    uint8_t type = MEM8(0x897E38), done = MEM8(0x897E3C);
    int fade_in = type == 5 || type == 9 || type == 13;
    if (fade_in && (from == 0x5E88E2u || from == 0x5E88F7u)) MEM8(0x897E3C) = 1;
    sub_00495B60_gen();
    MEM8(0x897E3C) = done;
}

/* ---- the free camera (VIEW in the touch tray: FREE), SilentHill3.FusionFix's modern camera (Camera.ixx, MIT) ----
 * A third-person camera over Heather's right shoulder, turned with the right stick (the touch controls' look drag).
 * Movement goes where the camera looks, readying a weapon brings it closer, and aiming with no target turns her to
 * where it looks. The game's own camera keeps running underneath: cutscenes, scripted camera moves, events, menus and
 * the map use it as before; only the final camera matrix is replaced, at the start of the game's camera tail
 * (sub_00471860, which copies it on to the renderer). Off (FIXED, the default): the game's cameras, as made.
 * Addresses: tools/patch_exe.py and the private build repo's research notes (FusionFix's byte patterns). */
#define PLAYER_PTR      0x0712C100u  /* +0 position (y down), +0x14 yaw, +0x180 health */
#define PLAYER_DT       0x0712C108u  /* seconds of the current player tick */
#define HEADING_REF     0x0712C12Cu  /* two camera yaws camera-relative movement uses */
#define HEADING_SLOT    0x0712C134u  /* byte: which; +1 its latch */
#define LOCKED_TARGET   0x0712C5A8u
#define AIMING          0x0712C650u
#define CAMERA_MODE     0x0712C750u  /* 0: the gameplay camera */
#define CAMERA_FLAGS    0x0711A608u  /* bits 0/1: a script holds the camera's position or target */
#define EVENT_ID        0x00BA9768u  /* -1: no event script */
#define RENDER_MODE     0x0081FAB5u  /* 2: an in-engine cutscene */
#define PAUSED          0x00BCC218u
#define STICK_RX        0x0089E89Cu
#define STICK_RY        0x0089E8A0u
#define PROJECTION      0x0712C720u  /* the projection distance SetProjection (0x4A0F20) keeps */
#define SET_CAMERA      0x004718B0u  /* void (int parent, const float *matrix) */
#define SET_PROJECTION  0x004A0F20u  /* void (float) */
#define SEGMENT_QUERY   0x004B1A10u  /* (int result[10], 0, float a[4], float b[4], 0x103E): what's in the way */
#define METRE 500.0f
#define PI_F 3.14159265f

static struct {
    int on, active, ticked, have_orbit, inactive;
    float yaw, pitch, dist, shoulder, height, zoom, collision, game_proj;
    uint32_t buf;  /* guest scratch: two points, a result, a matrix */
} FC = {.inactive = 1000, .pitch = -0.1f};

void game_set_free_camera(int on) {
    if (FC.on != !!on) rt_log("camera: %s", on ? "free (the right stick)" : "the game's");
    FC.on = !!on;
}
int game_free_camera_active(void) { return FC.active; }

static float wrap_angle(float a) { while (a > PI_F) a -= 2 * PI_F; while (a < -PI_F) a += 2 * PI_F; return a; }
static float approach(float cur, float target, float rate, float dt) { return target + (cur - target) * expf(-rate * dt); }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static uint32_t player(void) {
    uint32_t p = MEM32(PLAYER_PTR);
    return p >= 0x10000u && (uint64_t)p + 0x200 <= host_arena_bytes ? p : 0;
}

static int can_run(void) {
    if (!FC.on || !FC.ticked) return 0;
    uint32_t p = player();
    if (!p) return 0;
    if (MEM32(CAMERA_MODE) || (MEM32(CAMERA_FLAGS) & 3) || MEM8(RENDER_MODE) == 2 || (int32_t)MEM32(EVENT_ID) != -1 ||
        MEM8(PAUSED))
        return 0;
    return MEMF(p + 0x180) > 0;  /* not dead */
}

/* The share of the way from `from` to `to` that is free of walls (the game's own segment test). */
static float free_fraction(const float *from, const float *to) {
    uint32_t res = FC.buf, a = FC.buf + 64, b = FC.buf + 80;
    memset(GPTR(res), 0, 40);
    MEMF(a) = from[0], MEMF(a + 4) = from[1], MEMF(a + 8) = from[2], MEMF(a + 12) = 1;
    MEMF(b) = to[0], MEMF(b + 4) = to[1], MEMF(b + 8) = to[2], MEMF(b + 12) = 1;
    uint32_t args[5] = {res, 0, a, b, 0x103E};
    guest_call(SEGMENT_QUERY, 5, args, 0);
    if (!MEM32(res)) return 1;
    float dx = to[0] - from[0], dy = to[1] - from[1], dz = to[2] - from[2], len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (len < 1) return 1;
    float hx = MEMF(res + 4) - from[0], hy = MEMF(res + 8) - from[1], hz = MEMF(res + 12) - from[2];
    return clampf(sqrtf(hx * hx + hy * hy + hz * hz) / len, 0, 1);
}

static void set_projection(float v) { uint32_t u; memcpy(&u, &v, 4); guest_call(SET_PROJECTION, 1, &u, 0); }

static void free_camera_update(void) {
    int run = can_run();
    FC.ticked = 0;
    if (!run) {
        if (FC.active) set_projection(FC.game_proj);
        FC.active = 0;
        FC.inactive++;
        return;
    }
    if (!FC.buf) FC.buf = galloc(256);
    uint32_t p = player();
    float px = MEMF(p), py = MEMF(p + 4), pz = MEMF(p + 8), player_yaw = MEMF(p + 0x14);
    float dt = clampf(MEMF(PLAYER_DT), 0, 0.1f);
    int aiming = MEM8(AIMING) != 0;
    if (!FC.have_orbit || FC.inactive > 30) {  /* back from a cutscene or a while away: behind Heather */
        FC.yaw = player_yaw, FC.pitch = -0.1f, FC.have_orbit = 1, FC.collision = 0;
    }
    FC.inactive = 0;
    /* the right stick, straight (LOOK's steps already ease the touch drag): 3.6 radians a second at full tilt */
    float rx = MEMF(STICK_RX), ry = MEMF(STICK_RY);
    if (!(rx > -2 && rx < 2)) rx = 0;
    if (!(ry > -2 && ry < 2)) ry = 0;
    float sx = rx, sy = ry, speed = 3.6f;
    FC.yaw = wrap_angle(FC.yaw + sx * speed * dt);
    FC.pitch = clampf(FC.pitch - sy * speed * 0.65f * dt, -0.9f, 1.0f);  /* the stick down looks down */
    /* framing: further when walking, closer when aiming; the game's view is 2 atan(224 / 448) = 53 degrees high */
    float t_dist = (aiming ? 1.0f : 1.75f) * METRE, t_sh = (aiming ? 0.48f : 0.38f) * METRE, t_h = (aiming ? 1.45f : 1.5f) * METRE;
    float vertical = (60.0f - (aiming ? 10.0f : 0)) * PI_F / 180, t_zoom = 0.5f / tanf(vertical * 0.5f);
    if (FC.dist == 0) FC.dist = t_dist, FC.shoulder = t_sh, FC.height = t_h, FC.zoom = t_zoom;
    FC.dist = approach(FC.dist, t_dist, 8, dt);
    FC.shoulder = approach(FC.shoulder, t_sh, 8, dt);
    FC.height = approach(FC.height, t_h, 8, dt);
    FC.zoom = approach(FC.zoom, t_zoom, 8, dt);
    float sny = sinf(FC.yaw), csy = cosf(FC.yaw), snp = sinf(FC.pitch), csp = cosf(FC.pitch);
    float fwd[3] = {sny * csp, -snp, csy * csp}, right[3] = {csy, 0, -sny};
    /* the pivot over her shoulder (less of it looking steeply up or down), kept out of walls */
    float centre[3] = {px, py - FC.height, pz}, amount = FC.shoulder * (0.6f + 0.4f * csp);
    float sh[3] = {centre[0] + right[0] * amount, centre[1], centre[2] + right[2] * amount};
    float fr = free_fraction(centre, sh);
    if (fr < 1) {
        float keep = fr - 0.15f > 0 ? fr - 0.15f : 0;
        for (int i = 0; i < 3; i++) sh[i] = centre[i] + (sh[i] - centre[i]) * keep;
    }
    float want[3];
    for (int i = 0; i < 3; i++) want[i] = sh[i] - fwd[i] * FC.dist;
    /* walls between: five parallel rays round the camera */
    const float r = 0.12f * METRE, offs[5][2] = {{0, 0}, {r, 0}, {-r, 0}, {0, r}, {0, -r}};
    float frac = 1;
    for (int k = 0; k < 5; k++) {
        float a[3], b[3];
        for (int i = 0; i < 3; i++) {
            float side = right[i] * offs[k][0] + (i == 1 ? offs[k][1] : 0);
            a[i] = sh[i] + side * 0.25f, b[i] = want[i] + side;
        }
        float f = free_fraction(a, b);
        if (f < frac) frac = f;
    }
    float allowed = frac * FC.dist - 0.1f * METRE;
    if (allowed < 0.15f * METRE) allowed = 0.15f * METRE;
    if (FC.collision == 0 || allowed < FC.collision) FC.collision = allowed;  /* in front of a wall at once */
    else FC.collision = approach(FC.collision, allowed, 4, dt);             /* back out smoothly */
    float d = FC.collision < FC.dist ? FC.collision : FC.dist;
    float m[16] = {csy, 0, -sny, 0,  sny * snp, csp, csy * snp, 0,  sny * csp, -snp, csy * csp, 0,
                   sh[0] - fwd[0] * d, sh[1] - fwd[1] * d, sh[2] - fwd[2] * d, 1};
    uint32_t mva = FC.buf + 128;
    memcpy(GPTR(mva), m, 64);
    uint32_t args[2] = {0, mva};
    guest_call(SET_CAMERA, 2, args, 0);
    /* the zoom is relative to the game's own, kept while this camera runs */
    if (!FC.active) FC.game_proj = MEMF(PROJECTION);
    set_projection(FC.game_proj * FC.zoom);
    FC.active = 1;
}

/* The game's camera tail (copies the camera on to the renderer): the free camera's matrix goes in first. */
extern void sub_00471860_gen(void);
void sub_00471860(void) { free_camera_update(); sub_00471860_gen(); }

/* The player's update (its object's handler, called through a pointer): the camera runs only on frames it ran. */
extern void sub_004A17C0_gen(void);
void sub_004A17C0(void) { sub_004A17C0_gen(); FC.ticked = 1; }

/* Cave 0x47185B, called from 0x4D3BD4 in place of `mov ecx, [0x712C18C]` (tools/patch_exe.py), just after the game
 * worked out which way the camera faces for movement: while the free camera runs, that's its yaw. */
void sub_0047185B(void) {
    if (FC.active) {
        MEMF(HEADING_REF) = MEMF(HEADING_REF + 4) = FC.yaw;
        MEM8(HEADING_SLOT + 1) = 0;
    }
    g_ecx = MEM32(0x712C18C);
    g_esp += 4;
}

/* The control type the game moves by (0x60CC90): "2D" (where the stick points, from the camera) while it runs. */
extern void sub_0060CC90_gen(void);
void sub_0060CC90(void) {
    if (FC.active) { g_eax = 1; g_esp += 4; return; }
    sub_0060CC90_gen();
}

/* Facing while aiming (0x4D4700): with no target, Heather turns to where the free camera looks. */
extern void sub_004D4700_gen(void);
void sub_004D4700(void) {
    uint32_t p = player();
    if (FC.active && p && MEM8(AIMING) && !MEM32(LOCKED_TARGET)) {
        float fx = sinf(FC.yaw), fz = cosf(FC.yaw), target = atan2f(fx, fz), dt = MEMF(PLAYER_DT);
        float delta = wrap_angle(target - MEMF(p + 0x14)), k = 15 * dt < 1 ? 15 * dt : 1;
        MEMF(p + 0x14) = wrap_angle(fabsf(delta) < 0.01f ? target : MEMF(p + 0x14) + delta * k);
        g_esp += 4;  /* ret: cdecl */
        return;
    }
    sub_004D4700_gen();
}

/* Called once after sh3.exe is loaded: the variables tools/patch_exe.py points code at. */
void game_init(void) {
    host_arena_commit(0x0EC00000u, 0x1000);
    MEMF(0x0EC00000u) = ws_aspect() > 1.5 ? 0.0f : 0.5f;  /* cutscene borders: none on a wide screen */
    memcpy(GPTR(0x7214E4), pad_defaults, sizeof pad_defaults);
}

/* Indirect calls and the dispatch table reach the generated bodies (sub_X_gen) of wrapped functions: calls
 * by address are routed here first. */
recomp_func_t recomp_lookup_manual(uint32_t va) {
    switch (va) {
    case 0x00661D20u: return sub_00661D20;
    case 0x0066F9C0u: return sub_0066F9C0;
    case 0x004193A0u: return sub_004193A0;
    case 0x00419760u: return sub_00419760;
    case 0x00416AE0u: return sub_00416AE0;
    case 0x00416B70u: return sub_00416B70;
    case 0x0043AB80u: return sub_0043AB80;
    case 0x0043ABC0u: return sub_0043ABC0;
    case 0x00682BA0u: return sub_00682BA0;
    case 0x00401ED8u: return sub_00401ED8;
    case 0x00495B60u: return sub_00495B60;
    case 0x00471860u: return sub_00471860;
    case 0x004A17C0u: return sub_004A17C0;
    case 0x0047185Bu: return sub_0047185B;
    case 0x0060CC90u: return sub_0060CC90;
    case 0x004D4700u: return sub_004D4700;
    }
    return NULL;
}
