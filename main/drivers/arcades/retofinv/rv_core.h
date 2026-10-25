// rv_core.h - Return of the Invaders (Taito / UPL, 1985), platform independent.
// C port of dsp-cpp's retofinv driver on cMESYS components, with MAME's
// taito/retofinv.cpp as the reference for timing and the hookups:
//
//   main Z80, sub Z80, sound Z80 (all 18.432 MHz / 6 = 3.072 MHz)
//   MC68705P5 MCU (3.072 MHz, Taito hookup: machine/taito68705)
//   LS259 latch: IRQ masks and the reset lines of the sub CPU, sound CPU, MCU
//   2 x SN76489A (3.072 MHz), sound NMI at 120 Hz, sound latch -> IRQ
//   288x224 at 60.58 Hz (ROT90 cabinet: shown as 224x288), two 36x28
//   tilemaps (bg 4bpp, fg 1bpp) and 64 sprites (16x16, 32x16, 16x32, 32x32)
#ifndef RV_CORE_H
#define RV_CORE_H
#include <stdint.h>
#include <stdbool.h>
#include "z80/z80.h"
#include "sn76489.h"
#include "taito68705.h"

#define RV_CLOCK        3072000
#define RV_NATIVE_W     288             // the board's screen
#define RV_NATIVE_H     224
#define RV_W            224             // shown rotated (ROT90)
#define RV_H            288
#define RV_FPS          60.58
#define RV_SLICES       100             // per frame (MAME: 6000 Hz quantum)

typedef struct {
    bool coin1, coin2, start1, start2, service, tilt;
    bool left, right, fire;             // player 1
    bool left2, right2, fire2;          // player 2 (cocktail)
} rv_inputs;

// What the renderer needs (a copy can be taken at the frame's end).
typedef struct {
    uint8_t fg[0x800];                  // 8000-87FF: codes, then colours
    uint8_t bg[0x800];                  // A000-A7FF
    uint8_t spr[3][0x80];               // shared RAM +0780, +0F80, +1780
    uint8_t fg_bank, bg_bank;
    bool flip;
} rv_video;

typedef struct rv_core rv_core;
struct rv_core {
    z80 main, sub, snd;
    taito68705 mcu;
    SN76489 sn[2];
    uint8_t fg_vram[0x800], shared[0x1800], bg_vram[0x800];
    uint8_t snd_ram[0x800];
    uint8_t latch259;                   // LS259 outputs
    uint8_t soundlatch, snd_m6000;
    bool soundlatch_pending;
    bool snd_poll_ok;                   // idle skipping: a real poll pass just ended
    bool main_irq, sub_irq;             // IRQ lines (held until the mask is cleared)
    uint8_t fg_bank, bg_bank;
    bool flip;
    uint8_t in_p1, in_p2, in_sys, dsw[3];
    // time (main CPU cycles since power-on)
    int64_t main_cyc, sub_cyc, snd_cyc, mcu_cyc4;   // mcu_cyc4: MCU cycles x 4
    int64_t frame_end, next_nmi;
    uint64_t frame;
    // audio out
    int16_t *audio;
    int audio_n, audio_cap;
    uint32_t sample_rate, audio_acc;
    // idle skipping (same results, less host time)
    bool idle_skip;
    uint32_t skipped;
    void (*on_frame)(void *user, rv_core *c);   // at vblank start (the screen is drawn then)
    void *user;
};

void rv_init(rv_core *c, uint32_t sample_rate);
void rv_reset(rv_core *c);
void rv_set_inputs(rv_core *c, const rv_inputs *in);
void rv_run_frame(rv_core *c);
void rv_get_video(const rv_core *c, rv_video *v);

// Renderer (rv_video.c): rotated output rows [y0, y1) of the 224x288 picture
// as colour indices 0-255 (out: (y1 - y0) rows of RV_W bytes).
void rv_render(const rv_video *v, int y0, int y1, uint8_t *out);
// Colour i (0-255) as RGB888
void rv_palette_rgb(uint8_t rgb[256][3]);

#endif
