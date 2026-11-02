/* CHEAT (the touch tray's toggle, touch.c): for Silent Hill 3, still to come. SH2's version wrote James'
 * health and inventory at SH2's addresses; SH3 keeps Heather's elsewhere, and until they are found in this exe
 * the toggle only remembers its state (and lights up), touching nothing in the game. */
#include "../runtime/rt.h"

static int cheat_on;

void cheat_set(int on) {
    if (on != cheat_on) rt_log("cheat: %s (not wired to the game yet)", on ? "on" : "off");
    cheat_on = on;
}
int cheat_get(void) { return cheat_on; }
void cheat_tick(void) {}
