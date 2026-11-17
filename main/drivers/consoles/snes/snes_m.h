// snes_m.h - the Super Nintendo machine (snes_core.c): a C11 migration of
// dsp-cpp's drivers/consoles/snes. The 65C816 (cpu/w65c816) with 128 KB of
// work RAM, the S-PPU (video/snes_ppu), the eight DMA / HDMA channels, the
// multiplier / divider, the H/V timers and the pads. The sound side is the
// SPC700 (cpu/spc700) with its RAM, IPL and timers; as in dsp-cpp the DSP
// makes no sound yet.
#ifndef SNES_M_H
#define SNES_M_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../../../cpu/w65c816/w65c816.h"
#include "../../../cpu/spc700/spc700.h"
#include "../../../video/snes_ppu.h"

enum { kCpuClock = 3580000, kFps = 60, kLinesTotal = 262, kVisibleLines = 224,
       kCyclesPerLine = 1364 / 6, kSampleRate = 32000 };

typedef struct {
    uint8_t control, dest;
    uint32_t src;
    uint16_t count;
    uint16_t table;
    uint8_t line_count;
    bool repeat, done, pending;
    uint16_t indirect;
    uint8_t indirect_bank;
} snes_dma;

typedef struct {
    w65c816 cpu_;
    snes_ppu ppu_;
    uint8_t *rom_;
    size_t rom_size_;
    bool hirom_;
    uint8_t *sram_;
    size_t sram_size_;
    uint8_t wram_[0x20000];
    uint32_t wram_addr_;
    snes_dma dma_[8];
    uint8_t nmitimen_, rdnmi_, hdmaen_, memsel_, wrio_;
    uint16_t htime_, vtime_;
    bool irq_pending_, nmi_pending_, in_vblank_;
    int line_, line_cycle_;
    uint8_t wrmpya_;
    uint16_t wrdiv_, rddiv_, rdmpy_;
    uint16_t ophct_, opvct_;
    bool ophct_high_, opvct_high_, counter_latched_;
    int extra_clocks_;
    bool in_dma_, dma_vram_open_;
    uint16_t pad1_, pad1_shift_;
    uint8_t joy_latch_;
    spc700 apu_;
    uint8_t aram_[0x10000];
    uint8_t ipl_[64];
    bool ipl_visible_;
    uint8_t apu_out_[4], apu_in_[4];
    uint8_t apu_control_;
    uint8_t timer_target_[3], timer_stage_[3], timer_out_[3];
    int timer_div_[3];
    uint8_t dsp_[128];
    uint8_t dsp_addr_;
    int apu_cycles_;
    uint32_t *framebuffer_;        // kWidth x kHeight, 0xAARRGGBB
    uint8_t open_bus_;
} snes_m;

void snes_reset(snes_m *P);
void snes_run_frame(snes_m *P);
#endif
