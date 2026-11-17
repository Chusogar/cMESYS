// romfs_arcade.h - helpers for the arcades' rom_load.c: their ROMs from
// storage (romfs.h), each file of its exact size, the same decoding as
// their tools/gen_roms_*.
#ifndef ROMFS_ARCADE_H
#define ROMFS_ARCADE_H
#include "romfs.h"
#include <stdlib.h>
#include <string.h>

// A ROM of exactly `size` bytes (malloc'd) or NULL (romfs_error() says which).
static inline uint8_t *ra_get(const char *const *sets, const char *name, uint32_t size, uint32_t crc)
{
    return romfs_get(sets, name, crc, size, NULL);
}

// One file into dst; false if it is missing.
static inline bool ra_into(const char *const *sets, const char *name, uint32_t size, uint32_t crc, uint8_t *dst)
{
    return romfs_get_into(sets, name, crc, dst, size);
}

// A file's bytes into the even (odd = 0) or odd (odd = 1) bytes of dst.
static inline bool ra_lane(const char *const *sets, const char *name, uint32_t size, uint32_t crc, uint8_t *dst, int odd)
{
    uint8_t *b = ra_get(sets, name, size, crc);
    if (!b) return false;
    for (uint32_t i = 0; i < size; i++) dst[2 * i + odd] = b[i];
    free(b);
    return true;
}

// MSB-first bit of a ROM (0 past its end), as MAME's gfx decoding reads it.
static inline unsigned ra_bit(const uint8_t *rom, uint32_t len, uint32_t b)
{
    uint32_t i = b >> 3;
    return i < len ? (rom[i] >> (7 - (b & 7))) & 1 : 0;
}
#endif
