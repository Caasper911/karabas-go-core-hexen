/* Ф3 (karabas): the files of Doom that live on: default.cfg and the 6 savegames, in the save file
 * that the firmware of the board makes next to the WAD (<WAD dir>/saves/<WAD name>.sav, 1 MB:
 * the backup RAM request of doom_sd.v). The RAM files of syscalls.c stay the working copies:
 * - kg_save_load(name): before a file is opened for reading (it is read into its RAM file),
 * - kg_save_store(name, data, size): when a written file is closed or renamed to its name.
 * Layout, in 512-byte sectors: 0 the table (magic, then the size in bytes of each slot, 0 = no
 * file), slot 0 (default.cfg) from 1, 16 sectors; slot 1 + n (doomsav<n>.dsg) from 17 + 338 n,
 * 338 sectors (173056 bytes: the vanilla limit is 180224, the saves of the IWAD levels are far
 * smaller). Without a save file (no SD card, an old firmware) nothing is kept, as before.
 * KG_HERETIC (core-heretic): magic KGHTICS1, slot 0 heretic.cfg and slot 1 chocolate-heretic.cfg,
 * 16 sectors each, slots 2 + n (hticsav<n>.hsg) from 33 + 335 n, 335 sectors (171520 bytes; the
 * vanilla limit is 196608).
 * (Hexen: kg_save_fs.c.) */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "mmio.h"
#include "kg_save.h"

#define SD_SECTOR  MMIO_REG(0x80)
#define SD_CMD     MMIO_REG(0x84)
#define SD_STAT    MMIO_REG(0x88)
#define SD_SIZE    MMIO_REG(0x8c)
#define SD_BKREQ   MMIO_REG(0x90)
#define SD_BUF(i)  MMIO_REG(0x1000 + 4 * (i))

#define FILE_SECTORS 2048
#define CFG_SECTORS  16
#if defined(KG_HERETIC)
#define CFGS         2
#define SAVE_SECTORS 335
static const char MAGIC[8] = "KGHTICS1";
static const char *const CFG_NAME[CFGS] = { "heretic.cfg", "chocolate-heretic.cfg" };
#define SAVE_PREFIX  "hticsav"
#define SAVE_EXT     ".hsg"
#else
#define CFGS         1
#define SAVE_SECTORS 338
static const char MAGIC[8] = "KGDOOMS1";
static const char *const CFG_NAME[CFGS] = { "default.cfg" };
#define SAVE_PREFIX  "doomsav"
#define SAVE_EXT     ".dsg"
#endif
#define SLOTS        (CFGS + 6)

static int ready;
static uint32_t table[SLOTS];
static int wait_idle(void);
/* debug: the last console byte (DEBUG_ADDR low byte of the board) says the state of the saves */
static char last_st = '?';
static void st(char c) { last_st = c; MMIO_UART = (uint8_t) c; }
void kg_save_show(void) { MMIO_UART = (uint8_t) last_st; }

/* a sector takes a few ms; no answer in 500 ms or an error: the save file is given up for this run
   (the game goes on with the RAM files, a message in the log) */
static int wait_idle(void) {
    uint32_t t0 = MMIO_MS;
    while (SD_STAT & 1) if (MMIO_MS - t0 > 500) { printf("Saves: SD timeout, off\n"); ready = 0; st('T'); return 0; }
    if (SD_STAT & 2) { printf("Saves: SD error, off\n"); ready = 0; st('E'); return 0; }
    return 1;
}

static int sec_read(uint32_t s, uint8_t *buf) {
    SD_SECTOR = s;
    SD_CMD = 1;
    if (!wait_idle()) return 0;
    for (int i = 0; i < 512; i++) buf[i] = (uint8_t) SD_BUF(i);
    return 1;
}

static int sec_write(uint32_t s, const uint8_t *buf) {
    for (int i = 0; i < 512; i++) SD_BUF(i) = buf[i];
    SD_SECTOR = s;
    SD_CMD = 2;
    return wait_idle();
}

static int table_write(void) {
    uint8_t b[512];
    memset(b, 0, sizeof b);
    memcpy(b, MAGIC, 8);
    memcpy(b + 8, table, sizeof table);
    return sec_write(0, b);
}

/* the slot of a file name (its last path element), -1: not kept */
static int slot_of(const char *name) {
    const char *b = strrchr(name, '/');
    b = b ? b + 1 : name;
    for (int i = 0; i < CFGS; i++) if (!strcmp(b, CFG_NAME[i])) return i;
    size_t p = sizeof SAVE_PREFIX - 1;
    if (!strncmp(b, SAVE_PREFIX, p) && b[p] >= '0' && b[p] <= '5' && !strcmp(b + p + 1, SAVE_EXT)) return CFGS + (b[p] - '0');
    return -1;
}

static uint32_t slot_start(int s) {
    return s < CFGS ? 1 + (uint32_t) s * CFG_SECTORS : 1 + CFGS * CFG_SECTORS + (uint32_t) (s - CFGS) * SAVE_SECTORS;
}
static uint32_t slot_max(int s) { return (s < CFGS ? CFG_SECTORS : SAVE_SECTORS) * 512u; }

void kg_save_init(void) {
    ready = 0;
    /* a restart of the CPU (Reset / F12) keeps the mount: asking again makes the firmware open the
       file anew while the table is being read (the read failed: the saves were off after F12). A new
       WAD closes it first (size 0), so the request comes only when nothing is mounted */
    if (!(SD_STAT & 4)) SD_BKREQ = FILE_SECTORS;
    uint32_t t0 = MMIO_MS;
    while (!(SD_STAT & 4)) if (MMIO_MS - t0 > 2500) { printf("Saves: no save file\n"); st('N'); return; }
    if (SD_SIZE < FILE_SECTORS * 512u) { printf("Saves: the save file is too small\n"); st('S'); return; }
    uint8_t b[512];
    ready = 1;
    if (!sec_read(0, b)) { printf("Saves: read error\n"); ready = 0; return; }
    if (memcmp(b, MAGIC, 8)) {
        /* only a new file (sector 0 all zeros) gets an empty table: anything else (a bad read, a
           file of something else) is left as it is and the saves stay off */
        for (int i = 0; i < 512; i++) if (b[i]) { printf("Saves: the save file is not ours, off\n"); ready = 0; st('X'); return; }
        memset(table, 0, sizeof table);
        if (!table_write()) { printf("Saves: write error\n"); ready = 0; return; }
    } else
        memcpy(table, b + 8, sizeof table);
    for (int i = 0; i < SLOTS; i++) if (table[i] > slot_max(i)) table[i] = 0;
    ready = 1;
    printf("Saves: on the SD card\n");
    st('R');
}

int kg_save_kept(const char *name) { return ready && slot_of(name) >= 0; }

/* the size of a kept file, -1: none */
int kg_save_size(const char *name) {
    int s = slot_of(name);
    if (!ready || s < 0 || table[s] == 0) return -1;
    return (int) table[s];
}

int kg_save_load(const char *name, uint8_t *out, uint32_t cap) {
    int s = slot_of(name);
    if (!ready || s < 0 || table[s] == 0 || table[s] > cap) return -1;
    uint32_t size = table[s], start = slot_start(s);
    uint8_t b[512];
    for (uint32_t off = 0; off < size; off += 512) {
        if (!sec_read(start + off / 512, b)) return -1;
        uint32_t n = size - off < 512 ? size - off : 512;
        memcpy(out + off, b, n);
    }
    return (int) size;
}

int kg_save_store(const char *name, const uint8_t *data, uint32_t size) {
    int s = slot_of(name);
    if (!ready || s < 0) return 0;
    if (size > slot_max(s)) { printf("Saves: %s too big (%lu)\n", name, (unsigned long) size); return 0; }
    uint32_t start = slot_start(s);
    uint8_t b[512];
    for (uint32_t off = 0; off < size; off += 512) {
        uint32_t n = size - off < 512 ? size - off : 512;
        memset(b, 0, sizeof b);
        memcpy(b, data + off, n);
        if (!sec_write(start + off / 512, b)) { printf("Saves: write error\n"); return 0; }
    }
    table[s] = size;
    int ok = table_write();
    st(ok ? 'K' : 'w');
    return ok;
}

void kg_save_remove(const char *name) {
    int s = slot_of(name);
    if (!ready || s < 0 || table[s] == 0) return;
    table[s] = 0;
    table_write();
}

_Static_assert(1 + CFGS * CFG_SECTORS + 6 * SAVE_SECTORS <= FILE_SECTORS, "the slots must fit the save file");
