#include "sn76489.h"
#include <string.h>

static const int16_t voltab[16] = {
    4096, 3254, 2584, 2053, 1631, 1295, 1029, 817,
    649, 516, 410, 325, 258, 205, 163, 0
};

void sn76489_init(SN76489 *p, int clock, int sample_rate)
{
    memset(p, 0, sizeof(*p));
    p->clock = clock;
    p->sample_rate = sample_rate;
    p->step_q16 = (uint32_t)(((uint64_t)clock << 16) / (uint64_t)(16 * sample_rate));
    if (!p->step_q16) p->step_q16 = 1 << 16;
    sn76489_reset(p);
}

void sn76489_reset(SN76489 *p)
{
    for (int i = 0; i < 3; ++i) {
        p->tone_period[i] = 0x3ff;
        p->tone_count[i] = 0x3ff;
        p->tone_out[i] = 0;
        p->tone_accum[i] = 0;
    }
    for (int i = 0; i < 4; ++i) p->volume[i] = 15;
    p->noise_reg = 0;
    p->noise_lfsr = 0x8000;
    p->noise_count = 0x10;
    p->noise_accum = 0;
    p->noise_out = 0;
    p->latched_channel = 0;
    p->latched_type = 0;
}

static uint16_t noise_period(const SN76489 *p)
{
    switch (p->noise_reg & 3) {
    case 0: return 0x10;
    case 1: return 0x20;
    case 2: return 0x40;
    default: return p->tone_period[2] ? p->tone_period[2] : 1;
    }
}

void sn76489_write(SN76489 *p, uint8_t data)
{
    if (data & 0x80) {
        uint8_t ch = (data >> 5) & 3;
        uint8_t typ = (data >> 4) & 1;
        uint8_t val = data & 0x0f;
        p->latched_channel = ch;
        p->latched_type = typ;

        if (typ) {
            p->volume[ch] = val;
        } else if (ch == 3) {
            p->noise_reg = val & 7;
            p->noise_lfsr = 0x8000;
            p->noise_count = noise_period(p);
        } else {
            p->tone_period[ch] = (p->tone_period[ch] & 0x3f0) | val;
            if (!p->tone_period[ch]) p->tone_period[ch] = 1;
        }
    } else {
        uint8_t val = data & 0x3f;
        uint8_t ch = p->latched_channel;
        if (!p->latched_type && ch < 3) {
            p->tone_period[ch] = (p->tone_period[ch] & 0x00f) | (val << 4);
            if (!p->tone_period[ch]) p->tone_period[ch] = 1;
        }
    }
}

void sn76489_update(SN76489 *p, int16_t *dst, int samples)
{
    /*
     * SN76489: la base de tiempo efectiva para tonos es clock/16.
     * Usamos Q16 para no truncar clock/(16*sample_rate), que a 44.1 kHz
     * es fraccional. La version anterior usaba entero y dejaba el tono
     * claramente bajo/lento.
     */
    const uint32_t step = p->step_q16 ? p->step_q16 : (1u << 16);

    for (int i = 0; i < samples; ++i) {
        int mix = 0;

        for (int ch = 0; ch < 3; ++ch) {
            uint32_t period = p->tone_period[ch] ? p->tone_period[ch] : 1;
            uint32_t limit = period << 16;
            p->tone_accum[ch] += step;
            while (p->tone_accum[ch] >= limit) {
                p->tone_accum[ch] -= limit;
                p->tone_out[ch] ^= 1;
            }
            mix += p->tone_out[ch] ? voltab[p->volume[ch] & 15]
                                   : -voltab[p->volume[ch] & 15];
        }

        uint32_t nper = noise_period(p);
        if (!nper) nper = 1;
        uint32_t nlimit = nper << 16;
        p->noise_accum += step;
        while (p->noise_accum >= nlimit) {
            p->noise_accum -= nlimit;
            uint16_t fb;
            if (p->noise_reg & 4)
                fb = ((p->noise_lfsr >> 0) ^ (p->noise_lfsr >> 3)) & 1;
            else
                fb = p->noise_lfsr & 1;
            p->noise_lfsr = (p->noise_lfsr >> 1) | (fb << 15);
            p->noise_out = p->noise_lfsr & 1;
        }
        mix += p->noise_out ? voltab[p->volume[3] & 15]
                            : -voltab[p->volume[3] & 15];

        int v = dst[i] + (mix / 4);
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        dst[i] = (int16_t)v;
    }
}

