/* The game's side of the touch controls (touch.c): which screen is up, the control type, the flashlight.
 * Silent Hill 3's addresses, found in this exe by the byte patterns ThirteenAG's SilentHill3.WidescreenFix
 * (Camera.ixx, Menu.ixx; MIT) uses, and read in the generated code (the private build repo's research notes):
 *   0x712C100  the player object's pointer (0 before a game is loaded)
 *   0x81FAB5   the render mode: 2 while an in-engine cutscene plays
 *   0xBCC218   the pause menu's state (sub_005E8270 / sub_005E88B0): 0 when it isn't up
 *   0x72C780   the frame's width and, at 0x72C784, height (sub_004168E0 / sub_004168F0): the menus scale the
 *              cursor from them into their 512-wide space (sub_00600920)
 *   0x70E66CD  the control type the game moves by (sub_0060CC90 returns it): 0 the original "3D" tank
 *              controls, 1 the "2D" ones, where the stick points where Heather goes from the camera's view */
#include "../runtime/rt.h"
#include "../runtime/host.h"
#include <math.h>

#define PLAYER_PTR   0x0712C100u
#define RENDER_MODE  0x0081FAB5u
#define PAUSE_STATE  0x00BCC218u
#define CONTROL_TYPE 0x070E66CDu

static int classic, chosen;  /* chosen: the touch tray decides (iOS); elsewhere the game's own setting stands */

void controls_set_classic(int on) {
    classic = !!on;
    chosen = 1;
    rt_log("controls: %s", classic ? "classic (3D)" : "modern (2D)");
}

/* Game thread, once per presented frame (d3d8.c): holds the tray's choice, so the game's options can't undo it. */
static void frame_rate_tick(void);
void controls_tick(void) {
    frame_rate_tick();
    if (!chosen || !rt_game_loaded || !MEM32(PLAYER_PTR)) return;
    uint8_t want = classic ? 0 : 1;
    if (MEM8(CONTROL_TYPE) != want) MEM8(CONTROL_TYPE) = want;
}

/* The flashlight for LIGHT's picture: 0 none yet (hidden), 1 off, 2 on. SH3's switch and whether Heather has
 * the light aren't found yet, so it shows, drawn off (its button still toggles the light). */
int game_flashlight_on(void) { return rt_game_loaded && MEM32(PLAYER_PTR) ? 1 : 0; }

/* Before the game is loaded (the launcher draws the overlay too) none of its memory is there to read. */
int game_screen(void) {
    if (!rt_game_loaded) return SCREEN_MENU;
    if (MEM8(PAUSE_STATE)) return SCREEN_PAUSE;
    if (!MEM32(PLAYER_PTR)) return SCREEN_MENU;  /* the title, the main menu, options, loading a save */
    if (MEM8(RENDER_MODE) == 2) return SCREEN_CUTSCENE;
    return SCREEN_PLAY;
}

/* The pause menu takes the mouse (its rows are hit boxes), so a tap there is a click (user32.c); nothing here. */
int game_pause_tap(float y480) { (void)y480; return 0; }

int game_frame_size(int *w, int *h) {
    if (!rt_game_loaded) return 0;
    *w = (int)MEM32(0x0072C780u), *h = (int)MEM32(0x0072C784u);
    return *w > 0 && *h > 0 && *w <= 8192 && *h <= 8192;
}

/* Heather is somewhere in the game (not the title or its menus): the top row's game buttons show. */
int game_in_world(void) { return rt_game_loaded && MEM32(PLAYER_PTR) != 0; }

/* RATE in the touch tray (60 by default on iOS). Unlike SH2, the PC Silent Hill 3 runs at 60 itself: its frame
 * pacing (sub_0041B2C0) aims for the smaller of the display's rate (0x72C7E8) and its cap (0x72C790, 60 when 0),
 * and steps the game by the time each frame took. tools/patch_exe.py holds it at a steady 60 (FusionFix's fix);
 * RATE 30 sets the cap to 30, for a cooler phone. The cap is held every frame, as the game's settings may write it. */
#define FRAME_CAP 0x0072C790u
static int frame_rate;  /* 0: the game's own */
int game_set_frame_rate(int fps) {
    if (fps == 30 || fps == 60) frame_rate = fps;
    return 1;
}
static void frame_rate_tick(void) {
    if (frame_rate && rt_game_loaded && MEM32(FRAME_CAP) != (uint32_t)frame_rate) {
        MEM32(FRAME_CAP) = frame_rate;
        rt_log("controls: %d fps", frame_rate);
    }
}

/* Heather's head and the way she faces, in the world (y down), for the camera's selfies (runtime/camera.c): the
 * player object's position (+0, her feet) and yaw (+0x14), from SilentHill3.FusionFix's Camera.ixx. Her head is
 * 1.6 m above her feet (the game's units are 2 mm). */
int game_player_head(float head[3], float facing[3]) {
    if (!rt_game_loaded) return 0;
    uint32_t p = MEM32(PLAYER_PTR);
    if (p < 0x10000u || (uint64_t)p + 0x200 > host_arena_bytes) return 0;
    float yaw = MEMF(p + 0x14);
    head[0] = MEMF(p), head[1] = MEMF(p + 4) - 1.6f * 500.0f, head[2] = MEMF(p + 8);
    facing[0] = sinf(yaw), facing[1] = 0, facing[2] = cosf(yaw);
    return isfinite(head[0]) && isfinite(head[1]) && isfinite(head[2]) && isfinite(yaw);
}
