// atarisy1_core.h - Atari System 1 (Indiana Jones and the Temple of Doom,
// Road Runner). C port of dsp-cpp's drivers/arcade/atari_system1, platform
// free: the SDL2 frontends and the ESP32 CYD firmwares of both games use it.
//
// Main CPU: 68010 (Musashi) behind a SLAPSTIC (105 Indy, 108 Road Runner).
// Sound: 6502 with a YM2151, a POKEY and a TMS5220 behind a 6522 VIA.
// Road Runner adds the motion-object scanline interrupt (INT3) and an analog
// joystick on ADC channels 6/7.
#ifndef ATARISY1_CORE_H
#define ATARISY1_CORE_H

#include <stdint.h>
#include <stdbool.h>

#include "dsp6502.h"
#include "ym2151.h"
#include "pokey.h"
#include "tms5220.h"
#include "via6522.h"
#include "slapstic.h"
#include "atari_mo.h"

#define AS1_SCREEN_W 336
#define AS1_SCREEN_H 240
#define AS1_SCANLINES 262
#define AS1_ATARI_CLOCK 14318180u
#define AS1_MAIN_CLOCK (AS1_ATARI_CLOCK / 2)
#define AS1_SOUND_CLOCK (AS1_ATARI_CLOCK / 8)
#define AS1_YM_CLOCK (AS1_ATARI_CLOCK / 4)
#define AS1_FPS 59.922743

typedef enum { AS1_INDYTEMP = 0, AS1_ROADRUNN = 1 } as1_game;

// Graphics ROM image of each game: 2 MB (banks 1-4) Indy, 3 MB (1-6) Road Runner
static inline uint32_t as1_gfx_bytes(as1_game g) { return g == AS1_ROADRUNN ? 0x300000u : 0x200000u; }

// Size of the 68000 program image: $00000-$7FFFF as the CPU sees it (BIOS at
// 0, game program from $10000), then the four SLAPSTIC banks ($2000 bytes
// each) at $80000. Big-endian words.
#define AS1_MAIN_IMAGE 0x88000
#define AS1_GFX_BANKS 8          // graphics ROM banks 1..7 (512 KB of image each)
#define AS1_TILES_PER_BANK 0x1000

// ROM data. Everything is read in place (in flash on the ESP32).
typedef struct {
    as1_game game;
    const uint8_t *main;           // AS1_MAIN_IMAGE bytes, big-endian
    const uint8_t *sound;          // 0xc000 bytes: 6502 $4000-$FFFF
    const uint8_t *chars;          // 0x2000 bytes: 2bpp alphanumerics
    const uint8_t *proms;          // 0x400 bytes: 136036.152 then .151
    // 4bpp tiles of each graphics bank (index 1..7, NULL = absent): one
    // uint32_t per tile row, pixel x in bits 4x..4x+3.
    const uint32_t *tiles[AS1_GFX_BANKS];
} as1_roms;

// Everything the picture is drawn from (snapshot of it is taken at line 239).
typedef struct {
    uint16_t ram3[0x2000];          // $A00000: playfield, MO ($A02000), alpha ($A03000)
    uint16_t palette[0x400];
    uint8_t palette_set[0x400 / 8]; // entries written at least once
    uint16_t scroll_x[AS1_SCANLINES];
    uint16_t scroll_y[AS1_SCANLINES];
    uint16_t priority_pens;
    uint8_t tile_bank;
    uint8_t mo_bank;
    // Road Runner: playfield priority pens of each line (MAME redraws the
    // screen up to the current line when they change)
    bool pens_per_line;
    uint16_t pens_line[AS1_SCREEN_H];
} as1_video;

// A playfield tile's set-up, per (tile bank, attribute high byte).
typedef struct {
    const uint32_t *tiles;          // 4bpp rows of its graphics set (NULL = none)
    uint16_t base;                  // palette entry of pen 0
    uint16_t codehi;                // code bits 8-11
} as1_pf_info;

// Decoded from the PROMs once.
typedef struct {
    const as1_roms *roms;
    uint16_t playfield_lookup[256];
    uint16_t motable[256];
    uint8_t gfx_bank[16];           // graphics set -> ROM bank (0 = none)
    uint8_t gfx_bpp[16];
    uint8_t bank_color_shift[16];
    uint8_t bank_gfx[3][8];
    uint8_t next_gfx_index;
    as1_pf_info pf[256];
} as1_gfx;

typedef struct {
    bool coin1, coin2, start, button1, button2;
    bool up, down, left, right;
    // Road Runner: analog joystick, 0x00..0xff, 0x80 = centre (X: 0xf0 = left,
    // as MAME's PORT_REVERSE). When both are 0 the digital directions are used.
    uint8_t analog_x, analog_y;
} as1_inputs;

// One tile of a motion object, in drawing order.
typedef struct {
    int16_t x, y;
    uint16_t code;
    uint16_t color;
    uint8_t gfx, hflip, vflip, priority;
    int16_t ymin, ymax;             // lines it may draw: [ymin, ymax)
} as1_mo_tile;

#define AS1_MO_MAX_TILES 1024
typedef struct {
    int count;
    as1_mo_tile t[AS1_MO_MAX_TILES];
    atari_mo mo;                    // its own motion object engine (thread safe)
    bool mo_ready;
} as1_mo_list;

typedef struct as1_core as1_core;
typedef void (*as1_frame_cb)(void *user, as1_core *c);                     // at line 239
typedef void (*as1_ym_write_cb)(void *user, uint8_t reg, uint8_t val, uint32_t sample_pos);

#define AS1_ROM_PAGES (AS1_MAIN_IMAGE / 1024)

struct as1_core {
    as1_roms roms;
    // The 68000 image in 1 KB pages (all into roms.main; a frontend may point
    // the busiest ones at copies in RAM).
    const uint8_t *main_page[AS1_ROM_PAGES];
    as1_gfx gfx;
    as1_video v;                   // live video state

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
    as1_game game;
    uint32_t gfx_bytes;             // graphics ROM image size (banks used)
    uint8_t joy_bits;
    uint8_t analog_x, analog_y;
    // Road Runner INT3: raised at the line of a motion object marked 0xffff
    int int3_line, int3_off_line;
    bool int3_state;
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
    as1_ym_write_cb ym_write;

    // Idle skipping: two busy loops of the 68000 are run natively, whole
    // iterations at a time, with the same result and cycle count (see
    // idle_check() in atarisy1_core.c). Cycles of one iteration are measured.
    bool idle_skip;
    uint32_t slice;
    struct { uint32_t slice; int rem; int cost; } site[3];
    uint32_t skipped;               // 68000 cycles skipped (statistics)

    // Road Runner moves its motion objects while the screen is drawn (INT3):
    // like MAME's partial updates, the objects are taken from the RAM as it is
    // when each band of lines is finished. NULL for Indy (the frontend builds
    // the list from the state at line 239).
    as1_mo_list *frame_mo;          // objects of the frame being drawn
    int mo_line;                    // lines [0, mo_line) already taken

    as1_frame_cb on_frame;
    void *user;
};

// Builds tile data for bank `bank` (1..7) from the graphics ROM image
// (`chunk(i)` = 16 KB chunk i of the image, NULL = absent): out has
// AS1_TILES_PER_BANK * 8 rows. Returns false when the bank has no data.
bool as1_pack_bank(const uint8_t *(*chunk)(void *u, int index), void *u, int bank, uint32_t *out);

extern bool as1_ipl_dirty;

void as1_init(as1_core *c, const as1_roms *roms, uint32_t sample_rate);
void as1_reset(as1_core *c);
void as1_set_inputs(as1_core *c, const as1_inputs *in);
// Runs one frame (262 lines). The frame callback fires at line 239.
void as1_run_frame(as1_core *c);

// --- Video (works on any as1_video, live or a snapshot) ---
void as1_build_mo_list(const as1_gfx *g, const as1_video *v, as1_mo_list *list);
// Appends the objects of lines [y0, y1) as the RAM is now (Road Runner).
void as1_build_mo_band(const as1_gfx *g, const as1_video *v, as1_mo_list *list, int y0, int y1);
// Palette entries (0..0x3ff) of lines [y0, y1), AS1_SCREEN_W each.
void as1_compose(const as1_gfx *g, const as1_video *v, const as1_mo_list *mo, int y0, int y1, uint16_t *out);
// 0xRRGGBB of a palette entry (0 when never written).
uint32_t as1_palette_rgb(const as1_video *v, int index);

#endif
