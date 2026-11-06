/* CHEAT (the touch tray's toggle and the Halo of the Sun button, touch.c), for Silent Hill 3. While on, Heather's
 * health is held at its full every frame, so nothing kills her; off, she takes damage again.
 *
 * Where the game keeps it (read in the generated code, see the private build repo's research notes):
 *   0x712C100  the player object's pointer (0 before a game is loaded)
 *   +0x180     her health, +0x184 its maximum (floats; sub_004DAD20 sets the maximum by difficulty, 200 / 100 / 50...)
 *   0x712C614  the health kept between rooms, and 0x712C618 its maximum (sub_004A18F0 copies them into a new player)
 * SH2's cheat also handed out guns, ammo and healing items; SH3's inventory isn't found yet. */
#include "../runtime/rt.h"
#include "../runtime/host.h"

#define PLAYER_PTR  0x0712C100u
#define HEALTH      0x180u
#define HEALTH_MAX  0x184u
#define KEPT        0x0712C614u
#define KEPT_MAX    0x0712C618u

static int cheat_on;

void cheat_set(int on) {
    if (on != cheat_on) rt_log("cheat: %s", on ? "on (Heather can't die)" : "off");
    cheat_on = on;
}
int cheat_get(void) { return cheat_on; }

/* Game thread, once per presented frame (d3d8.c). */
void cheat_tick(void) {
    if (!cheat_on || !rt_game_loaded) return;
    uint32_t p = MEM32(PLAYER_PTR);
    if (p < 0x10000u || (uint64_t)p + HEALTH_MAX + 4 > host_arena_bytes) return;
    float max = MEMF(p + HEALTH_MAX);
    if (max > 0 && max < 100000 && MEMF(p + HEALTH) > 0 && MEMF(p + HEALTH) < max) MEMF(p + HEALTH) = max;
    float kmax = MEMF(KEPT_MAX);
    if (kmax > 0 && kmax < 100000 && MEMF(KEPT) < kmax) MEMF(KEPT) = kmax;
}
