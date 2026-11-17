// rom_load.c - the Track & Field ROMs from storage when rom_data.c has none
// (a stub, or ROMS=external): the same loading and decoding as
// tools/gen_roms_trackfld.c (MAME set "trackfld").
#include "rom_data.h"
#include "../../../rom/romfs_arcade.h"

static const char *const sets[] = {"trackfld", NULL};

bool tf_roms_get(void)
{
    if (tf_main_rom) return true;
    uint8_t *mainr = (uint8_t *)malloc(TF_MAIN_ROM_LEN), *audio = (uint8_t *)calloc(1, TF_AUDIO_ROM_LEN);
    uint8_t *vlm = (uint8_t *)calloc(1, TF_VLM_ROM_LEN), *prom = (uint8_t *)malloc(TF_PROM_LEN);
    uint8_t *gfx1 = (uint8_t *)malloc(0x8000), *gfx2 = (uint8_t *)malloc(0x6000);
    uint8_t *chr = (uint8_t *)malloc(TF_CHR_PIX_LEN), *spr = (uint8_t *)malloc(TF_SPR_PIX_LEN);
    static const char *const prog[5] = {"a01_e01.bin", "a02_e02.bin", "a03_k03.bin", "a04_e04.bin", "a05_e05.bin"};
    static const char *const g1[4] = {"c11_d06.bin", "c12_d07.bin", "c13_d08.bin", "c14_d09.bin"};
    static const char *const g2[3] = {"h16_e12.bin", "h15_e11.bin", "h14_e10.bin"};
    bool ok = mainr && audio && vlm && prom && gfx1 && gfx2 && chr && spr &&
              ra_into(sets, "c2_d13.bin", 0x2000, 0, audio) && ra_into(sets, "c9_d15.bin", 0x2000, 0, vlm) &&
              ra_into(sets, "361b16.f1", 0x20, 0, prom) && ra_into(sets, "361b17.b16", 0x100, 0, prom + 0x20) &&
              ra_into(sets, "361b18.e15", 0x100, 0, prom + 0x120);
    for (int i = 0; ok && i < 5; i++) ok = ra_into(sets, prog[i], 0x2000, 0, mainr + i * 0x2000);
    for (int i = 0; ok && i < 4; i++) ok = ra_into(sets, g1[i], 0x2000, 0, gfx1 + i * 0x2000);
    for (int i = 0; ok && i < 3; i++) ok = ra_into(sets, g2[i], 0x2000, 0, gfx2 + i * 0x2000);
    romfs_release();
    if (ok) {
        for (int code = 0; code < 768; ++code)          // chars: 4bpp packed, 32 bytes each
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) {
                    uint8_t v = gfx2[code * 32 + y * 4 + (x >> 1)];
                    chr[(code * 8 + y) * 8 + x] = (x & 1) ? (v & 0x0f) : (v >> 4);
                }
        static const int planeoffs[4] = {0, 4, 256 * 64 * 8 + 0, 256 * 64 * 8 + 4};
        static const int xoffs[16] = {0, 1, 2, 3, 8 * 8 + 0, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3,
                                      16 * 8 + 0, 16 * 8 + 1, 16 * 8 + 2, 16 * 8 + 3, 24 * 8 + 0, 24 * 8 + 1, 24 * 8 + 2, 24 * 8 + 3};
        static const int yoffs[16] = {0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8,
                                      32 * 8, 33 * 8, 34 * 8, 35 * 8, 36 * 8, 37 * 8, 38 * 8, 39 * 8};
        for (int code = 0; code < 256; ++code)
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) {
                    uint8_t pen = 0;
                    for (int plane = 0; plane < 4; ++plane)
                        pen |= (uint8_t)(ra_bit(gfx1, 0x8000, (uint32_t)(code * 64 * 8 + planeoffs[plane] + yoffs[y] + xoffs[x])) << plane);
                    spr[(code * 16 + y) * 16 + x] = pen;
                }
        tf_main_rom = mainr;
        tf_audio_rom = audio;
        tf_vlm_rom = vlm;
        tf_prom = prom;
        tf_chr_pix = chr;
        tf_spr_pix = spr;
    } else {
        free(mainr); free(audio); free(vlm); free(prom); free(chr); free(spr);
    }
    free(gfx1);
    free(gfx2);
    return ok;
}
