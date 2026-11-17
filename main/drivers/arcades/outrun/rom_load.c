// rom_load.c - the OutRun ROMs from storage when rom_data.c has none (a stub,
// or ROMS=external): the same loading and decoding as
// tools/gen_roms_outrun.py (MAME set "outrun", rev B).
#include "rom_data.h"
#include "../../../rom/romfs_arcade.h"

static const char *const sets[] = {"outrun", NULL};

typedef struct { const char *name; uint32_t crc; } rom;

// A 68000 program: two (even, odd) pairs of 64 KB, at 0 and 0x20000.
static bool prog(uint8_t *out, const rom *r4)
{
    for (int p = 0; p < 2; p++)
        if (!ra_lane(sets, r4[2 * p].name, 0x10000, r4[2 * p].crc, out + p * 0x20000, 0) ||
            !ra_lane(sets, r4[2 * p + 1].name, 0x10000, r4[2 * p + 1].crc, out + p * 0x20000, 1))
            return false;
    return true;
}

static bool cat(uint8_t *out, const rom *r, int n, uint32_t size)
{
    for (int i = 0; i < n; i++)
        if (!ra_into(sets, r[i].name, size, r[i].crc, out + i * size)) return false;
    return true;
}

bool or_roms_get(or_roms *r)
{
    if (or_rom_data(r)) return true;
    static const rom main_r[4] = {{"epr-10380b.133", 0x1f6cadad}, {"epr-10382b.118", 0xc4c3fa1a},
                                  {"epr-10381b.132", 0xbe8c412b}, {"epr-10383b.117", 0x10a2014a}};
    static const rom sub_r[4] = {{"epr-10327a.76", 0xe28a5baf}, {"epr-10329a.58", 0xda131c81},
                                 {"epr-10328a.75", 0xd5ec5e5d}, {"epr-10330a.57", 0xba9ec82a}};
    static const rom gfx_r[6] = {{"opr-10268.99", 0x95344b04}, {"opr-10232.102", 0x776ba1eb},
                                 {"opr-10267.100", 0xa85bb823}, {"opr-10231.103", 0x8908bcbf},
                                 {"opr-10266.101", 0x9f6f1a74}, {"opr-10230.104", 0x686f5e50}};
    static const rom spr_r[8] = {{"mpr-10371.9", 0x7cc86208}, {"mpr-10373.10", 0xb0d26ac9},
                                 {"mpr-10375.11", 0x59b60bd7}, {"mpr-10377.12", 0x17a1b04a},
                                 {"mpr-10372.13", 0xb557078c}, {"mpr-10374.14", 0x8051e517},
                                 {"mpr-10376.15", 0xf3b8f318}, {"mpr-10378.16", 0xa1062984}};
    static const rom road_r[2] = {{"opr-10186.47", 0x22794426}, {"opr-10185.11", 0x22794426}};
    static const rom pcm_r[6] = {{"opr-10193.66", 0xbcd10dde}, {"opr-10192.67", 0x770f1270},
                                 {"opr-10191.68", 0x20a284ab}, {"opr-10190.69", 0x7cab70e2},
                                 {"opr-10189.70", 0x01366b54}, {"opr-10188.71", 0xbad30ad9}};
    uint8_t *mainp = (uint8_t *)malloc(0x40000), *subp = (uint8_t *)malloc(0x40000), *snd = NULL;
    uint8_t *gfx = (uint8_t *)malloc(0x30000), *sp = (uint8_t *)malloc(8 * 0x20000), *rr = (uint8_t *)malloc(0x10000);
    uint8_t *road = (uint8_t *)malloc((256 * 2 + 1) * 512), *pcm = (uint8_t *)malloc(0x30000);
    uint32_t *tiles = (uint32_t *)malloc(0x10000 * sizeof(uint32_t)), *sprites = (uint32_t *)malloc(0x40000 * sizeof(uint32_t));
    bool ok = mainp && subp && gfx && sp && rr && road && pcm && tiles && sprites &&
              prog(mainp, main_r) && prog(subp, sub_r) &&
              (snd = ra_get(sets, "epr-10187.88", 0x8000, 0xa10abaa9)) != NULL &&
              cat(gfx, gfx_r, 6, 0x8000) && cat(sp, spr_r, 8, 0x20000) && cat(rr, road_r, 2, 0x8000) &&
              cat(pcm, pcm_r, 6, 0x8000);
    romfs_release();
    if (ok) {
        // gfx_8x8x3_planar: planes at 2/3 (bit 2), 1/3 (bit 1), 0 (bit 0)
        uint32_t spread[256];
        for (int b = 0; b < 256; b++) {
            uint32_t v = 0;
            for (int x = 0; x < 8; x++)
                if (b & (0x80 >> x)) v |= 1u << (4 * x);
            spread[b] = v;
        }
        for (uint32_t i = 0; i < 0x10000; i++)
            tiles[i] = (spread[gfx[0x20000 + i]] << 2) | (spread[gfx[0x10000 + i]] << 1) | spread[gfx[i]];
        // sprites: ROM_LOAD32_BYTE into a little-endian 32-bit region
        for (int half = 0; half < 2; half++) {
            const uint8_t *b0 = sp + (half * 4) * 0x20000, *b1 = b0 + 0x20000, *b2 = b1 + 0x20000, *b3 = b2 + 0x20000;
            for (uint32_t i = 0; i < 0x20000; i++)
                sprites[half * 0x20000 + i] = b0[i] | (uint32_t)b1[i] << 8 | (uint32_t)b2[i] << 16 | (uint32_t)b3[i] << 24;
        }
        // road: as segaic16_road_outrun_decode
        for (uint32_t y = 0; y < 512; y++) {
            uint32_t src = ((y & 0xff) * 0x40 + (y >> 8) * 0x8000) % 0x10000;
            for (uint32_t x = 0; x < 512; x++) {
                unsigned s = ~x & 7;
                uint8_t v = (uint8_t)(((rr[src + (x >> 3)] >> s) & 1) | (((rr[src + (x >> 3) + 0x4000] >> s) & 1) << 1));
                if (x >= 256 - 8 && x < 256 && v == 3) v |= 4;
                road[y * 512 + x] = v;
            }
        }
        memset(road + 512 * 512, 3, 512);
        r->main = mainp;
        r->sub = subp;
        r->sound = snd;
        r->tiles = tiles;
        r->sprites = sprites;
        r->road = road;
        r->pcm = pcm;
    } else {
        free(mainp); free(subp); free(snd); free(road); free(pcm); free(tiles); free(sprites);
    }
    free(gfx);
    free(sp);
    free(rr);
    return ok;
}
