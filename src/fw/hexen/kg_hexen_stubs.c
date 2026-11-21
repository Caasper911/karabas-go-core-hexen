/* karabas: the parts of chocolate-doom 2.3.0 that Hexen calls and the board has not (from the survey
   build of 2026-11-21, docs/PLAN.md):
   - the 640x480 planar startup screen of src/i_videohr.c: I_SetVideoModeHR fails, so hexen/st_start.c
     takes the text startup;
   - the CD music of src/i_cdmus.c (MAPINFO cd tracks): no CD drive, I_CDMusInit fails. */
#include "doomtype.h"
#include "i_cdmus.h"
#include "i_videohr.h"

boolean I_SetVideoModeHR(void) { return false; }
void I_UnsetVideoModeHR(void) {}
void I_SetWindowTitleHR(char *title) { (void) title; }
void I_ClearScreenHR(void) {}
void I_SlamBlockHR(int x, int y, int w, int h, const byte *src) { (void) x; (void) y; (void) w; (void) h; (void) src; }
void I_SlamHR(const byte *buffer) { (void) buffer; }
void I_InitPaletteHR(void) {}
void I_SetPaletteHR(const byte *palette) { (void) palette; }
void I_FadeToPaletteHR(const byte *palette) { (void) palette; }
void I_BlackPaletteHR(void) {}
boolean I_CheckAbortHR(void) { return false; }

int I_CDMusInit(void) { return -1; }
void I_CDMusPrintStartup(void) {}
int I_CDMusPlay(int track) { (void) track; return -1; }
int I_CDMusStop(void) { return 0; }
int I_CDMusResume(void) { return 0; }
int I_CDMusSetVolume(int volume) { (void) volume; return 0; }
int I_CDMusFirstTrack(void) { return 0; }
int I_CDMusLastTrack(void) { return 0; }
int I_CDMusTrackLength(int track) { (void) track; return 0; }
