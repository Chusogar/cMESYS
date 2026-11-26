// boogwing.c - Boogie Wings (Data East, 1992) for the common arcade
// frontends (arc.h): C port of dsp-cpp's drivers/arcade/boogwing (after
// dsp-emulator's boogiewings_hw.pas). A 68000 at 14 MHz (cpu/m68kdsp) whose
// program is encrypted with the DECO 102 (separate opcode and data views,
// machine/deco_cryptd), the DECO 104 protection that also reads the inputs
// and passes the sound commands (machine/deco146d), two DECO 16IC tilemap
// chips (video/deco16icd; the first with 5 bpp 16x16 tiles; all the graphics
// encrypted with the DECO 56), two sets of DECO 52 sprites and a 2048 colour
// palette faded by the DECO ACE; a HuC6280 with a YM2151 and two OKI M6295
// banked through the YM2151 port.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m68kdsp/m68kdsp.h"
#include "../../../cpu/hu6280dsp/hu6280dsp.h"
#include "../../../machine/deco146d.h"
#include "../../../machine/deco_cryptd.h"
#include "../../../sound/ym2151.h"
#include "../../../sound/okim6295dsp.h"
#include "../../../video/deco16icd.h"

#define W 320
#define H 240
#define FIRST_LINE 8
#define FPS 57.79965
#define SCANLINES 274
#define MAIN_CLOCK (28000000 / 2)
#define SOUND_XTAL 32220000
#define SOUND_CLOCK (SOUND_XTAL / 12)
#define YM_CLOCK (SOUND_XTAL / 9)
#define RATE 44100

static const char *const kSets[] = { "boogwing", NULL };
const arc_info arc_game = { "boogwing", "Boogie Wings", "Data East", 1992, W, H, FPS, RATE, kSets,
                            { "DISPARO", "GANCHO", "BOMBA", NULL }, ARC_JOY_8WAY };

static const arc_rom kProgram[] = {{"kn_00-2.2b", 0x40000, 0x00000, 0xe38892b9}, {"kn_02-2.2e", 0x40000, 0x00001, 0x8426efef},
                                   {"kn_01-2.4b", 0x40000, 0x80000, 0x3ad4b54c}, {"kn_03-2.4e", 0x40000, 0x80001, 0x10b61f4a}};
static const arc_rom kSound[] = {{"km06.18p", 0x10000, 0, 0x3e8bc4e1}};
static const arc_rom kChars[] = {{"km05.9e", 0x10000, 0, 0xd10aef95}, {"km04.8e", 0x10000, 1, 0x329323a8}};
static const arc_rom kTiles1[] = {{"mbd-01.9b", 0x100000, 0x000000, 0xd7de4f4b}, {"mbd-00.8b", 0x100000, 0x100000, 0xadb20ba9}};
static const arc_rom kTiles1Plane5[] = {{"mbd-02.10e", 0x80000, 0, 0xb25aa721}};
static const arc_rom kTiles2[] = {{"mbd-03.13b", 0x100000, 0x000000, 0xcf798f2c}, {"mbd-04.14b", 0x100000, 0x100000, 0xd9764d0b}};
static const arc_rom kSprites1[] = {{"mbd-05.16b", 0x200000, 0x200000, 0x1768c66a}, {"mbd-06.17b", 0x200000, 0x000000, 0x7750847a}};
static const arc_rom kSprites2[] = {{"mbd-07.18b", 0x200000, 0x200000, 0x241faac1}, {"mbd-08.19b", 0x200000, 0x000000, 0xf13b1e56}};
static const arc_rom kOki1[] = {{"mbd-10.17p", 0x80000, 0, 0xf159f76a}};
static const arc_rom kOki2[] = {{"mbd-09.16p", 0x80000, 0, 0xf44f2f87}};
#define NROM(k) ((int)(sizeof k / sizeof k[0]))

static struct state {
    m68kd cpu;
    hu6280d snd;
    ym2151 ym;
    okim6295d oki1, oki2;
    deco146d prot;
    deco16ic tilegen[2];
    deco16_gfx chars, tiles1, tiles2, sprites[2];
    uint16_t rom_opcodes[0x80000], rom_data[0x80000];
    uint8_t sound_rom[0x10000], oki1_rom[0x80000], oki2_rom[0x80000];
    uint16_t ram[0x8000], sprite_ram[2][0x400], ace_ram[0x28];
    uint32_t palette_ram[0x800], palette_buffer[0x800];
    uint16_t palette[0x1000];                   // 0-7ff faded, 800-fff as written
    uint8_t sound_ram[0x2000];
    uint16_t in0, in1, dsw, priority;
    uint8_t sound_latch;
    uint32_t frames;
    double main_debt, sound_debt;
    int64_t ym_acc, oki1_acc, oki2_acc, audio_acc;
    int32_t oki1_level, oki2_level;
    int16_t audio[4096];
    int naudio;
    uint16_t screen[W * H];
} *B_;
#define B (*B_)

static uint16_t rgb(int r, int g, int b) { return arc_rgb565((uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b); }

// DECO ACE: the shown palette is the buffered one moved towards the fade
// colour (or brightened, mode 0x1000) by the fade strengths.
static void update_palette(void)
{
    const int fade_r = B.ace_ram[0x20] & 0xff, fade_g = B.ace_ram[0x21] & 0xff, fade_b = B.ace_ram[0x22] & 0xff;
    const int ps_r = B.ace_ram[0x23] & 0xff, ps_g = B.ace_ram[0x24] & 0xff, ps_b = B.ace_ram[0x25] & 0xff;
    const uint16_t mode = B.ace_ram[0x26];
    for (int i = 0; i < 0x800; i++) {
        const uint32_t c = B.palette_buffer[i];
        int b = (int)((c >> 16) & 0xff), g = (int)((c >> 8) & 0xff), r = (int)(c & 0xff);
        B.palette[0x800 + i] = rgb(r, g, b);
        if (mode == 0x1000) {
            r += ps_r;
            g += ps_g;
            b += ps_b;
        } else {
            r += (fade_r - r) * ps_r / 255;
            g += (fade_g - g) * ps_g / 255;
            b += (fade_b - b) * ps_b / 255;
        }
        r = r < 0 ? 0 : r > 255 ? 255 : r;
        g = g < 0 ? 0 : g > 255 ? 255 : g;
        b = b < 0 ? 0 : b > 255 ? 255 : b;
        B.palette[i] = rgb(r, g, b);
    }
}

// ---------------------------------------------------------------------------
// The 68000

static uint16_t main_read(uint32_t a)
{
    a &= 0xffffff;
    if (a < 0x100000) return B.cpu.opcode ? B.rom_opcodes[a >> 1] : B.rom_data[a >> 1];
    if (a >= 0x200000 && a < 0x210000) return B.ram[(a & 0xffff) >> 1];
    if (a >= 0x242000 && a < 0x242800) return B.sprite_ram[0][(a & 0x7ff) >> 1];
    if (a >= 0x246000 && a < 0x246800) return B.sprite_ram[1][(a & 0x7ff) >> 1];
    if (a >= 0x24e000 && a < 0x24f000) return deco146d_read(&B.prot, (uint16_t)(a & 0x7ff));   // (bits 10-0)
    if (a >= 0x260000 && a < 0x280000) {
        deco16ic *chip = &B.tilegen[(a >> 16) & 1];
        switch ((a >> 12) & 0xf) {
        case 0x0: return chip->control[((a & 0xf) >> 1) & 7];
        case 0x4: case 0x5: return chip->data[0][(a & 0x1fff) >> 1];
        case 0x6: case 0x7: return chip->data[1][(a & 0x1fff) >> 1];
        case 0x8: return chip->rowscroll[0][(a & 0xfff) >> 1];
        case 0xa: return chip->rowscroll[1][(a & 0xfff) >> 1];
        default: return 0xffff;
        }
    }
    if (a >= 0x284000 && a < 0x286000) {
        const uint32_t e = B.palette_ram[(a >> 2) & 0x7ff];
        return (a & 2) ? (uint16_t)e : (uint16_t)(e >> 16);
    }
    if (a >= 0x3c0000 && a < 0x3c0050) return B.ace_ram[(a & 0xff) >> 1];
    return 0xffff;
}

static void main_write(uint32_t a, uint16_t v)
{
    a &= 0xffffff;
    if (a < 0x100000) return;
    if (a >= 0x200000 && a < 0x210000) B.ram[(a & 0xffff) >> 1] = v;
    else if (a == 0x220000) B.priority = v;
    else if (a >= 0x242000 && a < 0x242800) B.sprite_ram[0][(a & 0x7ff) >> 1] = v;
    else if (a >= 0x246000 && a < 0x246800) B.sprite_ram[1][(a & 0x7ff) >> 1] = v;
    else if (a >= 0x24e000 && a < 0x24f000) deco146d_write(&B.prot, (uint16_t)(a & 0x7ff), v, 0xffff);
    else if (a >= 0x260000 && a < 0x280000) {
        deco16ic *chip = &B.tilegen[(a >> 16) & 1];
        switch ((a >> 12) & 0xf) {
        case 0x0:
            if ((a & 0xfff) < 0x10) chip->control[((a & 0xf) >> 1) & 7] = v;
            break;
        case 0x4: case 0x5: chip->data[0][(a & 0x1fff) >> 1] = v; break;
        case 0x6: case 0x7: chip->data[1][(a & 0x1fff) >> 1] = v; break;
        case 0x8: chip->rowscroll[0][(a & 0xfff) >> 1] = v; break;
        case 0xa: chip->rowscroll[1][(a & 0xfff) >> 1] = v; break;
        default: break;
        }
    } else if (a == 0x282008) {                 // the palette DMA
        memcpy(B.palette_buffer, B.palette_ram, sizeof B.palette_buffer);
        update_palette();
    } else if (a >= 0x284000 && a < 0x286000) {
        uint32_t *e = &B.palette_ram[(a >> 2) & 0x7ff];
        if (a & 2) *e = (*e & 0xffff0000u) | v;
        else *e = (*e & 0x0000ffffu) | ((uint32_t)v << 16);
    } else if (a >= 0x3c0000 && a < 0x3c0050) {
        const size_t i = (a & 0xff) >> 1;
        if (B.ace_ram[i] != v) {
            B.ace_ram[i] = v;
            if (i >= 0x20 && i <= 0x26) update_palette();
        }
    }
}

static uint16_t cpu_rd(void *u, uint32_t a) { (void)u; return main_read(a); }
static void cpu_wr(void *u, uint32_t a, uint16_t v) { (void)u; main_write(a, v); }
static uint8_t cpu_rd8(void *u, uint32_t a)
{
    (void)u;
    const uint16_t w = main_read(a & ~1u);
    return (uint8_t)((a & 1) ? w : w >> 8);
}
static void cpu_wr8(void *u, uint32_t a, uint8_t v)
{
    (void)u;
    const uint32_t m = a & 0xffffff;
    if (m >= 0x24e000 && m < 0x24f000) {        // (no read first: the reads have side effects)
        deco146d_write(&B.prot, (uint16_t)(m & 0x7fe), (uint16_t)(v << 8 | v), (a & 1) ? 0x00ff : 0xff00);
        return;
    }
    const uint16_t w = main_read(a & ~1u);
    main_write(a & ~1u, (a & 1) ? (uint16_t)((w & 0xff00) | v) : (uint16_t)((w & 0x00ff) | (v << 8)));
}

static uint16_t port_a(void *u) { (void)u; return B.in0; }
static uint16_t port_b(void *u) { (void)u; return B.in1; }
static uint16_t port_c(void *u) { (void)u; return B.dsw; }
static void soundlatch(void *u, uint8_t v)
{
    (void)u;
    B.sound_latch = v;
    hu6280d_set_irq_line(&B.snd, 0, HU_HOLD);
}

// ---------------------------------------------------------------------------
// The sound HuC6280

static uint8_t snd_rd(void *u, uint32_t a)
{
    (void)u;
    if (a < 0x10000) return B.sound_rom[a];
    switch (a) {
    case 0x110000: case 0x110001: return ym2151_status(&B.ym);
    case 0x120000: case 0x120001: return okid_read(&B.oki1);
    case 0x130000: case 0x130001: return okid_read(&B.oki2);
    case 0x140000: case 0x140001: return B.sound_latch;
    default: break;
    }
    if (a >= 0x1f0000 && a < 0x1f2000) return B.sound_ram[a & 0x1fff];
    if (a >= 0x1fec00 && a < 0x1fec02) return hu6280d_timer_r(&B.snd);
    if (a >= 0x1ff400 && a < 0x1ff404) return hu6280d_irq_status_r(&B.snd, (uint8_t)(a & 3));
    return 0xff;
}

static void snd_wr(void *u, uint32_t a, uint8_t v)
{
    (void)u;
    if (a < 0x10000) return;
    switch (a) {
    case 0x110000: ym2151_select(&B.ym, v); return;
    case 0x110001: ym2151_write(&B.ym, v); return;
    case 0x120000: case 0x120001: okid_write(&B.oki1, v); return;
    case 0x130000: case 0x130001: okid_write(&B.oki2, v); return;
    default: break;
    }
    if (a >= 0x1f0000 && a < 0x1f2000) B.sound_ram[a & 0x1fff] = v;
    else if (a >= 0x1fec00 && a < 0x1fec02) hu6280d_timer_w(&B.snd, (uint8_t)(a & 1), v);
    else if (a >= 0x1ff400 && a < 0x1ff404) hu6280d_irq_status_w(&B.snd, (uint8_t)(a & 3), v);
}

static void snd_cycles(void *u, int cycles)
{
    (void)u;
    B.ym_acc += (int64_t)cycles * YM_CLOCK;
    const int ym_cycles = (int)(B.ym_acc / SOUND_CLOCK);
    B.ym_acc -= (int64_t)ym_cycles * SOUND_CLOCK;
    ym2151_run_timers(&B.ym, ym_cycles);
    B.oki1_acc += (int64_t)cycles * okid_sample_frequency(&B.oki1);
    while (B.oki1_acc >= SOUND_CLOCK) {
        B.oki1_acc -= SOUND_CLOCK;
        B.oki1_level = okid_update(&B.oki1);
    }
    B.oki2_acc += (int64_t)cycles * okid_sample_frequency(&B.oki2);
    while (B.oki2_acc >= SOUND_CLOCK) {
        B.oki2_acc -= SOUND_CLOCK;
        B.oki2_level = okid_update(&B.oki2);
    }
    B.audio_acc += (int64_t)cycles * RATE;
    while (B.audio_acc >= SOUND_CLOCK) {
        B.audio_acc -= SOUND_CLOCK;
        const int32_t s = (ym2151_update(&B.ym) * 3 + B.oki1_level * 3) / 8 + B.oki2_level * 3 / 16;
        if (B.naudio < 4096) B.audio[B.naudio++] = arc_clamp16(s);
    }
}

static void ym_irq(void *u, bool on) { (void)u; hu6280d_set_irq_line(&B.snd, 1, on ? HU_ASSERT : HU_CLEAR); }
static void ym_port(void *u, uint8_t v)
{
    (void)u;
    B.oki1.bank_offset = (v & 1) ? 0x40000 : 0;
    B.oki2.bank_offset = (v & 2) ? 0x40000 : 0;
}

// ---------------------------------------------------------------------------
// Video

// DECO 52 sprites in the board's flipped orientation: those whose priority
// bits (word 2, bits 15-14) are `priority`.
static void draw_sprites(int set, int priority)
{
    const uint16_t *ram = B.sprite_ram[set];
    const deco16_gfx *gfx = &B.sprites[set];
    const int color_add = set == 0 ? 0x500 : 0x700;
    for (int f = 0; f < 0x100; f++) {
        const uint16_t x0 = ram[f * 4 + 2];
        if (((x0 >> 8) & 0xc0) != priority) continue;
        const uint16_t y0 = ram[f * 4];
        if ((y0 & 0x1000) && (B.frames & 1)) continue;   // (flashing)
        const int color = (x0 >> 9) & 0x1f;
        const bool flip_x = !(y0 & 0x2000), flip_y = !(y0 & 0x4000);
        int multi = (1 << ((y0 & 0x0600) >> 9)) - 1;
        int code = ram[f * 4 + 1] & ~multi;
        if (code == 0) continue;
        int inc = -1;
        if (!(y0 & 0x4000)) {
            code += multi;
            inc = 1;
        }
        const int sx = 304 - ((304 - x0) & 0x1ff);
        for (; multi >= 0; multi--) {
            const int sy = (y0 + 16 * multi) & 0x1ff;
            const int t = code - multi * inc;
            const uint8_t *g = gfx->px + (size_t)(((t % gfx->total) + gfx->total) % gfx->total) * 256;
            for (int r = 0; r < 16; r++) {
                const int py = ((sy + r) & 0x1ff) - FIRST_LINE;
                if (py < 0 || py >= H) continue;
                const uint8_t *row = g + (flip_y ? 15 - r : r) * 16;
                uint16_t *out = &B.screen[py * W];
                for (int c = 0; c < 16; c++) {
                    const int px = sx + c;
                    if (px < 0 || px >= W) continue;
                    const int pen = row[flip_x ? 15 - c : c];
                    if (pen) out[px] = B.palette[(color_add + color * 16 + pen) & 0xfff];
                }
            }
        }
    }
}

static void pf(int chip, int playfield, bool transparent)
{
    if (!deco16ic_enabled(&B.tilegen[chip], playfield)) return;
    deco16ic_draw(&B.tilegen[chip], playfield, B.screen, W, H, FIRST_LINE, B.palette, !transparent);
}

static void sprites(int priority)
{
    draw_sprites(0, priority);
    draw_sprites(1, priority);
}

static void update_video(void)
{
    for (int i = 0; i < W * H; i++) B.screen[i] = B.palette[0];
    switch (B.priority & 7) {
    case 5:
        pf(0, 1, false);
        sprites(0x80);
        pf(1, 0, true);
        sprites(0x40);
        pf(1, 1, true);
        break;
    case 4:
        pf(1, 0, false);
        sprites(0x80);
        pf(1, 1, true);
        sprites(0x40);
        pf(0, 1, true);
        break;
    case 1: case 2: case 3:
        pf(1, 1, false);
        sprites(0x80);
        pf(0, 1, true);
        sprites(0x40);
        pf(1, 0, true);
        break;
    default:
        pf(1, 1, false);
        sprites(0x80);
        pf(1, 0, true);
        sprites(0x40);
        pf(0, 1, true);
        break;
    }
    sprites(0);
    pf(0, 0, true);
}

// ---------------------------------------------------------------------------

static const int kX8[8] = {0, 1, 2, 3, 4, 5, 6, 7};
static const int kY8[8] = {0, 16, 32, 48, 64, 80, 96, 112};
static const int kX16[16] = {256, 257, 258, 259, 260, 261, 262, 263, 0, 1, 2, 3, 4, 5, 6, 7};
static const int kY16[16] = {0, 16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 240};

// ROM_LOAD16_BYTE: each file's bytes to every other address from its offset
static bool load_interleaved(const arc_rom *list, int n, uint8_t *dst, uint32_t size)
{
    uint32_t max = 0;
    for (int i = 0; i < n; i++)
        if (list[i].size > max) max = list[i].size;
    uint8_t *part = (uint8_t *)malloc(max);
    bool ok = part != NULL;
    for (int i = 0; ok && i < n; i++) {
        arc_rom r = list[i];
        r.offset = 0;
        memset(part, 0xff, r.size);
        if (!(ok = arc_load(&r, 1, part, r.size))) break;
        for (uint32_t j = 0; j < r.size && list[i].offset + 2 * j < size; j++) dst[list[i].offset + 2 * j] = part[j];
    }
    free(part);
    return ok;
}

static bool decode_tiles(deco16_gfx *g, const uint8_t *raw, size_t size, int total, int planes, const int *offs)
{
    arc_layout l = {16, 16, total, planes, 32 * 16, false, false, false, {0}, kX16, kY16};
    for (int i = 0; i < planes; i++) l.plane_offsets[i] = offs[i];
    *g = (deco16_gfx){arc_gfx_decode(&l, raw, size), total};
    return g->px != NULL;
}

static bool load_gfx(void)
{
    uint8_t *gfx = (uint8_t *)calloc(1, 0x400000), *plane5 = (uint8_t *)calloc(1, 0x100000);
    bool ok = gfx && plane5;
    // 8x8 characters
    if (ok && (ok = load_interleaved(kChars, NROM(kChars), gfx, 0x20000))) {
        deco56_decrypt_gfx(gfx, 0x20000);
        const arc_layout l = {8, 8, 0x1000, 4, 16 * 8, false, false, false, {0x10000 * 8 + 8, 0x10000 * 8, 8, 0}, kX8, kY8};
        B.chars = (deco16_gfx){arc_gfx_decode(&l, gfx, 0x20000), 0x1000};
        ok = B.chars.px != NULL;
    }
    // the first chip's tiles: four encrypted planes plus a plain fifth one
    if (ok) {
        memset(gfx, 0, 0x300000);
        if ((ok = arc_load(kTiles1, NROM(kTiles1), gfx, 0x300000) && load_interleaved(kTiles1Plane5, NROM(kTiles1Plane5), plane5, 0x100000))) {
            deco56_decrypt_gfx(gfx, 0x200000);
            deco56_remap_gfx(plane5, 0x100000);
            memcpy(gfx + 0x200000, plane5, 0x100000);
            static const int p[5] = {0x200000 * 8, 0x100000 * 8 + 8, 0x100000 * 8, 8, 0};
            ok = decode_tiles(&B.tiles1, gfx, 0x300000, 0x4000, 5, p);
        }
    }
    free(plane5);
    if (ok) {
        memset(gfx, 0, 0x200000);
        if ((ok = arc_load(kTiles2, NROM(kTiles2), gfx, 0x200000))) {
            deco56_decrypt_gfx(gfx, 0x200000);
            static const int p[4] = {0x100000 * 8 + 8, 0x100000 * 8, 8, 0};
            ok = decode_tiles(&B.tiles2, gfx, 0x200000, 0x4000, 4, p);
        }
    }
    static const int ps[4] = {0x200000 * 8 + 8, 0x200000 * 8, 8, 0};
    for (int s = 0; ok && s < 2; s++) {
        memset(gfx, 0, 0x400000);
        if ((ok = s == 0 ? arc_load(kSprites1, NROM(kSprites1), gfx, 0x400000) : arc_load(kSprites2, NROM(kSprites2), gfx, 0x400000)))
            ok = decode_tiles(&B.sprites[s], gfx, 0x400000, 0x8000, 4, ps);
    }
    free(gfx);
    return ok;
}

static int bank_callback(int bank) { return ((bank >> 4) & 7) * 0x1000; }
static int bank_callback2(int bank)
{
    int offset = ((bank >> 4) & 7) * 0x1000;
    if ((bank & 0xf) == 0xa) offset += 0x800;   // (the transporter level)
    return offset;
}

bool arc_init(void)
{
    if (!(B_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    uint8_t *image = (uint8_t *)malloc(0x100000);
    uint16_t *words = (uint16_t *)malloc(0x80000 * sizeof(uint16_t));
    bool ok = image && words;
    if (ok) {
        memset(image, 0xff, 0x100000);
        if ((ok = load_interleaved(kProgram, NROM(kProgram), image, 0x100000))) {
            for (int i = 0; i < 0x80000; i++) words[i] = (uint16_t)(image[2 * i] << 8 | image[2 * i + 1]);
            deco102_decrypt_cpu(words, B.rom_opcodes, B.rom_data, 0x80000, 0x42ba, 0x00, 0x18);
        }
    }
    free(image);
    free(words);
    memset(B.sound_rom, 0xff, sizeof B.sound_rom);
    ok = ok && ARC_LOAD(kSound, B.sound_rom, sizeof B.sound_rom) && ARC_LOAD(kOki1, B.oki1_rom, sizeof B.oki1_rom) &&
         ARC_LOAD(kOki2, B.oki2_rom, sizeof B.oki2_rom) && load_gfx();
    if (!ok) return false;

    m68kd_init(&B.cpu, MAIN_CLOCK, M68K_T_68000);
    B.cpu.read_word = cpu_rd;
    B.cpu.write_word = cpu_wr;
    B.cpu.read_byte = cpu_rd8;
    B.cpu.write_byte = cpu_wr8;
    hu6280d_init(&B.snd, SOUND_CLOCK);
    B.snd.read = snd_rd;
    B.snd.write = snd_wr;
    B.snd.cycles = snd_cycles;
    ym2151_init(&B.ym, YM_CLOCK, RATE);
    B.ym.irq_handler = ym_irq;
    B.ym.port_handler = ym_port;
    okid_init(&B.oki1, SOUND_XTAL / 32, true, B.oki1_rom, sizeof B.oki1_rom);
    okid_init(&B.oki2, SOUND_XTAL / 16, true, B.oki2_rom, sizeof B.oki2_rom);
    deco146d_init(&B.prot, DECO104, DECO146_SCRAMBLE_REVERSE | DECO146_MAGIC_ADDRESS_XOR);
    B.prot.port_a = port_a;
    B.prot.port_b = port_b;
    B.prot.port_c = port_c;
    B.prot.soundlatch = soundlatch;
    for (int chip = 0; chip < 2; chip++)
        for (int p = 0; p < 2; p++) {
            deco16_layer *l = &B.tilegen[chip].layers[p];
            deco16ic_layer_defaults(l);
            l->gfx8 = &B.chars;
            if (chip == 0) {
                l->gfx16 = &B.tiles1;           // (5 bpp)
                l->color_base16 = 0x100;
                l->granularity16 = 32;
                if (p == 1) l->bank = bank_callback;
            } else {
                l->gfx16 = &B.tiles2;
                l->color_base16 = 0x300;
                l->color_bank = p == 1 ? 16 : 0;
                l->bank = bank_callback2;
            }
        }
    B.in0 = 0xffff;
    B.in1 = 0x0007;
    B.dsw = 0xffff;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    memset(B.ram, 0, sizeof B.ram);
    memset(B.sprite_ram, 0, sizeof B.sprite_ram);
    memset(B.palette_ram, 0, sizeof B.palette_ram);
    memset(B.palette_buffer, 0, sizeof B.palette_buffer);
    memset(B.ace_ram, 0, sizeof B.ace_ram);
    memset(B.sound_ram, 0, sizeof B.sound_ram);
    memset(B.palette, 0, sizeof B.palette);
    deco16ic_reset(&B.tilegen[0]);
    deco16ic_reset(&B.tilegen[1]);
    deco146d_reset(&B.prot);
    B.in1 = (uint16_t)((B.in1 & ~0x08) | 0x07);
    B.priority = 0;
    B.sound_latch = 0;
    B.main_debt = B.sound_debt = 0;
    B.ym_acc = B.oki1_acc = B.oki2_acc = B.audio_acc = 0;
    B.oki1_level = B.oki2_level = 0;
    m68kd_reset(&B.cpu);
    hu6280d_reset(&B.snd);
    ym2151_reset(&B.ym);
    okid_reset(&B.oki1);
    okid_reset(&B.oki2);
    B.oki1.bank_offset = B.oki2.bank_offset = 0;
}

int arc_run_frame(const int16_t **audio)
{
    const double main_cycles = (double)MAIN_CLOCK / FPS / SCANLINES;
    const double sound_cycles = (double)SOUND_CLOCK / FPS / SCANLINES;
    B.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 8) B.in1 &= (uint16_t)~0x08;
        if (line == 248) {
            m68kd_set_irq(&B.cpu, 6, M68_HOLD);
            update_video();
            B.in1 |= 0x08;                      // (VBLANK)
        }
        B.main_debt += main_cycles;
        B.main_debt -= m68kd_run(&B.cpu, (int)B.main_debt);
        B.sound_debt += sound_cycles;
        B.sound_debt -= hu6280d_run(&B.snd, (int)B.sound_debt);
    }
    B.frames++;
    if (audio) *audio = B.audio;
    return B.naudio;
}

const uint16_t *arc_frame(void) { return B.screen; }

static uint16_t pad(unsigned p)
{
    uint16_t v = 0xff;
    if (p & AP_UP) v &= (uint16_t)~0x01;
    if (p & AP_DOWN) v &= (uint16_t)~0x02;
    if (p & AP_LEFT) v &= (uint16_t)~0x04;
    if (p & AP_RIGHT) v &= (uint16_t)~0x08;
    if (p & AP_B1) v &= (uint16_t)~0x10;
    if (p & AP_B2) v &= (uint16_t)~0x20;
    if (p & AP_B3) v &= (uint16_t)~0x40;
    if (p & AP_START) v &= (uint16_t)~0x80;
    return v;
}

void arc_input(const arc_in *in)
{
    B.in0 = (uint16_t)(pad(in->p[0]) | pad(in->p[1]) << 8);
    uint16_t sys = (uint16_t)((B.in1 & 0x08) | 0x07);
    if (in->coin1) sys &= (uint16_t)~0x01;
    if (in->coin2) sys &= (uint16_t)~0x02;
    B.in1 = sys;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) B.dsw = (uint16_t)((B.dsw & 0xff00) | v);
    else if (bank == 1) B.dsw = (uint16_t)((B.dsw & 0x00ff) | (v << 8));
}
