/* Ф2 (karabas): the music of Doom on the board: DG_music_module of doomgeneric i_sound.c.
 * The OSD item Music (MMIO_SW bits 15..8) picks the player at each song (and when it changes):
 *   0 OPL3: chocolate-doom 2.3.0 i_oplmusic.c (GENMIDI instruments of the WAD) on the OPL3 of
 *     doom_audio.v (opl_kg.c);
 *   1 MIDI: the song as MIDI bytes to the SAM2695 of the board (MMIO_MIDI), timed by the timer
 *     interrupt (kg_midi_run); the music volume scales the volume controller (7) of each channel;
 *   2 off.
 * Both take the song from i_oplmusic's RegisterSong (MUS -> MIDI by mus2mid, midifile.c).
 * KG_HERETIC (core-heretic): the module is music_sdl_module (the first of i_sound.c's list). */
#include <stdint.h>
#include <string.h>
#include "doomtype.h"
#include "i_sound.h"
#include "midifile.h"
#include "mmio.h"
#include "kg_irq.h"

extern music_module_t music_opl_module;

enum { MODE_OPL = 0, MODE_MIDI = 1, MODE_OFF = 2 };

static int mode_of_osd(void) {
    int m = (MMIO_SW >> 8) & 0xff;
    return m > MODE_OFF ? MODE_OFF : m;
}

/* ------------------------------------------------------------------ MIDI sequencer */

#define MAX_TRACKS 16
static midi_file_t *m_file;
static midi_track_iter_t *m_iter[MAX_TRACKS];
static uint64_t m_next[MAX_TRACKS];        /* absolute microseconds of the next event, KG_NEVER: ended */
static int m_tracks, m_playing, m_paused, m_loop;
static uint32_t m_tempo = 500000, m_div = 96;
static int m_volume = 100;                 /* the music volume 0..127 */
static uint8_t m_chvol[16];                /* the song's controller 7 of each channel */

static void midi_byte(uint8_t b) { MMIO_MIDI = b; }
static void midi_send(uint8_t st, uint8_t d1, uint8_t d2, int n) {
    midi_byte(st); midi_byte(d1); if (n > 2) midi_byte(d2);
}
static void midi_chvol(int ch) {
    midi_send(0xb0 | ch, 7, (uint8_t) ((m_chvol[ch] * m_volume) / 127), 3);
}
static void midi_all_off(void) {
    for (int ch = 0; ch < 16; ch++) {
        midi_send(0xb0 | ch, 0x7b, 0, 3);      /* all notes off */
        midi_send(0xb0 | ch, 0x79, 0, 3);      /* reset controllers */
    }
}

static uint64_t ticks_us(unsigned int ticks) { return ((uint64_t) ticks * m_tempo) / m_div; }

static void midi_rewind(uint64_t now) {
    m_tempo = 500000;
    for (int ch = 0; ch < 16; ch++) m_chvol[ch] = 100;
    for (int t = 0; t < m_tracks; t++) {
        MIDI_RestartIterator(m_iter[t]);
        m_next[t] = now + ticks_us(MIDI_GetDeltaTime(m_iter[t]));
    }
}

static void midi_stop(void) {
    kg_lock();
    if (m_playing) midi_all_off();
    m_playing = 0;
    for (int t = 0; t < m_tracks; t++) MIDI_FreeIterator(m_iter[t]);
    m_tracks = 0;
    m_file = NULL;
    kg_unlock();
}

static void midi_start(midi_file_t *f, int loop) {
    midi_stop();
    if (!f) return;
    kg_lock();
    m_file = f;
    m_div = MIDI_GetFileTimeDivision(f);
    if (m_div == 0 || (m_div & 0x8000)) m_div = 96;    /* SMPTE time: not from mus2mid */
    m_tracks = MIDI_NumTracks(f);
    if (m_tracks > MAX_TRACKS) m_tracks = MAX_TRACKS;
    for (int t = 0; t < m_tracks; t++) m_iter[t] = MIDI_IterateTrack(f, t);
    m_loop = loop;
    m_paused = 0;
    midi_all_off();
    for (int ch = 0; ch < 16; ch++) { m_chvol[ch] = 100; midi_chvol(ch); }
    midi_rewind(kg_us());
    m_playing = 1;
    kg_unlock();
    kg_timer_kick();
}

uint64_t kg_midi_next(void) {
    if (!m_playing || m_paused) return KG_NEVER;
    uint64_t t = KG_NEVER;
    for (int i = 0; i < m_tracks; i++) if (m_next[i] < t) t = m_next[i];
    return t;
}

static void midi_event(midi_event_t *e) {
    unsigned int ch = e->data.channel.channel & 15;
    switch (e->event_type) {
    case MIDI_EVENT_NOTE_OFF: case MIDI_EVENT_NOTE_ON: case MIDI_EVENT_AFTERTOUCH: case MIDI_EVENT_PITCH_BEND:
        midi_send(e->event_type | ch, e->data.channel.param1, e->data.channel.param2, 3);
        break;
    case MIDI_EVENT_CONTROLLER:
        if (e->data.channel.param1 == MIDI_CONTROLLER_MAIN_VOLUME) {
            m_chvol[ch] = e->data.channel.param2;
            midi_chvol(ch);
        } else
            midi_send(e->event_type | ch, e->data.channel.param1, e->data.channel.param2, 3);
        break;
    case MIDI_EVENT_PROGRAM_CHANGE: case MIDI_EVENT_CHAN_AFTERTOUCH:
        midi_send(e->event_type | ch, e->data.channel.param1, 0, 2);
        break;
    case MIDI_EVENT_META:
        if (e->data.meta.type == MIDI_META_SET_TEMPO && e->data.meta.length == 3) {
            const byte *d = e->data.meta.data;
            m_tempo = ((uint32_t) d[0] << 16) | (d[1] << 8) | d[2];
        }
        break;
    default: break;                         /* sysex: not sent */
    }
}

void kg_midi_run(uint64_t now) {
    if (!m_playing || m_paused) return;
    int live = 0;
    for (int t = 0; t < m_tracks; t++) {
        while (m_next[t] != KG_NEVER && m_next[t] <= now) {
            midi_event_t *e;
            if (!MIDI_GetNextEvent(m_iter[t], &e)) { m_next[t] = KG_NEVER; break; }
            midi_event(e);
            if (e->event_type == MIDI_EVENT_META && e->data.meta.type == MIDI_META_END_OF_TRACK) {
                m_next[t] = KG_NEVER;
                break;
            }
            m_next[t] += ticks_us(MIDI_GetDeltaTime(m_iter[t]));
        }
        if (m_next[t] != KG_NEVER) live = 1;
    }
    if (!live) {
        if (m_loop) { midi_all_off(); midi_rewind(now); }
        else { midi_all_off(); m_playing = 0; }
    }
}

/* ------------------------------------------------------------------ the module */

static int mode = MODE_OFF;
static void *cur_song;
static int cur_loop, cur_playing;

static void start_mode(void) {
    if (!cur_playing) return;
    if (mode == MODE_OPL) music_opl_module.PlaySong(cur_song, cur_loop);
    else if (mode == MODE_MIDI) midi_start(cur_song, cur_loop);
}
static void stop_all(void) {
    music_opl_module.StopSong();
    midi_stop();
}

static boolean Kg_Init(void) {
    mode = mode_of_osd();
    music_opl_module.Init();
    kg_lock();
    midi_all_off();
    kg_unlock();
    return true;
}

static void Kg_Shutdown(void) { stop_all(); music_opl_module.Shutdown(); }

static void Kg_SetMusicVolume(int volume) {
    music_opl_module.SetMusicVolume(volume);
    kg_lock();
    m_volume = volume;
    if (m_playing) for (int ch = 0; ch < 16; ch++) midi_chvol(ch);
    kg_unlock();
}

static void Kg_PauseMusic(void) {
    music_opl_module.PauseMusic();
    kg_lock();
    if (m_playing && !m_paused) { m_paused = 1; midi_all_off(); }
    kg_unlock();
}

static void Kg_ResumeMusic(void) {
    music_opl_module.ResumeMusic();
    kg_lock();
    if (m_paused) {
        uint64_t now = kg_us();
        /* the events go on from now (the time of the pause is skipped) */
        uint64_t first = kg_midi_next();
        m_paused = 0;
        uint64_t nf = KG_NEVER;
        for (int t = 0; t < m_tracks; t++) if (m_next[t] < nf) nf = m_next[t];
        (void) first;
        if (nf != KG_NEVER && nf < now)
            for (int t = 0; t < m_tracks; t++) if (m_next[t] != KG_NEVER) m_next[t] += now - nf;
    }
    kg_unlock();
    kg_timer_kick();
}

static void *Kg_RegisterSong(void *data, int len) { return music_opl_module.RegisterSong(data, len); }

static void Kg_UnRegisterSong(void *handle) {
    if (handle == cur_song) { stop_all(); cur_playing = 0; cur_song = NULL; }
    music_opl_module.UnRegisterSong(handle);
}

static void Kg_PlaySong(void *handle, boolean looping) {
    stop_all();
    cur_song = handle;
    cur_loop = looping;
    cur_playing = 1;
    mode = mode_of_osd();
    start_mode();
}

static void Kg_StopSong(void) { stop_all(); cur_playing = 0; }

static boolean Kg_MusicIsPlaying(void) {
    if (mode == MODE_OPL) return music_opl_module.MusicIsPlaying();
    if (mode == MODE_MIDI) return m_playing;
    return false;
}

static void Kg_Poll(void) {
    int m = mode_of_osd();
    if (m != mode) {
        stop_all();
        mode = m;
        start_mode();
    }
}

static snddevice_t devices[] = { SNDDEVICE_ADLIB, SNDDEVICE_SB, SNDDEVICE_PAS, SNDDEVICE_GUS,
                                 SNDDEVICE_WAVEBLASTER, SNDDEVICE_SOUNDCANVAS, SNDDEVICE_GENMIDI,
                                 SNDDEVICE_AWE32 };

#ifdef KG_HERETIC
music_module_t music_sdl_module = {
#else
music_module_t DG_music_module = {
#endif
    devices,
    sizeof devices / sizeof devices[0],
    Kg_Init,
    Kg_Shutdown,
    Kg_SetMusicVolume,
    Kg_PauseMusic,
    Kg_ResumeMusic,
    Kg_RegisterSong,
    Kg_UnRegisterSong,
    Kg_PlaySong,
    Kg_StopSong,
    Kg_MusicIsPlaying,
    Kg_Poll,
};
