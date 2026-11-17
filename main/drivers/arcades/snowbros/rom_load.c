// rom_load.c - the Snow Bros ROMs from storage when rom_data.c has none (a
// stub, or ROMS=external): the same loading as tools/gen_roms_snowbros.py
// (MAME set "snowbros").
#include "rom_data.h"
#include "../../../rom/romfs_arcade.h"
#include <stdio.h>

static const char *const sets[] = {"snowbros", NULL};

// The first of `c` found, copied into dst (at most max bytes; any size).
static bool any_into(const romfs_cand *c, uint8_t *dst, uint32_t max)
{
    uint32_t n = 0;
    uint8_t *b = romfs_get_any(sets, c, &n, NULL);
    if (!b) return false;
    memcpy(dst, b, n < max ? n : max);
    free(b);
    return true;
}

bool sb_roms_get(void)
{
    if (sb_prog_rom) return true;
    uint8_t *prog = (uint8_t *)malloc(SB_PROG_ROM_LEN), *snd = (uint8_t *)calloc(1, SB_SND_ROM_LEN);
    uint8_t *gfx = (uint8_t *)calloc(1, SB_GFX_ROM_LEN), *even = NULL, *odd = NULL;
    static const romfs_cand c_even[] = {{"sn6.bin", 0}, {NULL, 0}}, c_odd[] = {{"sn5.bin", 0}, {NULL, 0}};
    static const romfs_cand c_snd[] = {{"snowbros.4", 0}, {"sbros-4.29", 0}, {NULL, 0}};
    static const romfs_cand c_gfx[] = {{"sbros-1.41", 0}, {NULL, 0}};
    uint32_t ne = 0, no = 0;
    bool ok = prog && snd && gfx && (even = romfs_get_any(sets, c_even, &ne, NULL)) != NULL &&
              (odd = romfs_get_any(sets, c_odd, &no, NULL)) != NULL && any_into(c_snd, snd, SB_SND_ROM_LEN);
    if (ok && !any_into(c_gfx, gfx, SB_GFX_ROM_LEN))
        for (int i = 0; ok && i < 4; i++) {               // or ch0..ch3 (4 x 128 KB)
            char a[8], b[12];
            snprintf(a, sizeof a, "ch%d", i);
            snprintf(b, sizeof b, "ch%d.bin", i);
            const romfs_cand c_ch[] = {{a, 0}, {b, 0}, {NULL, 0}};
            ok = any_into(c_ch, gfx + i * 0x20000, 0x20000);
        }
    romfs_release();
    if (ok) {
        memset(prog, 0xff, SB_PROG_ROM_LEN);
        for (uint32_t i = 0; i < 0x20000 && i < ne; i++) prog[i * 2] = even[i];
        for (uint32_t i = 0; i < 0x20000 && i < no; i++) prog[i * 2 + 1] = odd[i];
        sb_prog_rom = prog;
        sb_snd_rom = snd;
        sb_gfx_rom = gfx;
    } else {
        free(prog); free(snd); free(gfx);
    }
    free(even);
    free(odd);
    return ok;
}
