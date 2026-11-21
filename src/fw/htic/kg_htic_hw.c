/* Ф1 (karabas): the platform of Heretic (and of Hexen: KG_HEXEN, KG_GAME "hexen") on the board (src/rtl/doom_soc.v MMIO), with kg_htic.c
 * (timer, video). As kg_hw.c of Doom (doomgeneric), for chocolate-doom 2.3.0:
 * - Waits for the WAD (the FL item "WAD", MMIO_WADSIZE != 0) with a test picture and the log, then
 *   starts Heretic (D_DoomMain).
 * - I_StartTic: the USB boot report of kg_mcu2 (KBD0 / KBD1, KBDN counts reports) compared with the
 *   last one -> key events (USB usage -> doomkeys.h, data2 = the typed character for the savegame
 *   names); the sums of MMIO_MOUSE -> ev_mouse; pads (JOY, both ports): arrows, A fire, B use,
 *   C run, Start Enter (use the artifact / the menu), X Esc, Y / Z inventory right / left,
 *   Mode next weapon. The keys of Heretic stay its defaults ([ ] Enter inventory, PgUp / Ins /
 *   Home fly, PgDn / Del / End look). Hexen: the pad C is jump (key_jump, the keyboard / ), no run
 *   on the pad (Shift on the keyboard).
 * - OSD Controls (MMIO_SW 23..16): 0 Classic (arrows, mouse: left fire, right strafe, middle
 *   forward), 1 WASD (W / S forward / back, A / D strafe, E use outside the menu; mouse: left
 *   fire, right use). OSD Mouse move (31..24): 0 off (the mouse only turns), 1 on.
 * - The console (printf) goes to MMIO_UART and to a log of the last lines; at exit (I_Error, quit)
 *   the log is drawn on the screen in the 8x8 font (font8.h) with a grey palette, and the CPU waits.
 * - Sound: i_sound.c of chocolate takes the first module of each list that has the device of the
 *   config (SB): sound_sdl_module = i_kgsound.c, music_sdl_module = i_kgmusic.c; the PC speaker
 *   module is empty. */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "doomtype.h"
#include "doomkeys.h"
#include "d_event.h"
#include "i_sound.h"
#include "m_argv.h"
#include "m_controls.h"
#include "m_misc.h"
#include "mmio.h"
#include "font8.h"
#include "kg_irq.h"
#include "kg_save.h"

#define MMIO_KBD0  MMIO_REG(0x28)   /* key3 key2 key1 modifiers */
#define MMIO_KBD1  MMIO_REG(0x2c)   /* 0 key6 key5 key4 */
#define MMIO_KBDN  MMIO_REG(0x30)
#define MMIO_MOUSE MMIO_REG(0x34)   /* buttons[31:24], dy[23:12], dx[11:0] */
#define MMIO_JOY   MMIO_REG(0x38)

/* key codes of no keyboard key: the weapon cycle of the pad (key_nextweapon / key_prevweapon) */
#define KG_KEY_NEXTWEAPON 0xfd
#define KG_KEY_PREVWEAPON 0xfe

extern boolean MenuActive;            /* heretic/mn_menu.c, hexen/mn_menu.c */
#ifndef KG_GAME
#define KG_GAME "heretic"
#endif
#ifdef KG_HEXEN
#define KG_TITLE "Hexen"
#define KG_PAD_C key_jump
#else
#define KG_TITLE "Heretic"
#define KG_PAD_C key_speed
#endif
void D_DoomMain(void);
void kg_htic_chat_macros(void);           /* kg_htic.c */

/* ------------------------------------------------------------------ the log */

#define LOG_LINES 25
#define LOG_COLS 40
static char logbuf[LOG_LINES][LOG_COLS + 1];
static int log_line, log_col;

void kg_log_putc(char c) {
    if (c == '\n' || log_col == LOG_COLS) {
        log_line = (log_line + 1) % LOG_LINES;
        memset(logbuf[log_line], 0, LOG_COLS + 1);
        log_col = 0;
        if (c == '\n') return;
    }
    if (c >= 32 && c < 127) logbuf[log_line][log_col++] = c;
}

static void draw_char(uint8_t *fb, int x, int y, char c, uint8_t col) {
    const unsigned char *g = font8[(c >= 32 && c < 127) ? c - 32 : 0];
    for (int r = 0; r < 8; r++)
        for (int b = 0; b < 8; b++)
            if (g[r] & (0x80 >> b)) fb[(y + r) * 320 + x + b] = col;
}

static void draw_log(uint8_t *fb, uint8_t col) {
    for (int i = 0; i < LOG_LINES; i++) {
        const char *s = logbuf[(log_line + 1 + i) % LOG_LINES];
        for (int k = 0; s[k]; k++) draw_char(fb, k * 8, i * 8, s[k], col);
    }
}

/* the OSD item FPS (MMIO_SW 7..0 = 1): the frames of the last second in the top right corner of the
   frame being finished (I_FinishUpdate, before the copy to the shown buffer); white on black, the
   indices of the current palette. The game redraws the view each frame; a border (a smaller view)
   may keep the digits until it is redrawn. */
int I_GetPaletteIndex(int r, int g, int b);
void kg_fps_overlay(uint8_t *fb) {
    static uint32_t t0, n;
    static int fps;
    uint32_t now = MMIO_MS;
    n++;
    if (now - t0 >= 1000) { fps = (int) ((n * 1000 + (now - t0) / 2) / (now - t0)); n = 0; t0 = now; }
    if ((MMIO_SW & 0xff) != 1 || !fb) return;
    uint8_t black = (uint8_t) I_GetPaletteIndex(0, 0, 0), white = (uint8_t) I_GetPaletteIndex(255, 255, 255);
    char s[3] = { fps > 99 ? '9' : '0' + fps / 10 % 10, fps > 99 ? '9' : '0' + fps % 10, 0 };
    int x0 = 320 - 2 * 8 - 3, y0 = 1;
    for (int y = y0 - 1; y < y0 + 9; y++) memset(fb + y * 320 + x0 - 1, black, 2 * 8 + 2);
    for (int i = 0; i < 2; i++) draw_char(fb, x0 + 8 * i, y0, s[i], white);
}

/* the end: the log on the screen (called by _exit) */
void kg_exit_screen(int code) {
    uint8_t *fb = (uint8_t *)FB_BASE;
    for (int i = 0; i < 256; i++) MMIO_PAL(i) = (i << 16) | (i << 8) | i;
    memset(fb, code ? 0x30 : 0x00, 64000);
    draw_log(fb, 0xff);
    MMIO_FRAME = 0;
}

/* ------------------------------------------------------------------ keys */

static int usb_to_key(uint8_t u) {
    if (u >= 0x04 && u <= 0x1d) return 'a' + (u - 0x04);
    if (u >= 0x1e && u <= 0x26) return '1' + (u - 0x1e);
    if (u >= 0x3a && u <= 0x43) return KEY_F1 + (u - 0x3a);
    switch (u) {
    case 0x27: return '0';
    case 0x28: case 0x58: return KEY_ENTER;
    case 0x29: return KEY_ESCAPE;
    case 0x2a: return KEY_BACKSPACE;
    case 0x2b: return KEY_TAB;
    case 0x2c: return ' ';
    case 0x2d: return KEY_MINUS;
    case 0x2e: return KEY_EQUALS;
    case 0x2f: return '[';
    case 0x30: return ']';
    case 0x31: return '\\';
    case 0x33: return ';';
    case 0x34: return '\'';
    case 0x35: return '`';
    case 0x36: return ',';
    case 0x37: return '.';
    case 0x38: return '/';
    case 0x39: return KEY_CAPSLOCK;
    case 0x44: return KEY_F11;
    case 0x45: return KEY_F12;
    case 0x48: return KEY_PAUSE;
    case 0x49: return KEY_INS;
    case 0x4a: return KEY_HOME;
    case 0x4b: return KEY_PGUP;
    case 0x4c: return KEY_DEL;
    case 0x4d: return KEY_END;
    case 0x4e: return KEY_PGDN;
    case 0x4f: return KEY_RIGHTARROW;
    case 0x50: return KEY_LEFTARROW;
    case 0x51: return KEY_DOWNARROW;
    case 0x52: return KEY_UPARROW;
    case 0x56: return KEY_MINUS;     /* keypad - / + : the view size */
    case 0x57: return KEY_EQUALS;
    }
    return 0;
}

/* the modifiers: ctrl fire, shift run, alt strafe (the keys of chocolate's defaults) */
static const int mod_key[8] = { KEY_RCTRL, KEY_RSHIFT, KEY_RALT, 0, KEY_RCTRL, KEY_RSHIFT, KEY_RALT, 0 };

static void post_key(int pressed, int k, int typed) {
    if (!k) return;
    event_t ev;
    ev.type = pressed ? ev_keydown : ev_keyup;
    ev.data1 = k;
    ev.data2 = pressed ? typed : 0;
    ev.data3 = ev.data4 = 0;
    D_PostEvent(&ev);
}

static uint8_t last_rep[7];
static uint32_t last_kbdn;
static uint32_t last_joy;

static int held_as[256];
static int wasd, mouse_move;

/* the OSD items Controls / Mouse move, applied when they change (and once after the config) */
static void apply_controls(void) {
    static int last = -1;
    int sw = (MMIO_SW >> 16) & 0xffff;
    if (sw == last) return;
    last = sw;
    wasd = (sw & 0xff) == 1;
    mouse_move = (sw >> 8) == 1;
    key_nextweapon = KG_KEY_NEXTWEAPON;
    key_prevweapon = KG_KEY_PREVWEAPON;
    mousebfire = 0;
    mousebstrafe = wasd ? -1 : 1;
    mousebforward = wasd ? -1 : 2;
    mousebuse = wasd ? 1 : -1;
}

/* the key of a USB usage: WASD in the game (not in the menu: the names of the savegames) */
static int key_of(uint8_t u) {
    if (wasd && !MenuActive) switch (u) {
    case 0x1a: return KEY_UPARROW;     /* W */
    case 0x16: return KEY_DOWNARROW;   /* S */
    case 0x04: return key_strafeleft;  /* A */
    case 0x07: return key_straferight; /* D */
    case 0x08: return key_use;         /* E */
    }
    return usb_to_key(u);
}

/* the character typed by a key (the savegame names, the cheats), shift: upper case */
static int typed_of(uint8_t u, int shift) {
    int k = usb_to_key(u);
    if (k >= 'a' && k <= 'z') return shift ? k - 'a' + 'A' : k;
    if (k >= 32 && k < 127) return k;
    return 0;
}

static int has(const uint8_t *r, uint8_t u) {
    for (int i = 1; i < 7; i++) if (r[i] == u) return 1;
    return 0;
}

void I_StartTic(void) {
    uint32_t n = MMIO_KBDN;
    if (n != last_kbdn) {
        last_kbdn = n;
        uint32_t k0 = MMIO_KBD0, k1 = MMIO_KBD1;
        uint8_t rep[7] = { k0 & 0xff, (k0 >> 8) & 0xff, (k0 >> 16) & 0xff, k0 >> 24, k1 & 0xff, (k1 >> 8) & 0xff, (k1 >> 16) & 0xff };
        for (int b = 0; b < 8; b++) {
            int was = (last_rep[0] >> b) & 1, now = (rep[0] >> b) & 1;
            if (was != now) post_key(now, mod_key[b], 0);
        }
        int shift = (rep[0] & 0x22) != 0;
        for (int i = 1; i < 7; i++) {
            /* a key is released as the key it was pressed as (WASD: the menu may open between) */
            if (last_rep[i] > 3 && !has(rep, last_rep[i])) post_key(0, held_as[last_rep[i]], 0);
            if (rep[i] > 3 && !has(last_rep, rep[i])) post_key(1, held_as[rep[i]] = key_of(rep[i]), typed_of(rep[i], shift));
        }
        memcpy(last_rep, rep, 7);
    }
    uint32_t jr = MMIO_JOY, j = (jr | (jr >> 16)) & 0xffff;   /* both pads */
    if (j != last_joy) {
        /* bits: 1 up, 2 down, 3 left, 4 right, 5 Start, 6 A, 7 B, 8 C, 9 X, 10 Y, 11 Z, 12 Mode */
        const int jk[13] = { 0, KEY_UPARROW, KEY_DOWNARROW, KEY_LEFTARROW, KEY_RIGHTARROW,
                             KEY_ENTER, key_fire, key_use, KG_PAD_C, KEY_ESCAPE,
                             key_invright, key_invleft, KG_KEY_NEXTWEAPON };
        for (int b = 1; b < 13; b++)
            if (((j ^ last_joy) >> b) & 1) post_key((j >> b) & 1, jk[b], b == 5 ? KEY_ENTER : 0);
        last_joy = j;
    }
    apply_controls();
    uint32_t m = MMIO_MOUSE;
    int dx = (int32_t)(m << 20) >> 20, dy = (int32_t)(m << 8) >> 20, mb = m >> 24;
    static int last_mb;
    if (dx || dy || mb != last_mb) {
        event_t ev;
        ev.type = ev_mouse;
        ev.data1 = mb & 7;
        ev.data2 = dx * 4;
        ev.data3 = mouse_move ? -dy * 4 : 0;
        ev.data4 = 0;
        D_PostEvent(&ev);
        last_mb = mb;
    }
}

/* ------------------------------------------------------------------ sound modules */

static snddevice_t no_devices[1];
sound_module_t sound_pcsound_module = { no_devices, 0 };

/* ------------------------------------------------------------------ start */

static void wait_wad(void) {
    uint8_t *fb = (uint8_t *)FB_BASE;
    for (int i = 0; i < 256; i++) MMIO_PAL(i) = ((i & 0xe0) << 16) | ((i & 0x1c) << 11) | ((i & 0x03) << 6);
    for (int y = 0; y < 200; y++)
        for (int x = 0; x < 320; x++) fb[y * 320 + x] = (uint8_t)((y / 25) * 32 + x / 10);
    printf("Karabas " KG_TITLE ": load a WAD (Menu+F1)\n");
    for (int i = 0; i < 4; i++) draw_char(fb, 8 * i, 192, "WAD?"[i], 0xff);
    draw_log(fb, 0xff);
    MMIO_FRAME = 0;
    while (MMIO_WADSIZE == 0) {}
}

int main(void) {
#ifndef HWARGS
#define HWARGS ""
#endif
    static char args[] = KG_GAME " -iwad " KG_GAME ".wad -mb 8 " HWARGS;
    static char *av[16];
    int n = 0;
    char *s = args;
    while (*s && n < 15) {
        while (*s == ' ') *s++ = 0;
        if (!*s) break;
        av[n++] = s;
        while (*s && *s != ' ') s++;
    }
    av[n] = 0;
    myargc = n;
    myargv = av;
    kg_irq_init();
    wait_wad();
    kg_save_init();
    kg_save_show();
    kg_htic_chat_macros();
    D_DoomMain();
    return 0;
}
