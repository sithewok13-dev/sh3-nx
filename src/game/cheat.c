/* CHEAT (the touch tray's toggle, touch.c): everything, the way the other ports' "sithewok" cheat does it.
 * While on: full health every frame (so nothing kills James), every gun with a full magazine that never
 * empties, a stack of each ammo, and the healing items. Off ends the invincibility and the endless magazines;
 * what it gave stays in the inventory.
 *
 * Where the game keeps these (found in the generated code, see the private build repo's research notes):
 *   0x1FB7C80        James' character; its +0x13C is his health and +0x140 his maximum (floats)
 *   0x1F7A7E0        the inventory: one bit per item id (id >> 5 picks the word, id & 31 the bit), 0x3C bytes
 *   0x1F7A7EC        16-bit counts by item id, up to 999 (sub_00579330 caps pickups there)
 *   0x1F7A809        the weapon in hand (an item id)
 * The reload (sub_0049EB30) pairs each gun with its ammo, the next id, and fills magazines of 10 (handgun),
 * 6 (shotgun) and 4 (rifle). Ids 1-3 are the healing items, the counted ones below the guns
 * (sub_0046BDD0 counts ids 1-11). */
#include "../runtime/rt.h"
#include "../runtime/host.h"

#define PLAYER      0x01FB7C80u
#define HEALTH      0x13Cu
#define HEALTH_MAX  0x140u
#define INV_BITS    0x01F7A7E0u
#define INV_COUNT   0x01F7A7ECu
#define COUNT_MAX   999

static const struct { uint8_t id, ammo, magazine; } guns[] = {
    {4, 5, 10},  /* handgun, handgun bullets */
    {6, 7, 6},   /* shotgun, shotgun shells */
    {8, 9, 4},   /* hunting rifle, rifle rounds */
};
static const uint8_t healing[] = {1, 2, 3};  /* health drink, first aid kit, ampoule */

static int cheat_on;

static void give(unsigned id, unsigned count) {
    MEM32(INV_BITS + (id >> 5) * 4) |= 1u << (id & 31);
    if (count && MEM16(INV_COUNT + 2 * id) < count) MEM16(INV_COUNT + 2 * id) = count;
}

/* A guest pointer is only followed when it lies in the guest's memory. */
static int guest_ok(uint32_t va, uint32_t len) {
    return va >= 0x10000u && (uint64_t)va + len <= host_arena_bytes;
}

void cheat_set(int on) {
    if (on && !cheat_on) rt_log("cheat: on");
    if (!on && cheat_on) rt_log("cheat: off (the items stay)");
    cheat_on = on;
}
int cheat_get(void) { return cheat_on; }

/* Game thread, once per presented frame (d3d8.c), between the game's own frames. */
void cheat_tick(void) {
    if (!cheat_on) return;
    uint32_t james = MEM32(PLAYER);
    if (guest_ok(james, HEALTH_MAX + 4)) {
        float max = MEMF(james + HEALTH_MAX);
        if (max > 0 && MEMF(james + HEALTH) < max) MEMF(james + HEALTH) = max;
    }
    for (unsigned i = 0; i < sizeof guns / sizeof *guns; i++) {
        give(guns[i].id, guns[i].magazine);  /* a full magazine, always: it never runs dry */
        give(guns[i].ammo, COUNT_MAX);
    }
    for (unsigned i = 0; i < sizeof healing; i++) give(healing[i], 10);
}
