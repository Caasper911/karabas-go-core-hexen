/* karabas (Hexen Ф1г): memcpy / memset by words. newlib-nano has them by bytes (made for size): the
   visplanes of Hexen clear 320 bytes each (R_FindPlane / R_CheckPlane: memset 3..5 % of a frame), the
   title screens copy 64000 bytes. Words only where both ends share the alignment (VexRiscv traps a
   misaligned access), bytes else. Built with -fno-tree-loop-distribute-patterns (the loops must not
   become calls of themselves). Linked before libc: these take the place of its memcpy / memset. */
#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    if ((((uintptr_t) d ^ (uintptr_t) s) & 3) == 0 && n >= 8) {
        while ((uintptr_t) d & 3) { *d++ = *s++; n--; }
        uint32_t *dw = (uint32_t *) d;
        const uint32_t *sw = (const uint32_t *) s;
        while (n >= 16) {
            uint32_t a = sw[0], b = sw[1], c = sw[2], e = sw[3];
            dw[0] = a; dw[1] = b; dw[2] = c; dw[3] = e;
            dw += 4; sw += 4; n -= 16;
        }
        while (n >= 4) { *dw++ = *sw++; n -= 4; }
        d = (uint8_t *) dw;
        s = (const uint8_t *) sw;
    }
    while (n--) *d++ = *s++;
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    uint8_t *d = dst;
    uint8_t b = (uint8_t) c;
    if (n >= 8) {
        while ((uintptr_t) d & 3) { *d++ = b; n--; }
        uint32_t w = b * 0x01010101u;
        uint32_t *dw = (uint32_t *) d;
        while (n >= 16) { dw[0] = w; dw[1] = w; dw[2] = w; dw[3] = w; dw += 4; n -= 16; }
        while (n >= 4) { *dw++ = w; n -= 4; }
        d = (uint8_t *) dw;
    }
    while (n--) *d++ = b;
    return dst;
}
