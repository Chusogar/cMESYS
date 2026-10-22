// sb_core.h - Snow Bros (Toaplan/Kaneko 1990) core, platform independent.
// Port of zxtiny/zxm/snowbros.c without SDL.
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define SB_SCREEN_W   256
#define SB_SCREEN_H   224
#define SB_VIS_Y0     16
#define SB_CPU_CLOCK  8000000
#define SB_SND_CLOCK  6000000
#define SB_YM_CLOCK   3000000
#define SB_FPS        60

// input[0] = P1, input[1] = P2, input[2] = system (active low)
#define SB_IN_UP      0x0100
#define SB_IN_DOWN    0x0200
#define SB_IN_LEFT    0x0400
#define SB_IN_RIGHT   0x0800
#define SB_IN_B1      0x1000   // shoot snow
#define SB_IN_B2      0x2000   // jump
#define SB_IN_B3      0x4000
#define SB_SYS_START1 0x0100
#define SB_SYS_START2 0x0200
#define SB_SYS_COIN1  0x0400
#define SB_SYS_COIN2  0x0800

extern volatile uint16_t sb_input[3];

typedef struct {
    uint8_t spriteram[0x2000];
    uint8_t flip;
} sb_video_t;

void sb_init(int audio_rate);
void sb_reset(void);
// Emulates one frame of 68000 + Z80.
void sb_run_frame(void);
// YM3812 sample clock (3 MHz / 72 = 41666.67 Hz), derived from Z80 cycles.
#define SB_OPL_RATE   41666
uint32_t sb_sample_clock(void);
// Implemented by the platform: YM3812 register write (except timer regs 2-4)
// at YM sample position 'when'.
void sb_opl_write(uint32_t when, uint8_t reg, uint8_t val);
void sb_snapshot(sb_video_t *v);
// Render 256x224 palette indices (0..255) into fb.
void sb_render(const sb_video_t *v, uint8_t *fb);
// Current palette as xBGR555 words (index 0..255)
const uint16_t *sb_palette(void);
extern volatile uint32_t sb_palette_serial;   // incremented on each palette write
