/* Ф2 (karabas): the sound effects of Doom on the board: DG_sound_module of doomgeneric i_sound.c.
 * The DMX sound lumps of the WAD (mapped in memory: no copies) are mixed by the timer interrupt
 * (kg_sfx_tick, every 5 ms) into the ring of doom_audio.v (SFX_RING entries of { right, left },
 * played at 11025 Hz), LEAD samples ahead of the play position: ~40 ms of latency whatever the frame
 * rate. A lump of another rate is stepped (no filter, as the SB of DOS). Volume / separation as
 * chocolate-doom i_sdlsound.c: left = (254 - sep) * vol / 127, right = sep * vol / 127.
 * KG_HERETIC (Ф1 of core-heretic, the i_sound.h of chocolate-doom 2.3.0): the module is
 * sound_sdl_module (the first of i_sound.c's list), StartSound has the pitch of Heretic: with
 * snd_pitchshift the sound is stepped by 1 / (2 - pitch / 127), the length ratio of i_sdlsound.c. */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "doomtype.h"
#include "i_sound.h"
#include "w_wad.h"
#include "z_zone.h"
#include "mmio.h"
#include "kg_irq.h"

#define NUM_CH 16
#define LEAD   441                    /* 40 ms at 11025 Hz */

typedef struct {
    const uint8_t *data;              /* NULL: free */
    uint32_t len;                     /* samples */
    uint32_t pos;                     /* 16.16 */
    uint32_t step;                    /* 16.16 */
    int left, right;                  /* 0..254 */
} channel_t;

/* the settings of i_sound.c (libsamplerate is in the SDL module only) */
int use_libsamplerate = 0;
float libsamplerate_scale = 0.65f;

static channel_t ch[NUM_CH];
static boolean use_prefix;
static unsigned int wr;
static int started;

void kg_sfx_tick(void) {
    unsigned int pos = MMIO_SFX_POS & (SFX_RING - 1);
    if (!started) { wr = (pos + 8) & (SFX_RING - 1); started = 1; }
    /* behind the play position (a long stall): start again just ahead of it */
    unsigned int ahead = (wr - pos) & (SFX_RING - 1);
    if (ahead > LEAD + 64) wr = (pos + 8) & (SFX_RING - 1), ahead = 8;
    while (ahead < LEAD) {
        int l = 0, r = 0;
        for (int i = 0; i < NUM_CH; i++) {
            channel_t *c = &ch[i];
            if (!c->data) continue;
            uint32_t n = c->pos >> 16;
            if (n >= c->len) { c->data = NULL; continue; }
            int s = (int) c->data[n] - 128;
            l += s * c->left;
            r += s * c->right;
            c->pos += c->step;
        }
        l >>= 1; r >>= 1;               /* one sound at full volume: +-16k */
        if (l > 32767) l = 32767; else if (l < -32768) l = -32768;
        if (r > 32767) r = 32767; else if (r < -32768) r = -32768;
        MMIO_SFX(wr) = ((uint32_t) (uint16_t) r << 16) | (uint16_t) l;
        wr = (wr + 1) & (SFX_RING - 1);
        ahead++;
    }
}

static snddevice_t devices[] = { SNDDEVICE_SB, SNDDEVICE_PAS, SNDDEVICE_GUS, SNDDEVICE_WAVEBLASTER,
                                 SNDDEVICE_SOUNDCANVAS, SNDDEVICE_AWE32 };

static boolean Kg_Init(boolean prefix) {
    use_prefix = prefix;
    memset(ch, 0, sizeof ch);
    return true;
}

static void Kg_Shutdown(void) {
    kg_lock();
    memset(ch, 0, sizeof ch);
    kg_unlock();
}

static int Kg_GetSfxLumpNum(sfxinfo_t *sfx) {
    char name[9];
    if (sfx->link) sfx = sfx->link;          /* as i_sdlsound.c */
    if (use_prefix) snprintf(name, sizeof name, "ds%s", sfx->name);
    else snprintf(name, sizeof name, "%s", sfx->name);
    return W_GetNumForName(name);
}

static void Kg_Update(void) {}

static void set_params(channel_t *c, int vol, int sep) {
    if (vol < 0) vol = 0;
    if (vol > 127) vol = 127;
    if (sep < 0) sep = 0;
    if (sep > 254) sep = 254;
    c->left = ((254 - sep) * vol) / 127;
    c->right = (sep * vol) / 127;
}

static void Kg_UpdateSoundParams(int channel, int vol, int sep) {
    if (channel < 0 || channel >= NUM_CH) return;
    kg_lock();
    set_params(&ch[channel], vol, sep);
    kg_unlock();
}

#ifdef KG_HERETIC
#define NORM_PITCH 127
static int Kg_StartSound(sfxinfo_t *sfx, int channel, int vol, int sep, int pitch) {
#else
static int Kg_StartSound(sfxinfo_t *sfx, int channel, int vol, int sep) {
#endif
    if (channel < 0 || channel >= NUM_CH) return -1;
    int lump = sfx->lumpnum;
    const uint8_t *d = W_CacheLumpNum(lump, PU_STATIC);
    unsigned int lumplen = W_LumpLength(lump);
    /* DMX: format 3, rate, length (with 16 pad bytes at each end), the 8-bit unsigned samples */
    if (lumplen < 8 || d[0] != 0x03 || d[1] != 0x00) return -1;
    uint32_t rate = d[2] | (d[3] << 8);
    uint32_t len = d[4] | (d[5] << 8) | (d[6] << 16) | ((uint32_t) d[7] << 24);
    if (len > lumplen - 8 || len <= 48 || rate == 0) return -1;
    kg_lock();
    channel_t *c = &ch[channel];
    c->data = d + 8 + 16;
    c->len = len - 32;
    c->pos = 0;
    c->step = (uint32_t) (((uint64_t) rate << 16) / SFX_RATE);
#ifdef KG_HERETIC
    if (snd_pitchshift > 0 && pitch > 0 && pitch < 2 * NORM_PITCH)
        c->step = (uint32_t) (((uint64_t) rate << 16) * NORM_PITCH / ((uint64_t) SFX_RATE * (2 * NORM_PITCH - pitch)));
#endif
    set_params(c, vol, sep);
    kg_unlock();
    return channel;
}

static void Kg_StopSound(int channel) {
    if (channel < 0 || channel >= NUM_CH) return;
    kg_lock();
    ch[channel].data = NULL;
    kg_unlock();
}

static boolean Kg_SoundIsPlaying(int channel) {
    if (channel < 0 || channel >= NUM_CH) return false;
    return ch[channel].data != NULL;
}

static void Kg_CacheSounds(sfxinfo_t *sounds, int num_sounds) { (void) sounds; (void) num_sounds; }

#ifdef KG_HERETIC
sound_module_t sound_sdl_module = {
#else
sound_module_t DG_sound_module = {
#endif
    devices,
    sizeof devices / sizeof devices[0],
    Kg_Init,
    Kg_Shutdown,
    Kg_GetSfxLumpNum,
    Kg_Update,
    Kg_UpdateSoundParams,
    Kg_StartSound,
    Kg_StopSound,
    Kg_SoundIsPlaying,
    Kg_CacheSounds,
};
