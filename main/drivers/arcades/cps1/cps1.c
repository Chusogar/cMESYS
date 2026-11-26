// cps1.c - Capcom CPS-1 (1988-1993) for the common arcade frontends (arc.h):
// C port of dsp-cpp's drivers/arcade/cps1 (from dsp-emulator's cps1_hw.pas).
// A 68000 at 10 or 12 MHz, the CPS-A / CPS-B (layer and priority control,
// the multiplier and the ID check, as each board's B-chip table), three
// scroll layers of 8x8 / 16x16 / 32x32 tiles, sprites and Strider's stars,
// a Z80 with a YM2151 and an OKI M6295, or on the QSound games (Cadillacs
// and Dinosaurs, The Punisher) a Kabuki-encrypted Z80, the QSound and a 93C46.
// ARC_VARIANT, in dsp-cpp's order: 0 ghouls, 1 ffight, 2 kod, 3 sf2,
// 4 strider, 5 3wonders, 6 captcomm, 7 knights, 8 sf2ce, 9 dino,
// 10 punisher, 11 willow, 12 1941, 13 nemo.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m68kdsp/m68kdsp.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../machine/eeprom93c46.h"
#include "../../../sound/ym2151.h"
#include "../../../sound/okim6295dsp.h"
#include "../../../sound/qsoundd.h"

#define GAME ARC_VARIANT
#define G_DINO 9
#define G_PUNISHER 10
#define G_1941 12
#define QSOUND (GAME == G_DINO || GAME == G_PUNISHER)
#define ROTATED (GAME == G_1941)

#define SW 384                                  // the native screen
#define SH 224
#if ROTATED
#define W SH
#define H SW
#else
#define W SW
#define H SH
#endif
#define WORK 512
#define FPS 59.61
#define SCANLINES 262
#define RATE 44100
#define Z80_CLOCK_YM 3579545
#define Z80_CLOCK_QSOUND 8000000
#if GAME == 8 || QSOUND
#define MAIN_CLOCK 12000000
#else
#define MAIN_CLOCK 10000000
#endif
#if QSOUND
#define SOUND_CLOCK Z80_CLOCK_QSOUND
#else
#define SOUND_CLOCK Z80_CLOCK_YM
#endif
#define VRAM_WORDS 0x18000

#if GAME == 0
static const char *const kSets[] = { "ghouls", NULL };
const arc_info arc_game = { "ghouls", "Ghouls'n Ghosts", "Capcom", 1988, W, H, FPS, RATE, kSets, { "DISPARO", "SALTO", NULL }, ARC_JOY_8WAY };
#elif GAME == 1
static const char *const kSets[] = { "ffight", NULL };
const arc_info arc_game = { "ffight", "Final Fight", "Capcom", 1989, W, H, FPS, RATE, kSets, { "ATAQUE", "SALTO", NULL }, ARC_JOY_8WAY };
#elif GAME == 2
static const char *const kSets[] = { "kod", NULL };
const arc_info arc_game = { "kod", "The King of Dragons", "Capcom", 1991, W, H, FPS, RATE, kSets, { "ATAQUE", "SALTO", NULL }, ARC_JOY_8WAY };
#elif GAME == 3
static const char *const kSets[] = { "sf2", NULL };
const arc_info arc_game = { "sf2", "Street Fighter II", "Capcom", 1991, W, H, FPS, RATE, kSets, { "PUNO D.", "PUNO M.", "PUNO F." }, ARC_JOY_8WAY };
#elif GAME == 4
static const char *const kSets[] = { "strider", NULL };
const arc_info arc_game = { "strider", "Strider", "Capcom", 1989, W, H, FPS, RATE, kSets, { "ATAQUE", "SALTO", NULL }, ARC_JOY_8WAY };
#elif GAME == 5
static const char *const kSets[] = { "3wonders", NULL };
const arc_info arc_game = { "3wonders", "Three Wonders", "Capcom", 1991, W, H, FPS, RATE, kSets, { "ATAQUE", "SALTO", NULL }, ARC_JOY_8WAY };
#elif GAME == 6
static const char *const kSets[] = { "captcomm", NULL };
const arc_info arc_game = { "captcomm", "Captain Commando", "Capcom", 1991, W, H, FPS, RATE, kSets, { "ATAQUE", "SALTO", NULL }, ARC_JOY_8WAY };
#elif GAME == 7
static const char *const kSets[] = { "knights", NULL };
const arc_info arc_game = { "knights", "Knights of the Round", "Capcom", 1991, W, H, FPS, RATE, kSets, { "ATAQUE", "SALTO", NULL }, ARC_JOY_8WAY };
#elif GAME == 8
static const char *const kSets[] = { "sf2ce", NULL };
const arc_info arc_game = { "sf2ce", "Street Fighter II' Champion Edition", "Capcom", 1992, W, H, FPS, RATE, kSets, { "PUNO D.", "PUNO M.", "PUNO F." }, ARC_JOY_8WAY };
#elif GAME == 9
static const char *const kSets[] = { "dino", NULL };
const arc_info arc_game = { "dino", "Cadillacs and Dinosaurs", "Capcom", 1993, W, H, FPS, RATE, kSets, { "ATAQUE", "SALTO", NULL }, ARC_JOY_8WAY };
#elif GAME == 10
static const char *const kSets[] = { "punisher", NULL };
const arc_info arc_game = { "punisher", "The Punisher", "Capcom", 1993, W, H, FPS, RATE, kSets, { "ATAQUE", "SALTO", NULL }, ARC_JOY_8WAY };
#elif GAME == 11
static const char *const kSets[] = { "willow", NULL };
const arc_info arc_game = { "willow", "Willow", "Capcom", 1989, W, H, FPS, RATE, kSets, { "ATAQUE", "SALTO", NULL }, ARC_JOY_8WAY };
#elif GAME == 12
static const char *const kSets[] = { "1941", NULL };
const arc_info arc_game = { "1941", "1941: Counter Attack", "Capcom", 1990, W, H, FPS, RATE, kSets, { "DISPARO", "BOMBA", NULL }, ARC_JOY_8WAY };
#else
static const char *const kSets[] = { "nemo", NULL };
const arc_info arc_game = { "nemo", "Nemo", "Capcom", 1990, W, H, FPS, RATE, kSets, { "ATAQUE", "SALTO", NULL }, ARC_JOY_8WAY };
#endif

#define K_SPRITES 1
#define K_SCROLL1 2
#define K_SCROLL2 4
#define K_SCROLL3 8
#define K_STARS 16

typedef struct {
    uint16_t layerctrl, palctrl, testaddr, testval, mula, mulb, mull, mulh;
    uint8_t mask_sc1, mask_sc2, mask_sc3, mask_sc4;
    uint16_t pri_mask1, pri_mask2, pri_mask3, pri_mask4;
} cps_b;

static const cps_b kCpsB[13] = {
    {0x166, 0x170, 0x1ff, 0x0000, 0x1ff, 0x1ff, 0x1ff, 0x1ff, 0x02, 0x04, 0x08, 0x30, 0x168, 0x16a, 0x16c, 0x16e},
    {0x16e, 0x16a, 0x160, 0x0004, 0x1ff, 0x1ff, 0x1ff, 0x1ff, 0x02, 0x04, 0x08, 0x00, 0x166, 0x170, 0x168, 0x172},
    {0x160, 0x170, 0x1ff, 0x0000, 0x15e, 0x15c, 0x15a, 0x158, 0x30, 0x08, 0x30, 0x00, 0x16e, 0x16c, 0x16a, 0x168},
    {0x166, 0x170, 0x172, 0x0401, 0x1ff, 0x1ff, 0x1ff, 0x1ff, 0x08, 0x10, 0x20, 0x00, 0x168, 0x16a, 0x16c, 0x16e},
    {0x168, 0x170, 0x172, 0x0800, 0x14e, 0x14c, 0x14a, 0x148, 0x20, 0x04, 0x08, 0x12, 0x166, 0x164, 0x162, 0x160},
    {0x160, 0x170, 0x1ff, 0x0000, 0x146, 0x144, 0x142, 0x140, 0x20, 0x12, 0x12, 0x00, 0x16e, 0x16c, 0x16a, 0x168},
    {0x168, 0x170, 0x1ff, 0x0000, 0x146, 0x144, 0x142, 0x140, 0x20, 0x10, 0x82, 0x00, 0x166, 0x164, 0x162, 0x160},
    {0x166, 0x170, 0x172, 0x0000, 0x140, 0x142, 0x144, 0x146, 0x02, 0x04, 0x08, 0x30, 0x168, 0x16a, 0x16c, 0x16e},
    {0x14a, 0x144, 0x1ff, 0x0000, 0x1ff, 0x1ff, 0x1ff, 0x1ff, 0x16, 0x16, 0x16, 0x00, 0x14c, 0x14e, 0x140, 0x142},
    {0x152, 0x14c, 0x14e, 0x0c00, 0x1ff, 0x1ff, 0x1ff, 0x1ff, 0x04, 0x02, 0x20, 0x00, 0x154, 0x156, 0x148, 0x14a},
    {0x170, 0x166, 0x164, 0x0003, 0x1ff, 0x1ff, 0x1ff, 0x1ff, 0x20, 0x10, 0x08, 0x00, 0x16e, 0x16c, 0x16a, 0x168},
    {0x168, 0x172, 0x160, 0x0005, 0x1ff, 0x1ff, 0x1ff, 0x1ff, 0x02, 0x08, 0x20, 0x14, 0x16a, 0x16c, 0x16e, 0x170},
    {0x142, 0x14c, 0x14e, 0x0405, 0x1ff, 0x1ff, 0x1ff, 0x1ff, 0x04, 0x02, 0x20, 0x00, 0x144, 0x146, 0x148, 0x14a},
};

typedef struct { uint8_t type; uint32_t start, end; uint8_t num_bank; } bank_range;
typedef struct { uint32_t lbank[4]; bank_range ranges[7]; } bank_map;

static const bank_map kBanks[14] = {
    {{0x8000, 0x2000, 0x2000, 0}, {{K_SCROLL3, 0x8000, 0xbfff, 1}, {K_SPRITES, 0x2000, 0x3fff, 2},
                                   {K_STARS | K_SPRITES | K_SCROLL1 | K_SCROLL2 | K_SCROLL3, 0, 0x1ffff, 0}}},   // DM620
    {{0x8000, 0, 0, 0}, {{K_SPRITES, 0x0000, 0x43ff, 0}, {K_SCROLL1, 0x4400, 0x4bff, 0}, {K_SCROLL3, 0x4c00, 0x5fff, 0},
                         {K_SCROLL2, 0x6000, 0x7fff, 0}}},   // S224B
    {{0x8000, 0x8000, 0, 0}, {{K_SPRITES, 0x0000, 0x7fff, 0}, {K_SPRITES, 0x8000, 0x8fff, 1}, {K_SCROLL2, 0x9000, 0xbfff, 1},
                              {K_SCROLL1, 0xc000, 0xd7ff, 1}, {K_SCROLL3, 0xd800, 0xffff, 1}}},   // KD29B
    {{0x8000, 0x8000, 0x8000, 0}, {{K_SPRITES, 0x0000, 0x7fff, 0}, {K_SPRITES, 0x8000, 0xffff, 1}, {K_SPRITES, 0x10000, 0x11fff, 2},
                                   {K_SCROLL3, 0x2000, 0x3fff, 2}, {K_SCROLL1, 0x4000, 0x4fff, 2}, {K_SCROLL2, 0x5000, 0x7fff, 2}}},   // STF29
    {{0x8000, 0x8000, 0, 0}, {{K_STARS, 0x0000, 0x03ff, 0}, {K_SPRITES, 0x0000, 0x4fff, 0}, {K_SCROLL2, 0x4000, 0x7fff, 0},
                              {K_SCROLL3, 0x0000, 0x7fff, 1}, {K_SCROLL1, 0x7000, 0x7fff, 1}}},   // ST24M1
    {{0x8000, 0x8000, 0, 0}, {{K_SPRITES, 0x0000, 0x53ff, 0}, {K_SCROLL1, 0x5400, 0x6fff, 0}, {K_SCROLL3, 0x7000, 0x7fff, 0},
                              {K_SCROLL3, 0x0000, 0x3fff, 1}, {K_SCROLL2, 0x2800, 0x7fff, 1}, {K_SPRITES, 0x5400, 0x7fff, 1}}},   // RT24B
    {{0x8000, 0x8000, 0, 0}, {{K_SPRITES, 0x0000, 0x7fff, 0}, {K_SCROLL2, 0x0000, 0x7fff, 0}, {K_SPRITES, 0x8000, 0xffff, 1},
                              {K_SCROLL1, 0x8000, 0xffff, 1}, {K_SCROLL2, 0x8000, 0xffff, 1}, {K_SCROLL3, 0x8000, 0xffff, 1}}},   // CC63B
    {{0x8000, 0x8000, 0, 0}, {{K_SPRITES, 0x0000, 0x7fff, 0}, {K_SCROLL2, 0x0000, 0x7fff, 0}, {K_SCROLL1, 0x8000, 0x9fff, 1},
                              {K_SPRITES, 0x8000, 0xcfff, 1}, {K_SCROLL2, 0x8000, 0xcfff, 1}, {K_SCROLL3, 0xd000, 0xffff, 1}}},   // KR63B
    {{0x8000, 0x8000, 0x8000, 0}, {{K_SPRITES, 0x0000, 0x7fff, 0}, {K_SPRITES, 0x8000, 0xffff, 1}, {K_SPRITES, 0x10000, 0x11fff, 2},
                                   {K_SCROLL3, 0x2000, 0x3fff, 2}, {K_SCROLL1, 0x4000, 0x4fff, 2}, {K_SCROLL2, 0x5000, 0x7fff, 2}}},   // S9263B
    {{0x8000, 0x8000, 0, 0}, {{K_SCROLL1, 0x0000, 0x0fff, 0}, {K_SPRITES, 0x1000, 0x7fff, 0},
                              {K_SPRITES | K_SCROLL2, 0x8000, 0xdfff, 1}, {K_SCROLL3, 0xe000, 0xffff, 1}}},   // CD63B
    {{0x8000, 0x8000, 0, 0}, {{K_SCROLL1, 0x0000, 0x0fff, 0}, {K_SPRITES, 0x1000, 0x7fff, 0},
                              {K_SPRITES | K_SCROLL2, 0x8000, 0xdbff, 1}, {K_SCROLL3, 0xdc00, 0xffff, 1}}},   // PS63B
    {{0x8000, 0x4000, 0, 0}, {{K_SPRITES, 0x0000, 0x4fff, 0}, {K_SCROLL3, 0x5000, 0x6fff, 0}, {K_SCROLL1, 0x7000, 0x7fff, 0},
                              {K_SCROLL2, 0x0000, 0x3fff, 1}}},   // WL24B
    {{0x8000, 0, 0, 0}, {{K_SPRITES, 0x0000, 0x1fff, 0}, {K_SCROLL3, 0x2000, 0x3fff, 0}, {K_SCROLL1, 0x4000, 0x4fff, 0},
                         {K_SCROLL2, 0x4800, 0x7fff, 0}}},   // YI24B
    {{0x8000, 0, 0, 0}, {{K_SPRITES, 0x0000, 0x3fff, 0}, {K_SCROLL2, 0x0000, 0x3fff, 0}, {K_SCROLL1, 0x4000, 0x47ff, 0},
                         {K_SPRITES, 0x4800, 0x67ff, 0}, {K_SCROLL2, 0x4800, 0x67ff, 0}, {K_SCROLL3, 0x6800, 0x7fff, 0}}},   // NM24B
};

enum { L16B, LRAW, LSWAP, L64B, L64BB };
enum { D_CPU, D_SOUND, D_OKI, D_QS, D_GFX };
typedef struct { uint8_t mode, dest; arc_rom rom; } cps_load;
typedef struct {
    const cps_load *loads;
    int nloads, nbank, cps_b_index;
    uint8_t dsw_a, dsw_b, dsw_c;
    int chars, tiles16, tiles32;
    uint32_t gfx_size, sound_size;
    int stars;
} cps_game;
#include "cps1_roms.h"

static struct state {
    m68kd cpu;
    zd_z80 snd;
    ym2151 ym;
    okim6295d oki;
    qsoundd qs;
    eeprom93c46_t eeprom;
    const cps_b *b;
    const bank_map *banks;
    uint16_t *rom;                              // 0x100000 words
    uint16_t ram[0x8000], vram[VRAM_WORDS], sprite_buffer[0x400];
    uint8_t sound_rom[0x8000], sound_ram[0x800], snd_bank[16][0x4000], qram1[0x1000], qram2[0x1000];
    uint8_t qsnd_opcode[0x8000], qsnd_data[0x8000], stars[0x8000];
    uint8_t *oki_rom, *qs_rom;
    uint8_t *chars0, *chars1, *tiles16, *tiles32;
    int nchars, ntiles16, ntiles32;
    uint32_t palette[0xc00];
    bool trans_alt[4][16];
    uint32_t *scroll1, *scroll2, *scroll3, *priority, *composite;
    uint16_t fb[W * H];
    uint16_t in0, in1, in2;
    uint8_t dsw_a, dsw_b, dsw_c, sound_latch, sound_latch2, sound_bank;
    uint32_t sprites_base, scroll1_base, scroll2_base, scroll3_base, rowscroll_base, pal_base;
    uint16_t rowscrollstart, mula, mulb, layer, palctrl, pri_mask[4], video_control;
    uint16_t scroll_x1, scroll_y1, scroll_x2, scroll_y2, scroll_x3, scroll_y3;
    uint16_t stars_x1, stars_y1, stars_x2, stars_y2, scroll_pri_x, scroll_pri_y;
    uint32_t frame;
    bool stars_enabled, sprites_pri_draw, rowscroll_ena, flip_screen;
    int64_t audio_acc, oki_acc, qsound_acc, qsound_irq_acc;
    int32_t last_oki;
    int16_t audio[4096];
    int naudio;
} *S_;
#define S (*S_)

static inline uint32_t vram_index(uint32_t word) { return word % VRAM_WORDS; }
static inline uint32_t vram_index_from_byte(uint32_t byte_addr) { return (byte_addr >> 1) % VRAM_WORDS; }

// ---------------------------------------------------------------------------
// Kabuki (the QSound Z80's encryption)

#if QSOUND
static int bitswap1(int src, int key, int select)
{
    if (select & (1 << ((key >> 0) & 7))) src = (src & 0xfc) | ((src & 0x01) << 1) | ((src & 0x02) >> 1);
    if (select & (1 << ((key >> 4) & 7))) src = (src & 0xf3) | ((src & 0x04) << 1) | ((src & 0x08) >> 1);
    if (select & (1 << ((key >> 8) & 7))) src = (src & 0xcf) | ((src & 0x10) << 1) | ((src & 0x20) >> 1);
    if (select & (1 << ((key >> 12) & 7))) src = (src & 0x3f) | ((src & 0x40) << 1) | ((src & 0x80) >> 1);
    return src;
}

static int bitswap2(int src, int key, int select)
{
    if (select & (1 << ((key >> 12) & 7))) src = (src & 0xfc) | ((src & 0x01) << 1) | ((src & 0x02) >> 1);
    if (select & (1 << ((key >> 8) & 7))) src = (src & 0xf3) | ((src & 0x04) << 1) | ((src & 0x08) >> 1);
    if (select & (1 << ((key >> 4) & 7))) src = (src & 0xcf) | ((src & 0x10) << 1) | ((src & 0x20) >> 1);
    if (select & (1 << ((key >> 0) & 7))) src = (src & 0x3f) | ((src & 0x40) << 1) | ((src & 0x80) >> 1);
    return src;
}

static int bytedecode(int src, uint32_t k1, uint32_t k2, int xor_key, int select)
{
    src = bitswap1(src, (int)(k1 & 0xffff), select & 0xff);
    src = ((src & 0x7f) << 1) | ((src & 0x80) >> 7);
    src = bitswap2(src, (int)(k1 >> 16), select & 0xff);
    src ^= xor_key;
    src = ((src & 0x7f) << 1) | ((src & 0x80) >> 7);
    src = bitswap2(src, (int)(k2 & 0xffff), select >> 8);
    src = ((src & 0x7f) << 1) | ((src & 0x80) >> 7);
    src = bitswap1(src, (int)(k2 >> 16), select >> 8);
    return src;
}

static void kabuki_decode(const uint8_t *src, uint8_t *op, uint8_t *data, int length, uint32_t k1, uint32_t k2,
                          int addr_key, int xor_key)
{
    for (int a = 0; a < length; a++) {
        op[a] = (uint8_t)bytedecode(src[a], k1, k2, xor_key, a + addr_key);
        data[a] = (uint8_t)bytedecode(src[a], k1, k2, xor_key, (a ^ 0x1fc0) + addr_key + 1);
    }
}
#endif

// ---------------------------------------------------------------------------
// CPS-A / CPS-B

static void calc_mask(uint16_t mask, int index)
{
    for (int pen = 0; pen < 16; pen++) S.trans_alt[index][pen] = ((mask >> pen) & 1) == 0;
}

static void pal_calc(void)
{
    int pos_buf = 0;
    for (int page = 0; page < 6; page++) {
        if (S.palctrl & (1 << page)) {
            for (int offset = 0; offset < 0x200; offset++) {
                const uint16_t pal = S.vram[vram_index_from_byte((uint32_t)pos_buf * 2u + S.pal_base)];
                const int bright = 0xf + (int)((pal >> 12) << 1);
                int r = (((pal >> 8) & 0xf) * 0x11 * bright) / 0x2d;
                int g = (((pal >> 4) & 0xf) * 0x11 * bright) / 0x2d;
                int b = (((pal >> 0) & 0xf) * 0x11 * bright) / 0x2d;
                if (r > 255) r = 255;
                if (g > 255) g = 255;
                if (b > 255) b = 255;
                S.palette[page * 0x200 + offset] = 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
                pos_buf++;
            }
        } else if (pos_buf != 0) pos_buf += 0x200;
    }
}

static uint16_t read_io(uint16_t dir)
{
    uint16_t res = 0xffff;
    switch (dir) {
    case 0: case 2: case 4: case 6: res = S.in1; break;
    case 0x18: res = S.in0; break;
    case 0x1a: res = (uint16_t)((S.dsw_a << 8) | 0xff); break;
    case 0x1c: res = (uint16_t)((S.dsw_b << 8) | 0xff); break;
    case 0x1e: res = (uint16_t)((S.dsw_c << 8) | 0xff); break;
    case 0x176: res = S.in2; break;
    default: break;
    }
    if (dir == S.b->testaddr) res = S.b->testval;
    const uint32_t product = (uint32_t)S.mula * (uint32_t)S.mulb;
    if (dir == S.b->mull) res = (uint16_t)product;
    if (dir == S.b->mulh) res = (uint16_t)(product >> 16);
    return res;
}

static inline uint32_t object_base(uint16_t v) { return (uint32_t)v * 256u - 0x900000u; }

static void write_io(uint16_t dir, uint16_t value)
{
    switch (dir) {
    case 0x100: S.sprites_base = (object_base(value) & ~0x7ffu) >> 1; break;
    case 0x102: S.scroll1_base = object_base(value) & ~0x3fffu; break;
    case 0x104: S.scroll2_base = object_base(value) & ~0x3fffu; break;
    case 0x106: S.scroll3_base = object_base(value) & ~0x3fffu; break;
    case 0x108: S.rowscroll_base = (object_base(value) & ~0x7ffu) >> 1; break;
    case 0x10a:                                 // (the CPS-B takes the palette only when written)
        S.pal_base = object_base(value) & 0x3ffff;
        S.pal_base &= ~0x3ffu;
        pal_calc();
        break;
    case 0x10c: S.scroll_x1 = value & 0x1ff; break;
    case 0x10e: S.scroll_y1 = value & 0x1ff; break;
    case 0x110: S.scroll_x2 = value & 0x3ff; break;
    case 0x112: S.scroll_y2 = value & 0x3ff; break;
    case 0x114: S.scroll_x3 = value & 0x7ff; break;
    case 0x116: S.scroll_y3 = value & 0x7ff; break;
    case 0x118: S.stars_x1 = value; break;
    case 0x11a: S.stars_y1 = value; break;
    case 0x11c: S.stars_x2 = value; break;
    case 0x11e: S.stars_y2 = value; break;
    case 0x120: S.rowscrollstart = value & 0x7ff; break;
    case 0x122:
        S.video_control = value;
        S.flip_screen = (value & 0x8000) != 0;
        S.rowscroll_ena = (value & 1) != 0;
        break;
    case 0x180: case 0x182: case 0x184: case 0x186: S.sound_latch = (uint8_t)value; break;
    case 0x188: case 0x18a: case 0x18c: case 0x18e: S.sound_latch2 = (uint8_t)value; break;
    default: break;
    }
    if (dir == S.b->palctrl) S.palctrl = value;
    if (dir == S.b->mula) S.mula = value;
    if (dir == S.b->mulb) S.mulb = value;
    if (dir == S.b->layerctrl) S.layer = value;
    const uint16_t pm[4] = {S.b->pri_mask1, S.b->pri_mask2, S.b->pri_mask3, S.b->pri_mask4};
    for (int i = 0; i < 4; i++)
        if (dir == pm[i] && S.pri_mask[i] != value) {
            calc_mask(value, i);
            S.pri_mask[i] = value;
        }
}

// ---------------------------------------------------------------------------
// The 68000

static uint16_t main_read(uint32_t a)
{
    a &= 0xffffff;
#if QSOUND
    if (a <= 0x17ffff) return S.rom[(a >> 1) & 0xfffff];
    if (a >= 0x800000 && a <= 0x8001ff) return read_io((uint16_t)(a & 0x1fe));
    if (a >= 0x900000 && a <= 0x92ffff) return S.vram[(a & 0x3ffff) >> 1];
    if (a >= 0xf18000 && a <= 0xf19fff) return (uint16_t)(0xff00 | S.qram1[(a >> 1) & 0xfff]);
    if (a == 0xf1c000 || a == 0xf1c002) return 0xff;
    if (a == 0xf1c006) return eeprom93c46_do_read(&S.eeprom);
    if (a >= 0xf1e000 && a <= 0xf1ffff) return (uint16_t)(0xff00 | S.qram2[(a >> 1) & 0xfff]);
    if (a >= 0xff0000) return S.ram[(a & 0xffff) >> 1];
    return 0xffff;
#else
    if (a <= 0x3fffff) return S.rom[(a >> 1) & 0xfffff];
    if (a >= 0x800000 && a <= 0x8001ff) return read_io((uint16_t)(a & 0x1fe));
    if (a >= 0x900000 && a <= 0x92ffff) return S.vram[(a & 0x3ffff) >> 1];
    if (a >= 0xff0000) return S.ram[(a & 0xffff) >> 1];
    return 0xffff;
#endif
}

static void main_write(uint32_t a, uint16_t v)
{
    a &= 0xffffff;
    if (a >= 0x800000 && a <= 0x8001ff) {
        write_io((uint16_t)(a & 0x1fe), v);
        return;
    }
    if (a >= 0x900000 && a <= 0x92ffff) {
        S.vram[(a & 0x3ffff) >> 1] = v;
        return;
    }
#if QSOUND
    if (a >= 0xf18000 && a <= 0xf19fff) {
        S.qram1[(a >> 1) & 0xfff] = (uint8_t)v;
        return;
    }
    if (a == 0xf1c006) {
        eeprom93c46_di_write(&S.eeprom, (uint8_t)(v & 1));
        eeprom93c46_clk_write(&S.eeprom, (uint8_t)((v >> 6) & 1));
        eeprom93c46_cs_write(&S.eeprom, (uint8_t)((v >> 7) & 1));
        return;
    }
    if (a >= 0xf1e000 && a <= 0xf1ffff) {
        S.qram2[(a >> 1) & 0xfff] = (uint8_t)v;
        return;
    }
#endif
    if (a >= 0xff0000) S.ram[(a & 0xffff) >> 1] = v;
}

static uint16_t cpu_rd(void *u, uint32_t a) { (void)u; return main_read(a); }
static void cpu_wr(void *u, uint32_t a, uint16_t v) { (void)u; main_write(a, v); }

// ---------------------------------------------------------------------------
// The Z80

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
#if QSOUND
    if (a <= 0x7fff) return S.qsnd_data[a];
    if (a <= 0xbfff) return S.snd_bank[S.sound_bank & 0xf][a & 0x3fff];
    if (a <= 0xcfff) return S.qram1[a & 0xfff];
    if (a == 0xd007) return qsoundd_read(&S.qs);
    if (a >= 0xf000) return S.qram2[a & 0xfff];
    return 0xff;
#else
    if (a <= 0x7fff) return S.sound_rom[a];
    if (a <= 0xbfff) return S.snd_bank[S.sound_bank & 1][a & 0x3fff];
    if (a >= 0xd000 && a <= 0xd7ff) return S.sound_ram[a & 0x7ff];
    if (a == 0xf001) return ym2151_status(&S.ym);
    if (a == 0xf002) return okid_read(&S.oki);
    if (a == 0xf008) return S.sound_latch;
    if (a == 0xf00a) return S.sound_latch2;
    return 0xff;
#endif
}

#if QSOUND
static uint8_t sound_opcode(void *u, uint16_t a) { return a <= 0x7fff ? S.qsnd_opcode[a] : sound_read(u, a); }
#endif

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
#if QSOUND
    if (a >= 0xc000 && a <= 0xcfff) S.qram1[a & 0xfff] = v;
    else if (a >= 0xd000 && a <= 0xd002) qsoundd_write(&S.qs, (uint8_t)(a & 3), v);
    else if (a == 0xd003) S.sound_bank = v & 0xf;
    else if (a >= 0xf000) S.qram2[a & 0xfff] = v;
#else
    if (a >= 0xd000 && a <= 0xd7ff) S.sound_ram[a & 0x7ff] = v;
    else if (a == 0xf000) ym2151_select(&S.ym, v);
    else if (a == 0xf001) ym2151_write(&S.ym, v);
    else if (a == 0xf002) okid_write(&S.oki, v);
    else if (a == 0xf004) S.sound_bank = v & 1;
    else if (a == 0xf006) S.oki.divisor = (v & 1) ? 132 : 165;   // (pin 7)
#endif
}

static void sound_cycles(void *u, int cycles)
{
    (void)u;
#if QSOUND
    S.qsound_irq_acc += cycles * 250;
    while (S.qsound_irq_acc >= SOUND_CLOCK) {
        S.qsound_irq_acc -= SOUND_CLOCK;
        zd_set_irq(&S.snd, ZD_HOLD, 0xff);
    }
    S.qsound_acc += (int64_t)cycles * (4000000 / QSOUND_CLOCK_DIV);
    while (S.qsound_acc >= SOUND_CLOCK) {
        S.qsound_acc -= SOUND_CLOCK;
        qsoundd_clock(&S.qs);
    }
    S.audio_acc += (int64_t)cycles * RATE;
    while (S.audio_acc >= SOUND_CLOCK) {
        S.audio_acc -= SOUND_CLOCK;
        if (S.naudio < 4096) S.audio[S.naudio++] = arc_clamp16(qsoundd_mixed(&S.qs));
    }
#else
    ym2151_run_timers(&S.ym, (int)((int64_t)cycles * Z80_CLOCK_YM / SOUND_CLOCK));
    S.oki_acc += (int64_t)cycles * okid_sample_frequency(&S.oki);
    while (S.oki_acc >= SOUND_CLOCK) {
        S.oki_acc -= SOUND_CLOCK;
        S.last_oki = okid_update(&S.oki);
    }
    S.audio_acc += (int64_t)cycles * RATE;
    while (S.audio_acc >= SOUND_CLOCK) {
        S.audio_acc -= SOUND_CLOCK;
        const int32_t s = ym2151_update(&S.ym) + (int32_t)((float)S.last_oki * 0.8f);
        if (S.naudio < 4096) S.audio[S.naudio++] = arc_clamp16(s);
    }
#endif
}

#if !QSOUND
static void ym_irq(void *u, bool on) { (void)u; zd_set_irq(&S.snd, on ? ZD_ASSERT : ZD_CLEAR, 0xff); }
#endif

// ---------------------------------------------------------------------------
// Video

static int gfx_bank(uint8_t type, uint16_t nchar)
{
    const bank_map *map = S.banks;
    const int shift = type == K_SCROLL1 ? 0 : type == K_SCROLL3 ? 3 : 1;
    const uint32_t code = (uint32_t)nchar << shift;
    for (int r = 0; r < 7; r++) {
        const bank_range *range = &map->ranges[r];
        if (range->type == 0) break;
        if (code < range->start || code > range->end) continue;
        if ((range->type & type) == 0) continue;
        uint32_t base = 0;
        for (int i = 0; i < range->num_bank; i++) base += map->lbank[i];
        const uint32_t size = map->lbank[range->num_bank];
        const uint32_t masked = size != 0 ? (code & (size - 1)) : code;
        return (int)((base + masked) >> shift);
    }
    return -1;
}

static void draw_tile(const uint8_t *set, int total, int size, uint32_t *dest, int dw, int dh, int x, int y, int code,
                      int color, bool flipx, bool flipy, const bool *trans_alt)
{
    const uint8_t *pixels = set + (size_t)(code % total) * (size_t)(size * size);
    for (int row = 0; row < size; row++) {
        const int sy = flipy ? size - 1 - row : row, dy = y + row;
        if (dy < 0 || dy >= dh) continue;
        for (int col = 0; col < size; col++) {
            const int sx = flipx ? size - 1 - col : col;
            const uint8_t pen = pixels[sy * size + sx];
            if (pen == 15) continue;
            if (trans_alt && trans_alt[pen]) continue;
            const int dx = x + col;
            if (dx < 0 || dx >= dw) continue;
            dest[(size_t)dy * (size_t)dw + (size_t)dx] = S.palette[(size_t)(color + pen) % 0xc00];
        }
    }
}

static void clear_tile(uint32_t *dest, int dw, int dh, int x, int y, int size)
{
    for (int row = 0; row < size; row++) {
        const int dy = y + row;
        if (dy < 0 || dy >= dh) continue;
        for (int col = 0; col < size; col++) {
            const int dx = x + col;
            if (dx < 0 || dx >= dw) continue;
            dest[(size_t)dy * (size_t)dw + (size_t)dx] = 0;
        }
    }
}

static void blit_scrolled(const uint32_t *src, int sw, int sh, int scroll_x, int scroll_y)
{
    for (int y = 0; y < WORK; y++) {            // (the sizes are powers of 2: & is dsp-cpp's wrapped %)
        const int sy = (y + scroll_y) & (sh - 1);
        const uint32_t *row = src + (size_t)sy * (size_t)sw;
        uint32_t *out = S.composite + y * WORK;
        for (int x = 0; x < WORK; x++) {
            const uint32_t pixel = row[(x + scroll_x) & (sw - 1)];
            if (pixel != 0) out[x] = pixel;
        }
    }
}

static void blit_rowscroll(void)
{
    for (int y = 0; y < WORK; y++) {
        const int sy = (y + (int)S.scroll_y2) & 1023;
        const uint32_t index = vram_index(S.rowscroll_base + (((uint32_t)y + S.rowscrollstart) & 0x3ff));
        const int xoff = (int)S.scroll_x2 + (int)S.vram[index];
        for (int x = 0; x < WORK; x++) {
            const uint32_t pixel = S.scroll2[sy * 1024 + ((x + xoff) & 1023)];
            if (pixel != 0) S.composite[y * WORK + x] = pixel;
        }
    }
}

static void blit_sprite(int code, int pal_base, bool flipx, bool flipy, int x, int y)
{
    const uint8_t *pixels = S.tiles16 + (size_t)(code % S.ntiles16) * 256;
    for (int row = 0; row < 16; row++) {
        const int sy = flipy ? 15 - row : row, dy = (y + row) & 0x1ff;
        for (int px = 0; px < 16; px++) {
            const int sx = flipx ? 15 - px : px;
            const uint8_t pen = pixels[sy * 16 + sx];
            if (pen == 15) continue;
            S.composite[dy * WORK + ((x + px) & 0x1ff)] = S.palette[(size_t)(pal_base + pen) % 0xc00];
        }
    }
}

static void draw_sprites(void)
{
    int last = 0xff;
    for (int f = 0; f <= 0xff; f++)
        if ((S.sprite_buffer[f * 4 + 3] & 0xff00) == 0xff00) {
            last = f - 1;
            break;
        }
    for (int f = last; f >= 0; f--) {
        const int nchar = gfx_bank(K_SPRITES, S.sprite_buffer[f * 4 + 2]);
        if (nchar < 0) continue;
        const uint16_t color = S.sprite_buffer[f * 4 + 3];
        const int x = S.sprite_buffer[f * 4 + 0] & 0x1ff, y = S.sprite_buffer[f * 4 + 1] & 0x1ff;
        const int pal_base = (color & 0x1f) << 4;
        const bool flipx = (color & 0x20) != 0, flipy = (color & 0x40) != 0;
        if ((color & 0xff00) == 0) {
            blit_sprite(nchar, pal_base, flipx, flipy, x, y);
            continue;
        }
        const int nx = ((color >> 8) & 0xf) + 1, ny = ((color >> 12) & 0xf) + 1;
        for (int nys = 0; nys < ny; nys++) {
            const int sy = (y + nys * 16) & 0x1ff;
            for (int nxs = 0; nxs < nx; nxs++) {
                const int sx = (x + nxs * 16) & 0x1ff;
                const int col = flipx ? ((nchar + (nx - 1) - nxs) & 0xf) : ((nchar + nxs) & 0xf);
                const int code = (nchar & ~0xf) + col + 0x10 * (flipy ? (ny - 1 - nys) : nys);
                blit_sprite(code, pal_base, flipx, flipy, sx, sy);
            }
        }
    }
    if (S.sprites_pri_draw) {
        blit_scrolled(S.priority, 2048, 2048, S.scroll_pri_x, S.scroll_pri_y);
        S.sprites_pri_draw = false;
    }
}

static void fill32(uint32_t *p, size_t n) { memset(p, 0, n * 4); }

static void draw_layer(int nlayer, bool sprite_next)
{
    if (nlayer == 0) {
        draw_sprites();
        return;
    }
    if (nlayer == 1 && (S.layer & S.b->mask_sc1) != 0) {
        fill32(S.scroll1, 512 * 512);
        if (sprite_next) fill32(S.priority, 2048 * 2048);
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++) {
                const int pos = (y & 0x1f) + ((x & 0x3f) << 5) + ((y & 0x20) << 6);
                const uint32_t address = S.scroll1_base + (uint32_t)(pos * 4);
                const uint16_t atrib = S.vram[vram_index_from_byte(address + 2)];
                const int nchar = gfx_bank(K_SCROLL1, S.vram[vram_index_from_byte(address)]);
                if (nchar < 0) {
                    clear_tile(S.scroll1, 512, 512, x * 8, y * 8, 8);
                    if (sprite_next) clear_tile(S.priority, 2048, 2048, x * 8, y * 8, 8);
                    continue;
                }
                const bool flipx = (atrib & 0x20) != 0, flipy = (atrib & 0x40) != 0;
                const int color = ((atrib & 0x1f) + 0x20) << 4;
                const uint8_t *set = (pos & 0x20) ? S.chars1 : S.chars0;
                draw_tile(set, S.nchars, 8, S.scroll1, 512, 512, x * 8, y * 8, nchar, color, flipx, flipy, NULL);
                if (sprite_next)
                    draw_tile(set, S.nchars, 8, S.priority, 2048, 2048, x * 8, y * 8, nchar, color, flipx, flipy,
                              S.trans_alt[(atrib & 0x180) >> 7]);
            }
        if (sprite_next) {
            S.sprites_pri_draw = true;
            S.scroll_pri_x = S.scroll_x1 & 0x1ff;
            S.scroll_pri_y = S.scroll_y1 & 0x1ff;
        }
        blit_scrolled(S.scroll1, 512, 512, S.scroll_x1 & 0x1ff, S.scroll_y1 & 0x1ff);
        return;
    }
    if (nlayer == 2 && (S.layer & S.b->mask_sc2) != 0 && (S.video_control & 4) != 0) {
        fill32(S.scroll2, 1024 * 1024);
        if (sprite_next) fill32(S.priority, 2048 * 2048);
        for (int f = 0; f <= 0xfff; f++) {
            const int x = f % 0x40, y = f / 0x40;
            const int pos = (y & 0xf) + ((x & 0x3f) << 4) + ((y & 0x30) << 6);
            const uint32_t address = S.scroll2_base + (uint32_t)(pos * 4);
            const uint16_t atrib = S.vram[vram_index_from_byte(address + 2)];
            const int nchar = gfx_bank(K_SCROLL2, S.vram[vram_index_from_byte(address)]);
            if (nchar < 0) {
                clear_tile(S.scroll2, 1024, 1024, x * 16, y * 16, 16);
                if (sprite_next) clear_tile(S.priority, 2048, 2048, x * 16, y * 16, 16);
                continue;
            }
            const bool flipx = (atrib & 0x20) != 0, flipy = (atrib & 0x40) != 0;
            const int color = ((atrib & 0x1f) + 0x40) << 4;
            draw_tile(S.tiles16, S.ntiles16, 16, S.scroll2, 1024, 1024, x * 16, y * 16, nchar, color, flipx, flipy, NULL);
            if (sprite_next)
                draw_tile(S.tiles16, S.ntiles16, 16, S.priority, 2048, 2048, x * 16, y * 16, nchar, color, flipx, flipy,
                          S.trans_alt[(atrib & 0x180) >> 7]);
        }
        if (sprite_next) {
            S.sprites_pri_draw = true;
            S.scroll_pri_x = S.scroll_x2;
            S.scroll_pri_y = S.scroll_y2;
        }
        if (!S.rowscroll_ena) blit_scrolled(S.scroll2, 1024, 1024, S.scroll_x2, S.scroll_y2);
        else blit_rowscroll();
        return;
    }
    if (nlayer == 3 && (S.layer & S.b->mask_sc3) != 0 && (S.video_control & 8) != 0) {
        fill32(S.scroll3, 2048 * 2048);
        if (sprite_next) fill32(S.priority, 2048 * 2048);
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++) {
                const int pos = (y & 0x07) + ((x & 0x3f) << 3) + ((y & 0x38) << 6);
                const uint32_t address = S.scroll3_base + (uint32_t)(pos * 4);
                const uint16_t atrib = S.vram[vram_index_from_byte(address + 2)];
                const int nchar = gfx_bank(K_SCROLL3, S.vram[vram_index_from_byte(address)] & 0x3fff);
                if (nchar < 0) {
                    clear_tile(S.scroll3, 2048, 2048, x * 32, y * 32, 32);
                    if (sprite_next) clear_tile(S.priority, 2048, 2048, x * 32, y * 32, 32);
                    continue;
                }
                const bool flipx = (atrib & 0x20) != 0, flipy = (atrib & 0x40) != 0;
                const int color = ((atrib & 0x1f) + 0x60) << 4;
                draw_tile(S.tiles32, S.ntiles32, 32, S.scroll3, 2048, 2048, x * 32, y * 32, nchar, color, flipx, flipy, NULL);
                if (sprite_next)
                    draw_tile(S.tiles32, S.ntiles32, 32, S.priority, 2048, 2048, x * 32, y * 32, nchar, color, flipx, flipy,
                              S.trans_alt[(atrib & 0x180) >> 7]);
            }
        if (sprite_next) {
            S.sprites_pri_draw = true;
            S.scroll_pri_x = S.scroll_x3 & 0x7ff;
            S.scroll_pri_y = S.scroll_y3 & 0x7ff;
        }
        blit_scrolled(S.scroll3, 2048, 2048, S.scroll_x3 & 0x7ff, S.scroll_y3 & 0x7ff);
    }
}

static void draw_stars(void)
{
    for (int f = 0; f <= 0xfff; f++)
        for (int k = 0; k < 2; k++) {           // (layer 2 first, as dsp-cpp)
            const uint8_t col = S.stars[8 * f + (k == 0 ? 4 : 0)];
            const uint16_t star_x = k == 0 ? S.stars_x2 : S.stars_x1, star_y = k == 0 ? S.stars_y2 : S.stars_y1;
            const int pal_base = k == 0 ? 0xa00 : 0x800;
            if ((col & 0x1f) == 0x0f) continue;
            const int x = ((f / 256) * 32 - (int)star_x + (col & 0x1f)) & 0x1ff;
            const int y = ((f % 256) - (int)star_y) & 0xff;
            const int cnt = (col & 0x80) ? (int)((S.frame / 16) % 15) : (int)((S.frame / 16) % 16);
            S.composite[y * WORK + x] = S.palette[pal_base + ((col & 0xe0) >> 1) + cnt];
        }
}

static inline uint32_t src_at(int x, int y)
{
    if (S.flip_screen) {
        x = 64 + 383 - (x - 64);
        y = 16 + 223 - (y - 16);
    }
    return S.composite[y * WORK + x];
}

static inline uint16_t to565(uint32_t c) { return arc_rgb565(c); }

static void copy_final(void)
{
#if ROTATED
    for (int y = 0; y < SW; y++)                // (rot270; dsp-cpp writes outside its frame here and crashes)
        for (int x = 0; x < SH; x++) S.fb[(SW - 1 - y) * W + x] = to565(src_at(64 + y, 16 + x));
#else
    for (int y = 0; y < SH; y++)
        for (int x = 0; x < SW; x++) S.fb[y * W + x] = to565(src_at(64 + x, 16 + y));
#endif
}

static void update_video(void)
{
    uint32_t fill = S.palette[0xbff];
    if ((fill & 0xff000000u) == 0) fill = 0xff000000u;
    for (int i = 0; i < WORK * WORK; i++) S.composite[i] = fill;
    const int l0 = (S.layer >> 6) & 3, l1 = (S.layer >> 8) & 3, l2 = (S.layer >> 10) & 3, l3 = (S.layer >> 12) & 3;
    if (S.stars_enabled && (S.layer & S.b->mask_sc4) != 0) draw_stars();
    draw_layer(l0, l1 == 0);
    draw_layer(l1, l2 == 0);
    draw_layer(l2, l3 == 0);
    draw_layer(l3, false);
    copy_final();
}

// ---------------------------------------------------------------------------
// ROMs (dsp-cpp's load_16b / load_raw / load_swap_word / load_64b / load_64b_b)

static bool load_one(const arc_rom *r, uint8_t *buf)
{
    arc_rom one = *r;
    one.offset = 0;
    return arc_load(&one, 1, buf, one.size);
}

static bool load_roms(const cps_game *g, uint8_t *cpu, uint8_t *gfx, uint32_t gfx_cap, uint8_t *sound, uint8_t **oki,
                      uint8_t **qs)
{
    uint8_t *tmp = (uint8_t *)malloc(0x80000);
    if (!tmp) return false;
    bool ok = true;
    for (int i = 0; ok && i < g->nloads; i++) {
        const cps_load *l = &g->loads[i];
        const arc_rom *r = &l->rom;
        uint8_t *dest;
        uint32_t cap;
        switch (l->dest) {
        case D_CPU: dest = cpu; cap = 0x180000; break;
        case D_SOUND: dest = sound; cap = g->sound_size; break;
        case D_OKI:
            if (!*oki) *oki = (uint8_t *)calloc(1, 0x40000);
            dest = *oki;
            cap = 0x40000;
            break;
        case D_QS:
            if (!*qs) *qs = (uint8_t *)calloc(1, 0x200000);
            dest = *qs;
            cap = 0x200000;
            break;
        default: dest = gfx; cap = gfx_cap; break;
        }
        if (!dest || !(ok = load_one(r, tmp))) break;
        switch (l->mode) {
        case L16B:
            for (uint32_t k = 0; k < r->size; k++)
                if (r->offset + k * 2 < cap) dest[r->offset + k * 2] = tmp[k];
            break;
        case LRAW:
        case LSWAP:
            if (r->offset + r->size > cap) { ok = false; break; }
            memcpy(dest + r->offset, tmp, r->size);
            if (l->mode == LSWAP)
                for (uint32_t k = 0; k < r->size / 2; k++) {
                    const uint8_t t = dest[r->offset + k * 2];
                    dest[r->offset + k * 2] = dest[r->offset + k * 2 + 1];
                    dest[r->offset + k * 2 + 1] = t;
                }
            break;
        case L64B:
            for (uint32_t k = 0; k < r->size / 2; k++) {
                const uint32_t at = r->offset + k * 8;
                if (at + 1 < cap) {
                    dest[at] = tmp[k * 2];
                    dest[at + 1] = tmp[k * 2 + 1];
                }
            }
            break;
        default:                                // L64BB
            for (uint32_t k = 0; k < r->size; k++)
                if (r->offset + k * 8 < cap) dest[r->offset + k * 8] = tmp[k];
            break;
        }
    }
    free(tmp);
    return ok;
}

static uint8_t *decode_set(int w, int total, const int *xo, const int *yo, int inc, const uint8_t *gfx, uint32_t size)
{
    arc_layout l;
    memset(&l, 0, sizeof l);
    l.width = l.height = w;
    l.total = total;
    l.planes = 4;
    l.char_increment = inc;
    l.plane_offsets[0] = 24;
    l.plane_offsets[1] = 16;
    l.plane_offsets[2] = 8;
    l.plane_offsets[3] = 0;
    l.x_offsets = xo;
    l.y_offsets = yo;
    return arc_gfx_decode(&l, gfx, size);
}

bool arc_init(void)
{
    if (!(S_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    const cps_game *g = &kGames[GAME];
    S.b = &kCpsB[g->cps_b_index];
    S.banks = &kBanks[g->nbank];
    S.dsw_a = g->dsw_a;
    S.dsw_b = g->dsw_b;
    S.dsw_c = g->dsw_c;
    S.stars_enabled = g->stars != 0;
    S.scroll1 = (uint32_t *)malloc(4u * 512 * 512);
    S.scroll2 = (uint32_t *)malloc(4u * 1024 * 1024);
    S.scroll3 = (uint32_t *)malloc(4u * 2048 * 2048);
    S.priority = (uint32_t *)malloc(4u * 2048 * 2048);
    S.composite = (uint32_t *)malloc(4u * WORK * WORK);
    S.rom = (uint16_t *)calloc(0x100000, 2);
    uint8_t *cpu = (uint8_t *)calloc(1, 0x180000);
    const uint32_t gfx_cap = 0x600008;
    uint8_t *gfx = (uint8_t *)calloc(1, gfx_cap);
    uint8_t *sound = (uint8_t *)calloc(1, g->sound_size);
    if (!S.scroll1 || !S.scroll2 || !S.scroll3 || !S.priority || !S.composite || !S.rom || !cpu || !gfx || !sound) return false;
    if (!load_roms(g, cpu, gfx, gfx_cap, sound, &S.oki_rom, &S.qs_rom)) return false;
    for (int i = 0; i < 0xc0000; i++) S.rom[i] = (uint16_t)((cpu[i * 2] << 8) | cpu[i * 2 + 1]);
    free(cpu);
    if (S.stars_enabled) memcpy(S.stars, gfx, 0x8000);
    memcpy(S.sound_rom, sound, 0x8000);
    const uint32_t bank_off[6] = {0x8000, 0xc000, 0x10000, 0x14000, 0x18000, 0x1c000};
    for (int b = 0; b < (QSOUND ? 6 : 2); b++)
        if (bank_off[b] + 0x4000 <= g->sound_size) memcpy(S.snd_bank[b], sound + bank_off[b], 0x4000);
#if GAME == G_DINO
    kabuki_decode(sound, S.qsnd_opcode, S.qsnd_data, 0x8000, 0x76543210, 0x24601357, 0x4343, 0x43);
#elif GAME == G_PUNISHER
    kabuki_decode(sound, S.qsnd_opcode, S.qsnd_data, 0x8000, 0x67452103, 0x75316024, 0x2222, 0x22);
#endif
    free(sound);
    if (g->gfx_size < gfx_cap) memset(gfx + g->gfx_size, 0, gfx_cap - g->gfx_size);   // (resized to gfx_size)
    {
        static int x8a[8], x8b[8], y8[8], x16[16], y16[16], x32[32], y32[32];
        for (int i = 0; i < 8; i++) {
            x8a[i] = i;
            x8b[i] = 32 + i;
            y8[i] = i * 64;
            x16[i] = i;
            x16[8 + i] = 32 + i;
            x32[i] = i;
            x32[8 + i] = 32 + i;
            x32[16 + i] = 64 + i;
            x32[24 + i] = 96 + i;
        }
        for (int i = 0; i < 16; i++) y16[i] = i * 64;
        for (int i = 0; i < 32; i++) y32[i] = i * 128;
        S.nchars = g->chars;
        S.ntiles16 = g->tiles16;
        S.ntiles32 = g->tiles32;
        S.chars0 = decode_set(8, g->chars, x8a, y8, 64 * 8, gfx, g->gfx_size);
        S.chars1 = decode_set(8, g->chars, x8b, y8, 64 * 8, gfx, g->gfx_size);
        S.tiles16 = decode_set(16, g->tiles16, x16, y16, 4 * 16 * 16, gfx, g->gfx_size);
        S.tiles32 = decode_set(32, g->tiles32, x32, y32, 4 * 32 * 32, gfx, g->gfx_size);
    }
    free(gfx);
    if (!S.chars0 || !S.chars1 || !S.tiles16 || !S.tiles32) return false;

    m68kd_init(&S.cpu, MAIN_CLOCK, M68K_T_68000);
    S.cpu.read_word = cpu_rd;
    S.cpu.write_word = cpu_wr;
    S.snd.read = sound_read;
    S.snd.write = sound_write;
    S.snd.cycles = sound_cycles;
#if QSOUND
    S.snd.opcode_read = sound_opcode;
    qsoundd_init(&S.qs, S.qs_rom, 0x200000);
    eeprom93c46_init(&S.eeprom, 8);
#else
    ym2151_init(&S.ym, Z80_CLOCK_YM, RATE);
    S.ym.irq_handler = ym_irq;
    okid_init(&S.oki, 1000000, true, S.oki_rom, S.oki_rom ? 0x40000 : 0);
#endif
    zd_init(&S.snd);
    arc_reset();
    return true;
}

void arc_reset(void)
{
    m68kd_reset(&S.cpu);
    zd_reset(&S.snd);
#if QSOUND
    qsoundd_reset(&S.qs);
    eeprom93c46_reset(&S.eeprom);
#else
    ym2151_reset(&S.ym);
    okid_reset(&S.oki);
    S.oki.divisor = 132;
#endif
    S.in0 = S.in1 = S.in2 = 0xffff;
    S.sound_latch = S.sound_latch2 = S.sound_bank = 0;
    S.scroll_x1 = S.scroll_y1 = S.scroll_x2 = S.scroll_y2 = S.scroll_x3 = S.scroll_y3 = 0;
    S.stars_x1 = S.stars_x2 = S.stars_y1 = S.stars_y2 = 0;
    S.frame = 0;
    S.sprites_base = S.scroll1_base = S.scroll2_base = S.scroll3_base = S.pal_base = 0xffff;
    S.rowscroll_base = 0;
    S.rowscrollstart = 0;
    S.mula = S.mulb = S.layer = S.palctrl = 0;
    S.scroll_pri_x = S.scroll_pri_y = 0;
    S.sprites_pri_draw = S.rowscroll_ena = S.flip_screen = false;
    S.video_control = 0;
    memset(S.pri_mask, 0, sizeof S.pri_mask);
    for (int i = 0; i < 4; i++) calc_mask(0, i);
    S.audio_acc = S.oki_acc = S.qsound_acc = S.qsound_irq_acc = 0;
    S.last_oki = 0;
}

int arc_run_frame(const int16_t **audio)
{
    const int main_cycles = (int)((double)MAIN_CLOCK / FPS / SCANLINES);
    const int snd_cycles = (int)((double)SOUND_CLOCK / FPS / SCANLINES);
    S.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 240) {
            m68kd_set_irq(&S.cpu, 2, M68_HOLD);
            update_video();
            for (int i = 0; i < 0x400; i++) S.sprite_buffer[i] = S.vram[vram_index(S.sprites_base + (uint32_t)i)];
        }
        m68kd_run(&S.cpu, main_cycles);
        zd_run(&S.snd, snd_cycles);
    }
    S.frame++;
    if (audio) *audio = S.audio;
    return S.naudio;
}

const uint16_t *arc_frame(void) { return S.fb; }

void arc_input(const arc_in *in)
{
    const unsigned p1 = in->p[0], p2 = in->p[1];
    S.in0 = S.in1 = S.in2 = 0xffff;
    static const struct { unsigned bit; uint16_t mask; } kMap[7] = {
        {AP_RIGHT, 0x01}, {AP_LEFT, 0x02}, {AP_DOWN, 0x04}, {AP_UP, 0x08}, {AP_B1, 0x10}, {AP_B2, 0x20}, {AP_B3, 0x40}};
    for (int i = 0; i < 7; i++) {
        if (p1 & kMap[i].bit) S.in1 &= (uint16_t)~kMap[i].mask;
        if (p2 & kMap[i].bit) S.in1 &= (uint16_t)~(kMap[i].mask << 8);
    }
    if (in->coin1) S.in0 &= 0xfeff;
    if (in->coin2) S.in0 &= 0xfdff;
    if (p1 & AP_START) S.in0 &= 0xefff;
    if (p2 & AP_START) S.in0 &= 0xdfff;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) S.dsw_a = v;
    else if (bank == 1) S.dsw_b = v;
    else if (bank == 2) S.dsw_c = v;
}
