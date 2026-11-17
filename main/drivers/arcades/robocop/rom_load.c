// rom_load.c - the RoboCop ROMs from storage when rom_data.c has none (a
// stub, or ROMS=external): the same loading and decoding as
// tools/gen_roms_robocop.py (MAME set "robocop", World revision 4).
#include "rom_data.h"
#include "../../../rom/romfs_arcade.h"

static const char *const sets[] = {"robocop", NULL};

typedef struct { uint32_t off; const char *name; uint32_t size, crc; } part;

// A region of `size` bytes from its files.
static uint8_t *region(uint32_t size, const part *p, int n)
{
    uint8_t *r = (uint8_t *)calloc(1, size);
    for (int i = 0; r && i < n; i++)
        if (!ra_into(sets, p[i].name, p[i].size, p[i].crc, r + p[i].off)) { free(r); return NULL; }
    return r;
}

// MAME's gfx_layout decoding (planes MSB first, RGN_FRAC(1,4) elements) to
// words of 8 pens (pixel x in bits 4x): w / 8 words a row.
static uint32_t *decode(const uint8_t *rom, uint32_t len, int w, int h, const uint32_t *planes,
                        const uint32_t *xo, const uint32_t *yo, uint32_t inc)
{
    const uint32_t total = (len * 8 / 4) / inc;
    uint32_t *out = (uint32_t *)malloc((size_t)total * h * (w / 8) * sizeof(uint32_t)), *o = out;
    for (uint32_t code = 0; out && code < total; code++)
        for (int y = 0; y < h; y++)
            for (int g = 0; g < w; g += 8) {
                uint32_t v = 0;
                for (int x = 0; x < 8; x++) {
                    unsigned pen = 0;
                    for (int k = 0; k < 4; k++) pen = (pen << 1) | ra_bit(rom, len, code * inc + planes[k] + yo[y] + xo[g + x]);
                    v |= (uint32_t)(pen & 15) << (4 * x);
                }
                *o++ = v;
            }
    return out;
}

static uint32_t *tiles(const uint8_t *rom, uint32_t len)
{
    // tilelayout: planes {1/4, 3/4, 0, 2/4}, x {128-135, 0-7}, y step 8, 32 bytes a tile
    const uint32_t f = len * 8 / 4, planes[4] = {f, 3 * f, 0, 2 * f};
    uint32_t xo[16], yo[16];
    for (int i = 0; i < 8; i++) { xo[i] = 128 + i; xo[8 + i] = i; }
    for (int i = 0; i < 16; i++) yo[i] = i * 8;
    return decode(rom, len, 16, 16, planes, xo, yo, 256);
}

bool rc_roms_get(rc_roms *r)
{
    if (rc_rom_data(r)) return true;
    static const part ch_p[] = {{0, "ep23", 0x10000, 0xa77e4ab1}, {0x10000, "ep22", 0x10000, 0x9fbd6903}};
    static const part t1_p[] = {{0, "ep20", 0x10000, 0x1d8d38b8}, {0x10000, "ep21", 0x10000, 0x187929b2},
                                {0x20000, "ep18", 0x10000, 0xb6580b5e}, {0x30000, "ep19", 0x10000, 0x9bad01c7}};
    static const part t2_p[] = {{0, "ep14", 0x8000, 0xca56ceda}, {0x8000, "ep15", 0x8000, 0xa945269c},
                                {0x10000, "ep16", 0x8000, 0xe7fa4d58}, {0x18000, "ep17", 0x8000, 0x84aae89d}};
    static const part sp_p[] = {{0, "ep07", 0x10000, 0x495d75cf}, {0x10000, "ep06", 0x8000, 0xa2ae32e2},
                                {0x20000, "ep11", 0x10000, 0x62fa425a}, {0x30000, "ep10", 0x8000, 0xcce3bd95},
                                {0x40000, "ep09", 0x10000, 0x11bed656}, {0x50000, "ep08", 0x8000, 0xc45c7b4c},
                                {0x60000, "ep13", 0x10000, 0x8fca9f28}, {0x70000, "ep12", 0x8000, 0x3cd1d0c3}};
    uint8_t *prog = (uint8_t *)calloc(1, 0x40000), *snd = NULL, *mcu = NULL, *oki = NULL;
    uint8_t *chars = NULL, *t1 = NULL, *t2 = NULL, *sp = NULL;
    bool ok = prog &&
              ra_lane(sets, "ep05-4.11c", 0x10000, 0x29c35379, prog, 0) &&
              ra_lane(sets, "ep01-4.11b", 0x10000, 0x77507c69, prog, 1) &&
              ra_lane(sets, "ep04-3", 0x10000, 0x39181778, prog + 0x20000, 0) &&
              ra_lane(sets, "ep00-3", 0x10000, 0xe128541f, prog + 0x20000, 1) &&
              (snd = ra_get(sets, "ep03-3", 0x8000, 0x5b164b24)) != NULL &&
              (mcu = ra_get(sets, "en_24_mb7124e.a2", 0x200, 0xb8e2ca98)) != NULL &&
              (oki = ra_get(sets, "ep02", 0x10000, 0x711ce46f)) != NULL &&
              (chars = region(0x20000, ch_p, 2)) != NULL && (t1 = region(0x40000, t1_p, 4)) != NULL &&
              (t2 = region(0x20000, t2_p, 4)) != NULL && (sp = region(0x80000, sp_p, 8)) != NULL;
    romfs_release();
    uint32_t *chw = NULL, *t1w = NULL, *t2w = NULL, *spw = NULL;
    if (ok) {
        // charlayout: planes {0, 2/4, 1/4, 3/4}, x 0-7, y step 8, 8 bytes a char
        const uint32_t f = 0x20000 * 8 / 4, planes[4] = {0, 2 * f, f, 3 * f};
        uint32_t xo[8], yo[8];
        for (int i = 0; i < 8; i++) { xo[i] = i; yo[i] = i * 8; }
        chw = decode(chars, 0x20000, 8, 8, planes, xo, yo, 64);
        t1w = tiles(t1, 0x40000);
        t2w = tiles(t2, 0x20000);
        spw = tiles(sp, 0x80000);
        ok = chw && t1w && t2w && spw;
    }
    free(chars); free(t1); free(t2); free(sp);
    if (!ok) {
        free(prog); free(snd); free(mcu); free(oki); free(chw); free(t1w); free(t2w); free(spw);
        return false;
    }
    r->main = prog;
    r->sound = snd;
    r->mcu = mcu;
    r->oki = oki;
    r->chars = chw;
    r->tiles1 = t1w;
    r->tiles2 = t2w;
    r->sprites = spw;
    return true;
}
