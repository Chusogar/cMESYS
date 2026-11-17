// rom_load.c - the Puzzle Bobble ROMs from storage when rom_data.c has none
// (a stub, or ROMS=external): the same loading and decoding as
// tools/gen_roms_pbobble.py (MAME set "pbobble").
#include "rom_data.h"
#include "../../../rom/romfs_arcade.h"

static const char *const sets[] = {"pbobble", NULL};

bool pb_roms_get(void)
{
    if (pb_prog_rom) return true;
    uint8_t *prog = (uint8_t *)malloc(PB_PROG_ROM_LEN), *snd = NULL, *adpcm = NULL;
    uint8_t *lo = NULL, *hi = NULL, *gfx = (uint8_t *)malloc(PB_GFX_ROM_LEN);
    bool ok = prog && gfx &&
              ra_lane(sets, "pb-ic18.ic18", 0x40000, 0x5de14f49, prog, 0) &&
              ra_lane(sets, "pb-ic2.ic2", 0x40000, 0x2abe07d1, prog, 1) &&
              (snd = ra_get(sets, "pb-ic27.ic27", 0x20000, 0x26efa4c4)) != NULL &&
              (lo = ra_get(sets, "pb-ic14.ic14", 0x80000, 0x55f90ea4)) != NULL &&
              (hi = ra_get(sets, "pb-ic9.ic9", 0x80000, 0x3253aac9)) != NULL &&
              (adpcm = ra_get(sets, "pb-ic15.ic15", 0x100000, 0x0840cbc4)) != NULL;
    romfs_release();
    if (ok) {
        // char c, row y: plane bytes lo[c*16+y*2], lo[c*16+y*2+1], hi[...], hi[...+1];
        // pixel x = p0<<3 | p1<<2 | p2<<1 | p3, 4 bytes a row (left pixel in the high nibble)
        uint32_t spread[256];
        for (int b = 0; b < 256; b++) {
            uint32_t v = 0;
            for (int x = 0; x < 8; x++)
                if (b & (0x80 >> x)) v |= 1u << (4 * (7 - x));
            spread[b] = v;
        }
        for (uint32_t c = 0; c < 0x8000; c++)
            for (int y = 0; y < 8; y++) {
                uint32_t o = c * 16 + y * 2;
                uint32_t v = (spread[lo[o]] << 3) | (spread[lo[o + 1]] << 2) | (spread[hi[o]] << 1) | spread[hi[o + 1]];
                uint8_t *d = gfx + c * 32 + y * 4;
                d[0] = (uint8_t)(v >> 24); d[1] = (uint8_t)(v >> 16); d[2] = (uint8_t)(v >> 8); d[3] = (uint8_t)v;
            }
        pb_prog_rom = prog;
        pb_snd_rom = snd;
        pb_gfx_rom = gfx;
        pb_adpcma_rom = adpcm;
    } else {
        free(prog); free(snd); free(gfx); free(adpcm);
    }
    free(lo);
    free(hi);
    return ok;
}
