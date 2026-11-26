// atarisy1d.c - Atari System 1 for the common arcade frontends (arc.h):
// Marble Madness and Peter Pack Rat. C port of dsp-cpp's
// drivers/arcade/atari_system1 on its 68000 core (cpu/m68kdsp, as a 68010):
// the BIOS and the game ROMs read in place, the SLAPSTIC, a 6502 with a
// YM2151 and a POKEY, the playfield banks chosen by the colour PROMs, the
// motion objects (video/atari_mo) and the alphanumerics, drawn in bands of
// 8 lines through 256 palette slots as dsp-cpp does. (Indiana Jones and Road
// Runner stay on drivers/arcades/atarisy1, with their speech.)
// ARC_VARIANT, in dsp-cpp's order: 0 peterpak, 2 marble.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m68kdsp/m68kdsp.h"
#include "../../../cpu/m6502/m65xx.h"
#include "../../../machine/slapstic.h"
#include "../../../sound/ym2151.h"
#include "../../../sound/pokey.h"
#include "../../../sound/tms5220.h"
#include "../../../video/atari_mo.h"

#define G_PETERPAK 0
#define G_MARBLE 2
#define GAME ARC_VARIANT
#if GAME != G_PETERPAK && GAME != G_MARBLE
#error "atarisy1d: Peter Pack Rat (0) or Marble Madness (2)"
#endif

#define W 336
#define H 240
#define PFW 512
#define PFH 512
#define SCANLINES 262
#define ATARI_CLOCK 14318180
#define MAIN_CLOCK (ATARI_CLOCK / 2)
#define SOUND_CLOCK (ATARI_CLOCK / 8)
#define YM_CLOCK (ATARI_CLOCK / 4)
#define FPS 59.922743
#define RATE 44100
#define BAND 8
#define N(k) ((int)(sizeof k / sizeof k[0]))

#if GAME == G_MARBLE
static const char *const kSets[] = { "marble", "atarisy1", NULL };
const arc_info arc_game = { "marble", "Marble Madness", "Atari Games", 1984, W, H, FPS, RATE, kSets, { "EMPEZAR", NULL }, ARC_JOY_8WAY };
#else
static const char *const kSets[] = { "peterpak", "atarisy1", NULL };
const arc_info arc_game = { "peterpak", "Peter Pack Rat", "Atari Games", 1984, W, H, FPS, RATE, kSets, { "SALTO", "LANZAR", NULL }, ARC_JOY_8WAY };
#endif

static const arc_rom kBiosRoms[] = {
    {"136032.205.l13|136032.205", 0x4000, 0x0, 0x88d0be26},
    {"136032.206.l12|136032.206", 0x4000, 0x1, 0x3c79ef05},
};
static const arc_rom kCharRom = {"136032.104.f5|136032.104|136032.107.b2", 0x2000, 0x0, 0x7a29dc07};

#if GAME == G_PETERPAK
static const arc_rom kProgram[] = {
    {"136028.142", 0x4000, 0x00000, 0x4f9fc020}, {"136028.143", 0x4000, 0x00001, 0x9fb257cc},
    {"136028.144", 0x4000, 0x08000, 0x50267619}, {"136028.145", 0x4000, 0x08001, 0x7b6a5004},
    {"136028.146", 0x4000, 0x10000, 0x4183a67a}, {"136028.147", 0x4000, 0x10001, 0x14e2d97b},
    {"136028.148", 0x4000, 0x20000, 0x230e8ba9}, {"136028.149", 0x4000, 0x20001, 0x0ff0c13a},
};
static const arc_rom kSound[] = {{"136028.101", 0x4000, 0x8000, 0xff712aa2}, {"136028.102", 0x4000, 0xc000, 0x89ea21a1}};
static const arc_rom kBack[] = {
    {"136028.138", 0x8000, 0x00000, 0x53eaa018}, {"136028.139", 0x8000, 0x10000, 0x354a19cb},
    {"136028.140", 0x8000, 0x20000, 0x8d2c4717}, {"136028.141", 0x8000, 0x30000, 0xbf59ea19},
    {"136028.150", 0x8000, 0x80000, 0x83362483}, {"136028.151", 0x8000, 0x90000, 0x6e95094e},
    {"136028.152", 0x8000, 0xa0000, 0x9553f084}, {"136028.153", 0x8000, 0xb0000, 0xc2a9b028},
    {"136028.105", 0x4000, 0x104000, 0xac9a5a44}, {"136028.108", 0x4000, 0x114000, 0x51941e64},
    {"136028.111", 0x4000, 0x124000, 0x246599f3}, {"136028.114", 0x4000, 0x134000, 0x918a5082},
};
static const arc_rom kProms[] = {{"136028.136", 0x200, 0x000, 0x861cfa36}, {"136028.137", 0x200, 0x200, 0x8507e5ea}};
#define SLAPSTIC_TYPE 107
#define SLAPSTIC_WORD (0x20000 / 2)
#define PROGRAM_BYTES (0x8000 * 3)
#define GFX_BYTES 0x180000
#define HAS_ADC 1
#else
static const arc_rom kProgram[] = {
    {"136033.623", 0x4000, 0x00000, 0x284ed2e9}, {"136033.624", 0x4000, 0x00001, 0xd541b021},
    {"136033.625", 0x4000, 0x08000, 0x563755c7}, {"136033.626", 0x4000, 0x08001, 0x860feeb3},
    {"136033.627", 0x4000, 0x10000, 0xd1dbd439}, {"136033.628", 0x4000, 0x10001, 0x957d6801},
    {"136033.229|136033.129", 0x4000, 0x18000, 0xc81d5c14}, {"136033.630|136033.130", 0x4000, 0x18001, 0x687a09f7},
    {"136033.107", 0x4000, 0x20000, 0xf3b8745b}, {"136033.108", 0x4000, 0x20001, 0xe51eecaa},
};
static const arc_rom kSound[] = {{"136033.421", 0x4000, 0x8000, 0x78153dc3}, {"136033.422", 0x4000, 0xc000, 0x2e66300e}};
static const arc_rom kBack[] = {
    {"136033.137", 0x4000, 0x00000, 0x7a45f5c1}, {"136033.138", 0x4000, 0x04000, 0x7e954a88},
    {"136033.139", 0x4000, 0x10000, 0x1eb1bb5f}, {"136033.140", 0x4000, 0x14000, 0x8a82467b},
    {"136033.141", 0x4000, 0x20000, 0x52448965}, {"136033.142", 0x4000, 0x24000, 0xb4a70e4f},
    {"136033.143", 0x4000, 0x30000, 0x7156e449}, {"136033.144", 0x4000, 0x34000, 0x4c3e4c79},
    {"136033.145", 0x4000, 0x40000, 0x9062be7f}, {"136033.146", 0x4000, 0x44000, 0x14566dca},
    {"136033.149", 0x4000, 0x84000, 0xb6658f06}, {"136033.151", 0x4000, 0x94000, 0x84ee1c80},
    {"136033.153", 0x4000, 0xa4000, 0xdaa02926},
};
static const arc_rom kProms[] = {{"136033.118", 0x200, 0x000, 0x2101b0ed}, {"136033.119", 0x200, 0x200, 0x19f6e767}};
#define SLAPSTIC_TYPE 103
#define SLAPSTIC_WORD (0x20000 / 2)
#define PROGRAM_BYTES (0x8000 * 4)
#define GFX_BYTES 0x100000
#define HAS_ADC 0
#endif

typedef struct { const uint8_t *even, *odd; } rompair;

static struct state {
    m68kd cpu;
    m65xx snd;
    ym2151 ym;
    pokey pk;
    tms5220 tms;
    slapstic slap;
    atari_mo mo;
    uint8_t *files[64];                         // (the loaded ROM files)
    int nfiles;
    rompair main_pages[0x80], slap_pages[8];
    uint16_t ram[0x1000], ram3[0x2000], palette_ram[0x400];
    uint16_t *ram2[0x100];
    uint8_t eeprom[0x800], sound_ram[0x1000];
    const uint8_t *sound_rom[4], *char_rom;
    const uint8_t *gfx_chunks[GFX_BYTES / 0x4000];
    uint8_t gfx_bank[16], gfx_bpp[16], bank_color_shift[16], bank_gfx[3][8], next_gfx;
    const uint8_t *gfx_planes[16][8][2];
    uint16_t playfield_lookup[256], motable[256], mo_colors[256];
    uint8_t mo_gfxs[256];
    bool palette_set[0x400];
    uint8_t screen[W * H];
    uint16_t band[BAND * W];
    int band_y0, band_y1;
    uint16_t slot_of[0x400], slot_index[256];
    int slots_used;
    uint32_t slot_palette[256];
    uint16_t fb[W * H];
    uint8_t rom_bank;
    uint16_t scroll_x, scroll_y, scroll_y_latch, scroll_x_line[SCANLINES], scroll_y_line[SCANLINES];
    uint16_t bankselect, playfield_priority_pens;
    uint8_t playfield_tile_bank, vblank;
    bool int3_state;
    uint16_t in0;
    uint8_t in2, joy_bits, adc_channel, adc_value;
    bool adc_irq_enable, adc_busy, write_eeprom, sound_pending, main_pending, sound_halted;
    uint8_t sound_latch, main_latch;
    int line;
    int64_t audio_acc;
    int pokey_pending;
    uint32_t tms_table_clock;
    uint16_t tms_clocks[16];
    int track[2], track_cur[2][2];              // (Marble: the trackballs, rotated 45 degrees)
    int16_t audio[4096];
    int naudio;
} *S_;
#define S (*S_)

// ---------------------------------------------------------------------------
// Graphics

static uint8_t decode_bank(uint8_t prom1, uint8_t prom2, int bpp)
{
    int bi = 0;
    if ((prom1 & 0x10) == 0) bi = 1;
    else if ((prom1 & 0x20) == 0) bi = 2;
    else if ((prom1 & 0x40) == 0) bi = 3;
    else if ((prom1 & 0x80) == 0) bi = 4;
    else if ((prom2 & 0x40) == 0) bi = 5;
    else if ((prom2 & 0x80) == 0) bi = ((prom2 & 0x08) == 0) ? 7 : 6;
    else return 0;
    if (S.bank_gfx[bpp - 4][bi]) return S.bank_gfx[bpp - 4][bi];
    const uint32_t offset = 0x80000u * (uint32_t)(bi - 1);
    if (offset >= GFX_BYTES) return 0;
    const uint8_t gi = S.next_gfx++;
    if (gi >= 16) return 0;
    S.gfx_bank[gi] = (uint8_t)bi;
    S.gfx_bpp[gi] = (uint8_t)bpp;
    for (int k = 0; k < 8; k++)
        for (int half = 0; half < 2; half++) {
            const uint32_t at = offset + (uint32_t)k * 0x10000u + (uint32_t)half * 0x4000u;
            S.gfx_planes[gi][k][half] = (k < bpp && at / 0x4000 < GFX_BYTES / 0x4000) ? S.gfx_chunks[at / 0x4000] : NULL;
        }
    S.bank_gfx[bpp - 4][bi] = gi;
    S.bank_color_shift[gi] = (uint8_t)(bpp - 3);
    return gi;
}

static void convert_background(const uint8_t *proms)
{
    memset(S.bank_gfx, 0, sizeof S.bank_gfx);
    S.next_gfx = 1;
    memset(S.bank_color_shift, 1, sizeof S.bank_color_shift);
    memset(S.gfx_bank, 0, sizeof S.gfx_bank);
    memset(S.gfx_bpp, 0, sizeof S.gfx_bpp);
    for (int obj = 0; obj < 2; obj++)
        for (int i = 0; i < 256; i++) {
            const uint8_t prom1 = proms[i + 0x100 * obj], prom2 = proms[0x200 + i + 0x100 * obj];
            int bpp = 4;
            if (prom2 & 0x10) {
                bpp = 5;
                if (prom2 & 0x20) bpp = 6;
            }
            const uint8_t offset = prom1 & 0x0f;
            uint8_t bank = decode_bank(prom1, prom2, bpp);
            if (obj == 0) {
                uint8_t color = (uint8_t)(((uint8_t)~prom2 & 0x0f) >> (bpp - 4)), used = offset;
                if (bank == 0) {
                    bank = 1;
                    used = 0;
                    color = 0;
                }
                S.playfield_lookup[i] = (uint16_t)(used | (bank << 8) | (color << 12));
            } else {
                const uint8_t color = (uint8_t)(((uint8_t)~prom2 & 0x07) >> (bpp - 4));
                S.motable[i] = (uint16_t)(offset | (bank << 8) | (color << 12));
            }
        }
}

static inline bool gfx_valid(int gfx) { return gfx == 0 || (gfx < S.next_gfx && S.gfx_bank[gfx] != 0); }

static void tile_row(int gfx, int code, int row, uint8_t *pens)
{
    if (gfx == 0) {                             // (the 2 bpp alphanumerics)
        const uint8_t *r = S.char_rom + (size_t)(code & 0x1ff) * 16 + (size_t)row * 2;
        for (int half = 0; half < 2; half++)
            for (int xx = 0; xx < 4; xx++)
                pens[half * 4 + xx] = (uint8_t)((((r[half] >> (7 - xx)) & 1) << 1) | ((r[half] >> (3 - xx)) & 1));
        return;
    }
    const unsigned at = (unsigned)(code & 0xfff) * 8 + (unsigned)row;
    memset(pens, 0, 8);
    for (int k = 0; k < S.gfx_bpp[gfx]; k++) {
        const uint8_t *plane = S.gfx_planes[gfx][k][at >> 14];
        const uint8_t b = plane ? (uint8_t)~plane[at & 0x3fff] : 0;
        for (int x = 0; x < 8; x++) pens[x] |= (uint8_t)(((b >> (7 - x)) & 1) << k);
    }
}

static uint32_t palette_colour(int index)
{
    static const uint8_t kTable[16] = {0x0, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0x10, 0x11};
    if (!S.palette_set[index]) return 0;
    const uint16_t v = S.palette_ram[index], in = v >> 12;
    const uint8_t r = (uint8_t)(((v >> 8) & 0x0f) * kTable[in & 0x0f]);
    const uint8_t g = (uint8_t)(((v >> 4) & 0x0f) * kTable[in & 0x0f]);
    const uint8_t b = (uint8_t)((v & 0x0f) * kTable[in & 0x0f]);
    return 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static void mix_mo_pixel(uint16_t *pf, uint16_t mo)
{
    if (mo & 0xf000) {
        if ((mo & 0x0f) != 1) *pf = (uint16_t)(0x300 + ((*pf & 0x0f) << 4) + (mo & 0x0f));
    } else if ((*pf & 0xf8) != 0 || (S.playfield_priority_pens & (1u << (*pf & 7))) == 0) *pf = mo & 0x0fff;
}

static void mo_tile(void *u, int code, int color, bool hflip, bool vflip, int x, int y, int gfx, int priority)
{
    (void)u;
    if (y + 8 <= S.band_y0 || y >= S.band_y1) return;
    const int set = gfx_valid(gfx) && gfx > 0 ? gfx : (gfx_valid(1) ? 1 : 0);
    for (int row = 0; row < 8; row++) {
        const int ty = y + row;
        if (ty < S.band_y0 || ty >= S.band_y1 || ty < 0 || ty >= H) continue;
        uint8_t pens[8];
        tile_row(set, code, vflip ? 7 - row : row, pens);
        uint16_t *line = S.band + (size_t)(ty - S.band_y0) * W;
        for (int c = 0; c < 8; c++) {
            const int tx = x + c;
            if (tx < 0 || tx >= W) continue;
            const uint8_t pen = pens[hflip ? 7 - c : c];
            if (pen) mix_mo_pixel(&line[tx], (uint16_t)(((color + pen) & 0x0fff) | ((priority ? 1 : 0) << 12)));
        }
    }
}

static int mo_code(void *u, int raw) { (void)u; return (raw & 0xff) | ((S.motable[(raw >> 8) & 0xff] & 0xff) << 8); }
static int mo_color(void *u, int index) { (void)u; return S.mo_colors[index & 0xff]; }
static int mo_gfx(void *u, int raw_hi) { (void)u; return S.mo_gfxs[raw_hi & 0xff]; }
static const atari_mo_lookup kLookup = {mo_code, mo_color, mo_gfx};

static bool compose_band(int y0, int y1)
{
    S.band_y0 = y0;
    S.band_y1 = y1;
    for (int y = y0; y < y1; y++) {
        uint16_t *line = S.band + (size_t)(y - y0) * W;
        const int sx = S.scroll_x_line[y] & (PFW - 1), sy = S.scroll_y_line[y] & (PFH - 1);
        const int py = (y + sy) & (PFH - 1);
        int x = 0;
        while (x < W) {
            const int px = (x + sx) & (PFW - 1);
            const uint16_t atrib = S.ram3[(py >> 3) * 64 + (px >> 3)];
            const uint16_t lookup = S.playfield_lookup[((atrib >> 8) & 0x7f) | (S.playfield_tile_bank << 7)];
            int gi = (lookup >> 8) & 0xf;
            const int color = 0x20 + ((((lookup >> 12) & 0xf) << S.bank_color_shift[gi]));
            const int base = 0x100 + (color << 3);
            const int code = ((lookup & 0xff) << 8) | (atrib & 0xff);
            if (!gfx_valid(gi) || gi == 0) gi = 1;
            uint8_t pens[8];
            tile_row(gi, code, py & 7, pens);
            if (atrib & 0x8000)
                for (int i = 0; i < 4; i++) {
                    const uint8_t t = pens[i];
                    pens[i] = pens[7 - i];
                    pens[7 - i] = t;
                }
            const int first = px & 7, count = (8 - first) < (W - x) ? 8 - first : W - x;
            for (int c = 0; c < count; c++) line[x + c] = (uint16_t)((base + pens[first + c]) & 0x3ff);
            x += count;
        }
    }
    atari_mo_draw(&S.mo, &S.ram3[0x1000], 0, 256, -1, &kLookup, mo_tile, NULL);
    bool fits = true;
    for (int y = y0; y < y1; y++) {
        uint16_t *line = S.band + (size_t)(y - y0) * W;
        for (int cx = 0; cx < W / 8; cx++) {
            const uint16_t atrib = S.ram3[(0x3000 >> 1) + (y >> 3) * 64 + cx];
            const bool opaque = (atrib & 0x2000) != 0;
            const uint8_t *r = S.char_rom + (size_t)(atrib & 0x1ff) * 16 + (size_t)(y & 7) * 2;
            if (!opaque && r[0] == 0 && r[1] == 0) continue;
            const int base = ((atrib >> 10) & 7) << 2;
            uint8_t pens[8];
            tile_row(0, atrib & 0x3ff, y & 7, pens);
            for (int c = 0; c < 8; c++)
                if (opaque || pens[c]) line[cx * 8 + c] = (uint16_t)((base + pens[c]) & 0x3ff);
        }
        uint8_t *dst = S.screen + (size_t)y * W;
        for (int x = 0; x < W; x++) {
            const uint16_t index = line[x];
            uint16_t slot = S.slot_of[index];
            if (slot == 0) {
                if (S.slots_used < 256) {
                    S.slot_index[S.slots_used] = index;
                    slot = (uint16_t)++S.slots_used;
                    S.slot_of[index] = slot;
                } else {
                    fits = false;
                    slot = 1;
                }
            }
            dst[x] = (uint8_t)(slot - 1);
        }
    }
    return fits;
}

static void update_video(void)
{
    const uint32_t last_xpos = S.mo.last_xpos;
    for (int attempt = 0; attempt < 2; attempt++) {
        bool fits = true;
        for (int y = 0; y < H; y += BAND) {
            S.mo.last_xpos = last_xpos;
            fits = compose_band(y, y + BAND < H ? y + BAND : H) && fits;
        }
        if (fits) break;
        memset(S.slot_of, 0, sizeof S.slot_of);   // (more entries than slots: the slots afresh)
        S.slots_used = 0;
    }
    for (int i = 0; i < S.slots_used; i++) S.slot_palette[i] = palette_colour(S.slot_index[i] & 0x3ff);
    for (int i = 0; i < W * H; i++) {
        const uint32_t c = S.slot_palette[S.screen[i]];
        S.fb[i] = (uint16_t)(((c >> 8) & 0xf800) | ((c >> 5) & 0x07e0) | ((c >> 3) & 0x001f));
    }
}

// ---------------------------------------------------------------------------
// The 68010

static inline uint16_t rom_word(uint32_t a)
{
    const rompair *p = &S.main_pages[(a >> 12) & 0x7f];
    if (!p->even) return 0;
    const uint32_t i = (a & 0xfff) >> 1;
    return (uint16_t)((p->even[i] << 8) | p->odd[i]);
}

static void adc_start(uint32_t a)
{
    const int o = (int)((a >> 1) & 0xf);
    S.adc_channel = (uint8_t)(o & 7);
    S.adc_irq_enable = (o & 8) == 0;
    S.adc_busy = true;
    m68kd_set_irq(&S.cpu, 2, M68_CLEAR);
}

static void adc_complete(void)
{
    if (!HAS_ADC || !S.adc_busy) return;
    S.adc_busy = false;
    S.adc_value = (S.adc_channel <= 3) ? ((S.joy_bits & (1 << (7 - S.adc_channel))) ? 0xf0 : 0x00) : 0;
    if (S.adc_irq_enable) m68kd_set_irq(&S.cpu, 2, M68_HOLD);
}

static void set_sound_reset(bool running)
{
    if (running == !S.sound_halted) return;
    if (running) {
        S.sound_halted = false;
        ym2151_clear_external_irq(&S.ym);
        m65xx_set_irq(&S.snd, M65_CLEAR);
        m65xx_set_nmi(&S.snd, M65_CLEAR);
    } else {                                    // (MAME bankselect_w: the VIA reset and the main latch acknowledged)
        S.sound_halted = true;
        tms5220_reset(&S.tms);
        S.main_pending = false;
        m68kd_set_irq(&S.cpu, 6, M68_CLEAR);
    }
}

static uint16_t main_read(uint32_t a)
{
    a &= 0xffffff;
    if (a <= 0x7ffff) return rom_word(a);
    if (a <= 0x87fff) {
        const rompair *p = &S.slap_pages[(S.rom_bank & 3) * 2 + ((a >> 12) & 1)];
        const uint32_t i = (a & 0xfff) >> 1;
        const uint16_t v = p->even ? (uint16_t)((p->even[i] << 8) | p->odd[i]) : 0;
        S.rom_bank = slapstic_tweak(&S.slap, (uint16_t)((a & 0x7fff) >> 1));
        return v;
    }
    if (a == 0x2e0000) return S.int3_state ? 0x0080 : 0;
    if (a >= 0x400000 && a <= 0x401fff) return S.ram[(a & 0x1fff) >> 1];
    if (a >= 0x900000 && a <= 0x9fffff) {
        const uint16_t *p = S.ram2[(a & 0xfffff) >> 12];
        return p ? p[(a & 0xfff) >> 1] : 0;
    }
    if (a >= 0xa00000 && a <= 0xa03fff) return S.ram3[(a & 0x3fff) >> 1];
    if (a >= 0xb00000 && a <= 0xb007ff) return S.palette_ram[(a & 0x7ff) >> 1];
    if (a >= 0xf00000 && a <= 0xf00fff) return S.eeprom[(a & 0xfff) >> 1];
    if (a >= 0xf20000 && a <= 0xf20007) {
#if GAME == G_MARBLE                            // (MAME's trakball_r, the type rotated 45 degrees)
        const int player = (int)((a >> 2) & 1), which = (int)((a >> 1) & 1);
        if (which == 0) {
            const int8_t posx = (int8_t)S.track[0], posy = (int8_t)S.track[1];
            S.track_cur[player][0] = player ? 0xff : (uint8_t)(posx + posy);
            S.track_cur[player][1] = player ? 0xff : (uint8_t)(posx - posy);
        }
        return (uint16_t)(uint8_t)S.track_cur[player][which];
#else
        return 0x00ff;
#endif
    }
    if (a >= 0xf40000 && a <= 0xf4001f) {
        if (!HAS_ADC) return 0;
        const uint8_t v = S.adc_value;
        adc_start(a);
        return v;
    }
    if (a >= 0xf60000 && a <= 0xf60003) return (uint16_t)(S.in0 | S.vblank | (S.sound_pending ? 0x80 : 0));
    if (a == 0xfc0000) {
        S.main_pending = false;
        m68kd_set_irq(&S.cpu, 6, M68_CLEAR);
        return S.main_latch;
    }
    return 0xffff;
}

static void main_write(uint32_t a, uint16_t v)
{
    a &= 0xffffff;
    if (a <= 0x7ffff) return;
    if (a <= 0x87fff) {
        S.rom_bank = slapstic_tweak(&S.slap, (uint16_t)((a & 0x7fff) >> 1));
        return;
    }
    if (a >= 0x400000 && a <= 0x401fff) { S.ram[(a & 0x1fff) >> 1] = v; return; }
    if (a == 0x800000) { S.scroll_x = v; return; }
    if (a == 0x820000) {
        S.scroll_y_latch = v;
        S.scroll_y = (S.line < 240) ? (uint16_t)(v - (S.line + 1)) : v;
        return;
    }
    if (a == 0x840000) { S.playfield_priority_pens = v; return; }
    if (a == 0x860000) {
        const uint16_t diff = S.bankselect ^ v;
        if (diff & 0x04) S.playfield_tile_bank = (v >> 2) & 1;
        if (diff & 0x80) set_sound_reset((v & 0x80) != 0);
        S.mo.bank = (v >> 3) & 7;
        S.bankselect = v;
        return;                                 // (INT3 only on Road Runner)
    }
    if (a == 0x880000) return;
    if (a == 0x8a0000) { m68kd_set_irq(&S.cpu, 4, M68_CLEAR); return; }
    if (a == 0x8c0000) { S.write_eeprom = true; return; }
    if (a >= 0x900000 && a <= 0x9fffff) {
        uint16_t **p = &S.ram2[(a & 0xfffff) >> 12];
        if (!*p && !(*p = (uint16_t *)calloc(0x800, 2))) return;
        (*p)[(a & 0xfff) >> 1] = v;
        return;
    }
    if (a >= 0xa00000 && a <= 0xa03fff) { S.ram3[(a & 0x3fff) >> 1] = v; return; }
    if (a >= 0xb00000 && a <= 0xb007ff) {
        const int i = (int)((a & 0x7ff) >> 1);
        if (S.palette_ram[i] != v) {
            S.palette_ram[i] = v;
            S.palette_set[i] = true;
        }
        return;
    }
    if (a >= 0xf00000 && a <= 0xf00fff) {
        if (S.write_eeprom) {
            S.eeprom[(a & 0xfff) >> 1] = (uint8_t)v;
            S.write_eeprom = false;
        }
        return;
    }
    if (a >= 0xf40000 && a <= 0xf4001f) {
        if (HAS_ADC) adc_start(a);
        return;
    }
    if (a == 0xf80000 || a == 0xfe0000) {
        S.sound_latch = (uint8_t)v;
        S.sound_pending = true;
        m65xx_set_nmi(&S.snd, M65_ASSERT);
    }
}

static uint16_t cpu_rw(void *u, uint32_t a) { (void)u; return main_read(a); }
static void cpu_ww(void *u, uint32_t a, uint16_t v) { (void)u; main_write(a, v); }
static uint32_t sl_pc(void *u) { (void)u; return S.cpu.pc.l; }
static uint32_t sl_ppc(void *u) { (void)u; return S.cpu.ppc.l; }
static uint16_t sl_peek(void *u, uint32_t a) { (void)u; return main_read(a); }
static uint32_t sl_areg(void *u, int n) { (void)u; return S.cpu.a[n].l; }

// ---------------------------------------------------------------------------
// The 6502

static inline void sync_pokey(void)
{
    if (S.pokey_pending > 0) {
        pokey_run(&S.pk, S.pokey_pending);
        S.pokey_pending = 0;
    }
}

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
    if ((a & ~0x2000) <= 0x0fff) return S.sound_ram[a & 0x0fff];
    if (a >= 0x4000) {
        const uint8_t *p = S.sound_rom[a >> 14];
        return p ? p[a & 0x3fff] : 0;
    }
    if ((a & ~0x278e) == 0x1800 || (a & ~0x278e) == 0x1801) return ym2151_status(&S.ym);
    if ((a & ~0x278f) == 0x1810) {
        S.sound_pending = false;
        m65xx_set_nmi(&S.snd, M65_CLEAR);
        return S.sound_latch;
    }
    if ((a & ~0x278f) == 0x1820) {
        uint8_t v = (uint8_t)(S.in2 | (S.sound_pending ? 0x08 : 0) | (S.main_pending ? 0x10 : 0));
        if ((S.in0 & 0x0040) == 0) v ^= 0x80;   // (MAME switch_6502_r: the service bit)
        return v;
    }
    if ((a & ~0x2780) >= 0x1870 && (a & ~0x2780) <= 0x187f) {
        sync_pokey();
        return pokey_read(&S.pk, a & 0x0f);
    }
    return 0xff;
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if ((a & ~0x2000) <= 0x0fff) { S.sound_ram[a & 0x0fff] = v; return; }
    if ((a & ~0x278e) == 0x1800) { ym2151_select(&S.ym, v); return; }
    if ((a & ~0x278e) == 0x1801) { ym2151_write(&S.ym, v); return; }
    if ((a & ~0x278f) == 0x1810) {
        S.main_latch = v;
        S.main_pending = true;
        m68kd_set_irq(&S.cpu, 6, M68_ASSERT);
        return;
    }
    const uint16_t outlatch = a & ~0x2788;
    if (outlatch >= 0x1820 && outlatch <= 0x1827) return;   // (the LS259: absorbed)
    if ((a & ~0x2780) >= 0x1870 && (a & ~0x2780) <= 0x187f) {
        sync_pokey();
        pokey_write(&S.pk, a & 0x0f, v);
    }
}

static void sound_cycles(void *u, int cycles)
{
    (void)u;
    ym2151_run_timers(&S.ym, cycles * 2);
    S.pokey_pending += cycles;
    if (S.tms.clock != S.tms_table_clock) {
        S.tms_table_clock = S.tms.clock;
        for (int c = 0; c < 16; c++) S.tms_clocks[c] = (uint16_t)(((uint64_t)c * S.tms_table_clock + (SOUND_CLOCK / 2)) / SOUND_CLOCK);
    }
    const int tc = cycles < 16 ? S.tms_clocks[cycles] : (int)(((int64_t)cycles * S.tms.clock + (SOUND_CLOCK / 2)) / SOUND_CLOCK);
    tms5220_tick(&S.tms, tc);
    S.audio_acc += (int64_t)cycles * RATE;
    if (S.audio_acc >= SOUND_CLOCK) sync_pokey();
    while (S.audio_acc >= SOUND_CLOCK) {
        S.audio_acc -= SOUND_CLOCK;
        const int32_t s = ym2151_update(&S.ym) + pokey_update(&S.pk) + tms5220_last_sample(&S.tms);
        if (S.naudio < 4096) S.audio[S.naudio++] = arc_clamp16(s);
    }
}

static void ym_irq(void *u, bool on) { (void)u; m65xx_set_irq(&S.snd, on ? M65_HOLD : M65_CLEAR); }

// ---------------------------------------------------------------------------
// ROMs

static const uint8_t *rom_file(const arc_rom *r)
{
    if (S.nfiles >= N(S.files)) return NULL;
    uint8_t *d = (uint8_t *)malloc(r->size);
    arc_rom one = *r;
    one.offset = 0;
    if (!d || !arc_load(&one, 1, d, one.size)) {
        free(d);
        return NULL;
    }
    return S.files[S.nfiles++] = d;
}

typedef struct { uint32_t base, length; bool odd; const uint8_t *data; } chip;

static int load_chips(const arc_rom *l, int n, chip *out)
{
    for (int i = 0; i < n; i++) {
        const uint8_t *d = rom_file(&l[i]);
        if (!d) return -1;
        out[i] = (chip){l[i].offset & ~1u, l[i].size, (l[i].offset & 1) != 0, d};
    }
    return n;
}

static rompair page_of(const chip *c, int n, uint32_t offset)
{
    rompair p = {NULL, NULL};
    for (int i = 0; i < n; i++) {
        if (offset < c[i].base || offset + 0x1000 > c[i].base + c[i].length * 2) continue;
        const uint8_t *q = c[i].data + (offset - c[i].base) / 2;
        if (c[i].odd) p.odd = q;
        else p.even = q;
    }
    if (!p.even || !p.odd) p.even = p.odd = NULL;
    return p;
}

static bool load_roms(void)
{
    chip bios[2], game[16];
    if (load_chips(kBiosRoms, 2, bios) < 0) return false;
    for (uint32_t a = 0; a < 0x10000; a += 0x1000) S.main_pages[a >> 12] = page_of(bios, 2, a);
    if (!(S.char_rom = rom_file(&kCharRom))) return false;
    const int ng = load_chips(kProgram, N(kProgram), game);
    if (ng < 0) return false;
    for (uint32_t g = 0; g < PROGRAM_BYTES && 0x10000 + g < 0x80000; g += 0x1000) S.main_pages[(0x10000 + g) >> 12] = page_of(game, ng, g);
    for (uint32_t p = 0; p < 8; p++) S.slap_pages[p] = page_of(game, ng, SLAPSTIC_WORD * 2 + p * 0x1000);
    for (uint32_t p = 0; p < 0x80; p++) m68kd_set_direct_page_pair(&S.cpu, p, S.main_pages[p].even, S.main_pages[p].odd);
    for (int i = 0; i < N(kSound); i++) {
        const uint8_t *d = rom_file(&kSound[i]);
        if (!d) return false;
        S.sound_rom[(kSound[i].offset >> 14) & 3] = d;
    }
    for (int page = 0x40; page < 0x100; page++)
        if (S.sound_rom[page >> 6]) S.snd.page_r[page] = S.sound_rom[page >> 6] + (size_t)(page & 0x3f) * 0x100;
    for (int page = 0; page < 0x10; page++) {
        uint8_t *ram = S.sound_ram + (size_t)page * 0x100;
        S.snd.page_r[page] = S.snd.page_r[0x20 + page] = ram;
        S.snd.page_w[page] = S.snd.page_w[0x20 + page] = ram;
    }
    uint8_t proms[0x400];
    memset(proms, 0, sizeof proms);
    if (!arc_load(kProms, N(kProms), proms, sizeof proms)) return false;
    for (int i = 0; i < N(kBack); i++) {
        const uint8_t *d = rom_file(&kBack[i]);
        if (!d) return false;
        for (uint32_t o = 0; o < kBack[i].size; o += 0x4000)
            if ((kBack[i].offset + o) / 0x4000 < GFX_BYTES / 0x4000) S.gfx_chunks[(kBack[i].offset + o) / 0x4000] = d + o;
    }
    convert_background(proms);
    for (int i = 0; i < 256; i++) {
        S.mo_colors[i] = (S.motable[i] >> 12) & 0xf;
        S.mo_gfxs[i] = (uint8_t)((S.motable[i] >> 8) & 0xf);
    }
    return true;
}

bool arc_init(void)
{
    if (!(S_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    for (int i = 0; i < 256; i++) S.slot_palette[i] = 0xff000000u;
    m68kd_init(&S.cpu, MAIN_CLOCK, M68K_T_68010);
    S.cpu.read_word = cpu_rw;
    S.cpu.write_word = cpu_ww;
    if (!load_roms()) return false;
    memset(S.eeprom, 0xff, sizeof S.eeprom);
    S.snd.read = sound_read;
    S.snd.write = sound_write;
    S.snd.cycles = sound_cycles;
    ym2151_init(&S.ym, YM_CLOCK, RATE);
    S.ym.irq_handler = ym_irq;
    pokey_init(&S.pk);
    tms5220_init(&S.tms, ATARI_CLOCK / 2 / 11, false);
    const slapstic_cpu sc = {sl_pc, sl_ppc, sl_peek, sl_areg, NULL};
    slapstic_init(&S.slap, SLAPSTIC_TYPE, &sc);
    static const atari_mo_config cfg = {
        .tile_width = 8, .tile_height = 8, .bankcount = 8, .linked = true, .split = true,
        .slipheight = 0, .maxperline = 0x38, .palettebase = 0x100,
        .link_entry = {0, 0, 0, 0x003f},
        .code_entry = {{0, 0xffff, 0, 0}, {0, 0, 0, 0}},
        .color_entry = {{0, 0xff00, 0, 0}, {0, 0, 0, 0}},
        .xpos_entry = {0, 0, 0x3fe0, 0}, .ypos_entry = {0x3fe0, 0, 0, 0},
        .height_entry = {0x000f, 0, 0, 0}, .hflip_entry = {0x8000, 0, 0, 0},
        .priority_entry = {0, 0, 0x8000, 0}, .special_entry = {0, 0xffff, 0, 0}, .specialvalue = 0xffff};
    atari_mo_init(&S.mo, &cfg, W + 8, H + 8);
    arc_reset();
    return true;
}

void arc_reset(void)
{
    slapstic_reset(&S.slap);
    S.rom_bank = S.slap.current_bank;
    m68kd_reset(&S.cpu);
    m65xx_reset(&S.snd);
    ym2151_reset(&S.ym);
    pokey_reset(&S.pk);
    S.pokey_pending = 0;
    tms5220_reset(&S.tms);
    S.in0 = 0xff6f;
    S.in2 = 0x87;
    S.joy_bits = S.adc_channel = S.adc_value = 0;
    S.adc_irq_enable = true;
    S.adc_busy = false;
    m68kd_set_irq(&S.cpu, 2, M68_CLEAR);
    S.scroll_x = S.scroll_y = S.scroll_y_latch = 0;
    memset(S.scroll_x_line, 0, sizeof S.scroll_x_line);
    memset(S.scroll_y_line, 0, sizeof S.scroll_y_line);
    S.vblank = 0x10;
    S.bankselect = 0;
    S.playfield_tile_bank = 0;
    S.playfield_priority_pens = 0;
    S.int3_state = false;
    m68kd_set_irq(&S.cpu, 3, M68_CLEAR);
    S.write_eeprom = S.sound_pending = S.main_pending = false;
    S.main_latch = S.sound_latch = 0;
    S.sound_halted = false;                     // (the 6502 runs from power-on, as dsp-emulator)
    S.line = 0;
    S.audio_acc = 0;
    memset(S.slot_of, 0, sizeof S.slot_of);
    S.slots_used = 0;
    for (int i = 0; i < 256; i++) S.slot_palette[i] = 0xff000000u;
    memset(S.screen, 0, sizeof S.screen);
    S.track[0] = 0xff;                          // (reads 0xff, 0xff at rest: dsp-cpp's fixed value)
    S.track[1] = 0;
    for (int i = 0; i < 2; i++) S.track_cur[i][0] = S.track_cur[i][1] = 0xff;
}

int arc_run_frame(const int16_t **audio)
{
    const int mc = (int)((double)MAIN_CLOCK / FPS / SCANLINES + 0.5);
    const int sc = (int)((double)SOUND_CLOCK / FPS / SCANLINES + 0.5);
    S.naudio = 0;
    for (S.line = 0; S.line < SCANLINES; S.line++) {
        m68kd_run(&S.cpu, mc);
        if (!S.sound_halted) m65xx_run(&S.snd, sc);
        adc_complete();
        S.scroll_x_line[S.line] = S.scroll_x;
        S.scroll_y_line[S.line] = S.scroll_y;
        if (S.line == 239) {
            update_video();
            S.vblank = 0;
            m68kd_set_irq(&S.cpu, 4, M68_ASSERT);
        }
        if (S.line == 261) S.vblank = 0x10;
    }
    S.scroll_y = S.scroll_y_latch;
    S.line = 0;
    if (audio) *audio = S.audio;
    return S.naudio;
}

const uint16_t *arc_frame(void) { return S.fb; }

void arc_input(const arc_in *in)
{
    const unsigned p = in->p[0];
    S.in0 = 0xff6f;
    S.in2 = 0x87;
    if (p & AP_B1) S.in0 &= (uint16_t)~0x0001;
    if ((p | in->p[1]) & AP_START) S.in0 &= (uint16_t)~0x0002;
    if (p & AP_B2) S.in0 &= (uint16_t)~0x0004;
#if GAME == G_MARBLE                            // (Marble starts with the player button: START too)
    if (p & AP_START) S.in0 &= (uint16_t)~0x0001;
#endif
    if (in->coin1) S.in2 &= (uint8_t)~0x01;
    if (in->coin2) S.in2 &= (uint8_t)~0x02;
    S.joy_bits = 0;
    if (p & AP_UP) S.joy_bits |= 0x10;
    if (p & AP_DOWN) S.joy_bits |= 0x20;
    if (p & AP_LEFT) S.joy_bits |= 0x40;
    if (p & AP_RIGHT) S.joy_bits |= 0x80;
#if GAME == G_MARBLE                            // (the stick rolls the trackball: up / down on Y, left / right on X)
    if (p & AP_LEFT) S.track[0] += 8;          // (X reversed, as MAME's PORT_REVERSE)
    if (p & AP_RIGHT) S.track[0] -= 8;
    if (p & AP_UP) S.track[1] -= 8;
    if (p & AP_DOWN) S.track[1] += 8;
#endif
}

void arc_dip(int bank, uint8_t v) { (void)bank; (void)v; }
