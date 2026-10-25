// or_core.h - OutRun (Sega, 1986), platform independent.
// C port of dsp-cpp's drivers/arcade/outrun.cpp, with MAME's sega/segaorun.cpp
// (and the devices it uses: 315-5195 mapper, segaic16 tilemaps and road,
// Out Run sprites, 315-5218 PCM, i8255, ADC0804) as the reference.
//
// Two 68000s at 10 MHz (main, sub), a Z80 at 4 MHz with a YM2151 and a
// Sega PCM. Picture 320x224, 262 lines of 400 pixel clocks (60.05 Hz).
#ifndef OR_CORE_H
#define OR_CORE_H

#include <stdint.h>
#include <stdbool.h>

#include "sega_315_5195.h"
#include "or_sound.h"

#define OR_SCREEN_W     320
#define OR_SCREEN_H     224
#define OR_LINES        262
#define OR_MAIN_CLOCK   10000000u           // 40 MHz / 4
#define OR_SND_CLOCK    4000000u            // 16 MHz / 4 (Z80, YM2151, PCM)
#define OR_PIXEL_CLOCK  6293700.0           // 25.1748 MHz / 4
#define OR_FPS          (OR_PIXEL_CLOCK / (400.0 * OR_LINES))   // 60.0543
#define OR_PCM_RATE     (OR_SND_CLOCK / 128) // 31250 Hz
#define OR_TILES        0x2000

typedef struct {
    const uint8_t *main;        // 0x40000: main 68000 program (big-endian words)
    const uint8_t *sub;         // 0x40000: sub 68000 program
    const uint8_t *sound;       // 0x8000: Z80 program
    const uint32_t *tiles;      // OR_TILES * 8 rows, 3bpp pixels in nibbles (pixel x in bits 4x)
    const uint32_t *sprites;    // 0x40000 dwords (4 banks of 0x10000), as MAME's region32_le
    const uint8_t *road;        // (512 + 1) rows of 512 pixels: MAME's decoded road gfx
    const uint8_t *pcm;         // 6 x 0x8000: PCM samples (each ROM mirrored in 64 KB)
} or_roms;

typedef struct {
    bool coin1, coin2, start, service;
    bool gear_toggle;           // press to change gear (low <-> high)
    uint8_t steer;              // 0x20 (left) .. 0x80 .. 0xe0 (right)
    uint8_t gas, brake;         // 0 .. 0xff
} or_inputs;

// What a picture is made from. The core keeps it current; a renderer can
// work on the core's copy (same thread) or a snapshot of it.
typedef struct {
    const uint16_t *tileram;    // 0x8000 words: 16 pages of 64x32 tiles
    const uint16_t *textram;    // 0x800 words: text layer 64x28 and the scroll registers
    const uint16_t *palette;    // 0x1000 words
    const uint16_t *sprites;    // 0x800 words: the sprite list being shown (buffer)
    const uint16_t *road;       // 0x800 words: the road table being shown (buffer)
    uint8_t road_control;
    bool display_enable;
    // tilemap scroll / page registers latched at line 261 (FG, BG, alt FG, alt BG)
    uint16_t latched_page[4], latched_x[4], latched_y[4];
} or_video;

typedef struct or_core or_core;
typedef struct or_idle_site or_idle_site;
typedef void (*or_frame_cb)(void *user, or_core *c);

struct or_core {
    or_roms roms;
    // the two programs in 1 KB pages (a frontend may point busy ones at RAM
    // copies: 4-byte aligned, read 32 bits at a time, so IRAM will do)
    const uint8_t *main_page[256], *sub_page[256];

    // memory
    // the big ones on their own (allocated by or_init unless already set)
    uint16_t *workram;              // 0x4000 words: main CPU RAM (battery backed: high scores)
    uint16_t *cpu1ram;              // 0x4000 words: sub CPU RAM
    uint16_t *tileram;              // 0x8000 words
    uint16_t textram[0x800];
    uint16_t paletteram[0x1000];
    uint16_t spriteram[0x800], spritebuf[0x800];
    uint16_t roadram[0x800], roadbuf[0x800];
    uint8_t road_control;
    bool palette_dirty;             // a palette entry changed since the flag was cleared

    sega_315_5195 mapper;

    // CPUs: the two 68000s share Musashi through saved contexts
    void *ctx[2];
    int cur;                        // 68000 running now (0 main, 1 sub)
    uint8_t irq_level[2];           // interrupt level asserted on each
    bool sub_reset_pending;
    bool z80_reset;                 // sound CPU held in reset (PPI port C bit 0)
    int32_t debt[2];                // cycles run past the previous slice

    // I/O
    uint8_t ppi_control, ppi_out[3];
    bool display_enable;
    uint8_t adc_select, adc_result;
    bool adc_intr;                  // conversion done (INTR active)
    int64_t adc_done_at;            // main CPU time the conversion ends (-1: none)
    int32_t bankmotor_pos, bankmotor_delta;
    int64_t bankmotor_next;
    uint8_t in_service, dsw_a, dsw_b;
    uint8_t steer, gas, brake;
    bool gear_high, gear_prev;
    bool irq2, vblank;

    // time: main CPU cycles since power on (frame start + position in it)
    int64_t time, slice_start;
    int line;
    int32_t line_frac;              // fraction of a cycle carried over

    // tilemap latches (line 261)
    uint16_t latched_page[4], latched_x[4], latched_y[4];

    // sound board (allocated by or_init unless already set). With sound_async
    // the core only queues its events and another thread runs it (it must
    // keep up: the core waits when the queue is full).
    or_sound *sound;
    bool sound_async;
    uint32_t sample_rate;

    // exact idle-loop fast-forward
    bool idle_skip;
    uint32_t slice_no;
    int32_t slice_skipped;
    struct or_idle_site { uint32_t slice; int rem, cost; } site[3];
    uint32_t skipped[2];

    or_frame_cb on_frame;           // at the start of vblank (line 224)
    void *user;
};

extern bool or_ipl_dirty;
int or_running_cpu(void);

// c->workram / cpu1ram / tileram / sound may be set before (else allocated here).
// The sound board's samples (sample_rate) go to c->sound->audio.
void or_init(or_core *c, const or_roms *roms, uint32_t sample_rate);
void or_reset(or_core *c);
void or_set_inputs(or_core *c, const or_inputs *in);
void or_run_frame(or_core *c);
void or_get_video(const or_core *c, or_video *v);

// --- Video (or_video.c) ---
// Palette indices (0..0x1fff: 0x1000 + n = shadowed / highlighted n, 0x2000
// black) of lines [y0, y1) (at most 16), OR_SCREEN_W each. spr: work buffer of
// OR_SCREEN_W * (y1 - y0) words (NULL: an internal one, for one thread).
#define OR_BLACK 0x2000
void or_render(const or_video *v, const or_roms *roms, int y0, int y1, uint16_t *out, uint16_t *spr);
// 0xRRGGBB of palette index 0..0x1fff (MAME's resistor weights).
uint32_t or_palette_rgb(const uint16_t *paletteram, int index);

#endif
