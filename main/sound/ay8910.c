// ay8910.c - AY-3-8910 / AY-3-8912. See ay8910.h.
#include "ay8910.h"
#include <string.h>

#define AY_STEP       0x1000

enum {
    AY_AFINE = 0,
    AY_ACOARSE = 1,
    AY_BFINE = 2,
    AY_BCOARSE = 3,
    AY_CFINE = 4,
    AY_CCOARSE = 5,
    AY_NOISEPER = 6,
    AY_ENABLE = 7,
    AY_AVOL = 8,
    AY_BVOL = 9,
    AY_CVOL = 10,
    AY_EFINE = 11,
    AY_ECOARSE = 12,
    AY_ESHAPE = 13,
    AY_PORTA = 14,
    AY_PORTB = 15
};

/* Precomputed -1.5 dB steps from MAX/4 (MAME classic AY table). */
static const int32_t k_vol_table[32] = {
    0, 46, 55, 65, 77, 92, 109, 130, 154, 183, 218, 259, 308, 366, 435, 517,
    614, 730, 868, 1031, 1226, 1457, 1731, 2058, 2446, 2907, 3454, 4106, 4880,
    5799, 6892, 8192
};

static void set_amplitude(ay8910 *ay, float amplitude)
{
    /* Store amplitude as mantissa * 2^exponent without frexp (C99 portable). */
    if (amplitude <= 0.0f) {
        ay->amp_mantissa = 0;
        ay->amp_exponent = 0;
        return;
    }
    int exp = 0;
    float f = amplitude;
    while (f >= 1.0f) { f *= 0.5f; exp++; }
    while (f < 0.5f) { f *= 2.0f; exp--; }
    /* f in [0.5, 1): scale to 24-bit fixed. */
    ay->amp_mantissa = (int64_t)(f * (float)(1 << 24) + 0.5f);
    ay->amp_exponent = exp - 24;
}

static void recompute_step(ay8910 *ay)
{
    uint32_t clock = ay->clock ? ay->clock : 1773400;
    int rate = ay->sample_rate > 0 ? ay->sample_rate : AY8910_SAMPLE_RATE;
    ay->update_step = (int32_t)(((int64_t)AY_STEP * rate * 8) / (int64_t)clock);
    if (ay->update_step <= 0) ay->update_step = 1;
    ay->period_a = ay->period_b = ay->period_c = ay->period_e = ay->period_n = ay->update_step;
}

void ay8910_write_reg(ay8910 *ay, uint8_t reg, uint8_t value)
{
    if (reg > 15) return;
    ay->regs[reg] = value;
    int32_t old;
    switch (reg) {
    case AY_AFINE:
    case AY_ACOARSE:
        ay->regs[AY_ACOARSE] &= 0x0f;
        old = ay->period_a;
        ay->period_a = (ay->regs[AY_AFINE] + 256 * ay->regs[AY_ACOARSE]) * ay->update_step;
        if (ay->period_a == 0) ay->period_a = ay->update_step;
        ay->count_a += ay->period_a - old;
        if (ay->count_a <= 0) ay->count_a = 1;
        break;
    case AY_BFINE:
    case AY_BCOARSE:
        ay->regs[AY_BCOARSE] &= 0x0f;
        old = ay->period_b;
        ay->period_b = (ay->regs[AY_BFINE] + 256 * ay->regs[AY_BCOARSE]) * ay->update_step;
        if (ay->period_b == 0) ay->period_b = ay->update_step;
        ay->count_b += ay->period_b - old;
        if (ay->count_b <= 0) ay->count_b = 1;
        break;
    case AY_CFINE:
    case AY_CCOARSE:
        ay->regs[AY_CCOARSE] &= 0x0f;
        old = ay->period_c;
        ay->period_c = (ay->regs[AY_CFINE] + 256 * ay->regs[AY_CCOARSE]) * ay->update_step;
        if (ay->period_c == 0) ay->period_c = ay->update_step;
        ay->count_c += ay->period_c - old;
        if (ay->count_c <= 0) ay->count_c = 1;
        break;
    case AY_NOISEPER:
        ay->regs[AY_NOISEPER] &= 0x1f;
        old = ay->period_n;
        ay->period_n = ay->regs[AY_NOISEPER] * ay->update_step;
        if (ay->period_n == 0) ay->period_n = ay->update_step;
        ay->count_n += ay->period_n - old;
        if (ay->count_n <= 0) ay->count_n = 1;
        break;
    case AY_ENABLE:
        ay->last_enable = ay->regs[AY_ENABLE];
        break;
    case AY_AVOL:
        ay->regs[AY_AVOL] &= 0x1f;
        ay->envelope_a = ay->regs[AY_AVOL] & 0x10;
        old = ay->regs[AY_AVOL] ? ay->regs[AY_AVOL] * 2 + 1 : 0;
        ay->vol_a = ay->envelope_a ? ay->vol_e : k_vol_table[old];
        break;
    case AY_BVOL:
        ay->regs[AY_BVOL] &= 0x1f;
        ay->envelope_b = ay->regs[AY_BVOL] & 0x10;
        old = ay->regs[AY_BVOL] ? ay->regs[AY_BVOL] * 2 + 1 : 0;
        ay->vol_b = ay->envelope_b ? ay->vol_e : k_vol_table[old];
        break;
    case AY_CVOL:
        ay->regs[AY_CVOL] &= 0x1f;
        ay->envelope_c = ay->regs[AY_CVOL] & 0x10;
        old = ay->regs[AY_CVOL] ? ay->regs[AY_CVOL] * 2 + 1 : 0;
        ay->vol_c = ay->envelope_c ? ay->vol_e : k_vol_table[old];
        break;
    case AY_EFINE:
    case AY_ECOARSE:
        old = ay->period_e;
        ay->period_e = (ay->regs[AY_EFINE] + 256 * ay->regs[AY_ECOARSE]) * ay->update_step;
        if (ay->period_e == 0) ay->period_e = ay->update_step / 2;
        ay->count_e += ay->period_e - old;
        if (ay->count_e <= 0) ay->count_e = 1;
        break;
    case AY_ESHAPE:
        ay->regs[AY_ESHAPE] &= 0x0f;
        ay->attack = (ay->regs[AY_ESHAPE] & 0x04) ? 0x1f : 0;
        if ((ay->regs[AY_ESHAPE] & 0x08) == 0) {
            ay->hold = 1;
            ay->alternate = ay->attack;
        } else {
            ay->hold = ay->regs[AY_ESHAPE] & 1;
            ay->alternate = ay->regs[AY_ESHAPE] & 2;
        }
        ay->count_e = ay->period_e;
        ay->count_env = 0x1f;
        ay->holding = 0;
        ay->vol_e = k_vol_table[ay->count_env ^ ay->attack];
        if (ay->envelope_a) ay->vol_a = ay->vol_e;
        if (ay->envelope_b) ay->vol_b = ay->vol_e;
        if (ay->envelope_c) ay->vol_c = ay->vol_e;
        break;
    default:
        break;
    }
}

void ay8910_init(ay8910 *ay, uint32_t clock, int sample_rate)
{
    memset(ay, 0, sizeof *ay);
    ay->clock = clock ? clock : 1773400;
    ay->sample_rate = sample_rate > 0 ? sample_rate : AY8910_SAMPLE_RATE;
    set_amplitude(ay, 2.0f);
    recompute_step(ay);
    ay8910_reset(ay);
}

void ay8910_set_clock(ay8910 *ay, uint32_t clock)
{
    if (clock == 0 || clock == ay->clock) return;
    ay->clock = clock;
    recompute_step(ay);
}

void ay8910_reset(ay8910 *ay)
{
    ay->latch = 0;
    ay->output_a = ay->output_b = ay->output_c = 0;
    ay->output_n = 0xff;
    ay->rng = 1;
    ay->last_enable = -1;
    for (uint8_t reg = 0; reg <= 13; reg++) ay8910_write_reg(ay, reg, 0);
}

void ay8910_address_w(ay8910 *ay, uint8_t addr)
{
    ay->latch = (uint8_t)(addr & 0x0f);
}

void ay8910_data_w(ay8910 *ay, uint8_t data)
{
    ay8910_write_reg(ay, ay->latch, data);
}

uint8_t ay8910_data_r(const ay8910 *ay)
{
    return ay->regs[ay->latch];
}

static int32_t min_i32(int32_t a, int32_t b)
{
    return a < b ? a : b;
}

int32_t ay8910_render(ay8910 *ay)
{
    if (ay->regs[AY_ENABLE] & 0x01) {
        if (ay->count_a <= AY_STEP) ay->count_a += AY_STEP;
        ay->output_a = 1;
    } else if (ay->regs[AY_AVOL] == 0) {
        if (ay->count_a <= AY_STEP) ay->count_a += AY_STEP;
    }
    if (ay->regs[AY_ENABLE] & 0x02) {
        if (ay->count_b <= AY_STEP) ay->count_b += AY_STEP;
        ay->output_b = 1;
    } else if (ay->regs[AY_BVOL] == 0) {
        if (ay->count_b <= AY_STEP) ay->count_b += AY_STEP;
    }
    if (ay->regs[AY_ENABLE] & 0x04) {
        if (ay->count_c <= AY_STEP) ay->count_c += AY_STEP;
        ay->output_c = 1;
    } else if (ay->regs[AY_CVOL] == 0) {
        if (ay->count_c <= AY_STEP) ay->count_c += AY_STEP;
    }
    if ((ay->regs[AY_ENABLE] & 0x38) == 0x38) {
        if (ay->count_n <= AY_STEP) ay->count_n += AY_STEP;
    }

    int32_t out_noise = ay->output_n | ay->regs[AY_ENABLE];
    int32_t vol_a = 0, vol_b = 0, vol_c = 0;
    int32_t left = AY_STEP;

    do {
        int32_t next_event = min_i32(ay->count_n, left);

        if (out_noise & 0x08) {
            if (ay->output_a) vol_a += ay->count_a;
            ay->count_a -= next_event;
            while (ay->count_a <= 0) {
                ay->count_a += ay->period_a;
                if (ay->count_a > 0) {
                    ay->output_a ^= 1;
                    if (ay->output_a) vol_a += ay->period_a;
                    break;
                }
                ay->count_a += ay->period_a;
                vol_a += ay->period_a;
            }
            if (ay->output_a) vol_a -= ay->count_a;
        } else {
            ay->count_a -= next_event;
            while (ay->count_a <= 0) {
                ay->count_a += ay->period_a;
                if (ay->count_a > 0) {
                    ay->output_a ^= 1;
                    break;
                }
                ay->count_a += ay->period_a;
            }
        }

        if (out_noise & 0x10) {
            if (ay->output_b) vol_b += ay->count_b;
            ay->count_b -= next_event;
            while (ay->count_b <= 0) {
                ay->count_b += ay->period_b;
                if (ay->count_b > 0) {
                    ay->output_b ^= 1;
                    if (ay->output_b) vol_b += ay->period_b;
                    break;
                }
                ay->count_b += ay->period_b;
                vol_b += ay->period_b;
            }
            if (ay->output_b) vol_b -= ay->count_b;
        } else {
            ay->count_b -= next_event;
            while (ay->count_b <= 0) {
                ay->count_b += ay->period_b;
                if (ay->count_b > 0) {
                    ay->output_b ^= 1;
                    break;
                }
                ay->count_b += ay->period_b;
            }
        }

        if (out_noise & 0x20) {
            if (ay->output_c) vol_c += ay->count_c;
            ay->count_c -= next_event;
            while (ay->count_c <= 0) {
                ay->count_c += ay->period_c;
                if (ay->count_c > 0) {
                    ay->output_c ^= 1;
                    if (ay->output_c) vol_c += ay->period_c;
                    break;
                }
                ay->count_c += ay->period_c;
                vol_c += ay->period_c;
            }
            if (ay->output_c) vol_c -= ay->count_c;
        } else {
            ay->count_c -= next_event;
            while (ay->count_c <= 0) {
                ay->count_c += ay->period_c;
                if (ay->count_c > 0) {
                    ay->output_c ^= 1;
                    break;
                }
                ay->count_c += ay->period_c;
            }
        }

        ay->count_n -= next_event;
        if (ay->count_n <= 0) {
            if ((ay->rng + 1) & 2) {
                ay->output_n = ~ay->output_n;
                out_noise = ay->output_n | ay->regs[AY_ENABLE];
            }
            if (ay->rng & 1) ay->rng ^= 0x28000;
            ay->rng >>= 1;
            ay->count_n += ay->period_n;
        }
        left -= next_event;
    } while (left > 0);

    if (ay->holding == 0) {
        ay->count_e -= AY_STEP;
        if (ay->count_e <= 0) {
            do {
                ay->count_env--;
                ay->count_e += ay->period_e;
            } while (ay->count_e <= 0);
            if (ay->count_env < 0) {
                if (ay->hold) {
                    if (ay->alternate) ay->attack ^= 0x1f;
                    ay->holding = 1;
                    ay->count_env = 0;
                } else {
                    if (ay->alternate && (ay->count_env & 0x20)) ay->attack ^= 0x1f;
                    ay->count_env &= 0x1f;
                }
            }
            ay->vol_e = k_vol_table[ay->count_env ^ ay->attack];
            if (ay->envelope_a) ay->vol_a = ay->vol_e;
            if (ay->envelope_b) ay->vol_b = ay->vol_e;
            if (ay->envelope_c) ay->vol_c = ay->vol_e;
        }
    }

    {
        int64_t sum = (int64_t)vol_a * ay->vol_a + (int64_t)vol_b * ay->vol_b +
                      (int64_t)vol_c * ay->vol_c;
        int64_t scaled = sum * ay->amp_mantissa;
        int shift = ay->amp_exponent - 12; /* AY_STEP = 2^12 */
        if (shift >= 0) return (int32_t)(scaled << shift);
        int64_t magnitude = (scaled < 0 ? -scaled : scaled) >> -shift;
        return (int32_t)(scaled < 0 ? -magnitude : magnitude);
    }
}

void ay8910_update(ay8910 *ay, int16_t *dst, int samples)
{
    for (int i = 0; i < samples; i++) {
        int v = (int)dst[i] + ay8910_render(ay);
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        dst[i] = (int16_t)v;
    }
}
