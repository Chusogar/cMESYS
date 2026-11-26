// cninja.c - Caveman Ninja / Joe & Mac (Data East, 1991) for the common arcade
// frontends (arc.h): C port of dsp-cpp's drivers/arcade/cninja (after
// dsp-emulator's cavemanninja_hw.pas). A 68000 at 12 MHz (cpu/m68kdsp), the
// DECO 104 protection that also reads the inputs and passes the sound
// commands (machine/deco146d), two DECO 16IC tilemap chips (video/deco16icd)
// and the DECO 52 sprites; a HuC6280 with a YM2203, a YM2151 and two OKI
// M6295 (the second banked through the YM2151 port). Raster IRQ 3 / 4 and the
// VBLANK IRQ 5. ARC_VARIANT 1: Robocop 2 (1991) on the same board: a 68000 at
// 14 MHz, the DECO 146, 320 pixels wide and an 8 bpp mode of the second chip.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m68kdsp/m68kdsp.h"
#include "../../../cpu/hu6280dsp/hu6280dsp.h"
#include "../../../machine/deco146d.h"
#include "../../../sound/ym2151.h"
#include "../../../sound/ym2203dsp.h"
#include "../../../sound/okim6295dsp.h"
#include "../../../video/deco16icd.h"

#ifndef ARC_VARIANT
#define ARC_VARIANT 0
#endif
#define ROBOCOP2 (ARC_VARIANT == 1)
#if ROBOCOP2
#define W 320
#define MAIN_CLOCK 14000000
#define ROM_END 0x100000
#define PROT_BASE 0x18c000
#define N_TILES1 0x2000
#define N_TILES2 0x3000
#define N_SPRITES 0x6000
#else
#define W 256
#define MAIN_CLOCK 12000000
#define ROM_END 0xc0000
#define PROT_BASE 0x1bc000
#define N_TILES1 0x1000
#define N_TILES2 0x2000
#define N_SPRITES 0x4000
#endif
#define H 240
#define FIRST_LINE 8
#define FPS 58.0
#define SCANLINES 274
#define SOUND_XTAL 32220000
#define SOUND_CLOCK (SOUND_XTAL / 8)
#define YM2151_CLOCK (SOUND_XTAL / 9)
#define YM2203_CLOCK (SOUND_XTAL / 8)
#define RATE 44100

#if ROBOCOP2
static const char *const kSets[] = { "robocop2", NULL };
const arc_info arc_game = { "robocop2", "Robocop 2", "Data East", 1991, W, H, FPS, RATE, kSets,
                            { "DISPARO", "SALTO", NULL }, ARC_JOY_8WAY };

static const arc_rom kProgram[] = {{"gq-03.k1", 0x20000, 0x00000, 0xa7e90c28}, {"gq-07.k3", 0x20000, 0x00001, 0xd2287ec1},
                                   {"gq-02.j1", 0x20000, 0x40000, 0x6777b8a0}, {"gq-06.j3", 0x20000, 0x40001, 0xe11e27b5},
                                   {"go-01-1.h1", 0x20000, 0x80000, 0xab5356c0}, {"go-05-1.h3", 0x20000, 0x80001, 0xce21bda5},
                                   {"go-00.f1", 0x20000, 0xc0000, 0xa93369ea}, {"go-04.f3", 0x20000, 0xc0001, 0xee2f6ad9}};
static const arc_rom kSound[] = {{"gp-09.k13", 0x10000, 0, 0x4a4e0f8d}};
static const arc_rom kChars[] = {{"gp11-1.z6", 0x10000, 0, 0x030ded47}, {"gp10-1.y6", 0x10000, 1, 0xd25d719c}};
static const arc_rom kTiles1[] = {{"mah-04.z4", 0x80000, 0x00000, 0x9b6ca18c}, {"mah-03.y4", 0x80000, 0x80000, 0x37894ddc}};
static const arc_rom kTiles2[] = {{"mah-01.z1", 0x80000, 0x000000, 0x26e0dfff}, {"mah-00.y1", 0x80000, 0x080000, 0x7bd69e41},
                                  {"mah-02.a1", 0x80000, 0x100000, 0x328a247d}};
static const arc_rom kSprites[] = {{"mah-05.y9", 0x80000, 0x000000, 0x6773e613}, {"mah-08.y12", 0x80000, 0x000001, 0x88d310a5},
                                   {"mah-06.z9", 0x80000, 0x100000, 0x27a8808a}, {"mah-09.z12", 0x80000, 0x100001, 0xa58c43a7},
                                   {"mah-07.a9", 0x80000, 0x200000, 0x526f4190}, {"mah-10.a12", 0x80000, 0x200001, 0x14b770da}};
static const arc_rom kOki1[] = {{"gp-08.j13", 0x20000, 0, 0x365183b1}};
static const arc_rom kOki2[] = {{"mah-11.f13", 0x80000, 0, 0x642bc692}};
#else
static const char *const kSets[] = { "cninja", NULL };
const arc_info arc_game = { "cninja", "Caveman Ninja", "Data East", 1991, W, H, FPS, RATE, kSets,
                            { "ATAQUE", "SALTO", NULL }, ARC_JOY_8WAY };

static const arc_rom kProgram[] = {{"gn-02-3.1k", 0x20000, 0x00000, 0x39aea12a}, {"gn-05-2.3k", 0x20000, 0x00001, 0x0f4360ef},
                                   {"gn-01-2.1j", 0x20000, 0x40000, 0xf740ef7e}, {"gn-04-2.3j", 0x20000, 0x40001, 0xc98fcb62},
                                   {"gn-00.1h", 0x20000, 0x80000, 0x0b110b16}, {"gn-03.1k", 0x20000, 0x80001, 0x1e28e697}};
static const arc_rom kSound[] = {{"gl-07.13k", 0x10000, 0, 0xca8bef96}};
static const arc_rom kChars[] = {{"gl-09.6z", 0x10000, 0, 0x5a2d4752}, {"gl-08.6y", 0x10000, 1, 0x33a2b400}};
static const arc_rom kTiles1[] = {{"mag-02.4z", 0x80000, 0, 0xde89c69a}};
static const arc_rom kTiles2[] = {{"mag-00.1y", 0x80000, 0x00000, 0xa8f05d33}, {"mag-01.1z", 0x80000, 0x80000, 0x5b399eed}};
static const arc_rom kSprites[] = {{"mag-03.9y", 0x80000, 0x000000, 0x2220eb9f}, {"mag-05.12y", 0x80000, 0x000001, 0x56a53254},
                                   {"mag-04.9z", 0x80000, 0x100000, 0x144b94cc}, {"mag-06.12z", 0x80000, 0x100001, 0x82d44749}};
static const arc_rom kOki1[] = {{"gl-06.13j", 0x20000, 0, 0xd92e519d}};
static const arc_rom kOki2[] = {{"mag-07.13f", 0x80000, 0, 0x08eb5264}};
#endif
#define NROM(k) ((int)(sizeof k / sizeof k[0]))

static struct state {
    m68kd cpu;
    hu6280d snd;
    ym2203d ym2203;
    ym2151 ym2151;
    okim6295d oki1, oki2;
    deco146d prot;
    deco16ic tilegen[2];
    deco16_gfx chars, tiles1, tiles2, tiles2_8bpp, sprites;
    uint16_t rom[ROM_END / 2];
    uint8_t sound_rom[0x10000], oki1_rom[0x20000], oki2_rom[0x80000];
    uint16_t ram[0x2000], sprite_buf[0x400], sprite_ram[0x400], pal_ram[0x1000];
    uint16_t palette[0x800];
    uint8_t sound_ram[0x2000];
    uint16_t in0, in1, dsw, dsw_b, priority;
    uint8_t irq_mask, irq_line, sound_latch;
    bool raster_irq, flip_screen;
    uint32_t frames;
    int scanline;
    double main_debt, sound_debt;
    int64_t ym_acc, oki1_acc, oki2_acc, audio_acc;
    int32_t oki1_level, oki2_level;
    int16_t audio[4096];
    int naudio;
    uint16_t screen[W * H], fb[W * H];
} *C_;
#define C (*C_)

static uint16_t prot_address(uint32_t a)       // BITSWAP32(a, 31..18, 13, 12, 11, 17, 16, 15, 14, 10..0) & 0x7fff
{
    uint32_t out = (a & 0xfffc0000u) | (a & 0x7ffu);
    if (a & (1u << 13)) out |= 1u << 17;
    if (a & (1u << 12)) out |= 1u << 16;
    if (a & (1u << 11)) out |= 1u << 15;
    if (a & (1u << 17)) out |= 1u << 14;
    if (a & (1u << 16)) out |= 1u << 13;
    if (a & (1u << 15)) out |= 1u << 12;
    if (a & (1u << 14)) out |= 1u << 11;
    return (uint16_t)(out & 0x7fff);
}

static void update_palette_entry(int i)
{
    const uint16_t b = C.pal_ram[i * 2], gr = C.pal_ram[i * 2 + 1];
    C.palette[i] = arc_rgb565((uint32_t)(gr & 0xff) << 16 | (uint32_t)(gr >> 8) << 8 | (b & 0xff));
}

// ---------------------------------------------------------------------------
// The 68000

static uint16_t main_read(uint32_t a)
{
    a &= 0xffffff;
    if (a < ROM_END) return C.rom[a >> 1];
    if (a >= 0x144000 && a < 0x145000) return C.tilegen[0].data[0][(a & 0xfff) >> 1];
    if (a >= 0x146000 && a < 0x147000) return C.tilegen[0].data[1][(a & 0xfff) >> 1];
    if (a >= 0x14c000 && a < 0x14c800) return C.tilegen[0].rowscroll[0][(a & 0x7ff) >> 1];
    if (a >= 0x14e000 && a < 0x14e800) return C.tilegen[0].rowscroll[1][(a & 0x7ff) >> 1];
    if (a >= 0x154000 && a < 0x155000) return C.tilegen[1].data[0][(a & 0xfff) >> 1];
    if (a >= 0x156000 && a < 0x157000) return C.tilegen[1].data[1][(a & 0xfff) >> 1];
    if (a >= 0x15c000 && a < 0x15c800) return C.tilegen[1].rowscroll[0][(a & 0x7ff) >> 1];
    if (a >= 0x15e000 && a < 0x15e800) return C.tilegen[1].rowscroll[1][(a & 0x7ff) >> 1];
#if ROBOCOP2
    if (a >= 0x180000 && a < 0x180800) return C.sprite_buf[(a & 0x7ff) >> 1];
    if (a >= 0x18c000 && a < 0x190000) return deco146d_read(&C.prot, prot_address(a - 0x18c000));
    if (a >= 0x1a8000 && a < 0x1aa000) return C.pal_ram[(a & 0x1fff) >> 1];
    if (a >= 0x1b0000 && a < 0x1b0008) {
        switch ((a >> 1) & 7) {
        case 1: return (uint16_t)C.scanline;
        case 2:
            m68kd_set_irq(&C.cpu, 3, M68_CLEAR);
            m68kd_set_irq(&C.cpu, 4, M68_CLEAR);
            return 0;
        default: return 0;
        }
    }
    if (a >= 0x1b8000 && a < 0x1bc000) return C.ram[(a & 0x3fff) >> 1];
    if (a >= 0x1f8000 && a < 0x1f8002) return C.dsw_b;
    return 0xffff;
#else
    if (a >= 0x184000 && a < 0x188000) return C.ram[(a & 0x3fff) >> 1];
    if (a >= 0x190000 && a < 0x190008) {
        switch ((a >> 1) & 7) {
        case 1: return (uint16_t)C.scanline;
        case 2:
            m68kd_set_irq(&C.cpu, 3, M68_CLEAR);
            m68kd_set_irq(&C.cpu, 4, M68_CLEAR);
            return 0;
        default: return 0;
        }
    }
    if (a >= 0x19c000 && a < 0x19e000) return C.pal_ram[(a & 0x1fff) >> 1];
    if (a >= 0x1a4000 && a < 0x1a4800) return C.sprite_buf[(a & 0x7ff) >> 1];
    if (a >= 0x1bc000 && a < 0x1c0000) return deco146d_read(&C.prot, prot_address(a - 0x1bc000));
    return 0xffff;
#endif
}

static void write_irq(uint32_t a, uint16_t v)
{
    switch ((a >> 1) & 7) {
    case 0: C.irq_mask = (uint8_t)v; break;
    case 1:
        C.irq_line = (uint8_t)v;
        C.raster_irq = C.irq_line > 0 && C.irq_line < 240 && (C.irq_mask & 2) == 0;
        break;
    default: break;
    }
}

static void write_palette(size_t i, uint16_t v)
{
    if (C.pal_ram[i] != v) {
        C.pal_ram[i] = v;
        update_palette_entry((int)(i >> 1));
    }
}

#if ROBOCOP2
static void apply_robocop2_priority(void);
#endif

static void main_write(uint32_t a, uint16_t v)
{
    a &= 0xffffff;
    if (a < ROM_END) return;
    if (a >= 0x140000 && a < 0x140010) C.tilegen[0].control[((a & 0xf) >> 1) & 7] = v;
    else if (a >= 0x144000 && a < 0x145000) C.tilegen[0].data[0][(a & 0xfff) >> 1] = v;
    else if (a >= 0x146000 && a < 0x147000) C.tilegen[0].data[1][(a & 0xfff) >> 1] = v;
    else if (a >= 0x14c000 && a < 0x14c800) C.tilegen[0].rowscroll[0][(a & 0x7ff) >> 1] = v;
    else if (a >= 0x14e000 && a < 0x14e800) C.tilegen[0].rowscroll[1][(a & 0x7ff) >> 1] = v;
    else if (a >= 0x150000 && a < 0x150010) {
        C.tilegen[1].control[((a & 0xf) >> 1) & 7] = v;
        if ((a & 0xf) == 0) C.flip_screen = (v & 0x80) != 0;
    } else if (a >= 0x154000 && a < 0x155000) C.tilegen[1].data[0][(a & 0xfff) >> 1] = v;
    else if (a >= 0x156000 && a < 0x157000) C.tilegen[1].data[1][(a & 0xfff) >> 1] = v;
    else if (a >= 0x15c000 && a < 0x15c800) C.tilegen[1].rowscroll[0][(a & 0x7ff) >> 1] = v;
    else if (a >= 0x15e000 && a < 0x15e800) C.tilegen[1].rowscroll[1][(a & 0x7ff) >> 1] = v;
#if ROBOCOP2
    else if (a >= 0x180000 && a < 0x180800) C.sprite_buf[(a & 0x7ff) >> 1] = v;
    else if (a >= 0x18c000 && a < 0x190000) deco146d_write(&C.prot, prot_address(a - 0x18c000), v, 0xffff);
    else if (a >= 0x198000 && a < 0x198002) memcpy(C.sprite_ram, C.sprite_buf, sizeof C.sprite_ram);
    else if (a >= 0x1a8000 && a < 0x1aa000) write_palette((a & 0x1fff) >> 1, v);
    else if (a >= 0x1b0000 && a < 0x1b0008) write_irq(a, v);
    else if (a >= 0x1b8000 && a < 0x1bc000) C.ram[(a & 0x3fff) >> 1] = v;
    else if (a >= 0x1f0000 && a < 0x1f0002) {
        if (C.priority != v) {
            C.priority = v;
            apply_robocop2_priority();
        }
    }
#else
    else if (a >= 0x184000 && a < 0x188000) C.ram[(a & 0x3fff) >> 1] = v;
    else if (a >= 0x190000 && a < 0x190008) write_irq(a, v);
    else if (a >= 0x19c000 && a < 0x19e000) write_palette((a & 0x1fff) >> 1, v);
    else if (a >= 0x1a4000 && a < 0x1a4800) C.sprite_buf[(a & 0x7ff) >> 1] = v;
    else if (a >= 0x1b4000 && a < 0x1b4002) memcpy(C.sprite_ram, C.sprite_buf, sizeof C.sprite_ram);
    else if (a >= 0x1bc000 && a < 0x1c0000) deco146d_write(&C.prot, prot_address(a - 0x1bc000), v, 0xffff);
#endif
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
    if (m >= PROT_BASE && m < PROT_BASE + 0x4000) {   // (no read first: the reads have side effects)
        deco146d_write(&C.prot, prot_address(m - PROT_BASE), (uint16_t)(v << 8 | v), (a & 1) ? 0x00ff : 0xff00);
        return;
    }
    const uint16_t w = main_read(a & ~1u);
    main_write(a & ~1u, (a & 1) ? (uint16_t)((w & 0xff00) | v) : (uint16_t)((w & 0x00ff) | (v << 8)));
}

static uint16_t port_a(void *u) { (void)u; return C.in0; }
static uint16_t port_b(void *u) { (void)u; return C.in1; }
static uint16_t port_c(void *u) { (void)u; return C.dsw; }
static void soundlatch(void *u, uint8_t v)
{
    (void)u;
    C.sound_latch = v;
    hu6280d_set_irq_line(&C.snd, 0, HU_HOLD);
}

// ---------------------------------------------------------------------------
// The sound HuC6280

static uint8_t snd_rd(void *u, uint32_t a)
{
    (void)u;
    if (a < 0x10000) return C.sound_rom[a];
    switch (a) {
    case 0x100000: return ym2203d_status(&C.ym2203);
    case 0x100001: return ym2203d_read(&C.ym2203);
    case 0x110001: return ym2151_status(&C.ym2151);
    case 0x120000: case 0x120001: return okid_read(&C.oki1);
    case 0x130000: case 0x130001: return okid_read(&C.oki2);
    case 0x140000: case 0x140001: return C.sound_latch;
    default: break;
    }
    if (a >= 0x1f0000 && a < 0x1f2000) return C.sound_ram[a & 0x1fff];
    if (a >= 0x1fec00 && a < 0x1fec02) return hu6280d_timer_r(&C.snd);
    if (a >= 0x1ff400 && a < 0x1ff404) return hu6280d_irq_status_r(&C.snd, (uint8_t)(a & 3));
    return 0xff;
}

static void snd_wr(void *u, uint32_t a, uint8_t v)
{
    (void)u;
    if (a < 0x10000) return;
    switch (a) {
    case 0x100000: ym2203d_control(&C.ym2203, v); return;
    case 0x100001: ym2203d_write(&C.ym2203, v); return;
    case 0x110000: ym2151_select(&C.ym2151, v); return;
    case 0x110001: ym2151_write(&C.ym2151, v); return;
    case 0x120000: case 0x120001: okid_write(&C.oki1, v); return;
    case 0x130000: case 0x130001: okid_write(&C.oki2, v); return;
    default: break;
    }
    if (a >= 0x1f0000 && a < 0x1f2000) C.sound_ram[a & 0x1fff] = v;
    else if (a >= 0x1fec00 && a < 0x1fec02) hu6280d_timer_w(&C.snd, (uint8_t)(a & 1), v);
    else if (a >= 0x1ff400 && a < 0x1ff404) hu6280d_irq_status_w(&C.snd, (uint8_t)(a & 3), v);
}

static void snd_cycles(void *u, int cycles)
{
    (void)u;
    C.ym_acc += (int64_t)cycles * YM2151_CLOCK;
    const int ym_cycles = (int)(C.ym_acc / SOUND_CLOCK);
    C.ym_acc -= (int64_t)ym_cycles * SOUND_CLOCK;
    ym2151_run_timers(&C.ym2151, ym_cycles);
    C.oki1_acc += (int64_t)cycles * okid_sample_frequency(&C.oki1);
    while (C.oki1_acc >= SOUND_CLOCK) {
        C.oki1_acc -= SOUND_CLOCK;
        C.oki1_level = okid_update(&C.oki1);
    }
    C.oki2_acc += (int64_t)cycles * okid_sample_frequency(&C.oki2);
    while (C.oki2_acc >= SOUND_CLOCK) {
        C.oki2_acc -= SOUND_CLOCK;
        C.oki2_level = okid_update(&C.oki2);
    }
    C.audio_acc += (int64_t)cycles * RATE;
    while (C.audio_acc >= SOUND_CLOCK) {
        C.audio_acc -= SOUND_CLOCK;
        const int32_t s = (ym2151_update(&C.ym2151) * 2 + ym2203d_update(&C.ym2203) * 2 + C.oki1_level * 2 + C.oki2_level) / 6;
        if (C.naudio < 4096) C.audio[C.naudio++] = arc_clamp16(s);
    }
}

static void ym_irq(void *u, bool on) { (void)u; hu6280d_set_irq_line(&C.snd, 1, on ? HU_ASSERT : HU_CLEAR); }
static void ym_port(void *u, uint8_t v) { (void)u; C.oki2.bank_offset = (v & 1) ? 0x40000 : 0; }

// ---------------------------------------------------------------------------
// Video

static void draw_sprites(int priority)
{
    const int global_x = ROBOCOP2 ? 304 : 240, color_add = 0x300;
    for (int f = 0; f < 0x100; f++) {
        const uint16_t x0 = C.sprite_ram[f * 4 + 2];
        if (((x0 >> 8) & 0xc0) != priority) continue;
        const uint16_t y0 = C.sprite_ram[f * 4];
        if ((y0 & 0x1000) && (C.frames & 1)) continue;
        const int color = (x0 >> 9) & 0x1f;
        const bool flip_x = (y0 & 0x2000) != 0, flip_y = (y0 & 0x4000) != 0;
        int multi = (1 << ((y0 & 0x0600) >> 9)) - 1;
        int code = C.sprite_ram[f * 4 + 1] & ~multi, inc;
        if (flip_y) inc = -1;
        else {
            code += multi;
            inc = 1;
        }
        if (code == 0) continue;
        const int sx = (global_x - x0) & 0x1ff, sy = (240 - y0) & 0x1ff;
        for (; multi >= 0; multi--) {
            const int tile = code - multi * inc;
            const int py0 = (sy - 16 * multi) & 0x1ff;
            const uint8_t *g = C.sprites.px + (size_t)(((tile % C.sprites.total) + C.sprites.total) % C.sprites.total) * 256;
            for (int r = 0; r < 16; r++) {
                const int py = ((py0 + r) & 0x1ff) - FIRST_LINE;
                if (py < 0 || py >= H) continue;
                const uint8_t *row = g + (flip_y ? 15 - r : r) * 16;
                uint16_t *out = &C.screen[py * W];
                for (int c = 0; c < 16; c++) {
                    const int px = sx + c;
                    if (px < 0 || px >= W) continue;
                    const int pen = row[flip_x ? 15 - c : c];
                    if (pen) out[px] = C.palette[(color_add + color * 16 + pen) & 0x7ff];
                }
            }
        }
    }
}

static void pf(int chip, int playfield, bool transparent)
{
    if (!deco16ic_enabled(&C.tilegen[chip], playfield)) return;
    deco16ic_draw(&C.tilegen[chip], playfield, C.screen, W, H, FIRST_LINE, C.palette, !transparent);
}

static void update_video(void)
{
#if ROBOCOP2
    if ((C.priority & 4) == 0) {
        for (int i = 0; i < W * H; i++) C.screen[i] = C.palette[0];
        pf(1, 1, false);
    } else {
        draw_sprites(0xc0);                     // (drawn, then covered: as dsp-cpp)
        for (int i = 0; i < W * H; i++) C.screen[i] = C.palette[0x200];
    }
    draw_sprites(0x80);
    if (C.priority & 8) {
        pf(0, 1, true);
        draw_sprites(0x40);
        pf(1, 0, true);
    } else {
        pf(1, 0, (C.priority & 4) == 0);
        draw_sprites(0x40);
        pf(0, 1, true);
    }
#else
    for (int i = 0; i < W * H; i++) C.screen[i] = C.palette[0];
    pf(1, 1, false);
    draw_sprites(0x80);
    pf(1, 0, true);
    draw_sprites(0x40);
    pf(0, 1, true);
#endif
    draw_sprites(0);
    pf(0, 0, true);
    if (C.flip_screen) {
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) C.fb[y * W + x] = C.screen[(H - 1 - y) * W + (W - 1 - x)];
    } else memcpy(C.fb, C.screen, sizeof C.fb);
}

// ---------------------------------------------------------------------------

static const int kX8[8] = {0, 1, 2, 3, 4, 5, 6, 7};
static const int kY8[8] = {0, 16, 32, 48, 64, 80, 96, 112};
static const int kX16[16] = {256, 257, 258, 259, 260, 261, 262, 263, 0, 1, 2, 3, 4, 5, 6, 7};
static const int kY16[16] = {0, 16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 240};
static const int kSprX[16] = {512, 513, 514, 515, 516, 517, 518, 519, 0, 1, 2, 3, 4, 5, 6, 7};
static const int kSprY[16] = {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 480};

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

// each 512 KB ROM split into its 256 KB halves for the plane layout
static void rearrange(const uint8_t *src, uint8_t *dst, int roms)
{
    static const uint32_t two[4][2] = {{0x00000, 0x00000}, {0x80000, 0x40000}, {0x40000, 0x80000}, {0xc0000, 0xc0000}};
    static const uint32_t three[6][2] = {{0x00000, 0x00000}, {0xc0000, 0x40000}, {0x40000, 0x80000},
                                         {0x100000, 0xc0000}, {0x80000, 0x100000}, {0x140000, 0x140000}};
    for (int i = 0; i < roms * 2; i++) {
        const uint32_t *m = roms == 2 ? two[i] : three[i];
        memcpy(dst + m[0], src + m[1], 0x40000);
    }
}

static bool decode16(deco16_gfx *g, const uint8_t *raw, size_t size, int total, int planes, const int *offs)
{
    arc_layout l = {16, 16, total, planes, 64 * 8, false, false, false, {0}, kX16, kY16};
    for (int i = 0; i < planes; i++) l.plane_offsets[i] = offs[i];
    *g = (deco16_gfx){arc_gfx_decode(&l, raw, size), total};
    return g->px != NULL;
}

static bool load_gfx(void)
{
    uint8_t *raw = (uint8_t *)calloc(1, 0x300000), *gfx = (uint8_t *)calloc(1, 0x180000);
    bool ok = raw && gfx;
    if (ok && (ok = load_interleaved(kChars, NROM(kChars), raw, 0x20000))) {
        const arc_layout l = {8, 8, 0x1000, 4, 16 * 8, false, false, false, {0x10000 * 8 + 8, 0x10000 * 8, 8, 0}, kX8, kY8};
        C.chars = (deco16_gfx){arc_gfx_decode(&l, raw, 0x20000), 0x1000};
        ok = C.chars.px != NULL;
    }
    static const int p1[4] = {N_TILES1 * 512 + 8, N_TILES1 * 512, 8, 0}, p2[4] = {N_TILES2 * 512 + 8, N_TILES2 * 512, 8, 0};
#if ROBOCOP2
    if (ok) {
        memset(raw, 0, 0x100000);
        if ((ok = arc_load(kTiles1, NROM(kTiles1), raw, 0x100000))) {
            rearrange(raw, gfx, 2);
            ok = decode16(&C.tiles1, gfx, 0x100000, N_TILES1, 4, p1);
        }
    }
    if (ok) {
        memset(raw, 0, 0x180000);
        if ((ok = arc_load(kTiles2, NROM(kTiles2), raw, 0x180000))) {
            rearrange(raw, gfx, 3);
            static const int p8[8] = {0x100000 * 8 + 8, 0x100000 * 8, 0x40000 * 8 + 8, 0x40000 * 8, 0xc0000 * 8 + 8, 0xc0000 * 8, 8, 0};
            ok = decode16(&C.tiles2, gfx, 0x180000, N_TILES2, 4, p2) && decode16(&C.tiles2_8bpp, gfx, 0x180000, 0x1000, 8, p8);
        }
    }
#else
    if (ok) {
        memset(raw, 0, 0x80000);
        if ((ok = arc_load(kTiles1, NROM(kTiles1), raw, 0x80000))) ok = decode16(&C.tiles1, raw, 0x80000, N_TILES1, 4, p1);
    }
    if (ok) {
        memset(raw, 0, 0x100000);
        if ((ok = arc_load(kTiles2, NROM(kTiles2), raw, 0x100000))) {
            rearrange(raw, gfx, 2);
            ok = decode16(&C.tiles2, gfx, 0x100000, N_TILES2, 4, p2);
        }
    }
#endif
    free(gfx);
    if (ok) {
        memset(raw, 0, N_SPRITES * 128);
        if ((ok = load_interleaved(kSprites, NROM(kSprites), raw, N_SPRITES * 128))) {
            const arc_layout l = {16, 16, N_SPRITES, 4, 128 * 8, false, false, false, {16, 0, 24, 8}, kSprX, kSprY};
            C.sprites = (deco16_gfx){arc_gfx_decode(&l, raw, N_SPRITES * 128), N_SPRITES};
            ok = C.sprites.px != NULL;
        }
    }
    free(raw);
    return ok;
}

#if ROBOCOP2
static int tile_bank(int bank) { return (bank & 0x30) << 8; }
#else
static int tile_bank(int bank) { return ((bank >> 4) & 0xf) != 0 ? 0 : 0x1000; }
#endif

static void configure_tilegens(void)
{
    deco16_layer l;
    deco16ic_layer_defaults(&l);                // chip 0: the chars and tiles1, pf2 colour bank 16
    l.gfx8 = &C.chars;
    l.gfx16 = &C.tiles1;
    C.tilegen[0].layers[0] = l;
    l.color_bank = 16;
#if ROBOCOP2
    l.bank = tile_bank;
#endif
    C.tilegen[0].layers[1] = l;
    deco16ic_layer_defaults(&l);                // chip 1: the chars and tiles2, base 0x200, banked
    l.gfx8 = &C.chars;
    l.gfx16 = &C.tiles2;
    l.color_base16 = 0x200;
    l.bank = tile_bank;
    C.tilegen[1].layers[0] = l;
    l.color_bank = 48;
    C.tilegen[1].layers[1] = l;
}

#if ROBOCOP2
static void apply_robocop2_priority(void)     // bit 2: the second chip's 16x16 tiles at 8 bpp
{
    const bool use_8bpp = (C.priority & 4) != 0;
    for (int p = 0; p < 2; p++) {
        deco16_layer *l = &C.tilegen[1].layers[p];
        l->gfx16 = use_8bpp ? &C.tiles2_8bpp : &C.tiles2;
        l->color_mask16 = use_8bpp ? 0 : 0x0f;
    }
}
#endif

bool arc_init(void)
{
    if (!(C_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    uint8_t *image = (uint8_t *)malloc(ROM_END);
    bool ok = image != NULL;
    if (ok) {
        memset(image, 0xff, ROM_END);
        if ((ok = load_interleaved(kProgram, NROM(kProgram), image, ROM_END)))
            for (int i = 0; i < ROM_END / 2; i++) C.rom[i] = (uint16_t)(image[2 * i] << 8 | image[2 * i + 1]);
        free(image);
    }
    memset(C.sound_rom, 0xff, sizeof C.sound_rom);
    ok = ok && ARC_LOAD(kSound, C.sound_rom, sizeof C.sound_rom) && ARC_LOAD(kOki1, C.oki1_rom, sizeof C.oki1_rom) &&
         ARC_LOAD(kOki2, C.oki2_rom, sizeof C.oki2_rom) && load_gfx();
    if (!ok) return false;

    m68kd_init(&C.cpu, MAIN_CLOCK, M68K_T_68000);
    C.cpu.read_word = cpu_rd;
    C.cpu.write_word = cpu_wr;
    C.cpu.read_byte = cpu_rd8;
    C.cpu.write_byte = cpu_wr8;
    hu6280d_init(&C.snd, SOUND_CLOCK);
    C.snd.read = snd_rd;
    C.snd.write = snd_wr;
    C.snd.cycles = snd_cycles;
    ym2203d_init(&C.ym2203, YM2203_CLOCK, 0.7f, 0.7f);
    ym2151_init(&C.ym2151, YM2151_CLOCK, RATE);
    C.ym2151.amplitude = 0.7f;
    C.ym2151.irq_handler = ym_irq;
    C.ym2151.port_handler = ym_port;
    okid_init(&C.oki1, SOUND_XTAL / 32, true, C.oki1_rom, sizeof C.oki1_rom);
    okid_init(&C.oki2, SOUND_XTAL / 16, true, C.oki2_rom, sizeof C.oki2_rom);
    deco146d_init(&C.prot, ROBOCOP2 ? DECO146 : DECO104, DECO146_MAGIC_ADDRESS_XOR);
    C.prot.port_a = port_a;
    C.prot.port_b = port_b;
    C.prot.port_c = port_c;
    C.prot.soundlatch = soundlatch;
#if ROBOCOP2
    C.dsw = 0x7fbf;
    C.dsw_b = 0x00ff;
#else
    C.dsw = 0x7fff;
#endif
    configure_tilegens();
    arc_reset();
    return true;
}

void arc_reset(void)
{
    memset(C.ram, 0, sizeof C.ram);
    memset(C.sprite_buf, 0, sizeof C.sprite_buf);
    memset(C.sprite_ram, 0, sizeof C.sprite_ram);
    memset(C.pal_ram, 0, sizeof C.pal_ram);
    memset(C.palette, 0, sizeof C.palette);
    memset(C.sound_ram, 0, sizeof C.sound_ram);
    deco16ic_reset(&C.tilegen[0]);
    deco16ic_reset(&C.tilegen[1]);
    deco146d_reset(&C.prot);
    C.in0 = 0xffff;
    C.in1 = 0x00f7;
    C.irq_mask = C.irq_line = C.sound_latch = 0;
    C.priority = 0;
#if ROBOCOP2
    apply_robocop2_priority();
#endif
    C.raster_irq = C.flip_screen = false;
    C.scanline = 0;
    C.main_debt = C.sound_debt = 0;
    C.ym_acc = C.oki1_acc = C.oki2_acc = C.audio_acc = 0;
    C.oki1_level = C.oki2_level = 0;
    m68kd_reset(&C.cpu);
    hu6280d_reset(&C.snd);
    ym2203d_reset(&C.ym2203);
    ym2151_reset(&C.ym2151);
    okid_reset(&C.oki1);
    okid_reset(&C.oki2);
    C.oki2.bank_offset = 0;
}

int arc_run_frame(const int16_t **audio)
{
    const double main_cycles = (double)MAIN_CLOCK / FPS / SCANLINES;
    const double sound_cycles = (double)SOUND_CLOCK / FPS / SCANLINES;
    C.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        C.scanline = line;
        if (line <= 240) {
            if (C.raster_irq && C.irq_line == line) {
                m68kd_set_irq(&C.cpu, (C.irq_mask & 0x10) ? 3 : 4, M68_ASSERT);
                C.raster_irq = false;
            }
            if (line == 8) C.in1 &= (uint16_t)~0x08;
        }
        if (line == 248) {
            m68kd_set_irq(&C.cpu, 5, M68_HOLD);
            update_video();
            C.in1 |= 0x08;
        }
        C.main_debt += main_cycles;
        C.main_debt -= m68kd_run(&C.cpu, (int)C.main_debt);
        C.sound_debt += sound_cycles;
        C.sound_debt -= hu6280d_run(&C.snd, (int)C.sound_debt);
    }
    C.frames++;
    if (audio) *audio = C.audio;
    return C.naudio;
}

const uint16_t *arc_frame(void) { return C.fb; }

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
    C.in0 = (uint16_t)(pad(in->p[0]) | pad(in->p[1]) << 8);
    uint16_t sys = (uint16_t)((C.in1 & 0x08) | 0x07);
    if (in->coin1) sys &= (uint16_t)~0x01;
    if (in->coin2) sys &= (uint16_t)~0x02;
    C.in1 = sys;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) C.dsw = (uint16_t)((C.dsw & 0xff00) | v);
    else if (bank == 1) C.dsw = (uint16_t)((C.dsw & 0x00ff) | (v << 8));
}
