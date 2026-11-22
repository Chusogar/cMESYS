// ym2203dsp.h - Yamaha YM2203 (OPN): three FM channels (fmopn) plus the
// AY-3-8910 compatible PSG. C port of dsp-cpp's sound/ym2203 (from
// ym_2203.pas), sample by sample at 44100 Hz, as the arcades migrated from
// dsp-cpp use it. (RoboCop's YM2203 is the ym2610.c one.)
#ifndef YM2203DSP_H
#define YM2203DSP_H
#include <stdbool.h>
#include <stdint.h>
#include "fmopn.h"
#include "ay8910.h"

typedef struct {
    opn_core opn;
    ay8910 ay;
    uint8_t regs[256];
    float amplitude;
    // optional: the PSG's ports (reads of registers 14 / 15)
    uint8_t (*port_a_read)(void *user);
    uint8_t (*port_b_read)(void *user);
    void (*irq)(void *user, bool on);       // the timers' IRQ line
    void *user;
} ym2203d;

void ym2203d_init(ym2203d *y, uint32_t clock, float amplitude, float ay_amplitude);
void ym2203d_reset(ym2203d *y);
void ym2203d_control(ym2203d *y, uint8_t value);
void ym2203d_write(ym2203d *y, uint8_t value);
static inline uint8_t ym2203d_status(const ym2203d *y) { return y->opn.status_; }
uint8_t ym2203d_read(ym2203d *y);
int32_t ym2203d_update(ym2203d *y);         // the next sample (44100 Hz)
#endif
