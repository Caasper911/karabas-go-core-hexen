/* Ф0 bench: newlib system calls (from core-pico8). Console = MMIO UART, heap up to __heap_end.
   The only file: the WAD in memory (any name ending in .wad), so M_FileExists finds it;
   w_file_kg.c maps it without reading. */
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include "mmio.h"

extern char __heap_start[], __heap_end[];
static char *brk_ptr = __heap_start;

void *_sbrk(ptrdiff_t incr) {
    char *old = brk_ptr;
    if (brk_ptr + incr > __heap_end) {
        errno = ENOMEM;
        return (void *)-1;
    }
    brk_ptr += incr;
    return old;
}

#define WAD_FD 3
#define RAM_FD0 4
#define RAM_FILES 8
static uint32_t wad_pos;

/* the WAD: any name ending in .wad (read only, in memory at WAD_BASE) */
int kg_is_wad(const char *name) {
    size_t n = strlen(name);
    return n > 4 && !strcasecmp(name + n - 4, ".wad") && MMIO_WADSIZE != 0;
}

/* Ф2: files in RAM (lost at a reset): the MIDI of a song (i_oplmusic.c writes doom.mid and reads
   it back), default.cfg, savegames. One open handle per file. */
#include <stdlib.h>
#include <fcntl.h>
typedef struct { char name[64]; uint8_t *data; uint32_t size, cap, pos; int used, open, written; } ramfile_t;
/* Ф3: the kept files (default.cfg, savegames) come from and go to the save file on the SD card
   (kg_save.c of the board build: weak here, absent in the ISS bench build) */
#include "kg_save.h"
int kg_save_kept(const char *name) __attribute__((weak));
int kg_save_size(const char *name) __attribute__((weak));
int kg_save_load(const char *name, uint8_t *out, uint32_t cap) __attribute__((weak));
int kg_save_store(const char *name, const uint8_t *data, uint32_t size) __attribute__((weak));
void kg_save_remove(const char *name) __attribute__((weak));
static ramfile_t rf[RAM_FILES];

static ramfile_t *rf_find(const char *name) {
    for (int i = 0; i < RAM_FILES; i++) if (rf[i].used && !strcmp(rf[i].name, name)) return &rf[i];
    return 0;
}
static ramfile_t *rf_fd(int fd) {
    return (fd >= RAM_FD0 && fd < RAM_FD0 + RAM_FILES && rf[fd - RAM_FD0].used && rf[fd - RAM_FD0].open) ? &rf[fd - RAM_FD0] : 0;
}

/* the board build (kg_hw.c) keeps a log of the console and draws it at exit */
void kg_log_putc(char c) __attribute__((weak));
void kg_exit_screen(int code) __attribute__((weak));

int _write(int fd, const char *buf, int len) {
    ramfile_t *f = rf_fd(fd);
    if (f) {
        if (f->pos + len > f->cap) {
            uint32_t cap = f->cap ? f->cap : 4096;
            while (cap < f->pos + len) cap *= 2;
            uint8_t *d = realloc(f->data, cap);
            if (!d) { errno = ENOSPC; return -1; }
            f->data = d;
            f->cap = cap;
        }
        memcpy(f->data + f->pos, buf, len);
        f->pos += len;
        f->written = 1;
        if (f->pos > f->size) f->size = f->pos;
        return len;
    }
    if (fd != 1 && fd != 2) { errno = EBADF; return -1; }
    for (int i = 0; i < len; i++) {
        MMIO_UART = (uint8_t)buf[i];
        if (kg_log_putc) kg_log_putc(buf[i]);
    }
    return len;
}
int _read(int fd, char *buf, int len) {
    ramfile_t *f = rf_fd(fd);
    if (f) {
        if (f->pos >= f->size) return 0;
        if ((uint32_t) len > f->size - f->pos) len = (int) (f->size - f->pos);
        memcpy(buf, f->data + f->pos, len);
        f->pos += len;
        return len;
    }
    if (fd != WAD_FD) return 0;
    uint32_t sz = MMIO_WADSIZE;
    if (wad_pos >= sz) return 0;
    if ((uint32_t)len > sz - wad_pos) len = (int)(sz - wad_pos);
    memcpy(buf, (const char *)WAD_BASE + wad_pos, len);
    wad_pos += len;
    return len;
}
int _open(const char *name, int flags, int mode) {
    (void)mode;
    if ((flags & 3) == O_RDONLY && kg_is_wad(name)) { wad_pos = 0; return WAD_FD; }
    ramfile_t *f = rf_find(name);
    if (!f && !(flags & O_TRUNC) && kg_save_size && kg_save_size(name) >= 0) {
        /* a kept file: read it into a RAM file first (also for "rb+": the load menu of Heretic
           opens the savegames so, an empty new file there wiped the slot at its close) */
        int n = kg_save_size(name);
        for (int i = 0; i < RAM_FILES && !f; i++) if (!rf[i].used) f = &rf[i];
        if (f && strlen(name) < sizeof f->name) {
            memset(f, 0, sizeof *f);
            f->data = malloc(n ? n : 1);
            if (f->data && kg_save_load(name, f->data, n) == n) {
                strcpy(f->name, name);
                f->used = 1;
                f->size = f->cap = n;
            } else { free(f->data); memset(f, 0, sizeof *f); f = 0; }
        } else f = 0;
    }
    if ((flags & 3) == O_RDONLY || !(flags & O_CREAT)) {
        /* read, or update ("rb+") of a file that must exist */
        if (!f || f->open) { errno = ENOENT; return -1; }
        if (flags & O_TRUNC) { f->size = 0; f->written = 1; }
    } else {
        if (!f) {
            for (int i = 0; i < RAM_FILES && !f; i++) if (!rf[i].used) f = &rf[i];
            if (!f || strlen(name) >= sizeof f->name) { errno = ENOSPC; return -1; }
            memset(f, 0, sizeof *f);
            strcpy(f->name, name);
            f->used = 1;
        }
        if (f->open) { errno = EBUSY; return -1; }
        if (flags & O_TRUNC) f->size = 0;
        f->written = 1;                    /* created or truncated: stored at the close */
    }
    f->open = 1;
    f->pos = (flags & O_APPEND) ? f->size : 0;
    return RAM_FD0 + (int) (f - rf);
}
int _close(int fd) {
    ramfile_t *f = rf_fd(fd);
    if (f) {
        f->open = 0;
        if (f->written && kg_save_store && kg_save_kept(f->name)) kg_save_store(f->name, f->data, f->size);
        f->written = 0;
        return 0;
    }
    return fd == WAD_FD ? 0 : -1;
}
int _fstat(int fd, struct stat *st) {
    memset(st, 0, sizeof *st);
    ramfile_t *f = rf_fd(fd);
    if (f) { st->st_mode = S_IFREG; st->st_size = f->size; }
    else if (fd == WAD_FD) { st->st_mode = S_IFREG; st->st_size = MMIO_WADSIZE; }
    else st->st_mode = S_IFCHR;
    return 0;
}
int _isatty(int fd) { return fd < WAD_FD; }
int _lseek(int fd, int off, int whence) {
    ramfile_t *f = rf_fd(fd);
    if (f) {
        uint32_t base = whence == 0 ? 0 : whence == 1 ? f->pos : f->size;
        f->pos = base + off;
        return (int) f->pos;
    }
    if (fd != WAD_FD) return 0;
    uint32_t base = whence == 0 ? 0 : whence == 1 ? wad_pos : MMIO_WADSIZE;
    wad_pos = base + off;
    return (int)wad_pos;
}
int _unlink(const char *name) {
    if (kg_save_remove) kg_save_remove(name);
    ramfile_t *f = rf_find(name);
    if (!f) { errno = ENOENT; return -1; }
    free(f->data);
    memset(f, 0, sizeof *f);
    return 0;
}
int _link(const char *a, const char *b) { (void)a; (void)b; errno = EMLINK; return -1; }
int _stat(const char *name, struct stat *st) {
    memset(st, 0, sizeof *st);
    if (kg_is_wad(name)) { st->st_mode = S_IFREG; st->st_size = MMIO_WADSIZE; return 0; }
    ramfile_t *f = rf_find(name);
    if (f) { st->st_mode = S_IFREG; st->st_size = f->size; return 0; }
    if (kg_save_size && kg_save_size(name) >= 0) { st->st_mode = S_IFREG; st->st_size = kg_save_size(name); return 0; }
    errno = ENOENT;
    return -1;
}
int _rename(const char *a, const char *b) {
    ramfile_t *f = rf_find(a);
    if (!f || strlen(b) >= sizeof f->name) { errno = ENOENT; return -1; }
    ramfile_t *g = rf_find(b);
    if (g && g != f) { free(g->data); memset(g, 0, sizeof *g); }
    strcpy(f->name, b);
    if (kg_save_store && kg_save_kept(b)) kg_save_store(b, f->data, f->size);
    return 0;
}
int rename(const char *a, const char *b) { return _rename(a, b); }
int _mkdir(const char *name, int mode) { (void)name; (void)mode; return 0; }
int mkdir(const char *name, mode_t mode) { (void)name; (void)mode; return 0; }
int _getpid(void) { return 1; }
int _kill(int pid, int sig) { (void)pid; (void)sig; errno = EINVAL; return -1; }
clock_t _times(struct tms *t) { (void)t; return (clock_t)MMIO_MS; }
int _gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    uint32_t ms = MMIO_MS;
    tv->tv_sec = ms / 1000u;
    tv->tv_usec = (ms % 1000u) * 1000u;
    return 0;
}
void _exit(int code) {
    if (kg_exit_screen) kg_exit_screen(code);
    MMIO_EXIT = (uint32_t)code;
    for (;;) {}
}

/* Ф2: realloc with room to grow. chocolate-doom midifile.c grows the event array of a track by one
   event per realloc; the realloc of newlib-nano always allocates anew and copies, so a song of a
   few thousand events took minutes (quadratic: "New Game" hung at the level music). Linked with
   -Wl,--wrap=realloc: a block that is big enough is kept, a new one gets 1.5x. */
#include <malloc.h>
void *__wrap_realloc(void *p, size_t n) {
    if (!p) return malloc(n);
    if (n == 0) { free(p); return 0; }
    size_t have = malloc_usable_size(p);
    if (n <= have) return p;
    void *q = malloc(n + n / 2);
    if (!q) q = malloc(n);
    if (!q) return 0;
    memcpy(q, p, have);
    free(p);
    return q;
}
