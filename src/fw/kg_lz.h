/* karabas (Hexen Ф2): LZ packing of the savegames (kg_lz.c) */
#pragma once
#include <stdint.h>

uint32_t kg_lz_pack(const uint8_t *in, uint32_t n, uint8_t *out);           /* out: n + n / 128 + 16 */
uint32_t kg_lz_unpack(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t cap);
