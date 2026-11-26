// system18.c - Sega System 18 (1989-1992) for the common arcade frontends
// (arc.h): C port of dsp-cpp's drivers/arcade/system18. The System 16B
// tilemaps / sprites (video/sega16d) plus a Mega Drive VDP (video/
// sega_315_5313) mixed into one picture, a 68000 at 10 MHz (FD1094 on most),
// the 315-5195 mapper (machine/sega5195d) and 315-5296 I/O, a Z80 at 8 MHz
// with two YM3438 and an RF5C68, and Moonwalker's i8751.
// ARC_VARIANT, in dsp-cpp's order: 0 astorm, 1 bloxeed, 2 cltchitr, 3 ddcrew,
// 4 desertbr, 5 hamaway, 6 lghost, 7 mwalk, 8 pontoon, 9 shdancer, 10 wwallyj.
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m68kdsp/m68kdsp.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../cpu/mcs51dsp/mcs51dsp.h"
#include "../../../machine/fd1094d.h"
#include "../../../machine/sega5195d.h"
#include "../../../machine/sega5296d.h"
#include "../../../sound/ym2612.h"
#include "../../../sound/rf5c68d.h"
#include "../../../video/sega16d.h"
#include "../../../video/sega_315_5313.h"
#include "s18_roms.h"

#define G_ASTORM 0
#define G_BLOXEED 1
#define G_CLTCHITR 2
#define G_DDCREW 3
#define G_DESERTBR 4
#define G_HAMAWAY 5
#define G_LGHOST 6
#define G_MWALK 7
#define G_PONTOON 8
#define G_SHDANCER 9
#define G_WWALLYJ 10
#define GAME ARC_VARIANT

#define B_SHADOW 0                              // (the ROM boards)
#define B_5874 1
#define B_5987 2
#define B_837 3

#if GAME == G_DESERTBR || GAME == G_HAMAWAY
#define ROTATED 1
#else
#define ROTATED 0
#endif
#define NW 320
#define NH 224
#if ROTATED
#define W NH
#define H NW
#else
#define W NW
#define H NH
#endif
#define SCANLINES 262
#define CPU_SYNC 4
#define MAIN_CLOCK 10000000
#define SOUND_CLOCK 8000000
#define MCU_CLOCK 8000000
#define FPS 57.23
#define RATE 44100
#define MIX_GAIN 1.5
#define N(k) ((int)(sizeof k / sizeof k[0]))

#if GAME == G_ASTORM
static const char *const kSets[] = { "astorm", NULL };
const arc_info arc_game = { "astorm", "Alien Storm", "Sega", 1990, W, H, FPS, RATE, kSets, { "ATAQUE", "SALTO", "ESPECIAL" }, ARC_JOY_8WAY };
#elif GAME == G_BLOXEED
static const char *const kSets[] = { "bloxeed", NULL };
const arc_info arc_game = { "bloxeed", "Bloxeed", "Sega / Elorg", 1990, W, H, FPS, RATE, kSets, { "GIRAR", NULL }, ARC_JOY_4WAY };
#elif GAME == G_CLTCHITR
static const char *const kSets[] = { "cltchitr", NULL };
const arc_info arc_game = { "cltchitr", "Clutch Hitter", "Sega", 1991, W, H, FPS, RATE, kSets, { "BATEAR", "CORRER", "LANZAR" }, ARC_JOY_8WAY };
#elif GAME == G_DDCREW
static const char *const kSets[] = { "ddcrew", NULL };
const arc_info arc_game = { "ddcrew", "D. D. Crew", "Sega", 1991, W, H, FPS, RATE, kSets, { "ATAQUE", "SALTO", NULL }, ARC_JOY_8WAY };
#elif GAME == G_DESERTBR
static const char *const kSets[] = { "desertbr", NULL };
const arc_info arc_game = { "desertbr", "Desert Breaker", "Sega", 1992, W, H, FPS, RATE, kSets, { "DISPARO", "BOMBA", NULL }, ARC_JOY_8WAY };
#elif GAME == G_HAMAWAY
static const char *const kSets[] = { "hamaway", NULL };
const arc_info arc_game = { "hamaway", "Hammer Away", "Sega / Santos", 1991, W, H, FPS, RATE, kSets, { "MARTILLO", "SALTO", NULL }, ARC_JOY_8WAY };
#elif GAME == G_LGHOST
static const char *const kSets[] = { "lghost", NULL };
const arc_info arc_game = { "lghost", "Laser Ghost", "Sega", 1990, W, H, FPS, RATE, kSets, { "DISPARO", NULL }, ARC_JOY_8WAY };
#elif GAME == G_MWALK
static const char *const kSets[] = { "mwalk", NULL };
const arc_info arc_game = { "mwalk", "Michael Jackson's Moonwalker", "Sega", 1990, W, H, FPS, RATE, kSets, { "MAGIA", "BAILE", NULL }, ARC_JOY_8WAY };
#elif GAME == G_PONTOON
static const char *const kSets[] = { "pontoon", NULL };
const arc_info arc_game = { "pontoon", "Pontoon", "Sega", 1989, W, H, FPS, RATE, kSets, { "CARTA", "PLANTARSE", "APUESTA" }, ARC_JOY_8WAY };
#elif GAME == G_SHDANCER
static const char *const kSets[] = { "shdancer", NULL };
const arc_info arc_game = { "shdancer", "Shadow Dancer", "Sega", 1989, W, H, FPS, RATE, kSets, { "MAGIA", "ATAQUE", "SALTO" }, ARC_JOY_8WAY };
#else
static const char *const kSets[] = { "wwallyj", NULL };
const arc_info arc_game = { "wwallyj", "Wally wo Sagase!", "Sega", 1992, W, H, FPS, RATE, kSets, { "SELECCIONAR", NULL }, ARC_JOY_8WAY };
#endif

static struct state {
    m68kd cpu;
    zd_z80 snd;
    mcs51 mcu;
    sega5195d map;
    sega5296d io;
    fd1094d fd1094;
    ym2612 ym1, ym2;
    rf5c68d pcm;
    md_vdp vdp;
    s16video video;
    uint16_t *rom, *sprite_rom;
    uint32_t rom_words, sprite_words;
    uint8_t *sound_rom;                         // 0x200000
    uint8_t sound_ram[0x2000];
    uint16_t work_ram[0x2000];
    uint32_t fbuf[NW * NH], vdp_fb[NW * NH];
    uint8_t vdp_pri[NW * NH], layer_pri[NW * NH];
    uint16_t fb[W * H];
    int board, tile_n, sprite_banks;
    bool use_fd1094, use_mcu;
    uint32_t rom0_size, rom1_offset;
    uint8_t sound_bank, tile_bank_latch, vdp_mixing;
    bool vdp_enable, grayscale;
    uint8_t in_p1, in_p2, in_p3, in_service, dsw_coinage, dsw;
    double main_debt, sound_debt, mcu_debt;
    int16_t audio[4096];
    int naudio;
} *S_;
#define S (*S_)

static uint16_t main_read(uint32_t a);
static void main_write(uint32_t a, uint16_t v, bool allow_mapper);

static void configure_game(void)
{
    S.use_fd1094 = S.use_mcu = false;
    S.board = B_5874;
    S.tile_n = 8;
    S.sprite_banks = 16;
    S.rom0_size = 0x80000;
    S.rom1_offset = 0x80000;
    S.dsw = 0xfd;
    switch (GAME) {
    case G_ASTORM: S.use_fd1094 = true; break;
    case G_BLOXEED: S.use_fd1094 = true; S.tile_n = 2; S.sprite_banks = 1; break;
    case G_CLTCHITR: S.use_fd1094 = true; S.board = B_5987; S.tile_n = 16; S.sprite_banks = 24; break;
    case G_DDCREW: S.use_fd1094 = true; S.board = B_5987; S.sprite_banks = 32; break;
    case G_DESERTBR:
        S.use_fd1094 = true; S.board = B_5987; S.tile_n = 32; S.sprite_banks = 32;
        S.rom0_size = 0x100000; S.rom1_offset = 0x100000;
        break;
    case G_HAMAWAY: S.board = B_837; S.tile_n = 16; break;
    case G_LGHOST: S.use_fd1094 = true; S.board = B_5987; S.sprite_banks = 32; break;
    case G_MWALK: S.use_fd1094 = true; S.use_mcu = true; break;
    case G_PONTOON: S.use_fd1094 = true; S.sprite_banks = 4; break;
    case G_SHDANCER: S.board = B_SHADOW; break;
    default: S.use_fd1094 = true; S.board = B_5987; S.sprite_banks = 20; break;
    }
}

static void all_dirty(void)
{
    memset(S.video.tile_dirty, 1, sizeof S.video.tile_dirty);
    memset(S.video.text_dirty, 1, sizeof S.video.text_dirty);
}

static void apply_tile_bank_5874(uint8_t data)
{
    if (S.board != B_5874 && S.board != B_SHADOW) return;
    S.tile_bank_latch = data;
    if (S.board == B_SHADOW && data == 0) {     // (Shadow Dancer pulses 0 every frame: keep the identity)
        bool dirty = false;
        for (int i = 0; i < 8; i++)
            if (S.video.tile_bank[i] != i) {
                S.video.tile_bank[i] = (uint8_t)i;
                dirty = true;
            }
        if (dirty) all_dirty();
        return;
    }
    const uint8_t lo = data & 0x0f, hi = (data >> 4) & 0x0f;
    bool dirty = false;
    for (int i = 0; i < 4; i++) {
        const uint8_t b0 = (uint8_t)(lo * 4 + i), b1 = (uint8_t)(hi * 4 + i);
        if (S.video.tile_bank[i] != b0 || S.video.tile_bank[4 + i] != b1) {
            S.video.tile_bank[i] = b0;
            S.video.tile_bank[4 + i] = b1;
            dirty = true;
        }
    }
    if (dirty) all_dirty();
}

static void bank5987_w(uint16_t offset, uint16_t value)
{
    offset &= 0xf;
    uint8_t data = (uint8_t)value;
    if (offset < 8) {
        int maxbanks = S.video.tiles_total >> 10;
        if (maxbanks < 1) maxbanks = 1;
        if (data >= maxbanks) data = (uint8_t)(data % maxbanks);
        if (S.video.tile_bank[offset] != data) {
            S.video.tile_bank[offset] = data;
            all_dirty();
        }
    } else {
        const int pair = offset - 8;
        int maxbanks = (int)(S.sprite_words >> 17);   // (as dsp-cpp: the word count >> 17)
        if (maxbanks < 1) maxbanks = 1;
        uint8_t bank = data;
        if (bank >= maxbanks) bank = 255;
        S.video.sprite_bank[pair * 2 + 0] = bank == 255 ? 255 : (uint8_t)(bank * 2 + 0);
        S.video.sprite_bank[pair * 2 + 1] = bank == 255 ? 255 : (uint8_t)(bank * 2 + 1);
    }
}

static void bank837_w(uint16_t offset, uint16_t value)
{
    offset &= 0xf;
    uint8_t data = (uint8_t)value;
    if (offset < 8) {
        data &= 0x9f;
        if (data & 0x80) data = (uint8_t)(data + 0x20);
        data &= 0x3f;
        if (S.video.tile_bank[offset] != data) {
            S.video.tile_bank[offset] = data;
            all_dirty();
        }
    }
}

static uint16_t read_rom_word(uint32_t byte_offset)
{
    const size_t i = (byte_offset >> 1) % (S.rom_words ? S.rom_words : 1);
    if (S.use_fd1094 && S.cpu.opcode) return fd1094d_opcodes(&S.fd1094, S.rom, S.rom_words * 2)[i];
    return S.rom[i];
}

static uint16_t misc_io_r(uint16_t word_offset)
{
    const uint16_t o = word_offset & 0x1fff;
    switch (o & (0x3000 / 2)) {
    case 0x0000 / 2: case 0x1000 / 2: return (uint16_t)(sega5296d_read(&S.io, (uint8_t)o) | 0xff00);
    case 0x2000 / 2: return (o & 1) ? S.dsw_coinage : S.in_service;
    default: break;
    }
    if (GAME == G_WWALLYJ && o >= 0x3000 / 2 && o < 0x3018 / 2) return 0x8000;   // (the UPD4701 trackballs: centred)
    if (GAME == G_LGHOST && o >= 0x3010 / 2 && o <= 0x3016 / 2) return 0x00ff;    // (the gun ADC: done, mid-scale)
    return 0xffff;
}

static void misc_io_w(uint16_t word_offset, uint16_t value)
{
    const uint16_t o = word_offset & 0x1fff;
    switch (o & (0x3000 / 2)) {
    case 0x0000 / 2: case 0x1000 / 2: sega5296d_write(&S.io, (uint8_t)o, (uint8_t)value); break;
    case 0x2000 / 2: S.vdp_mixing = (uint8_t)value; break;
    default: break;
    }
}

static uint16_t main_read(uint32_t a)
{
    a &= 0xffffff;
    if (a >= 0xc00000 && a <= 0xdfffff) return md_vdp_read(&S.vdp, (uint8_t)(a & 0x1f));   // (hardwired)
    if (sega5195d_contains(&S.map, 0, a)) return read_rom_word(a & (S.rom0_size - 1));
    uint16_t r = 0xffff;
    bool mapped = false;
    if (sega5195d_contains(&S.map, 1, a)) {
        if (S.board == B_SHADOW) r = md_vdp_read(&S.vdp, (uint8_t)(a & 0x1f));
        else if (S.board == B_5987 || S.board == B_837) r = read_rom_word(S.rom1_offset + (a & (S.rom0_size - 1)));
        else r = read_rom_word(0x80000 + (a & 0x7ffff));
        mapped = true;
    }
    if (sega5195d_contains(&S.map, 2, a) && S.board != B_SHADOW) {
        r = md_vdp_read(&S.vdp, (uint8_t)(a & 0x1f));
        mapped = true;
    }
    if (sega5195d_contains(&S.map, 3, a)) { r = S.work_ram[(a >> 1) & 0x1fff]; mapped = true; }
    if (sega5195d_contains(&S.map, 4, a)) { r = S.video.sprite_ram[(a >> 1) & 0x7ff]; mapped = true; }
    if (sega5195d_contains(&S.map, 5, a)) {
        r = (a & 0x1ffff) <= 0xffff ? S.video.tile_ram[(a & 0xffff) >> 1] : S.video.char_ram[(a & 0xfff) >> 1];
        mapped = true;
    }
    if (sega5195d_contains(&S.map, 6, a)) { r = S.video.pal_ram[(a & 0xfff) >> 1]; mapped = true; }
    if (sega5195d_contains(&S.map, 7, a)) { r = misc_io_r((uint16_t)((a >> 1) & 0x1fff)); mapped = true; }
    if (!mapped) r = sega5195d_read_reg(&S.map, (uint8_t)((a >> 1) & 0x1f));
    return r;
}

static void mapper_write_watched(uint8_t reg, uint8_t v)
{
    const uint32_t old = S.map.start[5];
    sega5195d_write_reg(&S.map, reg, v);
    if (old != S.map.start[5]) memset(S.video.tile_dirty, 0, sizeof S.video.tile_dirty);   // (as dsp-cpp: false)
}

static void main_write(uint32_t a, uint16_t v, bool allow_mapper)
{
    a &= 0xffffff;
    if (a >= 0xc00000 && a <= 0xdfffff) {
        md_vdp_write(&S.vdp, (uint8_t)(a & 0x1f), v);
        return;
    }
    bool mapped = false;
    if (sega5195d_contains(&S.map, 0, a)) mapped = true;
    if (sega5195d_contains(&S.map, 1, a)) {
        if (S.board == B_SHADOW) md_vdp_write(&S.vdp, (uint8_t)(a & 0x1f), v);
        else if (S.board == B_5987) bank5987_w((uint16_t)((a >> 1) & 0xf), v);
        else if (S.board == B_837) bank837_w((uint16_t)((a >> 1) & 0xf), v);
        mapped = true;
    }
    if (sega5195d_contains(&S.map, 2, a) && S.board != B_SHADOW) {
        md_vdp_write(&S.vdp, (uint8_t)(a & 0x1f), v);
        mapped = true;
    }
    if (sega5195d_contains(&S.map, 3, a)) { S.work_ram[(a >> 1) & 0x1fff] = v; mapped = true; }
    if (sega5195d_contains(&S.map, 4, a)) { S.video.sprite_ram[(a >> 1) & 0x7ff] = v; mapped = true; }
    if (sega5195d_contains(&S.map, 5, a)) {
        if ((a & 0x1ffff) <= 0xffff) {
            const uint16_t o = (uint16_t)((a & 0xffff) >> 1);
            if (S.video.tile_ram[o] != v) {
                S.video.tile_ram[o] = v;
                s16_mark_tile(&S.video, o);
            }
        } else {
            const uint16_t o = (uint16_t)((a & 0xfff) >> 1);
            if (S.video.char_ram[o] != v) {
                S.video.char_ram[o] = v;
                S.video.text_dirty[o] = true;
            }
            s16_screen_select_16b(&S.video, o);
        }
        mapped = true;
    }
    if (sega5195d_contains(&S.map, 6, a)) { s16_set_palette_entry(&S.video, (int)((a & 0xfff) >> 1), v, true); mapped = true; }
    if (sega5195d_contains(&S.map, 7, a)) { misc_io_w((uint16_t)((a >> 1) & 0x1fff), v); mapped = true; }
    if (!mapped && allow_mapper) mapper_write_watched((uint8_t)((a >> 1) & 0x1f), (uint8_t)v);
}

static uint16_t cpu_rd(void *u, uint32_t a) { (void)u; return main_read(a); }

static bool is_vdp(uint32_t a)
{
    a &= 0xffffff;
    if (a >= 0xc00000 && a <= 0xdfffff) return true;
    if (sega5195d_contains(&S.map, 1, a) && S.board == B_SHADOW) return true;
    return sega5195d_contains(&S.map, 2, a) && S.board != B_SHADOW;
}

static void cpu_wb(void *u, uint32_t a, uint8_t v)   // (byte writes: read-modify-write, but not on the VDP)
{
    (void)u;
    if (is_vdp(a)) {
        md_vdp_write_byte(&S.vdp, (uint8_t)(a & 0x1f), v);
        return;
    }
    const uint16_t old = main_read(a & ~1u);
    main_write(a & ~1u, (a & 1) ? (uint16_t)((old & 0xff00) | v) : (uint16_t)((old & 0x00ff) | (v << 8)), true);
}
static void cpu_wr(void *u, uint32_t a, uint16_t v) { (void)u; main_write(a, v, true); }
static void cpu_cmpild(void *u, uint8_t reg, uint32_t imm) { (void)u; fd1094d_on_cmpild(&S.fd1094, reg, imm); }
static void cpu_rte(void *u) { (void)u; fd1094d_on_rte(&S.fd1094); }
static void cpu_irq_taken(void *u, int level) { (void)u; (void)level; fd1094d_on_irq(&S.fd1094); }

// the mapper's hookups
static uint8_t map_open_bus(void *u) { (void)u; return S.rom_words ? (uint8_t)(S.rom[(S.cpu.pc.l >> 1) % S.rom_words] >> 8) : 0xff; }
static uint16_t map_bus_read(void *u, uint32_t a) { (void)u; return main_read(a); }
static void map_bus_write(void *u, uint32_t a, uint16_t v) { (void)u; main_write(a, v, false); }
static void map_reset_line(void *u, int st)
{
    (void)u;
    if (st != 0 && S.use_fd1094) fd1094d_reset(&S.fd1094);
    m68kd_set_reset_line(&S.cpu, st);
}
static void map_irq(void *u, int level, int st) { (void)u; m68kd_set_irq(&S.cpu, level, st); }
static void map_pbf(void *u, int st) { (void)u; zd_set_nmi(&S.snd, st); }
static void map_mcu_int(void *u, int st) { (void)u; if (S.use_mcu) mcs51_set_irq1_line(&S.mcu, st); }
static uint8_t mcu_ext_r(void *u, uint16_t a) { (void)u; return sega5195d_read_reg(&S.map, (uint8_t)(a & 0x1f)); }
static void mcu_ext_w(void *u, uint16_t a, uint8_t v) { (void)u; mapper_write_watched((uint8_t)(a & 0x1f), v); }

// the 315-5296
static uint8_t io_p1(void *u) { (void)u; return S.in_p1; }
static uint8_t io_p2(void *u) { (void)u; return S.in_p2; }
static uint8_t io_p3(void *u) { (void)u; return S.in_p3; }
static uint8_t io_service(void *u) { (void)u; return S.in_service; }
static uint8_t io_coinage(void *u) { (void)u; return S.dsw_coinage; }
static uint8_t io_dsw(void *u) { (void)u; return S.dsw; }
static void io_out3(void *u, uint8_t v) { (void)u; S.grayscale = (v & 0x40) == 0; }
static void io_out7(void *u, uint8_t v) { (void)u; apply_tile_bank_5874(v); }
static void io_cnt(void *u, int bit, int st)
{
    (void)u;
    if (bit == 1) S.video.screen_enabled = st != 0;
    if (bit == 2) S.vdp_enable = st != 0;
}

static void ym_irq(void *u, bool on) { (void)u; zd_set_irq(&S.snd, on ? ZD_ASSERT : ZD_CLEAR, 0xff); }
static uint16_t vdp_dma(void *u, uint32_t a) { (void)u; return main_read(a); }
static void vdp_cram(void *u, int index, uint16_t value)   // (CRAM mirrored into the palette at 0x1000)
{
    (void)u;
    static const uint8_t k3to8[8] = {0, 36, 73, 109, 146, 182, 219, 255};
    const int r = (value >> 1) & 7, g = (value >> 5) & 7, b = (value >> 9) & 7;
    S.video.palette[0x1000 + (index & 0x3f)] = 0xff000000u | ((uint32_t)k3to8[r] << 16) | ((uint32_t)k3to8[g] << 8) | k3to8[b];
}

// ---------------------------------------------------------------------------
// The sound Z80

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x9fff) return S.sound_rom[a];
    if (a <= 0xbfff) return S.sound_rom[((uint32_t)S.sound_bank * 0x2000u + (a & 0x1fff)) & 0x1fffff];
    if (a <= 0xcfff) return 0xff;
    if (a <= 0xdfff) return rf5c68d_read_mem(&S.pcm, (uint16_t)(a & 0x0fff));
    return S.sound_ram[a & 0x1fff];
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a >= 0xc000 && a <= 0xcfff) rf5c68d_write_reg(&S.pcm, (uint8_t)(a & 0x0f), v);
    else if (a >= 0xd000 && a <= 0xdfff) rf5c68d_write_mem(&S.pcm, (uint16_t)(a & 0x0fff), v);
    else if (a >= 0xe000) S.sound_ram[a & 0x1fff] = v;
}

static uint8_t sound_in(void *u, uint16_t port)
{
    (void)u;
    const uint8_t p = (uint8_t)port;
    if ((p & 0xf0) == 0x80) return ym2612_read(&S.ym1);
    if ((p & 0xf0) == 0x90) return ym2612_read(&S.ym2);
    if ((p & 0xe0) == 0xc0) return sega5195d_pread(&S.map);
    return 0xff;
}

static void sound_out(void *u, uint16_t port, uint8_t v)
{
    (void)u;
    const uint8_t p = (uint8_t)port;
    if ((p & 0xf0) == 0x80) ym2612_write(&S.ym1, p & 3, v);
    else if ((p & 0xf0) == 0x90) ym2612_write(&S.ym2, p & 3, v);
    else if ((p & 0xe0) == 0xa0) S.sound_bank = v;
    else if ((p & 0xe0) == 0xc0) sega5195d_pwrite(&S.map, v);
}

// ---------------------------------------------------------------------------
// Video (MAME's segas18_v screen_update, as dsp-cpp)

static void overlay_vdp(void)
{
    if (!S.vdp_enable) return;
    const int vdplayer = (S.vdp_mixing >> 1) & 3;
    const uint8_t vdppri = (S.vdp_mixing & 1) ? (uint8_t)(1 << vdplayer) : 0;
    for (int i = 0; i < NW * NH; i++) {
        if (!S.vdp_pri[i]) continue;
        uint32_t px = S.vdp_fb[i];
        if (S.grayscale) {
            const uint8_t r = (uint8_t)(px >> 16), g = (uint8_t)(px >> 8), b = (uint8_t)px;
            const uint8_t grey = (uint8_t)(((uint16_t)r + g + b) / 3);
            px = 0xff000000u | ((uint32_t)grey << 16) | ((uint32_t)grey << 8) | grey;
        }
        S.fbuf[i] = px;
        if (vdppri) S.layer_pri[i] |= vdppri;
    }
}

static void to_frame(void)
{
#if ROTATED
#if GAME == G_HAMAWAY
    for (int y = 0; y < NH; y++)                // (ROT90)
        for (int x = 0; x < NW; x++) S.fb[x * NH + (NH - 1 - y)] = s16_to565(S.fbuf[y * NW + x]);
#else
    for (int y = 0; y < NH; y++)                // (ROT270)
        for (int x = 0; x < NW; x++) S.fb[(NW - 1 - x) * NH + y] = s16_to565(S.fbuf[y * NW + x]);
#endif
#else
    for (int i = 0; i < NW * NH; i++) S.fb[i] = s16_to565(S.fbuf[i]);
#endif
}

static void update_video(void)
{
    s16video *v = &S.video;
    const uint32_t blank = v->palette[0];
    for (int i = 0; i < NW * NH; i++) S.fbuf[i] = blank;
    if (!v->screen_enabled) {
        to_frame();
        return;
    }
    const int vdplayer = (S.vdp_mixing >> 1) & 3;
    memset(S.layer_pri, 0, sizeof S.layer_pri);
#define MAYBE_VDP(l) do { if (S.vdp_enable && vdplayer == (l)) overlay_vdp(); } while (0)
    s16_draw_tilemap_16b(v, S.fbuf, NULL, 1, -1, 0, true, 6, 0x1fff, true);
    MAYBE_VDP(0);
    s16_draw_tilemap_16b(v, S.fbuf, S.layer_pri, 1, 0, 0x01, false, 6, 0x1fff, true);
    s16_draw_tilemap_16b(v, S.fbuf, S.layer_pri, 1, 1, 0x02, false, 6, 0x1fff, true);
    MAYBE_VDP(1);
    s16_draw_tilemap_16b(v, S.fbuf, S.layer_pri, 0, 0, 0x02, false, 6, 0x1fff, true);
    s16_draw_tilemap_16b(v, S.fbuf, S.layer_pri, 0, 1, 0x04, false, 6, 0x1fff, true);
    MAYBE_VDP(2);
    s16_draw_text_16b(v, S.fbuf, S.layer_pri, 0, 0x04, 9, 0x1ff, true);
    s16_draw_text_16b(v, S.fbuf, S.layer_pri, 1, 0x08, 9, 0x1ff, true);
    MAYBE_VDP(3);
#undef MAYBE_VDP
    s16_mix_sprites_16b(v, S.fbuf, S.layer_pri, S.sprite_rom, S.sprite_words, S.sprite_banks, 0x800);
    to_frame();
}

// ---------------------------------------------------------------------------
// ROMs (dsp-cpp's sega16 load_rom_bytes / load_roms16w)

static bool load_part(const arc_rom *r, uint8_t *buf)
{
    arc_rom one = *r;
    one.offset = 0;
    return arc_load(&one, 1, buf, one.size);
}

static uint8_t *load_bytes(const arc_rom *l, int n, uint32_t *size, uint32_t min_size)
{
    uint32_t bytes = 0;
    for (int i = 0; i < n; i++)
        if (l[i].offset + l[i].size > bytes) bytes = l[i].offset + l[i].size;
    const uint32_t alloc = bytes > min_size ? bytes : min_size;
    uint8_t *d = (uint8_t *)calloc(1, alloc ? alloc : 1);
    if (!d) return NULL;
    for (int i = 0; i < n; i++)
        if (!load_part(&l[i], d + l[i].offset)) {
            free(d);
            return NULL;
        }
    *size = bytes;
    return d;
}

static uint16_t *load16w(const arc_rom *l, int n, uint32_t *words)
{
    uint32_t bytes = 0, maxpart = 0;
    for (int i = 0; i < n; i++) {
        if ((l[i].offset & ~1u) + l[i].size * 2 > bytes) bytes = (l[i].offset & ~1u) + l[i].size * 2;
        if (l[i].size > maxpart) maxpart = l[i].size;
    }
    *words = bytes / 2;
    uint16_t *d = (uint16_t *)calloc(*words ? *words : 1, 2);
    uint8_t *part = (uint8_t *)malloc(maxpart ? maxpart : 1);
    bool ok = d && part;
    for (int i = 0; ok && i < n; i++) {
        if (!(ok = load_part(&l[i], part))) break;
        const bool high = (l[i].offset & 1) == 0;
        for (uint32_t j = 0, k = l[i].offset >> 1; j < l[i].size; j++, k++)
            d[k] = high ? (uint16_t)((d[k] & 0x00ff) | (part[j] << 8)) : (uint16_t)((d[k] & 0xff00) | part[j]);
    }
    free(part);
    if (!ok) {
        free(d);
        return NULL;
    }
    return d;
}

static bool load_roms(void)
{
    const arc_rom *main = NULL, *key = NULL, *tiles = NULL, *sprites = NULL, *sound = NULL, *mcu = NULL;
    int nmain = 0, nkey = 0, ntiles = 0, nsprites = 0, nsound = 0, nmcu = 0;
#define SET(v, k) (v = k, n##v = N(k))
#define SETS(P) SET(main, k##P##Main); SET(tiles, k##P##Tiles); SET(sprites, k##P##Sprites); SET(sound, k##P##Sound)
    switch (GAME) {
    case G_ASTORM: SETS(Astorm); SET(key, kAstormKey); break;
    case G_BLOXEED: SETS(Bloxeed); SET(key, kBloxeedKey); break;
    case G_CLTCHITR: SETS(Cltchitr); SET(key, kCltchitrKey); break;
    case G_DDCREW: SETS(Ddcrew); SET(key, kDdcrewKey); break;
    case G_DESERTBR: SETS(Desertbr); SET(key, kDesertbrKey); break;
    case G_HAMAWAY: SETS(Hamaway); break;
    case G_LGHOST: SETS(Lghost); SET(key, kLghostKey); break;
    case G_MWALK: SETS(Mwalk); SET(key, kMwalkKey); SET(mcu, kMwalkMcu); break;
    case G_PONTOON: SETS(Pontoon); SET(key, kPontoonKey); break;
    case G_SHDANCER: SETS(Shdancer); break;
    default: SETS(Wwallyj); SET(key, kWwallyjKey); break;
    }
#undef SETS
#undef SET
    uint32_t n;
    if (!(S.rom = load16w(main, nmain, &S.rom_words))) return false;
    if (S.use_fd1094 && key) {                  // (the data space: the encrypted image)
        uint8_t *k = load_bytes(key, nkey, &n, 0);
        if (!k) return false;
        fd1094d_set_key(&S.fd1094, k, n);
        free(k);
    }
    uint8_t *snd = load_bytes(sound, nsound, &n, 0x200000);
    if (!snd) return false;
    if (n < 0x200000) memset(snd + n, 0xff, 0x200000 - n);
    S.sound_rom = snd;
    if (S.use_mcu && mcu) {
        uint8_t *m = load_bytes(mcu, nmcu, &n, 0);
        if (!m) return false;
        memset(S.mcu.rom, 0xff, sizeof S.mcu.rom);
        memcpy(S.mcu.rom, m, n < sizeof S.mcu.rom ? n : sizeof S.mcu.rom);
        free(m);
    }
    if (!(S.sprite_rom = load16w(sprites, nsprites, &S.sprite_words))) return false;
    uint8_t *t = load_bytes(tiles, ntiles, &n, 0);
    if (!t) return false;
    if (n >= 0x18000) S.tile_n = (int)(n / 0x18000);
    if (S.tile_n < 1) S.tile_n = 1;
    if ((uint32_t)S.tile_n * 0x18000 > n) {     // (dsp-cpp decodes from a short vector as zeros)
        uint8_t *p = (uint8_t *)realloc(t, (size_t)S.tile_n * 0x18000);
        if (!p) return false;
        memset(p + n, 0, (size_t)S.tile_n * 0x18000 - n);
        t = p;
    }
    S.video.tiles = s16_decode_tiles(t, S.tile_n);
    S.video.tiles_total = S.tile_n * 0x1000;
    free(t);
    return S.video.tiles != NULL;
}

bool arc_init(void)
{
    if (!(S_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    configure_game();
    s16_init(&S.video);
    S.dsw_coinage = 0xff;
    if (S.use_mcu) mcs51_init(&S.mcu);
    if (!load_roms()) return false;
    S.video.cram_words = 0x800;
    S.video.tile_banks = 0x0f;
    S.video.bank_size = 0x400;                  // (8 banks of 0x400 tiles)

    m68kd_init(&S.cpu, MAIN_CLOCK, M68K_T_68000);
    S.cpu.read_word = cpu_rd;
    S.cpu.write_word = cpu_wr;
    S.cpu.write_byte = cpu_wb;
    if (S.use_fd1094) {
        S.cpu.cmpild = cpu_cmpild;
        S.cpu.rte_cb = cpu_rte;
        S.cpu.irq_taken_cb = cpu_irq_taken;
    }
    S.snd.read = sound_read;
    S.snd.write = sound_write;
    S.snd.in = sound_in;
    S.snd.out = sound_out;
    zd_init(&S.snd);
    S.map.open_bus = map_open_bus;
    S.map.bus_read = map_bus_read;
    S.map.bus_write = map_bus_write;
    S.map.reset_line = map_reset_line;
    S.map.irq = map_irq;
    S.map.pbf = map_pbf;
    if (S.use_mcu) {
        S.map.mcu_int = map_mcu_int;
        S.mcu.clock = MCU_CLOCK / 12;
        S.mcu.read_external = mcu_ext_r;
        S.mcu.write_external = mcu_ext_w;
    }
    S.io.in[0] = io_p1;
    S.io.in[1] = io_p2;
    S.io.in[2] = io_p3;
    S.io.in[4] = io_service;
    S.io.in[5] = io_coinage;
    S.io.in[6] = io_dsw;
    S.io.out[3] = io_out3;
    S.io.out[7] = io_out7;
    S.io.cnt_write = io_cnt;
    ym2612_init(&S.ym1, 8000000, 0.40f);
    ym2612_init(&S.ym2, 8000000, 0.40f);
    S.ym1.opn.irq = ym_irq;
    rf5c68d_init(&S.pcm, 10000000, 1.0f, RATE);
    S.vdp.dma_read = vdp_dma;
    S.vdp.cram_write = vdp_cram;
    md_vdp_init(&S.vdp, false, RATE);
    arc_reset();
    return true;
}

void arc_reset(void)
{
    sega5195d_reset(&S.map);
    if (S.use_fd1094) fd1094d_reset(&S.fd1094);
    m68kd_reset(&S.cpu);
    zd_reset(&S.snd);
    if (S.use_mcu) mcs51_reset(&S.mcu);
    ym2612_reset(&S.ym1);
    ym2612_reset(&S.ym2);
    rf5c68d_reset(&S.pcm);
    md_vdp_reset(&S.vdp);
    sega5296d_reset(&S.io);
    s16_reset(&S.video);
    S.video.screen_enabled = false;
    S.video.bank_size = 0x400;
    memset(S.work_ram, 0, sizeof S.work_ram);
    memset(S.sound_ram, 0, sizeof S.sound_ram);
    S.sound_bank = S.tile_bank_latch = S.vdp_mixing = 0;
    S.vdp_enable = S.grayscale = false;
    S.main_debt = S.sound_debt = S.mcu_debt = 0;
    S.in_p1 = S.in_p2 = S.in_p3 = S.in_service = 0xff;
}

int arc_run_frame(const int16_t **audio)
{
    const double slices = (double)(SCANLINES * CPU_SYNC);
    const double main_cycles = (double)MAIN_CLOCK / FPS / slices, snd = (double)SOUND_CLOCK / FPS / slices;
    const double mcu_cycles = S.use_mcu ? (double)S.mcu.clock / FPS / slices : 0.0;
    const int samples_per_frame = (int)lround((double)RATE / FPS);
    int done = 0;
    S.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        md_vdp_scanline(&S.vdp, line);
        if (line < NH) {                        // (the VDP through palette[0x1000 +], as MAME's draw_vdp)
            const int w = md_vdp_width(&S.vdp) < NW ? md_vdp_width(&S.vdp) : NW;
            for (int x = 0; x < w; x++) {
                const int i = line * NW + x;
                const uint16_t pix = S.vdp.line_raw[x];
                if ((pix & 0x100) == 0 || (pix & 0xf) == 0) {
                    S.vdp_pri[i] = 0;
                    continue;
                }
                S.vdp_fb[i] = S.video.palette[0x1000 + (pix & 0x3f)];
                S.vdp_pri[i] = 1;
            }
            for (int x = w; x < NW; x++) S.vdp_pri[line * NW + x] = 0;
        }
        if (line == 224) {
            if (S.use_mcu) mcs51_set_irq0_line(&S.mcu, MCS51_HOLD);
            else m68kd_set_irq(&S.cpu, 4, M68_HOLD);
            update_video();
        }
        for (int slice = 0; slice < CPU_SYNC; slice++) {
            S.main_debt += main_cycles;
            S.main_debt -= m68kd_run(&S.cpu, (int)S.main_debt);
            S.sound_debt += snd;
            S.sound_debt -= zd_run(&S.snd, (int)S.sound_debt);
            if (S.use_mcu) {
                S.mcu_debt += mcu_cycles;
                S.mcu_debt -= mcs51_run(&S.mcu, (int)S.mcu_debt);
            }
        }
        const int target = (line + 1) * samples_per_frame / SCANLINES;
        while (done < target) {
            double mix = (double)ym2612_update(&S.ym1) + (double)ym2612_update(&S.ym2);
            mix += (double)rf5c68d_update(&S.pcm);
            mix += (double)sn76496_update(&S.vdp.psg) * 0.15;
            if (S.naudio < 4096) S.audio[S.naudio++] = arc_clamp16((int32_t)lround(mix * MIX_GAIN));
            done++;
        }
    }
    md_vdp_eof(&S.vdp);
    if (audio) *audio = S.audio;
    return S.naudio;
}

const uint16_t *arc_frame(void) { return S.fb; }

static uint8_t joy_bits(unsigned p)
{
    uint8_t v = 0xff;
    if (p & AP_B1) v &= (uint8_t)~0x01;
    if (p & AP_B2) v &= (uint8_t)~0x02;
    if (p & AP_B3) v &= (uint8_t)~0x04;
    if (p & AP_DOWN) v &= (uint8_t)~0x10;
    if (p & AP_UP) v &= (uint8_t)~0x20;
    if (p & AP_RIGHT) v &= (uint8_t)~0x40;
    if (p & AP_LEFT) v &= (uint8_t)~0x80;
    return v;
}

void arc_input(const arc_in *in)
{
    S.in_p1 = joy_bits(in->p[0]);
    S.in_p2 = joy_bits(in->p[1]);
    S.in_p3 = 0xff;
    S.in_service = 0xff;
    if (in->coin1) S.in_service &= (uint8_t)~0x01;
    if (in->coin2) S.in_service &= (uint8_t)~0x02;
    if (in->service) S.in_service &= (uint8_t)~0x04;
    if (in->p[0] & AP_START) S.in_service &= (uint8_t)~0x10;
    if (in->p[1] & AP_START) S.in_service &= (uint8_t)~0x20;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) S.dsw_coinage = v;
    else if (bank == 1) S.dsw = v;
}
