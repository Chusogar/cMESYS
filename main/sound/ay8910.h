// ay8910.h - AY-3-8910 / AY-3-8912 PSG (Spectrum 128K). C11 port of
// dsp-cpp sound/ay8910. Address latch + data write, mono sample render.
#ifndef AY8910_H
#define AY8910_H

#include <stdint.h>

#ifndef AY8910_SAMPLE_RATE
#define AY8910_SAMPLE_RATE 44100
#endif

typedef struct ay8910 {
    uint8_t regs[16];
    uint8_t latch;
    int32_t period_a, period_b, period_c, period_n, period_e;
    int32_t count_a, count_b, count_c, count_n, count_e;
    int32_t vol_a, vol_b, vol_c, vol_e;
    int32_t envelope_a, envelope_b, envelope_c;
    int32_t output_a, output_b, output_c, output_n;
    int32_t hold, alternate, attack, holding, rng, update_step;
    int8_t count_env;
    int16_t last_enable;
    uint32_t clock;
    int sample_rate;
    /* amplitude as amp_mantissa * 2^amp_exponent (integer mix path). */
    int64_t amp_mantissa;
    int amp_exponent;
} ay8910;

void ay8910_init(ay8910 *ay, uint32_t clock, int sample_rate);
void ay8910_reset(ay8910 *ay);
void ay8910_set_clock(ay8910 *ay, uint32_t clock);

/* Spectrum 128K: write address (reg select) then data. */
void ay8910_address_w(ay8910 *ay, uint8_t addr);
void ay8910_data_w(ay8910 *ay, uint8_t data);
uint8_t ay8910_data_r(const ay8910 *ay);

/* Direct register poke (e.g. from a snapshot). */
void ay8910_write_reg(ay8910 *ay, uint8_t reg, uint8_t value);

/* One mixed mono sample at sample_rate. */
int32_t ay8910_render(ay8910 *ay);

/* Fill dst[0..samples) adding mixed output (clamped to int16). */
void ay8910_update(ay8910 *ay, int16_t *dst, int samples);

#endif
