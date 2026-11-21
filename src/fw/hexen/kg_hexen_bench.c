/* Ф0 bench (karabas): the measurements of Hexen in the ISS, called from I_StartTic of htic/kg_htic.c.
   - zone: every 35 tics the zone in use (Z_ZoneSize - Z_FreeMemory: the purgable blocks count as free),
     its peak printed at the exit (also at the I_Error of -timedemo);
   - -kgbench (with -warp 1 -skill N, no demo): a scenario of the saves of a hub without input:
     save in slot 0, teleport to maps 2 and 3 of hub 1, save in slot 1, back to map 1, save in slot 1
     again, then the list of the RAM files (hexndata/hex*.hxs) and their sizes, and the quit;
   - -kgspin (with -warp N): turn on the spot (the arrow key held) for 300 tics, then quit: the views of a
     map start for the slow frames. */
#include <stdio.h>
#include "doomtype.h"
#include "d_loop.h"
#include "i_system.h"
#include "m_argv.h"
#include "z_zone.h"
#include "d_event.h"
#include "doomkeys.h"

void G_SaveGame(int slot, char *description);
void G_TeleportNewMap(int map, int position);
void kg_ramfiles_list(void);

static int zone_peak, zone_peak_tic, last_tic = -1, bench, spin;

static void kg_hexen_bench_exit(void) {
    printf("ZONE peak %d of %u bytes at tic %d\n", zone_peak, Z_ZoneSize(), zone_peak_tic);
    if (bench) kg_ramfiles_list();
}

static const struct { int tic, op, arg; } steps[] = {
    /* op 0: save in slot arg, 1: teleport to map arg, 2: quit */
    { 70, 0, 0 }, { 140, 1, 2 }, { 280, 1, 3 }, { 420, 0, 1 }, { 490, 1, 1 }, { 630, 0, 1 }, { 700, 2, 0 },
};
static int step;

void kg_bench_tic(void) {
    if (gametic == last_tic) return;
    if (last_tic < 0) {
        bench = M_ParmExists("-kgbench");
        /* -kgspin (with -warp N): turn left on the spot for 4 turns (~8 s of game), then quit */
        spin = M_ParmExists("-kgspin");
        if (spin) { event_t ev = { ev_keydown, KEY_LEFTARROW, 0, 0, 0 }; D_PostEvent(&ev); }
        I_AtExit(kg_hexen_bench_exit, true);
    }
    /* several tics can run in one loop: the zone every 35 tics, the steps at the first tic >= theirs */
    if (gametic / 35 != last_tic / 35) {
        int used = (int) Z_ZoneSize() - Z_FreeMemory();
        if (used > zone_peak) { zone_peak = used; zone_peak_tic = gametic; }
    }
    last_tic = gametic;
    if (spin && gametic >= 300) I_Quit();
    if (!bench || step >= (int) (sizeof steps / sizeof steps[0]) || gametic < steps[step].tic) return;
    switch (steps[step].op) {
    case 0: G_SaveGame(steps[step].arg, "BENCH"); break;
    case 1: G_TeleportNewMap(steps[step].arg, 0); break;
    default: I_Quit();
    }
    step++;
}
