// saa1099.h - Philips SAA1099 6-channel stereo square-wave synthesizer, as
// used by the SAM Coupe: C port of dsp-cpp's sound/saa1099 (the register
// number goes to the address port, then the value to the data port).
//
// 6 tone channels (3 per stereo side, sharing 2 noise generators), each with
// its own left / right amplitude. The phases are 32-bit fixed point (the
// original's doubles), so the boards without a double FPU keep up.
#ifndef SAA1099_H
#define SAA1099_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t amplitude;      // bits 0-3 right, 4-7 left
    uint8_t frequency;      // 8-bit code
    uint8_t octave;         // 0-7
    bool tone_enable, noise_enable;
    uint32_t phase, step;   // 2^32 = one period
} saa_channel;

typedef struct {
    uint8_t freq_select;    // 0-3
    uint32_t lfsr;
    uint32_t phase, step;   // 2^16 = one shift
    bool level;
} saa_noise;

typedef struct {
    uint32_t clock;
    int rate;
    uint8_t addr;
    bool enabled;
    saa_channel ch[6];
    saa_noise noise[2];
} saa1099;

void saa1099_init(saa1099 *s, uint32_t clock, int sample_rate);
void saa1099_reset(saa1099 *s);
static inline void saa1099_select(saa1099 *s, uint8_t reg) { s->addr = reg & 0x1f; }
void saa1099_write(saa1099 *s, uint8_t value);
// One sample: (left, right), each about 0..8000
void saa1099_update(saa1099 *s, int32_t *left, int32_t *right);
#endif
