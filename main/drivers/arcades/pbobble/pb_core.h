// pb_core.h - Puzzle Bobble (Taito B System, 1994) core, platform independent.
// After MAME's taito_b.cpp (pbobble): 68000 @ 12 MHz, Z80 @ 8 MHz, YM2610B
// @ 8 MHz, TC0180VCU video, TC0640FIO inputs, TC0140SYT sound mailbox,
// 93C46 EEPROM, MB87078 volume.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "tc0180vcu.h"

#define PB_SCREEN_W   VCU_SCREEN_W      // 320
#define PB_SCREEN_H   VCU_SCREEN_H      // 224
#define PB_CPU_CLOCK  12000000
#define PB_SND_CLOCK  8000000           // Z80 and YM2610B
#define PB_FPS        60
#define PB_LINES      256

// Inputs, active low (MAME port bits)
typedef struct {
    uint8_t service;    // 0x80 service (test) switch
    uint8_t coin;       // 0x10 coin 1, 0x20 coin 2 (bit 0 = EEPROM, set by the core)
    uint8_t start;      // 0x01 tilt, 0x02 service 1, 0x10 start 1, 0x20 start 2
    uint8_t p12a;       // P1 buttons 0x01 0x02 0x04, P2 0x10 0x20 0x40
    uint8_t p12b;       // P1 up 0x01 down 0x02 left 0x04 right 0x08, P2 << 4
} pb_inputs_t;
#define PB_COIN1   0x10
#define PB_COIN2   0x20
#define PB_START1  0x10
#define PB_START2  0x20
#define PB_B1      0x01     // shoot
#define PB_B2      0x02
#define PB_UP      0x01
#define PB_DOWN    0x02
#define PB_LEFT    0x04
#define PB_RIGHT   0x08

extern volatile pb_inputs_t pb_in;

void pb_init(void);
void pb_reset(void);
// One video frame (256 lines) of both CPUs. At line 240 (vblank start) the
// picture state is captured into pb_frame.
void pb_run_frame(void);
extern vcu_frame_t pb_frame;            // last captured picture state
const uint16_t *pb_vram(void);          // TC0180VCU tile RAM (live)
const uint16_t *pb_palette_ram(void);   // 4096 words, RRRRGGGGBBBBRGBx
extern volatile uint32_t pb_palette_serial;   // bumped on every palette write
// 0xRRGGBB of a palette word
static inline uint32_t pb_pal_rgb(uint16_t v)
{
    unsigned r = ((v >> 11) & 0x1e) | ((v >> 3) & 1);
    unsigned g = ((v >> 7) & 0x1e) | ((v >> 2) & 1);
    unsigned b = ((v >> 3) & 0x1e) | ((v >> 1) & 1);
    r = (r << 3) | (r >> 2); g = (g << 3) | (g >> 2); b = (b << 3) | (b >> 2);
    return (r << 16) | (g << 8) | b;
}

// Sound: Z80 cycles elapsed (= YM2610B clocks).
uint64_t pb_snd_clock(void);
// Implemented by the platform: YM2610B register write at YM clock 'when'.
void pb_ym_write(uint32_t when, int part, uint8_t reg, uint8_t val);
// MB87078 volume of the YM2610B, 0..256 (256 = 0 dB).
int pb_sound_gain(void);

// EEPROM (93C46, 64 x 16 bits, big-endian words): load before pb_reset or
// any time; *dirty is set when the game writes it.
uint8_t *pb_eeprom(bool **dirty);

// Idle loop fast-forward of both CPUs (same results; off for tests)
extern bool pb_idle_skip;

// Statistics / debug
typedef struct {
    uint32_t m68k_cycles_skipped, z80_cycles_skipped;
    uint32_t fb_cpu_accesses, no_erase_frames;
} pb_stats_t;
void pb_get_stats(pb_stats_t *s);
