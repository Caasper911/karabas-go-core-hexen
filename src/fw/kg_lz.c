/* karabas (Hexen Ф2): a small LZ77 (LZ4-like tokens) for the savegames in the save file of the SD card:
   the files of Hexen (hex<n>.hxs + one per map of the hub, 80..120 KB each) are mostly zeros and repeated
   structures. Greedy, a hash of 4 bytes, matches of 4..259 bytes up to 64 KB back.
   Stream: tokens { u8 tag; ... }: tag high nibble = literals (15: more in the next bytes, 255 each),
   low nibble = match length - 4 (15: more); literals; then u16 offset (little endian) when a match follows.
   The last token has literals only. kg_lz_pack returns the packed size (out must hold n + n / 128 + 16). */
#include <stdint.h>
#include <string.h>
#include "kg_lz.h"

#define HASH_BITS 12
#define MIN_MATCH 4
#define MAX_OFF 65535

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static unsigned hash4(uint32_t v) { return (v * 2654435761u) >> (32 - HASH_BITS); }

static uint8_t *put_len(uint8_t *o, uint32_t n)
{
    while (n >= 255) { *o++ = 255; n -= 255; }
    *o++ = (uint8_t) n;
    return o;
}

uint32_t kg_lz_pack(const uint8_t *in, uint32_t n, uint8_t *out)
{
    static uint32_t table[1 << HASH_BITS];
    const uint8_t *ip = in, *anchor = in, *end = in + n;
    uint8_t *op = out;
    memset(table, 0xff, sizeof table);
    while (n >= MIN_MATCH && ip + MIN_MATCH <= end) {
        uint32_t v = rd32(ip);
        unsigned h = hash4(v);
        uint32_t cand = table[h];
        table[h] = (uint32_t) (ip - in);
        if (cand != 0xffffffffu && (uint32_t) (ip - in) - cand <= MAX_OFF && rd32(in + cand) == v) {
            const uint8_t *m = in + cand;
            uint32_t len = MIN_MATCH;
            while (ip + len < end && ip[len] == m[len]) len++;
            uint32_t lit = (uint32_t) (ip - anchor), ml = len - MIN_MATCH;
            uint8_t *tag = op++;
            *tag = (uint8_t) ((lit < 15 ? lit : 15) << 4 | (ml < 15 ? ml : 15));
            if (lit >= 15) op = put_len(op, lit - 15);
            memcpy(op, anchor, lit);
            op += lit;
            uint32_t off = (uint32_t) (ip - m);
            *op++ = (uint8_t) off;
            *op++ = (uint8_t) (off >> 8);
            if (ml >= 15) op = put_len(op, ml - 15);
            ip += len;
            anchor = ip;
        } else
            ip++;
    }
    uint32_t lit = (uint32_t) (end - anchor);
    *op++ = (uint8_t) ((lit < 15 ? lit : 15) << 4);
    if (lit >= 15) op = put_len(op, lit - 15);
    memcpy(op, anchor, lit);
    op += lit;
    return (uint32_t) (op - out);
}

/* returns the unpacked size, or 0xffffffff on a bad stream (or more than cap) */
uint32_t kg_lz_unpack(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t cap)
{
    const uint8_t *ip = in, *iend = in + n;
    uint8_t *op = out, *oend = out + cap;
    while (ip < iend) {
        uint8_t tag = *ip++;
        uint32_t lit = tag >> 4, ml = tag & 15;
        if (lit == 15) { uint8_t b; do { if (ip >= iend) return 0xffffffffu; b = *ip++; lit += b; } while (b == 255); }
        if ((uint32_t) (iend - ip) < lit || (uint32_t) (oend - op) < lit) return 0xffffffffu;
        memcpy(op, ip, lit);
        op += lit;
        ip += lit;
        if (ip >= iend) break;                      /* the last token: literals only */
        if (iend - ip < 2) return 0xffffffffu;
        uint32_t off = ip[0] | (uint32_t) ip[1] << 8;
        ip += 2;
        if (ml == 15) { uint8_t b; do { if (ip >= iend) return 0xffffffffu; b = *ip++; ml += b; } while (b == 255); }
        ml += MIN_MATCH;
        if (off == 0 || off > (uint32_t) (op - out) || (uint32_t) (oend - op) < ml) return 0xffffffffu;
        const uint8_t *m = op - off;
        while (ml--) *op++ = *m++;                  /* overlapping copies are fine byte by byte */
    }
    return (uint32_t) (op - out);
}
