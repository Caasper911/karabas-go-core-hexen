/* Ф0 bench (karabas): the platform of Heretic (chocolate-doom 2.3.0 src/heretic) for the ISS:
   main (as src/i_main.c), the timer (src/i_timer.c without SDL), the video (src/i_video.c without
   SDL: Heretic draws straight into the SRAM frame FB_BASE, I_FinishUpdate marks the frame in MMIO,
   the palette goes to MMIO_PAL), no input, no joystick, no ENDOOM. Args: ARGS from the Makefile.
   Ф1: with KG_HW (the board build) main, I_StartTic and the sound stubs are in kg_htic_hw.c; the
   timer, the video and the other stubs stay here. */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "config.h"
#include "doomtype.h"
#include "d_event.h"
#include "i_joystick.h"
#include "i_system.h"
#include "i_timer.h"
#include "i_video.h"
#include "m_argv.h"
#include "m_misc.h"
#include "tables.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"
#include "deh_str.h"
#include "mmio.h"

void D_DoomMain(void);

/* heretic/ct_chat.c leaves chat_macros[] NULL (only a config sets them); M_SaveDefaults prints
   them with %s: "(null)" with glibc, but newlib-nano reads from address 0 up to a zero byte (the
   heretic.cfg of the first quit was 1 MB). Empty strings instead. */
extern char *chat_macros[10];
void kg_htic_chat_macros(void) {
    for (int i = 0; i < 10; i++) if (!chat_macros[i]) chat_macros[i] = "";
}

/* the game of the build: heretic, or hexen (the Makefile target hexen sets -DKG_GAME) */
#ifndef KG_GAME
#define KG_GAME "heretic"
#endif

#ifndef KG_HW
/* ---- main (src/i_main.c) ---- */
int main(void) {
    /* one writable string, split in place at the spaces */
    static char args[] = KG_GAME " -iwad " KG_GAME ".wad -mb " MB " " ARGS;
    static char *av[32];
    int n = 0;
    char *s = args;
    while (*s && n < 31) {
        while (*s == ' ') *s++ = 0;
        if (!*s) break;
        av[n++] = s;
        while (*s && *s != ' ') s++;
    }
    av[n] = 0;
    myargc = n;
    myargv = av;
    M_FindResponseFile();
    kg_htic_chat_macros();
    D_DoomMain();
    return 0;
}
/* the bench scenario of a game (hexen/kg_hexen_bench.c), called every loop of the tic code */
void kg_bench_tic(void) __attribute__((weak));
void I_StartTic(void) { if (kg_bench_tic) kg_bench_tic(); }
#else
void kg_save_show(void);
#endif

/* ---- timer (src/i_timer.c) ---- */
static uint32_t basetime;
int I_GetTime(void) { return (int) ((MMIO_MS - basetime) * TICRATE / 1000u); }
int I_GetTimeMS(void) { return (int) (MMIO_MS - basetime); }
void I_Sleep(int ms) { uint32_t t = MMIO_MS; while (MMIO_MS - t < (uint32_t) ms) {} }
void I_WaitVBL(int count) { I_Sleep((count * 1000) / 70); }
void I_InitTimer(void) { basetime = MMIO_MS; }

/* ---- video (src/i_video.c) ---- */
char *video_driver = "";
boolean screenvisible = true;
float mouse_acceleration = 2.0;
int mouse_threshold = 10;
int vanilla_keyboard_mapping = 1;
boolean screensaver_mode = false;
int usegamma = 0;
byte *I_VideoBuffer = NULL;
int screen_width = SCREENWIDTH, screen_height = SCREENHEIGHT, screen_bpp = 8;
int fullscreen = 1, aspect_ratio_correct = 1;
static uint32_t frame;
static byte cur_pal[256 * 3];

#ifdef KG_HWDRAW
/* Ф1в: the drawer of the FPGA (hexen/r_draw.c): present when STAT has its id; then the frame is
   accessed uncached (the drawer writes it behind the D$). kg_drw_newframe: set per frame here, the
   drawer path checks the colormaps table once per frame. */
int kg_hwdraw, kg_drw_newframe = 1;
#endif
void I_InitGraphics(void) {
    I_VideoBuffer = (byte *) FB_BASE;
#ifdef KG_HWDRAW
    kg_hwdraw = (MMIO_DRW_STAT >> 24) == DRW_ID && !M_ParmExists("-nohwdraw");
    if (kg_hwdraw) I_VideoBuffer = (byte *) (SRAM_UC + FB_BASE);
    printf("I_InitGraphics: drawer %s\n", kg_hwdraw ? "on" : "off");
#endif
    V_RestoreBuffer(); /* V_DrawPatch draws into I_VideoBuffer from now on */
    memset(I_VideoBuffer, 0, SCREENWIDTH * SCREENHEIGHT);
    screenvisible = true;
    I_SetPalette(W_CacheLumpName(DEH_String("PLAYPAL"), PU_CACHE)); /* as src/i_video.c */
}
void I_GraphicsCheckCommandLine(void) {}
void I_ShutdownGraphics(void) {}
void I_SetPalette(byte *doompalette) {
    for (int i = 0; i < 256; ++i) {
        /* VGA: 6 bits per channel */
        byte r = gammatable[usegamma][*doompalette++] & ~3;
        byte g = gammatable[usegamma][*doompalette++] & ~3;
        byte b = gammatable[usegamma][*doompalette++] & ~3;
        cur_pal[3 * i] = r; cur_pal[3 * i + 1] = g; cur_pal[3 * i + 2] = b;
        MMIO_PAL(i) = ((uint32_t) r << 16) | ((uint32_t) g << 8) | b;
    }
}
int I_GetPaletteIndex(int r, int g, int b) {
    int best = 0, best_diff = 0x7fffffff;
    for (int i = 0; i < 256; ++i) {
        int dr = r - cur_pal[3 * i], dg = g - cur_pal[3 * i + 1], db = b - cur_pal[3 * i + 2];
        int diff = dr * dr + dg * dg + db * db;
        if (diff < best_diff) { best = i; best_diff = diff; }
        if (diff == 0) break;
    }
    return best;
}
void I_UpdateNoBlit(void) {}
#ifdef KG_HW
void kg_fps_overlay(byte *fb);           /* kg_htic_hw.c: the OSD item FPS */
#endif
void I_FinishUpdate(void) {
#ifdef KG_HW
    kg_fps_overlay(I_VideoBuffer);
#endif
    MMIO_FRAME = frame++;
#ifdef KG_HWDRAW
    kg_drw_newframe = 1;
#endif
#ifdef KG_HW
    if ((frame & 63) == 0) kg_save_show();   /* the state of the saves in DEBUG_ADDR */
#endif
}
void I_ReadScreen(byte *scr) { memcpy(scr, I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT); }
void I_BeginRead(void) {}
void I_SetWindowTitle(char *title) { (void) title; }
void I_CheckIsScreensaver(void) {}
void I_SetGrabMouseCallback(grabmouse_callback_t func) { (void) func; }
void I_DisplayFPSDots(boolean dots_on) { (void) dots_on; }
void I_BindVideoVariables(void) {}
void I_InitWindowTitle(void) {}
void I_InitWindowIcon(void) {}
void I_StartFrame(void) {}
void I_EnableLoadingDisk(int xoffs, int yoffs) { (void) xoffs; (void) yoffs; }

/* ---- joystick (src/i_joystick.c), ENDOOM (src/i_endoom.c) ---- */
void I_InitJoystick(void) {}
void I_ShutdownJoystick(void) {}
void I_UpdateJoystick(void) {}
void I_BindJoystickVariables(void) {}
void I_Endoom(byte *data) { (void) data; }

/* ---- stubs: src/d_loop.c and heretic/d_net.c call the net client / server and dehacked even
   without FEATURE_MULTIPLAYER (never reached: not connected); src/i_sound.c binds the variables of
   the sound modules; F_DrawUnderwater resets the scale tables of src/i_video.c; W_ReadLump calls
   the disk icon of src/v_diskicon.c ---- */
#include "deh_main.h"
#include "gusconf.h"
#include "i_scale.h"
#include "i_sound.h"
#include "net_client.h"
#include "net_server.h"
#include "v_diskicon.h"
boolean net_client_connected = false;
boolean drone = false;
net_waitdata_t net_client_wait_data;
void NET_CL_Run(void) {}
void NET_CL_StartGame(net_gamesettings_t *settings) { (void) settings; }
boolean NET_CL_GetSettings(net_gamesettings_t *settings) { (void) settings; return false; }
void NET_SV_Run(void) {}
__attribute__((weak)) void DEH_Checksum(sha1_digest_t digest) { memset(digest, 0, sizeof(sha1_digest_t)); }
char *timidity_cfg_path = "";
char *gus_patch_path = "";
int gus_ram_kb = 1024;
void I_InitTimidityConfig(void) {}
#ifndef KG_HW
char *snd_dmxoption = "";                   /* the board build: src/i_oplmusic.c */
int opl_io_port = 0x388;
void I_SetOPLDriverVer(opl_driver_ver_t ver) { (void) ver; }
int usemouse = 0;
#else
int usemouse = 1;
#endif
void I_ResetScaleTables(byte *palette) { (void) palette; }
void V_BeginRead(size_t nbytes) { (void) nbytes; }
