// pp_rom_load.inc - Pole Position / Pole Position II ROMs from storage when
// rom_data.c has none (a stub, or ROMS=external): the same loading and
// decoding as tools/gen_roms_polepos.py. Included by polepos/rom_load.c and
// polepos2/rom_load.c with PP_LOAD_PP2 set to 0 or 1.
#include "rom_data.h"
#include "../../../rom/romfs_arcade.h"

typedef struct { const char *name; uint32_t size, crc; } pprom;
#define END {NULL, 0, 0}

#if PP_LOAD_PP2
static const char *const sets[] = {"polepos2", "polepos", "namco51", "namco52", "namco53", "namco54", NULL};
static const pprom z80_r[] = {{"pp4_9.6h", 0x2000, 0xbcf87004}, {"pp4_10.5h", 0x1000, 0xa9d4c380}, END};
static const pprom sub1e[] = {{"pp4_2.8l", 0x2000, 0x51b9a669}, END}, sub1o[] = {{"pp4_1.8m", 0x2000, 0x3f6ac294}, END};
static const pprom sub2e[] = {{"pp4_6.4l", 0x2000, 0x38d04e0f}, {"pp4_8.3l", 0x1000, 0xef25a2ee}, END};
static const pprom sub2o[] = {{"pp4_5.4m", 0x2000, 0xc3053cae}, {"pp4_7.3m", 0x1000, 0xad1c8994}, END};
static const pprom chars_r[] = {{"pp4_28.1f", 0x2000, 0x280dde7d}, END};
static const pprom tiles_r[] = {{"pp4_29.1e", 0x2000, 0xec3ec6e6}, END};
static const pprom spr_r[] = {{"pp4_25.1n", 0x2000, 0xfd098e65}, {"pp4_26.1m", 0x2000, 0x35ac62b3}, END};
static const struct { uint32_t off; const char *name; uint32_t crc; } big_r[] = {
    {0x0000, "pp1_17.5n", 0x2e134b46}, {0x2000, "pp1_19.4n", 0x43ff83e1}, {0x4000, "pp1_21.3n", 0x5f958eb4},
    {0x6000, "pp4_23.2n", 0x9e056fcd}, {0x8000, "pp1_18.5m", 0x6f9997d2}, {0xa000, "pp1_20.4m", 0xec18075b},
    {0xc000, "pp1_22.3m", 0x1d2f30b1}, {0xe000, "pp4_24.2m", 0x795268cf}, {0, NULL, 0}};
static const pprom proms_r[] = {
    {"pp4-7.8l", 0x100, 0x16d69c31}, {"pp4-8.9l", 0x100, 0x07340311}, {"pp4-9.10l", 0x100, 0x1efc84d7},
    {"pp4-10.2h", 0x100, 0x064d51a0}, {"pp4-11.4d", 0x100, 0x7880c5af}, {"pp1-15.9a", 0x100, 0x2d502464},
    {"pp1-16.10a", 0x100, 0x027aa62c}, {"pp1-17.11a", 0x100, 0x1f8d0df3}, {"pp4-12.3c", 0x400, 0x8b270902},
    {"pp4-6.6m", 0x400, 0x647212b5}, END};
static const pprom engine_r[] = {{"pp4_15.6a", 0x2000, 0x7d93bc1c}, {"pp4_16.5a", 0x2000, 0x7d93bc1c}, END};
static const pprom voice_r[] = {{"pp1_11.2e", 0x2000, 0x45b9bfeb}, {"pp1_13.1e", 0x2000, 0xa4237466},
                                {"pp1_12.2f", 0x2000, 0xa31b4be5}, {"pp1_14.1f", 0x2000, 0x944580f9}, END};
#else
static const char *const sets[] = {"polepos", "namco51", "namco52", "namco53", "namco54", NULL};
static const pprom z80_r[] = {{"pp3_9.6h", 0x2000, 0xc0511173}, {"pp1_10b.5h", 0x1000, 0x7174bcb7}, END};
static const pprom sub1e[] = {{"pp3_2.8l", 0x2000, 0xfafb9049}, END}, sub1o[] = {{"pp3_1.8m", 0x2000, 0x65c1c2c2}, END};
static const pprom sub2e[] = {{"pp3_6.4l", 0x2000, 0xacc1ebc3}, END}, sub2o[] = {{"pp3_5.4m", 0x2000, 0x46e5c99a}, END};
static const pprom chars_r[] = {{"pp3_28.1f", 0x1000, 0x2e77187e}, END};
static const pprom tiles_r[] = {{"pp1_29.1e", 0x1000, 0x706e888a}, END};
static const pprom spr_r[] = {{"pp3_25.1n", 0x2000, 0xb52c086b}, {"pp3_26.1m", 0x2000, 0xd24a5707}, END};
static const struct { uint32_t off; const char *name; uint32_t crc; } big_r[] = {
    {0x0000, "pp1_17.5n", 0x2e134b46}, {0x2000, "pp1_19.4n", 0x43ff83e1}, {0x4000, "pp1_21.3n", 0x5f958eb4},
    {0x8000, "pp1_18.5m", 0x6f9997d2}, {0xa000, "pp1_20.4m", 0xec18075b}, {0xc000, "pp1_22.3m", 0x1d2f30b1},
    {0, NULL, 0}};
static const pprom proms_r[] = {
    {"pp1-7.8l", 0x100, 0xf07ff2ad}, {"pp1-8.9l", 0x100, 0xadbde7d7}, {"pp1-9.10l", 0x100, 0xddac786a},
    {"pp2-10.2h", 0x100, 0x1e8d0491}, {"pp1-11.4d", 0x100, 0x0e4fe8a0}, {"pp1-15.9a", 0x100, 0x2d502464},
    {"pp1-16.10a", 0x100, 0x027aa62c}, {"pp1-17.11a", 0x100, 0x1f8d0df3}, {"pp1-12.3c", 0x400, 0x7afc7cfc},
    {"pp3-6.6m", 0x400, 0x63fb6057}, END};
static const pprom engine_r[] = {{"pp1_15.6a", 0x2000, 0xb5ad4d5f}, {"pp1_16.5a", 0x2000, 0x8fdd2f6f}, END};
static const pprom voice_r[] = {{"pp2_11.2e", 0x2000, 0x5b4cf05e}, {"pp2_12.2f", 0x2000, 0x32b694c2},
                                {"pp2_13.1e", 0x2000, 0x8842138a}, END};
#endif
static const pprom road_r[] = {{"pp1_30.3a", 0x2000, 0xee6b3315}, {"pp1_31.2a", 0x2000, 0x6d1e7042},
                               {"pp1_32.1a", 0x1000, 0x4e97f101}, END};
static const pprom scale_r[] = {{"pp1_27.1l", 0x1000, 0xa61bff15}, END};
static const pprom wave_r[] = {{"pp1-5.3b", 0x100, 0x8568decc}, END};
static const pprom n51_r[] = {{"51xx.bin", 0x400, 0xc2f57ef8}, END};
static const pprom n52_r[] = {{"52xx.bin", 0x400, 0x3257d11e}, END};
static const pprom n53_r[] = {{"53xx.bin", 0x400, 0xb326fecb}, END};
static const pprom n54_r[] = {{"54xx.bin", 0x400, 0xee7357e0}, END};

static uint32_t total(const pprom *r)
{
    uint32_t n = 0;
    for (; r->name; r++) n += r->size;
    return n;
}

// The files one after the other (malloc'd, *len bytes), NULL if one is missing.
static uint8_t *cat(const pprom *r, uint32_t *len)
{
    uint32_t n = total(r), at = 0;
    uint8_t *b = (uint8_t *)malloc(n ? n : 1);
    for (; b && r->name; r++) {
        if (!ra_into(sets, r->name, r->size, r->crc, b + at)) { free(b); return NULL; }
        at += r->size;
    }
    if (len) *len = n;
    return b;
}

// The Z8002s' program: even and odd bytes, padded to 32 KB.
static uint8_t *interleave(const pprom *even, const pprom *odd)
{
    uint32_t ne = 0, no = 0;
    uint8_t *e = cat(even, &ne), *o = cat(odd, &no), *out = (uint8_t *)calloc(1, 0x8000);
    bool ok = e && o && out && ne == no && 2 * ne <= 0x8000;
    for (uint32_t i = 0; ok && i < ne; i++) {
        out[2 * i] = e[i];
        out[2 * i + 1] = o[i];
    }
    free(e);
    free(o);
    if (!ok) { free(out); return NULL; }
    return out;
}

// MAME's gfx_layout decoding to one byte a pixel (first plane: top bit).
static uint8_t *decode(const uint8_t *rom, uint32_t len, int count, int w, int h, const uint32_t *planes, int np,
                       const uint32_t *xo, const uint32_t *yo, uint32_t inc)
{
    uint8_t *out = (uint8_t *)malloc((size_t)count * w * h), *p = out;
    for (int n = 0; out && n < count; n++)
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                unsigned v = 0;
                for (int k = 0; k < np; k++) v = (v << 1) | ra_bit(rom, len, n * inc + planes[k] + yo[y] + xo[x]);
                *p++ = (uint8_t)v;
            }
    return out;
}

bool pp_roms_get(pp_roms *r)
{
    if (pp_rom_data(r)) return true;
    memset(r, 0, sizeof *r);
    uint32_t z80n = 0, chn = 0, tin = 0, spn = 0, engn = 0, voin = 0;
    uint8_t *z80 = cat(z80_r, &z80n), *sub1 = interleave(sub1e, sub1o), *sub2 = interleave(sub2e, sub2o);
    uint8_t *chars_rom = cat(chars_r, &chn), *tiles_rom = cat(tiles_r, &tin), *spr_rom = cat(spr_r, &spn);
    uint8_t *big = (uint8_t *)calloc(1, 0x10000);
    bool ok = z80 && sub1 && sub2 && chars_rom && tiles_rom && spr_rom && big;
    for (int i = 0; ok && big_r[i].name; i++) ok = ra_into(sets, big_r[i].name, 0x2000, big_r[i].crc, big + big_r[i].off);
    uint8_t *proms = ok ? cat(proms_r, NULL) : NULL, *engine = ok ? cat(engine_r, &engn) : NULL;
    uint8_t *voice = ok ? cat(voice_r, &voin) : NULL, *road = ok ? cat(road_r, NULL) : NULL;
    uint8_t *scale = ok ? cat(scale_r, NULL) : NULL, *wave = ok ? cat(wave_r, NULL) : NULL;
    uint8_t *n51 = ok ? cat(n51_r, NULL) : NULL, *n52 = ok ? cat(n52_r, NULL) : NULL;
    uint8_t *n53 = ok ? cat(n53_r, NULL) : NULL, *n54 = ok ? cat(n54_r, NULL) : NULL;
    ok = ok && proms && engine && voice && road && scale && wave && n51 && n52 && n53 && n54;
    romfs_release();
    uint8_t *chars = NULL, *tiles = NULL, *sprites = NULL, *bigs = NULL;
    if (ok) {
        // charlayout: 8x8x2, planes {0, 4}, x {0-3, 64-67}, 16 bytes a character
        static const uint32_t cp[2] = {0, 4}, cx[8] = {0, 1, 2, 3, 64, 65, 66, 67};
        uint32_t cy[8], sy[16], by[32], bx[32];
        for (int i = 0; i < 8; i++) cy[i] = i * 8;
        chars = decode(chars_rom, chn, chn / 16, 8, 8, cp, 2, cx, cy, 128);
        tiles = decode(tiles_rom, tin, tin / 16, 8, 8, cp, 2, cx, cy, 128);
        // small sprites: 16x16x4, planes {0, 4, 0x2000*8, 0x2000*8+4}
        static const uint32_t sp[4] = {0, 4, 0x2000 * 8, 0x2000 * 8 + 4};
        static const uint32_t sx[16] = {0, 1, 2, 3, 8, 9, 10, 11, 16, 17, 18, 19, 24, 25, 26, 27};
        for (int i = 0; i < 16; i++) sy[i] = i * 32;
        sprites = decode(spr_rom, spn, 128, 16, 16, sp, 4, sx, sy, 16 * 32);
        // big sprites: 32x32x4, planes {0, 4, half*8, half*8+4}
        const uint32_t half = 0x10000 / 2, bp[4] = {0, 4, half * 8, half * 8 + 4};
        for (int i = 0; i < 32; i++) {
            bx[i] = (uint32_t)((i / 4) * 8 + (i % 4));
            by[i] = (uint32_t)i * 64;
        }
        bigs = decode(big, 0x10000, (int)(half / 256), 32, 32, bp, 4, bx, by, 32 * 64);
        ok = chars && tiles && sprites && bigs;
    }
    free(chars_rom);
    free(tiles_rom);
    free(spr_rom);
    free(big);
    if (!ok) {
        free(z80); free(sub1); free(sub2); free(proms); free(engine); free(voice); free(road); free(scale);
        free(wave); free(n51); free(n52); free(n53); free(n54); free(chars); free(tiles); free(sprites); free(bigs);
        return false;
    }
    r->pp2 = PP_LOAD_PP2;
    r->z80 = z80; r->z80_size = z80n;
    r->sub1 = sub1; r->sub2 = sub2;
    r->chars = chars; r->nchars = (int)(chn / 16);
    r->tiles = tiles; r->ntiles = (int)(tin / 16);
    r->sprites = sprites; r->bigsprites = bigs; r->nbig = 0x8000 / 256;
    r->road = road; r->scale = scale; r->proms = proms; r->wave = wave;
    r->engine = engine; r->engine_size = engn;
    r->voice = voice; r->voice_size = voin;
    r->n51 = n51; r->n52 = n52; r->n53 = n53; r->n54 = n54;
    return true;
}
