/* Hexen Ф2 (karabas): the board build only. kg_save_notice (called by I_StartTic of htic/kg_htic_hw.c):
   when a file of a savegame did not fit the save file (kg_save_fs.c), the message replaces the
   GAME SAVED of the save. */
#include "h2def.h"
#include "p_local.h"

extern int kg_save_full;

void kg_save_notice(void)
{
    if (kg_save_full && gamestate == GS_LEVEL)
    {
        kg_save_full = 0;
        P_SetMessage(&players[consoleplayer], "SAVE FILE FULL: NOT KEPT", true);
    }
}
