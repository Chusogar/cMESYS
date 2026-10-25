// rc_core.h - RoboCop (Data East, 1988), platform independent.
// C port of dsp-cpp's drivers/arcade/dec0.cpp (Robocop variant), with MAME's
// dataeast/dec0.cpp (and decbac06, decmxc06, mb8421, h6280) as the reference.
//
// DEC0 board: 68000 at 10 MHz, three BAC06 tilemap chips, MXC06 sprites; a
// HuC6280 at 1.34 MHz running the protection program, talking to the 68000
// through an MB8421 mailbox RAM; the sound board (rc_sound.h).
// Picture 256x240 (lines 8-247 of 272, 384 pixel clocks a line at 6 MHz:
// 57.44 Hz).
#ifndef RC_CORE_H
#define RC_CORE_H

#include <stdint.h>
#include <stdbool.h>

#include "h6280/h6280.h"
#include "rc_sound.h"

#define RC_SCREEN_W     256
#define RC_SCREEN_H     240
#define RC_FIRST_LINE   8               // first visible line
#define RC_LINES        272
#define RC_LINE_CYCLES  640             // 68000 cycles a line (384 pixels at 6 MHz)
#define RC_MAIN_CLOCK   10000000u
#define RC_MCU_CLOCK    1342329         // 21.477272 MHz / 16
#define RC_FPS          (6000000.0 / (384.0 * RC_LINES))       // 57.4449

typedef struct {
    const uint8_t *main;        // 0x40000: 68000 program (big-endian words)
    const uint8_t *sound;       // 0x8000: 6502 program (8000-FFFF)
    const uint8_t *mcu;         // 0x200: HuC6280 program (1E00-1FFF)
    const uint8_t *oki;         // 0x10000: ADPCM samples
    const uint32_t *chars;      // 0x1000 x 8 rows of 8 pens (pixel x in bits 4x)
    const uint32_t *tiles1;     // 0x800 x 16 rows of 2 words
    const uint32_t *tiles2;     // 0x400 x 16 rows of 2 words
    const uint32_t *sprites;    // 0x1000 x 16 rows of 2 words
} rc_roms;

typedef struct {
    bool up, down, left, right, attack, jump;
} rc_player;

typedef struct {
    rc_player p[2];
    bool coin1, coin2, start1, start2, service;
} rc_inputs;

// One BAC06 playfield generator
typedef struct {
    uint16_t ctrl0[4], ctrl1[4];
    uint16_t colscroll[0x40], rowscroll[0x200];
    uint16_t *data;             // tile RAM (size words)
    uint16_t size;
} rc_bac06;

// What a picture is made from (rc_video.c draws it)
typedef struct {
    const rc_bac06 *pf[3];      // text, playfield 1, playfield 2
    const uint16_t *sprites;    // 0x400 words: the buffered sprite list
    uint16_t pri;               // priority register
    bool odd_frame;             // flashing sprites show on odd frames
} rc_video;

typedef struct rc_core rc_core;
typedef void (*rc_frame_cb)(void *user, rc_core *c);

struct rc_core {
    rc_roms roms;
    // the 68000 program in 1 KB pages (a frontend may point busy ones at RAM
    // copies: 4-byte aligned, read 32 bits at a time, so IRAM will do)
    const uint8_t *main_page[256];

    // 68000 memory
    uint16_t ram[0x2000];           // ff8000-ffbfff (mirror 318000)
    uint16_t ram1[0xc00];           // 242800-243fff
    uint16_t spriteram[0x400], spritebuf[0x400];
    uint16_t palette[0x400], palette_ext[0x400];
    bool palette_dirty;
    rc_bac06 pf[3];
    uint16_t text_data[0x1000], pf1_data[0x400], pf2_data[0x400];
    uint16_t pri;

    // HuC6280 and the MB8421 mailbox
    h6280 mcu;
    uint8_t mcu_ram[0x2000];
    uint8_t dpram[0x800];
    bool intl, intr;                // mailbox interrupts: to the 68000 (IRQ4), to the HuC6280 (IRQ1)
    int64_t mcu_target;             // HuC6280 clocks owed so far

    // 68000
    bool irq6;
    int32_t debt;
    bool in_main;

    // I/O
    uint16_t in_players, in_system, dsw;
    bool vblank;

    // time: 68000 cycles since power on
    int64_t time, slice_start;
    int line;
    uint32_t frame;

    // sound board (allocated by rc_init unless already set). With sound_async
    // the core only queues the latch writes and another thread runs it.
    rc_sound *sound;
    bool sound_async;
    uint32_t sample_rate;

    // exact idle-loop fast-forward
    bool idle_skip;
    uint32_t slice_no;
    int32_t slice_skipped;
    struct rc_idle_site { uint32_t slice; int rem, cost; } site[2];
    uint32_t skipped, mcu_skipped;

    rc_frame_cb on_frame;           // at the start of vblank (line 248)
    void *user;
};

extern bool rc_ipl_dirty;

// c->sound may be set before (else allocated here). The sound board's
// samples (sample_rate) go to c->sound->audio.
void rc_init(rc_core *c, const rc_roms *roms, uint32_t sample_rate);
void rc_reset(rc_core *c);
void rc_set_inputs(rc_core *c, const rc_inputs *in);
void rc_run_frame(rc_core *c);
void rc_get_video(const rc_core *c, rc_video *v);

// --- Video (rc_video.c) ---
typedef struct {
    uint16_t code;
    int16_t x, y;               // y of the first part (the others above it, 16 apart)
    uint8_t h, colour, flip;    // flip: 1 X, 2 Y
    int8_t incy;
    uint32_t pmask;             // MAME priority mask (bit 31: other sprites)
} rc_sprite;
typedef struct {
    int n;
    rc_sprite rec[0x100];
} rc_sprite_list;
// The sprite records of a frame (once a frame, before drawing it)
void rc_build_sprites(const rc_video *v, rc_sprite_list *list);
// Palette indices (0..0x3ff) of screen lines [y0, y1) (0..RC_SCREEN_H, at
// most 16 a call), RC_SCREEN_W each. work: RC_SCREEN_W * 16 bytes (NULL: an
// internal one, for one thread).
void rc_render(const rc_video *v, const rc_sprite_list *sprites, const rc_roms *roms, int y0, int y1,
               uint16_t *out, uint8_t *work);
// 0xRRGGBB of palette entry i (xBGR 888 over the two palette RAMs)
static inline uint32_t rc_palette_rgb(const uint16_t *pal, const uint16_t *ext, int i)
{
    return ((uint32_t)(pal[i] & 0xff) << 16) | (uint32_t)(pal[i] & 0xff00) | (ext[i] & 0xff);
}

#endif
