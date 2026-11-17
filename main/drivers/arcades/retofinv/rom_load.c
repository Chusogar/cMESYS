// rom_load.c - the Return of the Invaders ROMs from storage when rom_data.c
// has none (a stub, or ROMS=external): the same loading and decoding as
// tools/gen_roms_retofinv.py (MAME set "retofinv").
#include "rom_data.h"
#include "../../../rom/romfs_arcade.h"

static const char *const sets[] = {"retofinv", NULL};

// MAME's gfx_layout decoding of 4-plane tiles to words of 8 pens (pixel x
// in bits 4x), w / 8 words a row.
static void decode(const uint8_t *rom, uint32_t len, int count, int w, int h, const uint32_t *planes,
                   const uint32_t *xo, const uint32_t *yo, uint32_t inc, uint32_t *out)
{
    for (int n = 0; n < count; n++)
        for (int y = 0; y < h; y++)
            for (int g = 0; g < w; g += 8) {
                uint32_t v = 0;
                for (int x = 0; x < 8; x++) {
                    unsigned pen = 0;
                    for (int k = 0; k < 4; k++) pen = (pen << 1) | ra_bit(rom, len, n * inc + planes[k] + yo[y] + xo[g + x]);
                    v |= (uint32_t)pen << (4 * x);
                }
                *out++ = v;
            }
}

bool rv_roms_get(void)
{
    if (rv_main_rom) return true;
    uint8_t *mainr = (uint8_t *)malloc(RV_MAIN_ROM_LEN), *sub = NULL, *snd = NULL, *mcu = NULL, *fg16 = NULL;
    uint8_t *spr = (uint8_t *)malloc(0x8000), *bg = (uint8_t *)malloc(0x4000), *prom = (uint8_t *)malloc(0x300);
    uint8_t *c = (uint8_t *)malloc(0x1000), *clut = (uint8_t *)malloc(0x800), *lut = (uint8_t *)malloc(RV_CLUT_LEN);
    uint32_t *bgw = (uint32_t *)malloc(RV_BG_GFX_LEN * 4), *spw = (uint32_t *)malloc(RV_SPR_GFX_LEN * 4);
    bool ok = mainr && spr && bg && prom && c && clut && lut && bgw && spw &&
              ra_into(sets, "a37__03.ic70", 0x2000, 0xeae7459d, mainr) &&
              ra_into(sets, "a37__02.ic71", 0x2000, 0x72895e37, mainr + 0x2000) &&
              ra_into(sets, "a37__01.ic72", 0x2000, 0x505dd20b, mainr + 0x4000) &&
              (sub = ra_get(sets, "a37__04.ic62", 0x2000, 0xd2899cc1)) != NULL &&
              (snd = ra_get(sets, "a37__05.ic17", 0x2000, 0x9025abea)) != NULL &&
              (mcu = ra_get(sets, "a37__09.ic37", 0x800, 0x6a6d008d)) != NULL &&
              (fg16 = ra_get(sets, "a37__16.gfxboard.ic61", 0x2000, 0x4e3f501c)) != NULL &&
              ra_into(sets, "a37__10.gfxboard.ic8", 0x2000, 0x6afdeec8, spr) &&
              ra_into(sets, "a37__11.gfxboard.ic9", 0x2000, 0xd3dc9da3, spr + 0x2000) &&
              ra_into(sets, "a37__12.gfxboard.ic10", 0x2000, 0xd10b2eed, spr + 0x4000) &&
              ra_into(sets, "a37__13.gfxboard.ic11", 0x2000, 0x00ca6b3d, spr + 0x6000) &&
              ra_into(sets, "a37__14.gfxboard.ic55", 0x2000, 0xef7f8651, bg) &&
              ra_into(sets, "a37__15.gfxboard.ic56", 0x2000, 0x03b40905, bg + 0x2000) &&
              ra_into(sets, "a37-06.ic13", 0x100, 0xe9643b8b, prom) &&
              ra_into(sets, "a37-07.ic4", 0x100, 0xe8f34e11, prom + 0x100) &&
              ra_into(sets, "a37-08.ic3", 0x100, 0x50030af0, prom + 0x200) &&
              ra_into(sets, "a37-17.gfxboard.ic36", 0x400, 0xc63cf10e, c) &&
              ra_into(sets, "a37-18.gfxboard.ic37", 0x400, 0x6db07bd1, c + 0x400) &&
              ra_into(sets, "a37-19.gfxboard.ic83", 0x400, 0xa92aea27, c + 0x800) &&
              ra_into(sets, "a37-20.gfxboard.ic84", 0x400, 0x77a7aaf6, c + 0xc00);
    romfs_release();
    if (ok) {
        for (int i = 0; i < 0x400; i++) {
            clut[i] = (uint8_t)(((c[i] & 15) << 4) | (c[0x400 + i] & 15));
            clut[0x400 + i] = (uint8_t)(((c[0x800 + i] & 15) << 4) | (c[0xc00 + i] & 15));
        }
        // colour lookup with MAME's descramble: pen i -> clut[bitswap(i, ..., 0, 1, 2)]
        for (int i = 0; i < 0x800; i++) lut[i] = clut[(i & ~7) | ((i & 1) << 2) | (i & 2) | ((i >> 2) & 1)];
        // bglayout: 8x8x4, planes {0, H+4, H, 4}, x {0-3, 64-67}, 16 bytes a tile
        uint32_t H = 0x2000 * 8, yo[16];
        const uint32_t bp[4] = {0, H + 4, H, 4}, bx[8] = {0, 1, 2, 3, 64, 65, 66, 67};
        for (int y = 0; y < 8; y++) yo[y] = y * 8;
        decode(bg, 0x4000, 512, 8, 8, bp, bx, yo, 128, bgw);
        // spritelayout: 16x16x4, planes {0, H+4, H, 4}, 64 bytes a sprite
        H = 0x4000 * 8;
        const uint32_t sp[4] = {0, H + 4, H, 4};
        static const uint32_t sx[16] = {0, 1, 2, 3, 64, 65, 66, 67, 128, 129, 130, 131, 192, 193, 194, 195};
        for (int y = 0; y < 8; y++) { yo[y] = y * 8; yo[8 + y] = 256 + y * 8; }
        decode(spr, 0x8000, 256, 16, 16, sp, sx, yo, 512, spw);
        rv_main_rom = mainr;
        rv_sub_rom = sub;
        rv_sound_rom = snd;
        rv_mcu_rom = mcu;
        rv_fg_rom = fg16;                       // (its first 4 KB: the rest is empty)
        rv_bg_gfx = bgw;
        rv_spr_gfx = spw;
        rv_palette_prom = prom;
        rv_clut = lut;
    } else {
        free(mainr); free(sub); free(snd); free(mcu); free(fg16); free(prom); free(lut); free(bgw); free(spw);
    }
    free(spr); free(bg); free(c); free(clut);
    return ok;
}
