/* The game's side of the touch controls (touch.c): which screen is up, the control type, the flashlight.
 * Silent Hill 3's addresses, found in this exe by the byte patterns ThirteenAG's SilentHill3.WidescreenFix
 * (Camera.ixx, Menu.ixx; MIT) uses, and read in the generated code (the private build repo's research notes):
 *   0x712C100  the player object's pointer (0 before a game is loaded)
 *   0x81FAB5   the render mode: 2 while an in-engine cutscene plays
 *   0xBCC218   the pause menu's state (sub_005E8270 / sub_005E88B0): 0 when it isn't up
 *   0x70E66CD  the control type the game moves by (sub_0060CC90 returns it): 0 the original "3D" tank
 *              controls, 1 the "2D" ones, where the stick points where Heather goes from the camera's view */
#include "../runtime/rt.h"

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
void controls_tick(void) {
    if (!chosen || !MEM32(PLAYER_PTR)) return;
    uint8_t want = classic ? 0 : 1;
    if (MEM8(CONTROL_TYPE) != want) MEM8(CONTROL_TYPE) = want;
}

/* The flashlight's state is not known yet in SH3: LIGHT is drawn unlit (its button still toggles the light). */
int game_flashlight_on(void) { return 0; }

int game_screen(void) {
    if (MEM8(PAUSE_STATE)) return SCREEN_PAUSE;
    if (!MEM32(PLAYER_PTR)) return SCREEN_MENU;  /* the title, the main menu, options, loading a save */
    if (MEM8(RENDER_MODE) == 2) return SCREEN_CUTSCENE;
    return SCREEN_PLAY;
}

/* The pause menu takes the mouse (its rows are hit boxes), so a tap there is a click (dinput.c); nothing here. */
int game_pause_tap(float y480) { (void)y480; return 0; }
