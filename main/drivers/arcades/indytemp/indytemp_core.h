// indytemp_core.h - Indiana Jones and the Temple of Doom (Atari System 1).
// C port of dsp-cpp's drivers/arcade/atari_system1 (Indy set), platform free:
// the SDL frontend (indytemp.c) and the ESP32 CYD firmware both use it.
//
// Main CPU: 68010 (Musashi) behind a SLAPSTIC 105. Sound: 6502 with a YM2151,
// a POKEY and a TMS5220 behind a 6522 VIA.
#ifndef INDYTEMP_CORE_H
#define INDYTEMP_CORE_H

#include <stdint.h>
#include <stdbool.h>

#include "dsp6502.h"
#include "ym2151.h"
#include "pokey.h"
#include "tms5220.h"
#include "via6522.h"
#include "slapstic.h"
#include "atari_mo.h"

#define INDY_SCREEN_W 336
#define INDY_SCREEN_H 240
#define INDY_SCANLINES 262
#define INDY_ATARI_CLOCK 14318180u
#define INDY_MAIN_CLOCK (INDY_ATARI_CLOCK / 2)
#define INDY_SOUND_CLOCK (INDY_ATARI_CLOCK / 8)
#define INDY_YM_CLOCK (INDY_ATARI_CLOCK / 4)
#define INDY_FPS 59.922743

// Size of the 68000 program image: $00000-$7FFFF as the CPU sees it (BIOS at
// 0, game program from $10000), then the four SLAPSTIC banks ($2000 bytes
// each) at $80000. Big-endian words.
#define INDY_MAIN_IMAGE 0x88000
#define INDY_GFX_BANKS 8          // graphics ROM banks 1..7 (512 KB of image each)
#define INDY_TILES_PER_BANK 0x1000

// ROM data. Everything is read in place (in flash on the ESP32).
typedef struct {
    const uint8_t *main;           // INDY_MAIN_IMAGE bytes, big-endian
    const uint8_t *sound;          // 0xc000 bytes: 6502 $4000-$FFFF
    const uint8_t *chars;          // 0x2000 bytes: 2bpp alphanumerics
    const uint8_t *proms;          // 0x400 bytes: 136036.152 then .151
    // 4bpp tiles of each graphics bank (index 1..7, NULL = absent): one
    // uint32_t per tile row, pixel x in bits 4x..4x+3.
    const uint32_t *tiles[INDY_GFX_BANKS];
} indy_roms;

// Everything the picture is drawn from (snapshot of it is taken at line 239).
typedef struct {
    uint16_t ram3[0x2000];          // $A00000: playfield, MO ($A02000), alpha ($A03000)
    uint16_t palette[0x400];
    uint8_t palette_set[0x400 / 8]; // entries written at least once
    uint16_t scroll_x[INDY_SCANLINES];
    uint16_t scroll_y[INDY_SCANLINES];
    uint16_t priority_pens;
    uint8_t tile_bank;
    uint8_t mo_bank;
} indy_video;

// A playfield tile's set-up, per (tile bank, attribute high byte).
typedef struct {
    const uint32_t *tiles;          // 4bpp rows of its graphics set (NULL = none)
    uint16_t base;                  // palette entry of pen 0
    uint16_t codehi;                // code bits 8-11
} indy_pf_info;

// Decoded from the PROMs once.
typedef struct {
    const indy_roms *roms;
    uint16_t playfield_lookup[256];
    uint16_t motable[256];
    uint8_t gfx_bank[16];           // graphics set -> ROM bank (0 = none)
    uint8_t gfx_bpp[16];
    uint8_t bank_color_shift[16];
    uint8_t bank_gfx[3][8];
    uint8_t next_gfx_index;
    indy_pf_info pf[256];
} indy_gfx;

typedef struct {
    bool coin1, coin2, start, button1, button2;
    bool up, down, left, right;
} indy_inputs;

// One tile of a motion object, in drawing order.
typedef struct {
    int16_t x, y;
    uint16_t code;
    uint16_t color;
    uint8_t gfx, hflip, vflip, priority;
} indy_mo_tile;

#define INDY_MO_MAX_TILES 1024
typedef struct {
    int count;
    indy_mo_tile t[INDY_MO_MAX_TILES];
    atari_mo mo;                    // its own motion object engine (thread safe)
    bool mo_ready;
} indy_mo_list;

typedef struct indy_core indy_core;
typedef void (*indy_frame_cb)(void *user, indy_core *c);                     // at line 239
typedef void (*indy_ym_write_cb)(void *user, uint8_t reg, uint8_t val, uint32_t sample_pos);

#define INDY_ROM_PAGES (INDY_MAIN_IMAGE / 1024)

struct indy_core {
    indy_roms roms;
    // The 68000 image in 1 KB pages (all into roms.main; a frontend may point
    // the busiest ones at copies in RAM).
    const uint8_t *main_page[INDY_ROM_PAGES];
    indy_gfx gfx;
    indy_video v;                   // live video state

    // CPUs and chips
    dsp6502 snd;
    ym2151 ym;
    pokey pk;
    via6522 via;
    tms5220 tms;
    slapstic slap;

    uint16_t ram[0x1000];           // $400000
    uint16_t *ram2[256];            // $900000, 4 KB pages made when written
    uint8_t eeprom[0x800];          // $F00000
    uint8_t sound_ram[0x1000];

    uint8_t rom_bank;
    uint16_t scroll_x, scroll_y, scroll_y_latch;
    uint16_t bankselect;
    uint8_t vblank;
    uint16_t in0;
    uint8_t in2;
    uint8_t joy_bits;
    uint8_t adc_channel, adc_value;
    bool adc_irq_enable, adc_busy;
    bool write_eeprom, eeprom_dirty;
    bool sound_pending, main_pending;
    uint8_t sound_latch, main_latch;
    bool sound_halted;
    int line;

    // 68000 interrupt lines (LINE_CLEAR/ASSERT/HOLD) and their deferred update.
    uint8_t irq[8];
    bool in_main;
    // dsp-cpp's pc() during an opcode fetch (the fetch address).
    bool fetching;
    uint32_t fetch_pc;

    // Audio
    uint32_t sample_rate;
    int32_t audio_acc;
    // Work of the 6502's chips owed since the last time they were looked at:
    // run when the 6502 touches them (or a sample is due), with the same
    // result as running them after every instruction.
    int pokey_pending;              // POKEY clocks
    int via_pending;                // VIA clocks
    int tms_pending;                // TMS5220 clocks (rounded per instruction)
    int32_t ym_pending, ym_due;     // YM2151 clocks; run at a timer's expiry
    uint32_t snd_skipped;           // 6502 cycles of the polling loop fast-forwarded
    uint32_t tms_table_clock;
    uint16_t tms_clocks[16];
    bool audio_enabled;
    bool ym_external;               // the YM2151 is synthesized by the frontend
    int16_t *audio;                 // samples of the frame (frontend's buffer)
    int audio_cap, audio_n;
    uint32_t sample_pos;            // samples made since reset
    indy_ym_write_cb ym_write;

    // Idle skipping: two busy loops of the 68000 are run natively, whole
    // iterations at a time, with the same result and cycle count (see
    // idle_check() in indytemp_core.c). Cycles of one iteration are measured.
    bool idle_skip;
    uint32_t slice;
    struct { uint32_t slice; int rem; int cost; } site[2];
    uint32_t skipped;               // 68000 cycles skipped (statistics)

    indy_frame_cb on_frame;
    void *user;
};

// Builds tile data for bank `bank` (1..7) from the graphics ROM image
// (`chunk(i)` = 16 KB chunk i of the image, NULL = absent): out has
// INDY_TILES_PER_BANK * 8 rows. Returns false when the bank has no data.
bool indy_pack_bank(const uint8_t *(*chunk)(void *u, int index), void *u, int bank, uint32_t *out);

extern bool indy_ipl_dirty;

void indy_init(indy_core *c, const indy_roms *roms, uint32_t sample_rate);
void indy_reset(indy_core *c);
void indy_set_inputs(indy_core *c, const indy_inputs *in);
// Runs one frame (262 lines). The frame callback fires at line 239.
void indy_run_frame(indy_core *c);

// --- Video (works on any indy_video, live or a snapshot) ---
void indy_build_mo_list(const indy_gfx *g, const indy_video *v, indy_mo_list *list);
// Palette entries (0..0x3ff) of lines [y0, y1), INDY_SCREEN_W each.
void indy_compose(const indy_gfx *g, const indy_video *v, const indy_mo_list *mo, int y0, int y1, uint16_t *out);
// 0xRRGGBB of a palette entry (0 when never written).
uint32_t indy_palette_rgb(const indy_video *v, int index);

#endif
