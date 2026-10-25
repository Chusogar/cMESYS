// ym2610.h - Yamaha YM2610 / YM2610B (OPNB): 4 (2610) or 6 (2610B) FM
// channels, SSG, 6 ADPCM-A channels and the ADPCM-B channel.
//
// C port of dsp-cpp sound/ym2610.cpp + sound/fmopn.cpp (OPN FM core from
// fmopn.pas / MAME fm.c), extended with: the 6 FM channels of the YM2610B, the
// LFO, the ADPCM-A decoder and volume as MAME's ymfm, and the SSG.
//
// The chip is split in two halves so that the synthesis can run on another
// core (ESP32) from the CPU emulation:
//   ym2610_ctl_t  CPU side: address latches, timers, IRQ, status flags,
//                 ADPCM-A end-of-sample flags. Clocked in chip clocks; every
//                 register write is passed on (time-stamped by the caller).
//   ym2610_t      synthesis: registers -> samples at any output rate.
#pragma once
#include <stdint.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// CPU side
// ---------------------------------------------------------------------------
typedef struct ym2610_ctl {
    uint16_t addr;              // 0x000-0x0ff part 0, 0x100-0x1ff part 1
    uint8_t ta_hi, ta_lo, tb, mode;
    uint8_t status;             // bit 0 timer A, bit 1 timer B
    bool irq;
    int64_t ta_left, tb_left;   // clocks to overflow (running timers)
    bool ta_run, tb_run;
    // ADPCM-A end-of-sample flags (status port 2)
    uint8_t a_regs[0x30];
    uint8_t eos, eos_mask;
    int64_t a_end_left[6];      // clocks until the playing sample ends (<0: idle)
    // callbacks
    void (*irq_cb)(void *user, bool state);
    // every data write that matters to the synthesis: part 0/1, register, value
    void (*reg_cb)(void *user, int part, uint8_t reg, uint8_t val);
    void *user;
} ym2610_ctl_t;

void ym2610_ctl_reset(ym2610_ctl_t *c);
void ym2610_ctl_write(ym2610_ctl_t *c, int port, uint8_t v);
uint8_t ym2610_ctl_read(ym2610_ctl_t *c, int port);
// Advances the timers by 'clocks' chip clocks (may raise the IRQ).
void ym2610_ctl_advance(ym2610_ctl_t *c, int32_t clocks);
// Clocks until the next timer overflow or ADPCM-A end flag (INT32_MAX if none).
int32_t ym2610_ctl_next_event(const ym2610_ctl_t *c);

// ---------------------------------------------------------------------------
// Synthesis
// ---------------------------------------------------------------------------
typedef struct ym2610 ym2610_t;

// fm_channels: 6 for the YM2610B, 4 for the YM2610 (channels 1, 2, 4, 5),
// 3 for a YM2203 (ym2203_create).
ym2610_t *ym2610_create(uint32_t clock, int rate, int fm_channels,
                        const uint8_t *adpcm_a, uint32_t adpcm_a_size,
                        const uint8_t *adpcm_b, uint32_t adpcm_b_size);
void ym2610_reset(ym2610_t *y);
// YM2203 (OPN): the same FM core with 3 channels and the SSG, no LFO / ADPCM;
// native rate clock / (prescale x 12), prescale 6 (default), 3 or 2 (selected
// by writing the address 2D / 2E / 2F: ym2203_set_prescale).
ym2610_t *ym2203_create(uint32_t clock, int rate);
void ym2203_set_prescale(ym2610_t *y, int prescale);
// Output weights of the FM (+ ADPCM) and the SSG sum, 256 = 1.0 (defaults
// 256 and 192). ssg_output is the average of the 3 SSG channels.
void ym2610_set_gains(ym2610_t *y, int fm_gain, int ssg_gain);
void ym2610_write(ym2610_t *y, int part, uint8_t reg, uint8_t val);
// Mono output L+R (as MAME's mono speaker), signed 16 bits, clamped. gain: 0..256 (256 = 0 dB).
void ym2610_render(ym2610_t *y, int16_t *out, int n, int gain);
// Debug: register writes seen per area (index: 0 SSG, 1 ADPCM-B, 2 LFO,
// 3 CSM / 3-slot mode, 4 FM, 5 ADPCM-A).
extern uint32_t ym2610_usage[6];
