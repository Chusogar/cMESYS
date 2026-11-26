// ym2610d.h - Yamaha YM2610 (OPNB) as dsp-cpp's sound/ym2610 (NeoGeo): the
// OPN core of sound/fmopn (4 channels), an AY-3-8910 SSG, six ADPCM-A and
// one ADPCM-B channels; mono at 44100 Hz. (sound/ym2610.c is the fuller one.)
#ifndef YM2610D_H
#define YM2610D_H
#include <stdbool.h>
#include <stdint.h>
#include "fmopn.h"
#include "ay8910.h"

typedef struct {
    bool playing;
    uint32_t start, end, pos;
    int signal, step, nibble;
    uint8_t pan_level, start_hi, start_lo, end_hi, end_lo;
} ym2610d_adpcma;

typedef struct {
    opn_core opn;
    ay8910 ay;
    uint8_t address[2];
    float amplitude;
    uint32_t clock;
    const uint8_t *a_rom, *b_rom;
    uint32_t a_size, b_size;
    ym2610d_adpcma a[6];
    uint8_t a_tl, a_flags;
    double a_acc, a_step;
    int32_t a_mix;
    bool b_playing, b_repeat;
    uint32_t b_start, b_end, b_pos;
    uint16_t b_delta;
    uint8_t b_volume, b_pan, b_start_hi, b_start_lo, b_end_hi, b_end_lo, b_delta_hi, b_delta_lo;
    int b_signal, b_step, b_nibble;
    uint8_t b_status;
    double b_acc;
    int32_t b_mix;
} ym2610d;

void ym2610d_init(ym2610d *y, uint32_t clock, float amplitude);   // (the IRQ: y->opn.irq / user after)
void ym2610d_reset(ym2610d *y);
void ym2610d_write(ym2610d *y, int port, uint8_t value);
uint8_t ym2610d_read(ym2610d *y, int port);
static inline uint8_t ym2610d_status(const ym2610d *y) { return (uint8_t)(y->opn.status_ | y->b_status); }
int32_t ym2610d_update(ym2610d *y);

#endif
