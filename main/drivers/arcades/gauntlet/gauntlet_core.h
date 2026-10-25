// gauntlet_core.h - Gauntlet (Atari, 1985), platform independent.
// C port of dsp-cpp's drivers/arcade/gauntlet.cpp, with the hardware details
// checked against MAME's atari/gauntlet.cpp (memory map mirrors, MO shadow
// pen, playfield flip bit, SLIP motion objects, sound IRQ timer, LS259s).
//
// Main CPU: 68010 (Musashi) behind a SLAPSTIC 104. Sound: 6502 with a YM2151,
// a POKEY and a TMS5220C.
#ifndef GAUNTLET_CORE_H
#define GAUNTLET_CORE_H

#include <stdint.h>
#include <stdbool.h>

#include "dsp6502.h"
#include "ym2151.h"
#include "pokey.h"
#include "tms5220.h"
#include "slapstic.h"
#include "atari_mo.h"

#define GT_SCREEN_W 336
#define GT_SCREEN_H 240
#define GT_SCANLINES 262
#define GT_HTOTAL 456
#define GT_ATARI_CLOCK 14318180u
#define GT_MAIN_CLOCK (GT_ATARI_CLOCK / 2)
#define GT_SOUND_CLOCK (GT_ATARI_CLOCK / 8)
#define GT_YM_CLOCK (GT_ATARI_CLOCK / 4)
#define GT_FPS 59.922743
#define GT_TILES 0x2000

// ROM data, read in place (flash on the ESP32).
typedef struct {
    // 68010 program $00000-$7FFFF, big-endian words. The SLAPSTIC area
    // $38000-$3FFFF holds its 4 banks of $2000 bytes.
    const uint8_t *main;
    const uint8_t *sound;           // 0xc000 bytes: 6502 $4000-$FFFF
    const uint8_t *chars;           // 0x4000 bytes: 2bpp alphanumerics (1024)
    const uint32_t *tiles;          // GT_TILES * 8 rows, 4bpp, pixel x in bits 4x..4x+3
} gt_roms;

// What the picture is drawn from (a snapshot is taken at the start of vblank).
typedef struct {
    uint16_t pf[0x1000];            // $900000 playfield, 64x64 tiles (column major)
    uint16_t mo[0x1000];            // $902000 motion objects (4 split words x 1024)
    uint16_t alpha[0x800];          // $905000 alphanumerics 64x31, Y scroll ($905F6E), SLIP ($905F80)
    uint16_t palette[0x400];
    uint16_t xscroll[GT_SCREEN_H];  // X scroll register of each line
    uint16_t yscroll[GT_SCREEN_H];  // Y scroll / bank register of each line
} gt_video;

typedef struct {
    bool coin[4];                   // coins 1-4
    bool service;
    // players 1-4: up, down, left, right, fire (button 1), magic (button 2)
    struct { bool up, down, left, right, fire, magic; } p[4];
} gt_inputs;

typedef struct gt_core gt_core;
typedef void (*gt_frame_cb)(void *user, gt_core *c);     // at the start of vblank
typedef void (*gt_ym_write_cb)(void *user, uint8_t reg, uint8_t val, uint32_t sample_pos);

struct gt_core {
    gt_roms roms;
    const uint8_t *main_page[0x80000 / 1024];   // the program in 1 KB pages (RAM copies allowed)
    gt_video v;

    dsp6502 snd;
    ym2151 ym;
    pokey pk;
    tms5220 tms;
    slapstic slap;

    uint16_t ram[0x1000];           // $800000
    uint16_t ram2[0x800];           // $904000
    uint8_t eeprom[0x200];          // $802000 (2804, 512 x 8)
    uint8_t sound_ram[0x1000];
    bool eeprom_unlocked, eeprom_dirty;

    uint8_t rom_bank;
    uint16_t xscroll;
    uint8_t outlatch;               // 68010 LS259 (Q7 = sound CPU run)
    uint8_t soundctl;               // 6502 LS259: Q0 YM run, Q1 /WS, Q2 /RS, Q3 squeak
    uint8_t mixer;                  // $1020 write: YM 0-2, POKEY 3-4, TMS 5-7
    bool sound_halted;
    bool soundlatch_full, mainlatch_full;
    uint8_t soundlatch, mainlatch;
    uint16_t in[4];                 // $803000-$803006, active low
    uint8_t coins;                  // $1020 read
    bool service;
    int line;

    int32_t m68k_debt;              // 68010 cycles run past the previous slice
    int32_t slice_skipped;          // cycles fast-forwarded in this slice
    uint8_t irq[8];
    bool in_main;
    bool fetching;
    uint32_t fetch_pc;

    // Audio (as atarisy1_core: POKEY / TMS5220 work owed is run lazily, the
    // YM2151 timers exactly at their expiry)
    uint32_t sample_rate;
    int32_t audio_acc;
    int pokey_pending, tms_pending;
    int32_t ym_pending, ym_due;
    uint32_t tms_table_clock;
    uint16_t tms_clocks[16];
    bool ym_external;               // the YM2151 is synthesized by the frontend
    int16_t *audio;
    int audio_cap, audio_n;
    uint32_t sample_pos;
    gt_ym_write_cb ym_write;
    void (*ym_reset_cb)(void *user);

    // Exact idle-loop fast-forward (see gauntlet_core.c)
    bool idle_skip;
    uint32_t slice;
    struct { uint32_t slice; int rem; int cost; } site[2];
    uint32_t skipped, snd_skipped;
    uint8_t poll_i;                 // where the sound poll loop stopped

    gt_frame_cb on_frame;
    void *user;
};

extern bool gt_ipl_dirty;

void gt_init(gt_core *c, const gt_roms *roms, uint32_t sample_rate);
void gt_reset(gt_core *c);
void gt_set_inputs(gt_core *c, const gt_inputs *in);
void gt_run_frame(gt_core *c);
// YM2151 volume set by the sound program (mixer), 0..256, for frontends that
// synthesize it themselves; POKEY and speech are mixed by the core.
int gt_ym_gain(const gt_core *c);

// --- Video ---
// Packs the tile ROMs (8 x 32 KB, as MAME's "spr_tiles", not yet inverted)
// into rows of 4bpp pixels.
void gt_pack_tiles(const uint8_t *rom, uint32_t *out);
// One renderer per thread (it owns the motion object engine).
typedef struct {
    atari_mo mo;
    bool ready;
    const gt_roms *roms;
    int band_y0, band_y1;           // lines being drawn
    uint16_t *mobuf;                // motion objects of the band (0xffff = none)
} gt_renderer;
// Sets up the motion object engine (allocates its list, and the band buffer
// when mobuf is NULL); done by the first gt_compose() otherwise.
void gt_renderer_init(gt_renderer *r);
// Palette entries (0..0x3ff) of lines [y0, y1) (y1 - y0 <= 16), GT_SCREEN_W each.
void gt_compose(gt_renderer *r, const gt_roms *roms, const gt_video *v, int y0, int y1, uint16_t *out);
// 0xRRGGBB of a palette entry (IRGB 4444 as MAME).
uint32_t gt_palette_rgb(const gt_video *v, int index);

#endif
