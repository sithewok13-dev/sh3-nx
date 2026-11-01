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
