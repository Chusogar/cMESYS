// pp_core.h - Namco Pole Position / Pole Position II: a Z80 and two Z8002s,
// the Namco 06xx bus with the 51xx (I/O, coins), 52xx (speech samples),
// 53xx (steering, DIP switches) and 54xx (noise) MCUs, the 8-voice WSG and
// the engine sound; road, two tile layers and zoomed sprites.
// C port of dsp-cpp's drivers/arcade/polepos (MAME's namco/polepos.cpp as
// the reference). Both games use this core (pp_roms.pp2 tells them apart).
#ifndef PP_CORE_H
#define PP_CORE_H
#include <stdint.h>
#include <stdbool.h>
#include "z80/z80.h"
#include "z8002/z8002.h"
#include "namco5x.h"
#include "polepos_snd.h"

#define PP_W            256
#define PP_H            224
#define PP_VIS_TOP      16              // first visible line of the 256 drawn
#define PP_LINES        264
#define PP_MASTER_CLOCK 24576000u
#define PP_CPU_CLOCK    (PP_MASTER_CLOCK / 8)       // 3.072 MHz: Z80 and both Z8002s
#define PP_LINE_CYCLES  192
#define PP_FPS          ((PP_MASTER_CLOCK / 4) / (384.0 * PP_LINES))     // 60.606

typedef struct {
    bool pp2;                           // Pole Position II
    const uint8_t *z80;                 // 0000-2FFF
    uint32_t z80_size;
    const uint8_t *sub1, *sub2;         // 32 KB each, as the Z8002s see them
    const uint8_t *chars, *tiles;       // 8x8, one byte a pixel (0-3)
    int nchars, ntiles;
    const uint8_t *sprites;             // 128 x 16x16, one byte a pixel (0-15)
    const uint8_t *bigsprites;          // nbig x 32x32
    int nbig;
    const uint8_t *road;                // 0x5000: control + 2 bitplanes
    const uint8_t *scale;               // 0x1000 sprite scaling
    const uint8_t *proms;               // 0x1000: colour PROMs and lookups
    const uint8_t *wave;                // 0x100 WSG wavetable
    const uint8_t *engine, *voice;
    uint32_t engine_size, voice_size;
    const uint8_t *n51, *n52, *n53, *n54;
} pp_roms;

typedef struct {
    bool coin1, coin2, service;
    bool gas, brake, left, right;
    bool gear;                          // a press toggles low / high gear
    uint8_t wheel;                      // analog wheel 1-255 (0x80 centre), 0 = use left / right
} pp_inputs;

typedef struct pp_core pp_core;
typedef void (*pp_frame_cb)(void *user, pp_core *c);

struct pp_core {
    pp_roms roms;
    z80 cpu;
    z8002 sub[2];
    namco51 n51;
    namco52 n52;
    namco53 n53;
    namco54 n54;
    pp_wsg wsg;
    pp_engine engine;

    uint8_t nvram[0x800], sound_ram[0x400];
    uint8_t sprite_ram[0x1000], road_ram[0x800], alpha_ram[0x800], view_ram[0x1000];

    // colours: 128 RGB, pens -> colour (0xf00), road vertical position
    uint32_t rgb[128];
    uint8_t pens[0xf00];
    uint16_t vpos_mod[256];

    uint16_t view_hscroll, road_vscroll;
    uint8_t ls259, chacl, sub_irq_mask;
    bool sub_reset[2], irq_enable, adc_channel, adc_ready;
    int scanline, z80_budget, sub_budget[2];
    uint8_t dswa, dswb, in0;
    uint8_t accel, brake, steer;
    bool gear_hi, gear_prev;

    // Namco 06xx
    uint8_t n06_control;
    bool n06_timer_state, n06_read_stretch;
    int n06_cycle_acc, n06_period, mcu_cycle_acc;

    // steering encoder (read through the 53xx)
    uint8_t steer_last, steer_delta;
    int16_t steer_accum;

    // Pole Position II IC25 multiplier
    int16_t ic25_result;
    int8_t ic25_signed;
    uint8_t ic25_unsigned;
    uint16_t ic25_word;

    // sound
    uint32_t sample_rate;
    uint32_t audio_acc;
    int32_t dc_x, dc_y;                 // DC blocker
    int16_t *audio;                     // output samples (the frontend's buffer)
    int audio_cap, audio_n;

    uint32_t frame;
    pp_frame_cb on_frame;               // at the end of each frame (the picture is complete)
    void *user;
};

void pp_init(pp_core *c, const pp_roms *roms, uint32_t sample_rate);
void pp_reset(pp_core *c);
void pp_set_inputs(pp_core *c, const pp_inputs *in);
void pp_run_frame(pp_core *c);

// --- Video (pp_video.c) ---
// Colour numbers (0-127, c->rgb) of screen lines [y0, y1) (0..PP_H), PP_W
// each, drawn from the core's RAM (no state: any thread, any band).
void pp_render(const pp_core *c, int y0, int y1, uint8_t *out);

#endif
