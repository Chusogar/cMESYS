// dec0.c - Data East's DEC0 / DEC1 boards (1987-1990) for the common arcade
// frontends (arc.h): C port of dsp-cpp's drivers/arcade/dec0 (after
// dsp-emulator's dec0_hw.pas). A 68000 at 10 MHz (cpu/m68kdsp) with three
// BAC06 playfields and the MXC06 sprites (video/deco_bac06d), a YM2203, a
// YM3812 and an OKI M6295. On DEC0 a 6502 drives the sound, with an i8751
// (Bad Dudes) or a HuC6280 (Hippodrome) for the protection; on DEC1 (Sly Spy,
// Boulder Dash) a HuC6280 drives it and the video and sound maps are banked
// by a state machine.
// ARC_VARIANT: 0 Bad Dudes vs. Dragonninja, 1 Hippodrome, 2 Sly Spy,
// 3 Boulder Dash, 4 Dragonninja (Japan), 5 Secret Agent (World): the last two
// are cMESYS's own (dsp-cpp has the aliases, not their ROMs).
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m68kdsp/m68kdsp.h"
#include "../../../cpu/m6502/m65xx.h"
#include "../../../cpu/hu6280dsp/hu6280dsp.h"
#include "../../../cpu/mcs51dsp/mcs51dsp.h"
#include "../../../sound/ym3812dsp.h"
#include "../../../sound/ym2203dsp.h"
#include "../../../sound/okim6295dsp.h"
#include "../../../video/deco_bac06d.h"

#ifndef ARC_VARIANT
#define ARC_VARIANT 0
#endif
#define BADDUDES (ARC_VARIANT == 0 || ARC_VARIANT == 4)
#define HIPPODRM (ARC_VARIANT == 1)
#define SLYSPY (ARC_VARIANT == 2 || ARC_VARIANT == 5)
#define BOULDASH (ARC_VARIANT == 3)
#define DEC1 (SLYSPY || BOULDASH)

#define W 256
#define H 240
#define FPS 57.444885
#define SCANLINES 272
#define MAIN_CLOCK 10000000
#define SOUND_CLOCK 1500000
#define MCU_CLOCK (21477200 / 16)
#define DEC1_SOUND_CLOCK (12000000 / 4)
#define MCS51_CLOCK 8000000
#define RATE 44100

#if BADDUDES
#if ARC_VARIANT == 4
static const char *const kSets[] = { "drgninja", NULL };
const arc_info arc_game = { "drgninja", "Dragonninja", "Data East", 1988, W, H, FPS, RATE, kSets,
                            { "GOLPE", "SALTO", NULL }, ARC_JOY_8WAY };
static const arc_rom kMain[] = {{"eg04-1.3c", 0x10000, 0x00000, 0x41b8b3f8}, {"eg01-1.3a", 0x10000, 0x00001, 0xe08e6885},
                                {"eg06.6c", 0x10000, 0x40000, 0x2b81faf7}, {"eg03.6a", 0x10000, 0x40001, 0xc52c2e9d}};
static const arc_rom kMcu[] = {{"eg31.9a", 0x1000, 0, 0x657aab2d}};
static const arc_rom kSound[] = {{"eg07.8a", 0x8000, 0x8000, 0x001d2f51}};
static const arc_rom kOki[] = {{"eg08.2c", 0x10000, 0, 0x92f2c916}};
static const arc_rom kChar[] = {{"eg25.15h", 0x8000, 0x0000, 0x6791bc20}, {"eg26.16h", 0x8000, 0x8000, 0x5d75fc8f}};
static const arc_rom kTiles1[] = {{"eg18.14d", 0x10000, 0x00000, 0x05cfc3e5}, {"eg20.17d", 0x10000, 0x10000, 0xe11e988f},
                                  {"eg22.14f", 0x10000, 0x20000, 0xb893d880}, {"eg24.17f", 0x10000, 0x30000, 0x6f226dda}};
static const arc_rom kTiles2[] = {{"eg30.9h", 0x10000, 0x20000, 0x2438e67e}, {"eg28.9f", 0x10000, 0x30000, 0x5c692ab3}};
static const arc_rom kSprites[] = {{"eg15.16c", 0x10000, 0x00000, 0x5617d67f}, {"eg16.17c", 0x08000, 0x10000, 0x17e42633},
                                   {"eg11.16a", 0x10000, 0x20000, 0xba83e8d8}, {"eg12.17a", 0x08000, 0x30000, 0xfea2a134},
                                   {"eg13.13c", 0x10000, 0x40000, 0xfd91e08e}, {"eg14.14c", 0x08000, 0x50000, 0xe83c760a},
                                   {"eg09.13a", 0x10000, 0x60000, 0x601b7b23}, {"eg10.14a", 0x08000, 0x70000, 0xeeee8a1a}};
#else
static const char *const kSets[] = { "baddudes", NULL };
const arc_info arc_game = { "baddudes", "Bad Dudes vs. Dragonninja", "Data East", 1988, W, H, FPS, RATE, kSets,
                            { "GOLPE", "SALTO", NULL }, ARC_JOY_8WAY };
static const arc_rom kMain[] = {{"ei04-1.3c", 0x10000, 0x00000, 0x4bf158a7}, {"ei01-1.3a", 0x10000, 0x00001, 0x74f5110c},
                                {"ei06.6c", 0x10000, 0x40000, 0x3ff8da57}, {"ei03.6a", 0x10000, 0x40001, 0xf8f2bd94}};
static const arc_rom kMcu[] = {{"ei31.9a", 0x1000, 0, 0x2a8745d2}};
static const arc_rom kSound[] = {{"ei07.8a", 0x8000, 0x8000, 0x9fb1ef4b}};
static const arc_rom kOki[] = {{"ei08.2c", 0x10000, 0, 0x3c87463e}};
static const arc_rom kChar[] = {{"ei25.15j", 0x8000, 0x0000, 0xbcf59a69}, {"ei26.16j", 0x8000, 0x8000, 0x9aff67b8}};
static const arc_rom kTiles1[] = {{"ei18.14d", 0x10000, 0x00000, 0x05cfc3e5}, {"ei20.17d", 0x10000, 0x10000, 0xe11e988f},
                                  {"ei22.14f", 0x10000, 0x20000, 0xb893d880}, {"ei24.17f", 0x10000, 0x30000, 0x6f226dda}};
static const arc_rom kTiles2[] = {{"ei30.9j", 0x10000, 0x20000, 0x982da0d1}, {"ei28.9f", 0x10000, 0x30000, 0xf01ebb3b}};
static const arc_rom kSprites[] = {{"ei15.16c", 0x10000, 0x00000, 0xa38a7d30}, {"ei16.17c", 0x08000, 0x10000, 0x17e42633},
                                   {"ei11.16a", 0x10000, 0x20000, 0x3a77326c}, {"ei12.17a", 0x08000, 0x30000, 0xfea2a134},
                                   {"ei13.13c", 0x10000, 0x40000, 0xe5ae2751}, {"ei14.14c", 0x08000, 0x50000, 0xe83c760a},
                                   {"ei09.13a", 0x10000, 0x60000, 0x6901e628}, {"ei10.14a", 0x08000, 0x70000, 0xeeee8a1a}};
#endif
enum { N_CHARS = 0x800, N_TILES1 = 0x800, N_TILES2 = 0x400, N_SPRITES = 0x1000 };
enum { CHARS_SIZE = 0x10000, TILES1_SIZE = 0x40000, TILES2_SIZE = 0x40000, SPRITES_SIZE = 0x80000, OKI_SIZE = 0x10000 };
#elif HIPPODRM
static const char *const kSets[] = { "hippodrm", NULL };
const arc_info arc_game = { "hippodrm", "Hippodrome", "Data East", 1989, W, H, FPS, RATE, kSets,
                            { "GOLPE", "SALTO", NULL }, ARC_JOY_8WAY };
static const arc_rom kMain[] = {{"ew02", 0x10000, 0x00000, 0xdf0d7dc6}, {"ew01", 0x10000, 0x00001, 0xd5670aa7},
                                {"ew05", 0x10000, 0x20000, 0xc76d65ec}, {"ew00", 0x10000, 0x20001, 0xe9b427a6}};
static const arc_rom kMcu[] = {{"ew08", 0x10000, 0, 0x53010534}};
static const arc_rom kSound[] = {{"ew04", 0x8000, 0x8000, 0x9871b98d}};
static const arc_rom kOki[] = {{"ew03", 0x10000, 0, 0xb606924d}};
static const arc_rom kChar[] = {{"ew14", 0x10000, 0x00000, 0x71ca593d}, {"ew13", 0x10000, 0x10000, 0x86be5fa7}};
static const arc_rom kTiles1[] = {{"ew19", 0x8000, 0x00000, 0x6b80d7a3}, {"ew18", 0x8000, 0x08000, 0x78d3d764},
                                  {"ew20", 0x8000, 0x10000, 0xce9f5de3}, {"ew21", 0x8000, 0x18000, 0x487a7ba2}};
static const arc_rom kTiles2[] = {{"ew24", 0x8000, 0x00000, 0x4e1bc2a4}, {"ew25", 0x8000, 0x08000, 0x9eb47dfb},
                                  {"ew23", 0x8000, 0x10000, 0x9ecf479e}, {"ew22", 0x8000, 0x18000, 0xe55669aa}};
static const arc_rom kSprites[] = {{"ew15", 0x10000, 0x00000, 0x95423914}, {"ew16", 0x10000, 0x10000, 0x96233177},
                                   {"ew10", 0x10000, 0x20000, 0x4c25dfe8}, {"ew11", 0x10000, 0x30000, 0xf2e007fc},
                                   {"ew06", 0x10000, 0x40000, 0xe4bb8199}, {"ew07", 0x10000, 0x50000, 0x470b6989},
                                   {"ew17", 0x10000, 0x60000, 0x8c97c757}, {"ew12", 0x10000, 0x70000, 0xa2d244bc}};
enum { N_CHARS = 0x1000, N_TILES1 = 0x400, N_TILES2 = 0x400, N_SPRITES = 0x1000 };
enum { CHARS_SIZE = 0x20000, TILES1_SIZE = 0x20000, TILES2_SIZE = 0x20000, SPRITES_SIZE = 0x80000, OKI_SIZE = 0x10000 };
#elif SLYSPY
#if ARC_VARIANT == 5
static const char *const kSets[] = { "secretag", NULL };
const arc_info arc_game = { "secretag", "Secret Agent", "Data East", 1989, W, H, FPS, RATE, kSets,
                            { "DISPARO", "SALTO", NULL }, ARC_JOY_8WAY };
static const arc_rom kMain[] = {{"fb14-3.17l", 0x10000, 0x00000, 0x9be6ac90}, {"fb12-3.9l", 0x10000, 0x00001, 0x28904b6b},
                                {"fb15.19l", 0x10000, 0x20000, 0x106bb26c}, {"fb13.11l", 0x10000, 0x20001, 0x90523413}};
static const arc_rom kSound[] = {{"fb10.5h", 0x10000, 0, 0xdfd2ff25}};
static const arc_rom kOki[] = {{"fa11.11k", 0x20000, 0, 0x4e547bad}};
static const arc_rom kChar[] = {{"fb05.11a", 0x8000, 0x0000, 0x09802924}, {"fb04.9a", 0x8000, 0x8000, 0xec25b895}};
static const arc_rom kTiles1[] = {{"fb07.17a", 0x10000, 0x00000, 0xe932268b}, {"fb06.15a", 0x10000, 0x10000, 0xc4dd38c0}};
static const arc_rom kTiles2[] = {{"fb09.22a", 0x20000, 0x00000, 0x1395e9be}, {"fb08.21a", 0x20000, 0x20000, 0x4d7464db}};
static const arc_rom kSprites[] = {{"fb01.4a", 0x20000, 0x00000, 0x99b0cd92}, {"fb03.7a", 0x20000, 0x20000, 0x0e7ea74d},
                                   {"fb00.2a", 0x20000, 0x40000, 0xf7df3fd7}, {"fb02.5a", 0x20000, 0x60000, 0x84e8da9d}};
#else
static const char *const kSets[] = { "slyspy", NULL };
const arc_info arc_game = { "slyspy", "Sly Spy", "Data East", 1989, W, H, FPS, RATE, kSets,
                            { "DISPARO", "SALTO", NULL }, ARC_JOY_8WAY };
static const arc_rom kMain[] = {{"fa14-4.17l", 0x10000, 0x00000, 0x60f16e31}, {"fa12-4.9l", 0x10000, 0x00001, 0xb9b9fdcf},
                                {"fa15.19l", 0x10000, 0x20000, 0x04a79266}, {"fa13.11l", 0x10000, 0x20001, 0x641cc4b3}};
static const arc_rom kSound[] = {{"fa10.5h", 0x10000, 0, 0xdfd2ff25}};
static const arc_rom kOki[] = {{"fa11.11k", 0x20000, 0, 0x4e547bad}};
static const arc_rom kChar[] = {{"fa05.11a", 0x8000, 0x0000, 0x09802924}, {"fa04.9a", 0x8000, 0x8000, 0xec25b895}};
static const arc_rom kTiles1[] = {{"fa07.17a", 0x10000, 0x00000, 0xe932268b}, {"fa06.15a", 0x10000, 0x10000, 0xc4dd38c0}};
static const arc_rom kTiles2[] = {{"fa09.22a", 0x20000, 0x00000, 0x1395e9be}, {"fa08.21a", 0x20000, 0x20000, 0x4d7464db}};
static const arc_rom kSprites[] = {{"fa01.4a", 0x20000, 0x00000, 0x99b0cd92}, {"fa03.7a", 0x20000, 0x20000, 0x0e7ea74d},
                                   {"fa00.2a", 0x20000, 0x40000, 0xf7df3fd7}, {"fa02.5a", 0x20000, 0x60000, 0x84e8da9d}};
#endif
enum { N_CHARS = 0x800, N_TILES1 = 0x400, N_TILES2 = 0x800, N_SPRITES = 0x1000 };
enum { CHARS_SIZE = 0x10000, TILES1_SIZE = 0x20000, TILES2_SIZE = 0x40000, SPRITES_SIZE = 0x80000, OKI_SIZE = 0x20000 };
#else
static const char *const kSets[] = { "bouldash", NULL };
const arc_info arc_game = { "bouldash", "Boulder Dash / Boulder Dash Part 2", "Data East", 1990, W, H, FPS, RATE, kSets,
                            { "EMPUJAR", NULL }, ARC_JOY_4WAY };
static const arc_rom kMain[] = {{"fw-15-2.17l", 0x10000, 0x00000, 0xca19a967}, {"fw-12-2.9l", 0x10000, 0x00001, 0x242bdc2a},
                                {"fw-16-2.19l", 0x10000, 0x20000, 0xb7217265}, {"fw-13-2.11l", 0x10000, 0x20001, 0x19209ef4},
                                {"fw-17-2.20l", 0x10000, 0x40000, 0x78a632a1}, {"fw-14-2.13l", 0x10000, 0x40001, 0x69b6112d}};
static const arc_rom kSound[] = {{"fn-10", 0x10000, 0, 0xc74106e7}};
static const arc_rom kOki[] = {{"fn-11", 0x10000, 0, 0x990fd8d9}};
static const arc_rom kChar[] = {{"fn-04", 0x10000, 0x00000, 0x40f5a760}, {"fn-05", 0x10000, 0x10000, 0x824f2168}};
static const arc_rom kTiles1[] = {{"fn-07", 0x10000, 0x00000, 0xeac6a3b3}, {"fn-06", 0x10000, 0x10000, 0x3feee292}};
static const arc_rom kTiles2[] = {{"fn-09", 0x20000, 0x00000, 0xc2b27bd2}, {"fn-08", 0x20000, 0x20000, 0x5ac97178}};
static const arc_rom kSprites[] = {{"fn-01", 0x10000, 0x00000, 0x9333121b}, {"fn-03", 0x10000, 0x10000, 0x254ba60f},
                                   {"fn-00", 0x10000, 0x20000, 0xec18d098}, {"fn-02", 0x10000, 0x30000, 0x4f060cba}};
enum { N_CHARS = 0x1000, N_TILES1 = 0x400, N_TILES2 = 0x800, N_SPRITES = 0x800 };
enum { CHARS_SIZE = 0x20000, TILES1_SIZE = 0x20000, TILES2_SIZE = 0x40000, SPRITES_SIZE = 0x40000, OKI_SIZE = 0x10000 };
#endif
#define NROM(k) ((int)(sizeof k / sizeof k[0]))

static struct state {
    m68kd cpu;
#if DEC1
    hu6280d snd;                                // (the sound CPU)
#else
    m65xx snd;
#endif
#if HIPPODRM
    hu6280d mcu;
#elif BADDUDES
    mcs51 mcu;
#endif
    ym3812d ym3812;
    ym2203d ym2203;
    okim6295d oki;
    bac06_chip bac;
    bac06_gfx gchars, gtiles1, gtiles2, gsprites;
    uint16_t rom[0x30000];                      // big-endian words
    uint16_t ram1[0xc00], ram2[0x2000], sprite_buffer[0x800], palette_ram[0x800];
    uint16_t pal[0x400];
    uint8_t sound_memory[0x10000], mcu_ram[0x2000], mcu_shared_ram[0x2000], oki_rom[OKI_SIZE];
#if HIPPODRM
    uint8_t mcu_rom[0x10000];
#endif
    uint16_t pens[256 * 256];
    uint8_t sound_latch, priority, hippodrm_lsb, slyspy_state, slyspy_sound_state;
    uint16_t i8751_return, i8751_command;
    uint8_t i8751_ports[4];
    uint16_t in0, in1, dsw;
    int main_debt, sound_debt, mcu_debt;
    uint32_t frames;
    int64_t audio_acc;
    int16_t audio[1024];
    int naudio;
    uint16_t fb[W * H];
} *D_;
#define D (*D_)

static uint8_t pal4bit(uint16_t v) { return (uint8_t)((v & 15) * 0x11); }

static uint8_t bitswap_0654321_7(uint8_t v)     // bitswap8(v, 0, 6, 5, 4, 3, 2, 1, 7)
{
    return (uint8_t)(((v & 1) << 7) | (v & 0x7e) | (v >> 7));
}

static void update_palette_entry(int i)
{
    unsigned r, g, b;
#if DEC1
    const uint16_t v = D.palette_ram[i];
    r = pal4bit(v);
    g = pal4bit((uint16_t)(v >> 4));
    b = pal4bit((uint16_t)(v >> 8));
#else
    r = D.palette_ram[i] & 0xff;
    g = D.palette_ram[i] >> 8;
    b = D.palette_ram[0x400 + i] & 0xff;
#endif
    D.pal[i] = arc_rgb565(r << 16 | g << 8 | b);
}

static void write_palette(int i, uint16_t v, int plane)
{
    const int p = i + plane * 0x400;
    if (D.palette_ram[p] == v) return;
    D.palette_ram[p] = v;
    update_palette_entry(i);
}

static void generate_audio(int cycles, uint32_t clock)
{
    D.audio_acc += (int64_t)cycles * RATE;
    while (D.audio_acc >= clock) {
        D.audio_acc -= clock;
        const int32_t s = ym3812d_update(&D.ym3812) + ym2203d_update(&D.ym2203) + okid_update(&D.oki);
        if (D.naudio < 1024) D.audio[D.naudio++] = arc_clamp16(s);
    }
}

// ---------------------------------------------------------------------------
// The 68000: DEC0

#if !DEC1
static uint16_t main_read(uint32_t a)
{
    bac06_layer *t1 = &D.bac.tile[0], *t2 = &D.bac.tile[1], *t3 = &D.bac.tile[2];
    if (a < 0x60000) return D.rom[a >> 1];
    if (a >= 0x180000 && a <= 0x180fff) return D.mcu_shared_ram[(a & 0xfff) >> 1];
    if (a >= 0x240000 && a <= 0x24ffff) {         // (write only, but read before a byte write)
        switch (a & 0xfff000) {
        case 0x000000:
            if ((a & 0xff8) == 0x000) return t1->control_0[(a & 7) >> 1];
            if ((a & 0xff8) == 0x010) return t1->control_1[(a & 7) >> 1];
            break;
        case 0x006000:
            if ((a & 0xff8) == 0x000) return t2->control_0[(a & 7) >> 1];
            if ((a & 0xff8) == 0x010) return t2->control_1[(a & 7) >> 1];
            break;
        case 0x00c000:
            if ((a & 0xff8) == 0x000) return t3->control_0[(a & 7) >> 1];
            if ((a & 0xff8) == 0x010) return t3->control_1[(a & 7) >> 1];
            break;
        default: break;
        }
        if (a <= 0x24207f) return t1->colscroll[(a & 0x7f) >> 1];
        if (a >= 0x242400 && a <= 0x2427ff) return t1->rowscroll[(a & 0x3ff) >> 1];
        if (a >= 0x242800 && a <= 0x243fff) return D.ram1[(a - 0x242800) >> 1];
        if (a >= 0x244000 && a <= 0x245fff) return t1->data[(a & 0x1fff) >> 1];
        if (a >= 0x248000 && a <= 0x24807f) return t2->colscroll[(a & 0x7f) >> 1];
        if (a >= 0x248400 && a <= 0x2487ff) return t2->rowscroll[(a & 0x3ff) >> 1];
        if (a >= 0x24a000 && a <= 0x24a7ff) return t2->data[(a & 0x7ff) >> 1];
        if (a >= 0x24c800 && a <= 0x24c87f) return t3->colscroll[(a & 0x7f) >> 1];
        if (a >= 0x24cc00 && a <= 0x24cfff) return t3->rowscroll[(a & 0x3ff) >> 1];
        if (a >= 0x24d000 && a <= 0x24d7ff) return t3->data[(a & 0x7ff) >> 1];
        return 0;
    }
    switch (a) {
    case 0x30c000: return D.in0;
    case 0x30c002: return D.in1;
    case 0x30c004: return D.dsw;
    case 0x30c006: return 0xffff;
    case 0x30c008: return D.i8751_return;
    default: break;
    }
    if (a >= 0x310000 && a <= 0x3107ff) return D.palette_ram[(a & 0x7ff) >> 1];
    if (a >= 0x314000 && a <= 0x3147ff) return D.palette_ram[((a & 0x7ff) >> 1) + 0x400];
    if (a >= 0xff8000 && a <= 0xffbfff) return D.ram2[(a & 0x3fff) >> 1];
    if (a >= 0xffc000 && a <= 0xffcfff) return D.sprite_buffer[(a & 0x7ff) >> 1];
    return 0;
}

static void main_write(uint32_t a, uint16_t v)
{
    if (a < 0x60000) return;
    if (a >= 0x180000 && a <= 0x180fff) {
        D.mcu_shared_ram[(a & 0xfff) >> 1] = (uint8_t)v;
#if HIPPODRM
        if ((a & 0xfff) == 0xffe) hu6280d_set_irq_line(&D.mcu, 0, HU_HOLD);
#endif
        return;
    }
    static const uint32_t base[3] = {0x240000, 0x246000, 0x24c000};
    for (int l = 0; l < 3; l++) {
        if (a >= base[l] && a <= base[l] + 7) {
            bac06_control0(&D.bac.tile[l], (int)((a & 7) >> 1), v);
            return;
        }
        if (a >= base[l] + 0x10 && a <= base[l] + 0x17) {
            bac06_control1(&D.bac.tile[l], (int)((a & 7) >> 1), v, false);
            return;
        }
    }
    bac06_layer *t1 = &D.bac.tile[0], *t2 = &D.bac.tile[1], *t3 = &D.bac.tile[2];
    if (a >= 0x242000 && a <= 0x24207f) t1->colscroll[(a & 0x7f) >> 1] = v;
    else if (a >= 0x242400 && a <= 0x2427ff) t1->rowscroll[(a & 0x3ff) >> 1] = v;
    else if (a >= 0x242800 && a <= 0x243fff) D.ram1[(a - 0x242800) >> 1] = v;
    else if (a >= 0x244000 && a <= 0x245fff) t1->data[(a & 0x1fff) >> 1] = v;
    else if (a >= 0x248000 && a <= 0x24807f) t2->colscroll[(a & 0x7f) >> 1] = v;
    else if (a >= 0x248400 && a <= 0x2487ff) t2->rowscroll[(a & 0x3ff) >> 1] = v;
    else if (a >= 0x24a000 && a <= 0x24a7ff) t2->data[(a & 0x7ff) >> 1] = v;
    else if (a >= 0x24c800 && a <= 0x24c87f) t3->colscroll[(a & 0x7f) >> 1] = v;
    else if (a >= 0x24cc00 && a <= 0x24cfff) t3->rowscroll[(a & 0x3ff) >> 1] = v;
    else if (a >= 0x24d000 && a <= 0x24d7ff) t3->data[(a & 0x7ff) >> 1] = v;
    else if (a >= 0x30c010 && a <= 0x30c01f) {
        switch (a & 0xf) {
        case 0x0: D.priority = (uint8_t)v; break;
        case 0x2: memcpy(D.bac.sprite_ram, D.sprite_buffer, 0x400 * sizeof(uint16_t)); break;
        case 0x4:
            D.sound_latch = (uint8_t)v;
            m65xx_set_nmi(&D.snd, M65_PULSE);
            break;
        case 0x6:
            D.i8751_command = v;
#if BADDUDES
            if (D.i8751_ports[2] & 8) mcs51_set_irq1_line(&D.mcu, MCS51_ASSERT);
#endif
            break;
        case 0xe:
            D.i8751_command = 0;
            D.i8751_return = 0;
            break;
        default: break;
        }
    } else if (a >= 0x310000 && a <= 0x3107ff) write_palette((int)((a & 0x7ff) >> 1), v, 0);
    else if (a >= 0x314000 && a <= 0x3147ff) write_palette((int)((a & 0x7ff) >> 1), v, 1);
    else if (a >= 0xff8000 && a <= 0xffbfff) D.ram2[(a & 0x3fff) >> 1] = v;
    else if (a >= 0xffc000 && a <= 0xffcfff) D.sprite_buffer[(a & 0x7ff) >> 1] = v;
}

// the sound 6502
static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x07ff || a >= 0x8000) return D.sound_memory[a];
    if (a == 0x3000) return D.sound_latch;
    if (a == 0x3800) return okid_read(&D.oki);
    return 0;
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x07ff) {
        D.sound_memory[a] = v;
        return;
    }
    switch (a) {
    case 0x0800: ym2203d_control(&D.ym2203, v); break;
    case 0x0801: ym2203d_write(&D.ym2203, v); break;
    case 0x1000: ym3812d_control(&D.ym3812, v); break;
    case 0x1001: ym3812d_write(&D.ym3812, v); break;
    case 0x3800: okid_write(&D.oki, v); break;
    default: break;
    }
}

static void sound_cycles(void *u, int c) { (void)u; generate_audio(c, SOUND_CLOCK); }
static void ym_irq(void *u, bool on) { (void)u; m65xx_set_irq(&D.snd, on ? M65_ASSERT : M65_CLEAR); }
#endif

#if HIPPODRM
// the HuC6280 (the protection)
static uint8_t hippo_mcu_read(void *u, uint32_t a)
{
    (void)u;
    if (a <= 0xffff) return D.mcu_rom[a];
    if (a >= 0x180000 && a <= 0x1800ff) return D.mcu_shared_ram[a & 0xff];
    if (a == 0x1807ff) return 0xff;
    if (a >= 0x1d0000 && a <= 0x1d00ff) {         // the handshake
        if (D.hippodrm_lsb == 0x45) return 0x4e;
        if (D.hippodrm_lsb == 0x92) return 0x15;
        return 0;
    }
    if (a >= 0x1a1000 && a <= 0x1a17ff) {
        const uint16_t w = D.bac.tile[2].data[(a & 0x7ff) >> 1];
        return (uint8_t)((a & 1) ? w >> 8 : w);
    }
    if (a >= 0x1f0000 && a <= 0x1f1fff) return D.mcu_ram[a & 0x1fff];
    if (a >= 0x1ff402 && a <= 0x1ff403) return (uint8_t)(D.in1 >> 7);
    return 0;
}

static uint16_t merge_byte(uint16_t cur, uint32_t a, uint8_t v)
{
    return (a & 1) ? (uint16_t)((cur & 0x00ff) | (v << 8)) : (uint16_t)((cur & 0xff00) | v);
}

static void hippo_mcu_write(void *u, uint32_t a, uint8_t v)
{
    (void)u;
    bac06_layer *t3 = &D.bac.tile[2];
    if (a <= 0xffff) return;
    if (a >= 0x180000 && a <= 0x1800ff) D.mcu_shared_ram[a & 0xff] = v;
    else if (a >= 0x1a0000 && a <= 0x1a0007) {
        const int p = (int)((a & 7) >> 1);
        bac06_control0(t3, p, merge_byte(t3->control_0[p], a, v));
    } else if (a >= 0x1a0010 && a <= 0x1a001f) {
        const int p = (int)((a & 7) >> 1);
        bac06_control1(t3, p, merge_byte(t3->control_1[p], a, v), false);
    } else if (a >= 0x1a1000 && a <= 0x1a17ff) {
        const size_t p = (a & 0x7ff) >> 1;
        t3->data[p] = merge_byte(t3->data[p], a, v);
    } else if (a >= 0x1d0000 && a <= 0x1d00ff) D.hippodrm_lsb = v;
    else if (a >= 0x1f0000 && a <= 0x1f1fff) D.mcu_ram[a & 0x1fff] = v;
    else if (a >= 0x1ff400 && a <= 0x1ff403) hu6280d_irq_status_w(&D.mcu, (uint8_t)(a & 3), v);
}
#endif

#if BADDUDES
// the i8751 (the protection)
static uint8_t mcu_port0_read(void *u)
{
    (void)u;
    uint8_t r = 0xff;
    if ((D.i8751_ports[2] & 0x10) == 0) r &= (uint8_t)(D.i8751_command >> 8);
    if ((D.i8751_ports[2] & 0x20) == 0) r &= (uint8_t)(D.i8751_command & 0xff);
    return r;
}

static uint8_t mcu_port_ff(void *u) { (void)u; return 0xff; }

static void mcu_port_write(int port, uint8_t v)
{
    if (port != 2) {
        D.i8751_ports[port] = v;
        return;
    }
    if ((v & 4) == 0 && (D.i8751_ports[2] & 4) != 0) m68kd_set_irq(&D.cpu, 5, M68_HOLD);
    if ((v & 8) == 0) mcs51_set_irq1_line(&D.mcu, MCS51_CLEAR);
    if ((v & 0x40) != 0 && (D.i8751_ports[2] & 0x40) == 0) D.i8751_return = (uint16_t)((D.i8751_return & 0xff00) | D.i8751_ports[0]);
    if ((v & 0x80) != 0 && (D.i8751_ports[2] & 0x80) == 0) D.i8751_return = (uint16_t)((D.i8751_return & 0x00ff) | (D.i8751_ports[0] << 8));
    D.i8751_ports[2] = v;
}
static void mcu_p0_w(void *u, uint8_t v) { (void)u; mcu_port_write(0, v); }
static void mcu_p1_w(void *u, uint8_t v) { (void)u; mcu_port_write(1, v); }
static void mcu_p2_w(void *u, uint8_t v) { (void)u; mcu_port_write(2, v); }
static void mcu_p3_w(void *u, uint8_t v) { (void)u; mcu_port_write(3, v); }
#endif

// ---------------------------------------------------------------------------
// The 68000: DEC1 (Sly Spy, Boulder Dash)

#if DEC1
static uint16_t main_read(uint32_t a)
{
    bac06_layer *t1 = &D.bac.tile[0], *t2 = &D.bac.tile[1], *t3 = &D.bac.tile[2];
    if (a < 0x60000) return D.rom[a >> 1];
    if (a >= 0x240000 && a <= 0x24ffff) {
        const uint32_t key = (a & 0xffff) | ((uint32_t)D.slyspy_state << 16);
        if ((key & 0xffff) == 0x4000) {
            D.slyspy_state = (uint8_t)((D.slyspy_state + 1) & 3);
            return 0;
        }
        switch (key) {                          // (read before every byte write)
        case 0x0000: case 0x0002: case 0x0004: case 0x0006: return t2->control_0[(a & 7) >> 1];
        case 0x0010: case 0x0012: case 0x0014: case 0x0016: return t2->control_1[(a & 7) >> 1];
        case 0x8000: case 0x8002: case 0x8004: case 0x8006: return t1->control_0[(a & 7) >> 1];
        case 0x8010: case 0x8012: case 0x8014: case 0x8016: return t1->control_1[(a & 7) >> 1];
        default: break;
        }
        const uint32_t o = key & 0xffff;
        if (key <= 0xffff) {                    // state 0
            if (o >= 0x2000 && o <= 0x207f) return t2->colscroll[(o & 0x7f) >> 1];
            if (o >= 0x2400 && o <= 0x27ff) return t2->rowscroll[(o & 0x3ff) >> 1];
            if (o >= 0x6000 && o <= 0x7fff) return t2->data[(o & 0x1fff) >> 1];
            if (o >= 0xc000 && o <= 0xc07f) return t1->colscroll[(o & 0x7f) >> 1];
            if (o >= 0xc400 && o <= 0xc7ff) return t1->rowscroll[(o & 0x3ff) >> 1];
            if (o >= 0xe000) return t1->data[(o & 0x1fff) >> 1];
            return 0;
        }
        if (key >= 0x18000 && key <= 0x19fff) return t1->data[(o & 0x1fff) >> 1];
        if (key >= 0x1c000 && key <= 0x1dfff) return t2->data[(o & 0x1fff) >> 1];
        if (key >= 0x20000 && key <= 0x21fff) return t2->data[(o & 0x1fff) >> 1];
        if (key >= 0x22000 && key <= 0x23fff) return t1->data[(o & 0x1fff) >> 1];
        if (key >= 0x30000 && key <= 0x31fff) return t1->data[(o & 0x1fff) >> 1];
        if (key >= 0x38000 && key <= 0x39fff) return t2->data[(o & 0x1fff) >> 1];
        return 0;
    }
    if (a >= 0x300800 && a <= 0x30087f) return t3->colscroll[(a & 0x7f) >> 1];
    if (a >= 0x300c00 && a <= 0x300fff) return t3->rowscroll[(a & 0x3ff) >> 1];
    if (a >= 0x301000 && a <= 0x3017ff) return t3->data[(a & 0x7ff) >> 1];
    if (a >= 0x304000 && a <= 0x307fff) return D.ram2[(a & 0x3fff) >> 1];
    if (a >= 0x308000 && a <= 0x3087ff) return D.sprite_buffer[(a & 0x7ff) >> 1];
    if (a >= 0x310000 && a <= 0x3107ff) return D.palette_ram[(a & 0x7ff) >> 1];
    if (a >= 0x314008 && a <= 0x31400f) {
        switch ((a & 7) >> 1) {
        case 0: return D.dsw;
        case 1: return D.in0;
        case 2: return D.in1;
        default: return 0xffff;
        }
    }
    if (a >= 0x31c000 && a <= 0x31c00f) {
        switch (a & 0xe) {
        case 0x2: return 0x13;
        case 0x6: return 2;
        case 0xc: return (uint16_t)(D.ram2[0x2028 >> 1] >> 8);
        default: return 0;
        }
    }
    return 0;
}

static void main_write(uint32_t a, uint16_t v)
{
    bac06_layer *t1 = &D.bac.tile[0], *t2 = &D.bac.tile[1], *t3 = &D.bac.tile[2];
    if (a < 0x60000) return;
    if (a >= 0x240000 && a <= 0x24ffff) {
        const uint32_t key = (a & 0xffff) | ((uint32_t)D.slyspy_state << 16), o = a & 0xffff;
        if (o == 0xa000) {
            D.slyspy_state = 0;
            return;
        }
        if (key <= 0xffff) {                    // state 0
            if (o <= 0x0007) bac06_control0(t2, (int)((o & 7) >> 1), v);
            else if (o >= 0x0010 && o <= 0x0017) bac06_control1(t2, (int)((o & 7) >> 1), v, true);
            else if (o >= 0x2000 && o <= 0x207f) t2->colscroll[(o & 0x7f) >> 1] = v;
            else if (o >= 0x2400 && o <= 0x27ff) t2->rowscroll[(o & 0x3ff) >> 1] = v;
            else if (o >= 0x6000 && o <= 0x7fff) t2->data[(o & 0x1fff) >> 1] = v;
            else if (o >= 0x8000 && o <= 0x8007) bac06_control0(t1, (int)((o & 7) >> 1), v);
            else if (o >= 0x8010 && o <= 0x8017) bac06_control1(t1, (int)((o & 7) >> 1), v, false);
            else if (o >= 0xc000 && o <= 0xc07f) t1->colscroll[(o & 0x7f) >> 1] = v;
            else if (o >= 0xc400 && o <= 0xc7ff) t1->rowscroll[(o & 0x3ff) >> 1] = v;
            else if (o >= 0xe000) t1->data[(o & 0x1fff) >> 1] = v;
            return;
        }
        if (key >= 0x18000 && key <= 0x19fff) t1->data[(o & 0x1fff) >> 1] = v;
        else if (key >= 0x1c000 && key <= 0x1dfff) t2->data[(o & 0x1fff) >> 1] = v;
        else if (key >= 0x20000 && key <= 0x21fff) t2->data[(o & 0x1fff) >> 1] = v;
        else if (key >= 0x22000 && key <= 0x23fff) t1->data[(o & 0x1fff) >> 1] = v;
        else if (key >= 0x30000 && key <= 0x31fff) t1->data[(o & 0x1fff) >> 1] = v;
        else if (key >= 0x38000 && key <= 0x39fff) t2->data[(o & 0x1fff) >> 1] = v;
        return;
    }
    if (a >= 0x300000 && a <= 0x300007) bac06_control0(t3, (int)((a & 7) >> 1), v);
    else if (a >= 0x300010 && a <= 0x300017) bac06_control1(t3, (int)((a & 7) >> 1), v, false);
    else if (a >= 0x300800 && a <= 0x30087f) t3->colscroll[(a & 0x7f) >> 1] = v;
    else if (a >= 0x300c00 && a <= 0x300fff) t3->rowscroll[(a & 0x3ff) >> 1] = v;
    else if (a >= 0x301000 && a <= 0x3017ff) t3->data[(a & 0x7ff) >> 1] = v;
    else if (a >= 0x304000 && a <= 0x307fff) D.ram2[(a & 0x3fff) >> 1] = v;
    else if (a >= 0x308000 && a <= 0x3087ff) {
        D.sprite_buffer[(a & 0x7ff) >> 1] = v;
        memcpy(D.bac.sprite_ram, D.sprite_buffer, 0x400 * sizeof(uint16_t));
    } else if (a >= 0x310000 && a <= 0x3107ff) write_palette((int)((a & 0x7ff) >> 1), v, 0);
    else if (a >= 0x314000 && a <= 0x31400f) {
        switch ((a & 0xf) >> 1) {
        case 0:
            D.sound_latch = (uint8_t)v;
            hu6280d_set_irq_line(&D.snd, 0, HU_HOLD);
            break;
        case 1: D.priority = (uint8_t)v; break;
        default: break;
        }
    }
}

// the sound HuC6280
static uint8_t sound_read(void *u, uint32_t a)
{
    (void)u;
    if (a <= 0xffff) return D.sound_memory[a];
    if (a >= 0x80000 && a <= 0xfffff) {
        const uint32_t key = (a & 0x7ffff) | ((uint32_t)D.slyspy_sound_state * 0x80000);
        switch (key) {
        case 0x020000: case 0x0a0000: case 0x120000: case 0x1a0000:
            D.slyspy_sound_state = (uint8_t)((D.slyspy_sound_state + 1) & 3);
            return 0;
        case 0x050000: case 0x0d0000: case 0x150000: case 0x1d0000:
            D.slyspy_sound_state = 0;
            return 0;
        case 0x060000: case 0x090000: case 0x130000: case 0x1f0000: return okid_read(&D.oki);
        case 0x070000: case 0x0c0000: case 0x110000: case 0x1e0000: return D.sound_latch;
        default: return 0;
        }
    }
    if (a >= 0x1f0000) return D.mcu_ram[a & 0x1fff];
    return 0;
}

static void sound_write(void *u, uint32_t a, uint8_t v)
{
    (void)u;
    if (a <= 0xffff) return;
    if (a >= 0x80000 && a <= 0xfffff) {
        const uint32_t key = (a & 0x7ffff) | ((uint32_t)D.slyspy_sound_state * 0x80000);
        switch (key) {
        case 0x010000: case 0x0f0000: case 0x170000: case 0x190000: ym3812d_control(&D.ym3812, v); break;
        case 0x010001: case 0x0f0001: case 0x170001: case 0x190001: ym3812d_write(&D.ym3812, v); break;
        case 0x030000: case 0x0e0000: case 0x140000: case 0x1c0000: ym2203d_control(&D.ym2203, v); break;
        case 0x030001: case 0x0e0001: case 0x140001: case 0x1c0001: ym2203d_write(&D.ym2203, v); break;
        case 0x060000: case 0x090000: case 0x130000: case 0x1f0000: okid_write(&D.oki, v); break;
        default: break;
        }
        return;
    }
    if (a >= 0x1f0000) D.mcu_ram[a & 0x1fff] = v;
}

static void sound_cycles(void *u, int c) { (void)u; generate_audio(c, DEC1_SOUND_CLOCK); }
static void ym_irq(void *u, bool on) { (void)u; hu6280d_set_irq_line(&D.snd, 1, on ? HU_ASSERT : HU_CLEAR); }
#endif

static uint16_t cpu_rd(void *u, uint32_t a) { (void)u; return main_read(a); }
static void cpu_wr(void *u, uint32_t a, uint16_t v) { (void)u; main_write(a, v); }

// ---------------------------------------------------------------------------
// Video

static void update_video(void)
{
    bac06_layer *t1 = &D.bac.tile[0], *t2 = &D.bac.tile[1], *t3 = &D.bac.tile[2];
    const bool odd = (D.frames & 1) != 0;
    memset(D.pens, 0, sizeof D.pens);
#if BADDUDES
    if ((D.priority & 1) == 0) {
        bac06_draw(t2, &D.gtiles1, false, false, D.pens);
        bac06_draw(t3, &D.gtiles2, true, false, D.pens);
        if (D.priority & 2) bac06_draw(t2, &D.gtiles1, true, true, D.pens);
        bac06_draw_sprites(&D.bac, &D.gsprites, 0, 0, odd, D.pens);
        if (D.priority & 4) bac06_draw(t3, &D.gtiles2, true, true, D.pens);
    } else {
        bac06_draw(t3, &D.gtiles2, false, false, D.pens);
        bac06_draw(t2, &D.gtiles1, true, false, D.pens);
        if (D.priority & 2) bac06_draw(t3, &D.gtiles2, true, true, D.pens);
        bac06_draw_sprites(&D.bac, &D.gsprites, 0, 0, odd, D.pens);
        if (D.priority & 4) bac06_draw(t2, &D.gtiles1, true, true, D.pens);
    }
#elif HIPPODRM
    if (D.priority & 1) {
        bac06_draw(t2, &D.gtiles1, false, false, D.pens);
        bac06_draw(t3, &D.gtiles2, true, false, D.pens);
    } else {
        bac06_draw(t3, &D.gtiles2, false, false, D.pens);
        bac06_draw(t2, &D.gtiles1, true, false, D.pens);
    }
    bac06_draw_sprites(&D.bac, &D.gsprites, 0, 0, odd, D.pens);
#else
    bac06_draw(t3, &D.gtiles2, false, false, D.pens);
    bac06_draw(t2, &D.gtiles1, true, false, D.pens);
    bac06_draw_sprites(&D.bac, &D.gsprites, 0, 0, odd, D.pens);
    if (D.priority & 0x80) bac06_draw(t2, &D.gtiles1, true, true, D.pens);
#endif
    bac06_draw(t1, &D.gchars, true, false, D.pens);
}

static void present(void)
{
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) D.fb[y * W + x] = D.pal[D.pens[(y + 8) * 256 + x] & 0x3ff];
}

// ---------------------------------------------------------------------------

static const int kTileX[16] = {128, 129, 130, 131, 132, 133, 134, 135, 0, 1, 2, 3, 4, 5, 6, 7};
static const int kTileY[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120};
static const int kCharX[8] = {0, 1, 2, 3, 4, 5, 6, 7};

static uint8_t *decode(const uint8_t *raw, size_t size, const arc_layout *l)
{
    return raw ? arc_gfx_decode(l, raw, size) : NULL;
}

static void swap_blocks(uint8_t *d, uint32_t a, uint32_t b, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        const uint8_t t = d[a + i];
        d[a + i] = d[b + i];
        d[b + i] = t;
    }
}

static bool load_gfx(void)
{
    uint8_t *raw = (uint8_t *)malloc(SPRITES_SIZE > TILES1_SIZE ? (SPRITES_SIZE > TILES2_SIZE ? SPRITES_SIZE : TILES2_SIZE) : TILES1_SIZE);
    if (!raw) return false;
    bool ok;
    // characters: 8x8, four planes
    memset(raw, 0, CHARS_SIZE);
    if ((ok = arc_load(kChar, NROM(kChar), raw, CHARS_SIZE))) {
#if SLYSPY
        swap_blocks(raw, 0x0000, 0x4000, 0x4000);
        swap_blocks(raw, 0x8000, 0xc000, 0x4000);
#elif BOULDASH
        swap_blocks(raw, 0x00000, 0x08000, 0x8000);
        swap_blocks(raw, 0x10000, 0x18000, 0x8000);
#endif
        const arc_layout cl = {8, 8, N_CHARS, 4, 64, false, false, false,
                               {0, N_CHARS * 64 * 2, N_CHARS * 64, N_CHARS * 64 * 3}, kCharX, kTileY};
        D.gchars = (bac06_gfx){decode(raw, CHARS_SIZE, &cl), N_CHARS};
        ok = D.gchars.px != NULL;
    }
    // tiles and sprites: 16x16, four planes
#define TILE_LAYOUT(n) {16, 16, n, 4, 256, false, false, false, {(n) * 256, (n) * 256 * 3, 0, (n) * 256 * 2}, kTileX, kTileY}
    if (ok) {
        memset(raw, 0, TILES1_SIZE);
        const arc_layout tl = TILE_LAYOUT(N_TILES1);
        if ((ok = arc_load(kTiles1, NROM(kTiles1), raw, TILES1_SIZE))) {
            D.gtiles1 = (bac06_gfx){decode(raw, TILES1_SIZE, &tl), N_TILES1};
            ok = D.gtiles1.px != NULL;
        }
    }
    if (ok) {
        memset(raw, 0, TILES2_SIZE);
        const arc_layout tl = TILE_LAYOUT(N_TILES2);
        if ((ok = arc_load(kTiles2, NROM(kTiles2), raw, TILES2_SIZE))) {
#if BADDUDES
            // the two ROMs are loaded high and rearranged into the first half
            memmove(raw + 0x8000, raw + 0x20000, 0x8000);
            memmove(raw + 0x0000, raw + 0x28000, 0x8000);
            memmove(raw + 0x18000, raw + 0x30000, 0x8000);
            memmove(raw + 0x10000, raw + 0x38000, 0x8000);
#endif
            D.gtiles2 = (bac06_gfx){decode(raw, TILES2_SIZE, &tl), N_TILES2};
            ok = D.gtiles2.px != NULL;
        }
    }
    if (ok) {
        memset(raw, 0, SPRITES_SIZE);
        const arc_layout sl = TILE_LAYOUT(N_SPRITES);
        if ((ok = arc_load(kSprites, NROM(kSprites), raw, SPRITES_SIZE))) {
            D.gsprites = (bac06_gfx){decode(raw, SPRITES_SIZE, &sl), N_SPRITES};
            ok = D.gsprites.px != NULL;
        }
    }
    free(raw);
    return ok;
}

static bool load_main(void)
{
    uint8_t *tmp = (uint8_t *)calloc(1, 0x60000), *one = (uint8_t *)malloc(0x10000);
    bool ok = tmp && one;
    for (int i = 0; ok && i < NROM(kMain); i++) {
        arc_rom r = kMain[i];
        r.offset = 0;
        if (!(ok = arc_load(&r, 1, one, r.size))) break;
        for (uint32_t j = 0; j < r.size; j++) tmp[(kMain[i].offset & ~1u) + j * 2 + (kMain[i].offset & 1u)] = one[j];
    }
    if (ok)
        for (int i = 0; i < 0x30000; i++) D.rom[i] = (uint16_t)(tmp[i * 2] << 8 | tmp[i * 2 + 1]);
    free(tmp);
    free(one);
    return ok;
}

bool arc_init(void)
{
    if (!(D_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    bool ok = load_main() && ARC_LOAD(kSound, D.sound_memory, sizeof D.sound_memory) && ARC_LOAD(kOki, D.oki_rom, sizeof D.oki_rom);
#if DEC1
    for (int i = 0; i < 0x10000; i++) D.sound_memory[i] = bitswap_0654321_7(D.sound_memory[i]);   // (scrambled)
#endif
#if HIPPODRM
    ok = ok && ARC_LOAD(kMcu, D.mcu_rom, sizeof D.mcu_rom);
    for (int i = 0; i < 0x10000; i++) D.mcu_rom[i] = bitswap_0654321_7(D.mcu_rom[i]);
    D.mcu_rom[0x189] = D.mcu_rom[0x1af] = D.mcu_rom[0x1db] = D.mcu_rom[0x21a] = 0x60;   // (the protection: RTS)
#elif BADDUDES
    mcs51_init(&D.mcu);
    ok = ok && ARC_LOAD(kMcu, D.mcu.rom, 0x1000);
#endif
    ok = ok && load_gfx();
    if (!ok) return false;
    (void)bitswap_0654321_7;

    m68kd_init(&D.cpu, MAIN_CLOCK, M68K_T_68000);
    D.cpu.read_word = cpu_rd;
    D.cpu.write_word = cpu_wr;
#if DEC1
    hu6280d_init(&D.snd, DEC1_SOUND_CLOCK);
#endif
    D.snd.read = sound_read;
    D.snd.write = sound_write;
    D.snd.cycles = sound_cycles;
#if HIPPODRM
    hu6280d_init(&D.mcu, MCU_CLOCK);
    D.mcu.read = hippo_mcu_read;
    D.mcu.write = hippo_mcu_write;
#elif BADDUDES
    D.mcu.clock = MCS51_CLOCK / 12;
    D.mcu.port_read[0] = mcu_port0_read;
    D.mcu.port_read[1] = D.mcu.port_read[2] = D.mcu.port_read[3] = mcu_port_ff;
    D.mcu.port_write[0] = mcu_p0_w;
    D.mcu.port_write[1] = mcu_p1_w;
    D.mcu.port_write[2] = mcu_p2_w;
    D.mcu.port_write[3] = mcu_p3_w;
#endif
    ym3812d_init(&D.ym3812, 3000000, false, 1.0f);
    D.ym3812.irq = ym_irq;
    ym2203d_init(&D.ym2203, 1500000, 1.0f, 1.0f);
    okid_init(&D.oki, 1000000, true, D.oki_rom, sizeof D.oki_rom);
    bac06_init(&D.bac, 0x000, 0x200, 0x300, 1, 1, 1, 0x100, 0x0f);
    D.dsw = 0xffff;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    m68kd_reset(&D.cpu);
    D.main_debt = D.sound_debt = D.mcu_debt = 0;
    D.frames = 0;
    memset(D.ram1, 0, sizeof D.ram1);
    memset(D.ram2, 0, sizeof D.ram2);
    memset(D.sprite_buffer, 0, sizeof D.sprite_buffer);
    memset(D.palette_ram, 0, sizeof D.palette_ram);
    memset(D.pal, 0, sizeof D.pal);
    memset(D.mcu_ram, 0, sizeof D.mcu_ram);
    memset(D.mcu_shared_ram, 0, sizeof D.mcu_shared_ram);
    memset(D.pens, 0, sizeof D.pens);
#if DEC1
    hu6280d_reset(&D.snd);
#else
    m65xx_reset(&D.snd);
#endif
#if HIPPODRM
    hu6280d_reset(&D.mcu);
#elif BADDUDES
    mcs51_reset(&D.mcu);
    D.i8751_return = D.i8751_command = 0;
    memset(D.i8751_ports, 0, sizeof D.i8751_ports);
#endif
    ym3812d_reset(&D.ym3812);
    ym2203d_reset(&D.ym2203);
    okid_reset(&D.oki);
    bac06_reset(&D.bac);
    D.in0 = 0xffff;
    D.in1 = 0x00f7;
    D.sound_latch = D.priority = D.hippodrm_lsb = D.slyspy_state = D.slyspy_sound_state = 0;
    D.audio_acc = 0;
}

int arc_run_frame(const int16_t **audio)
{
    const double main_per_line = (double)MAIN_CLOCK / FPS / SCANLINES;
#if DEC1
    const double sound_per_line = (double)DEC1_SOUND_CLOCK / FPS / SCANLINES;
    const uint16_t vblank_bit = 0x0008;
#else
    const double sound_per_line = (double)SOUND_CLOCK / FPS / SCANLINES;
    const uint16_t vblank_bit = 0x0080;
#endif
#if HIPPODRM
    const double mcu_per_line = (double)MCU_CLOCK / FPS / SCANLINES;
#elif BADDUDES
    const double mcu_per_line = (double)D.mcu.clock / FPS / SCANLINES;
#endif
    double main_budget = D.main_debt, sound_budget = D.sound_debt, mcu_budget = D.mcu_debt;
    D.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 8) D.in1 = (uint16_t)(D.in1 & ~vblank_bit);
        else if (line == 248) {
            m68kd_set_irq(&D.cpu, 6, M68_HOLD);
#if HIPPODRM
            hu6280d_set_irq_line(&D.mcu, 0, HU_HOLD);
#endif
            update_video();
            D.in1 = (uint16_t)(D.in1 | vblank_bit);
        }
        main_budget += main_per_line;
        main_budget -= m68kd_run(&D.cpu, (int)main_budget);
        sound_budget += sound_per_line;
#if DEC1
        sound_budget -= hu6280d_run(&D.snd, (int)sound_budget);
#else
        sound_budget -= m65xx_run(&D.snd, (int)sound_budget);
        mcu_budget += mcu_per_line;
#if BADDUDES
        mcu_budget -= mcs51_run(&D.mcu, (int)mcu_budget);
#else
        mcu_budget -= hu6280d_run(&D.mcu, (int)mcu_budget);
#endif
#endif
    }
    D.main_debt = (int)main_budget;
    D.sound_debt = (int)sound_budget;
    D.mcu_debt = (int)mcu_budget;
    D.frames++;
    present();
    if (audio) *audio = D.audio;
    return D.naudio;
}

const uint16_t *arc_frame(void) { return D.fb; }

void arc_input(const arc_in *in)
{
    const unsigned p1 = in->p[0], p2 = in->p[1];
    static const unsigned bits[7] = {AP_UP, AP_DOWN, AP_LEFT, AP_RIGHT, AP_B1, AP_B2, AP_B3};
    uint16_t in0 = 0xffff;
    for (int b = 0; b < 7; b++) {
        if (p1 & bits[b]) in0 &= (uint16_t)~(1u << b);
        if (p2 & bits[b]) in0 &= (uint16_t)~(0x100u << b);
    }
#if DEC1
    const uint16_t vblank_bit = 0x0008;
    uint16_t in1 = (uint16_t)(0xffff & ~vblank_bit);
    if (p1 & AP_START) in0 &= 0xff7f;
    if (p2 & AP_START) in0 &= 0x7fff;
    if (in->coin1) in1 &= 0xfffe;
    if (in->coin2) in1 &= 0xfffd;
#else
    const uint16_t vblank_bit = 0x0080;
    uint16_t in1 = (uint16_t)(0xffff & ~vblank_bit);
    if (p1 & AP_START) in1 &= 0xfffb;
    if (p2 & AP_START) in1 &= 0xfff7;
    if (in->coin1) in1 &= 0xffef;
    if (in->coin2) in1 &= 0xffdf;
#endif
    D.in0 = in0;
    D.in1 = (uint16_t)((in1 & ~vblank_bit) | (D.in1 & vblank_bit));
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) D.dsw = (uint16_t)((D.dsw & 0xff00) | v);
    else if (bank == 1) D.dsw = (uint16_t)((D.dsw & 0x00ff) | (v << 8));
}
