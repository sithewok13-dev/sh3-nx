/* CONTROLS (the touch tray, touch.c): modern controls by default, the original ones as CLASSIC.
 * The game's control type is the byte at 0x1DBC008 (SH2EE's GetControlTypePointer: the operand of the
 * `mov al, [..]` 0x16 bytes past 0x4676E9): 0 rotational (the original "3D" tank controls), 1 directional
 * (the "2D" type: the stick points where James goes, from the camera's view). The tray's choice is kept in
 * touch.cfg and held every frame, so the game's own options menu (or a save's settings) can't undo it. */
#include "../runtime/rt.h"

#define CONTROL_TYPE 0x01DBC008u

static int classic, chosen;  /* chosen: the touch tray decides (iOS); elsewhere the game's own setting stands */

void controls_set_classic(int on) {
    classic = !!on;
    chosen = 1;
    rt_log("controls: %s", classic ? "classic (rotational)" : "modern (directional)");
}

/* Game thread, once per presented frame (d3d8.c). */
void controls_tick(void) {
    if (!chosen) return;
    uint8_t want = classic ? 0 : 1;
    if (MEM8(CONTROL_TYPE) != want) MEM8(CONTROL_TYPE) = want;
}

/* The flashlight's switch, for the touch overlay's LIGHT picture: the byte at 0x1F7A806 (SH2EE's
 * GetFlashlightSwitchPointer: 0x29 bytes past the pattern at 0x43ED25), flipped by the light's button
 * (sub_0048E800) and set at a new game (sub_0049EE10). */
int game_flashlight_on(void) { return MEM8(0x01F7A806u) != 0; }

/* ---- which screen the game is on, for the touch overlay (touch.c) ----
 * The addresses are SH2EE's (Patches/Common.cpp), found in this exe by the same byte patterns:
 *   0x932178  the event index (GetEventIndexPointer): 4 in play, 15 a movie, 16 the pause menu, 2 the main menu,
 *             5 the map, 6 the inventory, 7 options, 8 memos, 9 saving, 0 and 1 loading, 10-13 results and endings
 *   0x1F7A7C4 the cutscene playing, 0 for none (GetCutsceneIDPointer)
 *   0x932030  the pause menu's quit question is up (GetQuitSubmenuFlagPointer)
 *   PAUSE_ROW the pause menu's highlighted row (GetPauseMenuButtonIndexPointer)
 * The pause menu's rows, in the game's 480-high screen: from 216, 30 apart, Resume to Quit (SH2EE's hitboxes). */
#define EVENT_INDEX 0x00932178u
#define CUTSCENE_ID 0x01F7A7C4u
#define QUIT_FLAG   0x00932030u
#define PAUSE_ROW   0x009326B4u  /* the operand of the `mov eax, [..]` 0x10 past the pattern at 0x407497 */

int game_screen(void) {
    switch (MEM8(EVENT_INDEX)) {
    case 4: return MEM32(CUTSCENE_ID) ? SCREEN_CUTSCENE : SCREEN_PLAY;
    case 15: return SCREEN_CUTSCENE;
    case 16: return SCREEN_PAUSE;
    case 0: case 1: return SCREEN_LOADING;
    case 5: case 6: case 8: return SCREEN_GAME_MENU;
    default: return SCREEN_MENU;
    }
}

/* A tap on the pause menu, y in the game's 480-high screen: picks the row under it and says to confirm (1), or
 * nothing (0). The quit question (yes / no) is left to the arrows and OK. */
int game_pause_tap(float y480) {
    if (MEM8(QUIT_FLAG)) return 0;
    int row = (int)((y480 - 216) / 30);
    if (y480 < 216 || row > 4) return 0;
    MEM8(PAUSE_ROW) = (uint8_t)row;
    return 1;
}
